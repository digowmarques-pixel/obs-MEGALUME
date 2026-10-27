#include "SwitcherBusOutputs.hpp"

#include <widgets/OBSBasic.hpp>

#include <obs.h>
#include <util/platform.h>

#include <QTimer>

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#elif defined(__linux__)
#include <pulse/simple.h>
#include <pulse/error.h>
#endif

#include "moc_SwitcherBusOutputs.cpp"

namespace {

/* Audio queued past this is dropped, keeping the bus close to real time */
constexpr uint32_t MaxLatencyMs = 120;

#ifdef _WIN32

template<typename T> void SafeRelease(T *&p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

class WasapiDevice : public SwitcherBusOutputs::Device {
	IAudioClient *client = nullptr;
	IAudioRenderClient *render = nullptr;
	UINT32 bufferFrames = 0;
	UINT32 maxQueued = 0;
	uint32_t channels = 0;

public:
	~WasapiDevice() override
	{
		if (client) {
			client->Stop();
		}
		SafeRelease(render);
		SafeRelease(client);
	}

	bool Open(const QString &id, uint32_t &sampleRate, uint32_t &channelsOut) override
	{
		IMMDeviceEnumerator *enumerator = nullptr;
		IMMDevice *device = nullptr;
		WAVEFORMATEX *format = nullptr;
		bool success = false;

		HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
					      __uuidof(IMMDeviceEnumerator), (void **)&enumerator);
		if (FAILED(hr)) {
			blog(LOG_WARNING, "Switcher bus output: no device enumerator (%08lX)", hr);
			return false;
		}

		if (id == "default") {
			hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
		} else {
			hr = enumerator->GetDevice(reinterpret_cast<const wchar_t *>(id.utf16()), &device);
		}
		if (FAILED(hr)) {
			blog(LOG_WARNING, "Switcher bus output: device '%s' not found (%08lX)", id.toUtf8().constData(),
			     hr);
			goto done;
		}

		hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client);
		if (FAILED(hr)) {
			goto done;
		}

		/* Shared mode always mixes in float, at the device's rate */
		hr = client->GetMixFormat(&format);
		if (FAILED(hr)) {
			goto done;
		}

		hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, format, nullptr);
		if (FAILED(hr)) {
			blog(LOG_WARNING, "Switcher bus output: initialize failed (%08lX)", hr);
			goto done;
		}

		hr = client->GetBufferSize(&bufferFrames);
		if (FAILED(hr)) {
			goto done;
		}

		hr = client->GetService(__uuidof(IAudioRenderClient), (void **)&render);
		if (FAILED(hr)) {
			goto done;
		}

		hr = client->Start();
		if (FAILED(hr)) {
			goto done;
		}

		sampleRate = format->nSamplesPerSec;
		channels = channelsOut = format->nChannels;
		maxQueued = sampleRate * MaxLatencyMs / 1000;
		success = true;

	done:
		if (format) {
			CoTaskMemFree(format);
		}
		SafeRelease(device);
		SafeRelease(enumerator);
		return success;
	}

	bool Write(const float *data, uint32_t frames) override
	{
		UINT32 padding = 0;
		if (FAILED(client->GetCurrentPadding(&padding))) {
			return false;
		}

		/* Device running slower than the mix: drop rather than lag */
		if (padding > maxQueued || padding + frames > bufferFrames) {
			return true;
		}

		BYTE *buffer = nullptr;
		if (FAILED(render->GetBuffer(frames, &buffer))) {
			return false;
		}
		memcpy(buffer, data, size_t(frames) * channels * sizeof(float));
		return SUCCEEDED(render->ReleaseBuffer(frames, 0));
	}
};

using PlatformDevice = WasapiDevice;

#elif defined(__linux__)

/* pa_simple blocks while writing, so a thread of its own feeds it and the
 * audio thread only queues */
class PulseDevice : public SwitcherBusOutputs::Device {
	pa_simple *stream = nullptr;
	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<float> queue;
	size_t maxQueued = 0;
	bool stopping = false;
	std::atomic<bool> broken = false;

	void Run()
	{
		std::vector<float> chunk;
		for (;;) {
			{
				std::unique_lock lock(mutex);
				wake.wait(lock, [this]() { return stopping || !queue.empty(); });
				if (stopping) {
					return;
				}
				chunk.assign(queue.begin(), queue.end());
				queue.clear();
			}

			int error = 0;
			if (pa_simple_write(stream, chunk.data(), chunk.size() * sizeof(float), &error) < 0) {
				blog(LOG_WARNING, "Switcher bus output: %s", pa_strerror(error));
				broken = true;
				return;
			}
		}
	}

public:
	~PulseDevice() override
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
		}
		wake.notify_all();
		if (thread.joinable()) {
			thread.join();
		}
		if (stream) {
			pa_simple_free(stream);
		}
	}

	bool Open(const QString &id, uint32_t &sampleRate, uint32_t &channels) override
	{
		obs_audio_info oai;
		obs_get_audio_info(&oai);

		pa_sample_spec spec = {};
		spec.format = PA_SAMPLE_FLOAT32NE;
		spec.rate = oai.samples_per_sec;
		spec.channels = 2;

		pa_buffer_attr attr = {};
		attr.maxlength = (uint32_t)-1;
		attr.tlength = (uint32_t)pa_usec_to_bytes(60 * 1000, &spec);
		attr.prebuf = (uint32_t)-1;
		attr.minreq = (uint32_t)-1;

		QByteArray device = id.toUtf8();
		int error = 0;
		stream = pa_simple_new(nullptr, "OBS MEGALUME", PA_STREAM_PLAYBACK,
				       id == "default" ? nullptr : device.constData(), "Switcher bus", &spec, nullptr,
				       &attr, &error);
		if (!stream) {
			blog(LOG_WARNING, "Switcher bus output: %s", pa_strerror(error));
			return false;
		}

		sampleRate = spec.rate;
		channels = spec.channels;
		maxQueued = size_t(sampleRate) * channels * MaxLatencyMs / 1000;
		thread = std::thread(&PulseDevice::Run, this);
		return true;
	}

	bool Write(const float *data, uint32_t frames) override
	{
		if (broken) {
			return false;
		}
		{
			std::lock_guard lock(mutex);
			if (queue.size() < maxQueued) {
				queue.insert(queue.end(), data, data + size_t(frames) * 2);
			}
		}
		wake.notify_one();
		return true;
	}
};

using PlatformDevice = PulseDevice;

#else

/* No bus outputs on this platform yet; the device never opens */
class NullDevice : public SwitcherBusOutputs::Device {
public:
	bool Open(const QString &, uint32_t &, uint32_t &) override { return false; }
	bool Write(const float *, uint32_t) override { return false; }
};

using PlatformDevice = NullDevice;

#endif

QString ConfigKey(int bus)
{
	return QString("Bus%1Device").arg(bus);
}

} // namespace

SwitcherBusOutputs::SwitcherBusOutputs(QObject *parent) : QObject(parent)
{
	for (int i = 0; i < SWITCHER_BUS_COUNT; i++) {
		outputs[i].owner = this;
		outputs[i].mixIdx = (size_t)i;
	}

	/* Devices that were unplugged or failed get another try */
	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &SwitcherBusOutputs::Retry);
	timer->start(3000);

	Load();
}

SwitcherBusOutputs::~SwitcherBusOutputs()
{
	for (Output &output : outputs) {
		Close(output);
	}
}

QString SwitcherBusOutputs::DeviceId(int bus)
{
	const char *id =
		config_get_string(OBSBasic::Get()->Config(), "SwitcherBusOutputs", ConfigKey(bus).toUtf8().constData());
	return id ? QString::fromUtf8(id) : QString();
}

void SwitcherBusOutputs::SetDeviceId(int bus, const QString &id)
{
	config_set_string(OBSBasic::Get()->Config(), "SwitcherBusOutputs", ConfigKey(bus).toUtf8().constData(),
			  id.toUtf8().constData());
}

void SwitcherBusOutputs::Load()
{
	for (int i = 0; i < SWITCHER_BUS_COUNT; i++) {
		Output &output = outputs[i];
		/* A bus that is turned off plays nowhere */
		QString id = SwitcherBusEnabled(i) ? DeviceId(i) : QString();
		if (id == output.deviceId && (output.connected || id.isEmpty())) {
			continue;
		}

		Close(output);
		output.deviceId = id;
		Open(output);
	}
}

void SwitcherBusOutputs::Open(Output &output)
{
	{
		if (output.deviceId.isEmpty()) {
			return;
		}

		auto device = std::make_unique<PlatformDevice>();
		uint32_t sampleRate = 0;
		uint32_t channels = 0;
		if (!device->Open(output.deviceId, sampleRate, channels)) {
			output.failed = true;
			return;
		}

		struct audio_convert_info conversion = {};
		conversion.samples_per_sec = sampleRate;
		conversion.format = AUDIO_FORMAT_FLOAT;
		conversion.speakers = (enum speaker_layout)channels;

		output.device = std::move(device);
		output.failed = false;
		output.connected = audio_output_connect(obs_get_audio(), output.mixIdx, &conversion,
							SwitcherBusOutputs::AudioMixed, &output);
		if (!output.connected) {
			output.device.reset();
			output.failed = true;
			return;
		}

		blog(LOG_INFO, "Switcher bus %zu playing on '%s' (%u Hz, %u channels)", output.mixIdx,
		     output.deviceId.toUtf8().constData(), sampleRate, channels);
	}
}

void SwitcherBusOutputs::Close(Output &output)
{
	if (output.connected) {
		audio_output_disconnect(obs_get_audio(), output.mixIdx, SwitcherBusOutputs::AudioMixed, &output);
		output.connected = false;
	}
	output.device.reset();
	output.failed = false;
}

void SwitcherBusOutputs::Retry()
{
	for (Output &output : outputs) {
		if (output.failed && !output.deviceId.isEmpty()) {
			Close(output);
			Open(output);
		}
	}
}

void SwitcherBusOutputs::AudioMixed(void *param, size_t, struct audio_data *data)
{
	Output *output = static_cast<Output *>(param);
	if (output->failed || !output->device) {
		return;
	}

	if (!output->device->Write(reinterpret_cast<const float *>(data->data[0]), data->frames)) {
		output->failed = true;
	}
}
