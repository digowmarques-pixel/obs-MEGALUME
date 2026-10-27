#pragma once

#include "SwitcherBusMeters.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

struct audio_data;

/* Sends each switcher bus (Master, A-D) to an audio device of its own, like
 * vMix's bus outputs: Bus A to headphones, Bus B to a second sound card, and
 * so on. The device for each bus is saved in the profile. */
class SwitcherBusOutputs : public QObject {
	Q_OBJECT

public:
	/* Plays interleaved float audio on one device */
	class Device {
	public:
		virtual ~Device() = default;
		/* Opens the device and reports the format it wants */
		virtual bool Open(const QString &id, uint32_t &sampleRate, uint32_t &channels) = 0;
		/* Called from the audio thread; false means the device is gone */
		virtual bool Write(const float *data, uint32_t frames) = 0;
	};

private:
	struct Output {
		SwitcherBusOutputs *owner = nullptr;
		size_t mixIdx = 0;
		QString deviceId;
		std::unique_ptr<Device> device;
		std::atomic<bool> failed = false;
		bool connected = false;
	};

	Output outputs[SWITCHER_BUS_COUNT];

	static void AudioMixed(void *param, size_t mixIdx, struct audio_data *data);
	void Open(Output &output);
	void Close(Output &output);
	void Retry();

public:
	SwitcherBusOutputs(QObject *parent = nullptr);
	~SwitcherBusOutputs();

	/* Reopens every bus with the devices saved in the profile */
	void Load();

	static QString DeviceId(int bus);
	static void SetDeviceId(int bus, const QString &id);
};
