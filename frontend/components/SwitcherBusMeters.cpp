#include "SwitcherBusMeters.hpp"
#include "SwitcherInputGrid.hpp"

#include <OBSApp.hpp>
#include <obs.h>

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "moc_SwitcherBusMeters.cpp"

#define METER_FLOOR_DB -60.0f
#define METER_DECAY_DB 1.2f

SwitcherBusMeters::SwitcherBusMeters(QWidget *parent) : QFrame(parent)
{
	setObjectName("switcherBusMeters");

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(6);

	const char *names[SWITCHER_BUS_COUNT] = {"Switcher.Bus.MasterName", "Switcher.Bus.AName", "Switcher.Bus.BName",
						 "Switcher.Bus.CName", "Switcher.Bus.DName"};
	const char *tips[SWITCHER_BUS_COUNT] = {"Switcher.Bus.MasterTT", "Switcher.Bus.ATT", "Switcher.Bus.BTT",
						"Switcher.Bus.CTT", "Switcher.Bus.DTT"};

	struct audio_convert_info conversion = {};
	conversion.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conversion.speakers = SPEAKERS_STEREO;

	for (int i = 0; i < SWITCHER_BUS_COUNT; i++) {
		Bus &bus = buses[i];
		bus.owner = this;
		bus.mixIdx = i;
		bus.pendingPeak[0] = METER_FLOOR_DB;
		bus.pendingPeak[1] = METER_FLOOR_DB;

		QFrame *strip = new QFrame(this);
		strip->setObjectName("switcherBusStrip");
		strip->setToolTip(QTStr(tips[i]));

		QVBoxLayout *stripLayout = new QVBoxLayout(strip);
		stripLayout->setContentsMargins(0, 0, 0, 0);
		stripLayout->setSpacing(2);

		QLabel *name = new QLabel(QTStr(names[i]), strip);
		name->setObjectName(i == 0 ? "switcherBusMasterName" : "switcherBusName");
		name->setAlignment(Qt::AlignCenter);

		bus.meter = new SwitcherVuMeter(strip);
		bus.meter->setFixedWidth(22);
		bus.meter->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

		QLabel *track = new QLabel(QTStr("Switcher.Bus.Track").arg(i + 1), strip);
		track->setObjectName("switcherBusTrack");
		track->setAlignment(Qt::AlignCenter);

		stripLayout->addWidget(name);
		stripLayout->addWidget(bus.meter, 1, Qt::AlignHCenter);
		stripLayout->addWidget(track);

		layout->addWidget(strip);

		audio_output_connect(obs_get_audio(), bus.mixIdx, &conversion, SwitcherBusMeters::AudioMixed, &bus);
	}

	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &SwitcherBusMeters::UpdateMeters);
	timer->start(33);
}

SwitcherBusMeters::~SwitcherBusMeters()
{
	for (Bus &bus : buses) {
		audio_output_disconnect(obs_get_audio(), bus.mixIdx, SwitcherBusMeters::AudioMixed, &bus);
	}
}

void SwitcherBusMeters::AudioMixed(void *param, size_t, struct audio_data *data)
{
	Bus *bus = static_cast<Bus *>(param);

	for (int ch = 0; ch < 2; ch++) {
		const float *samples = reinterpret_cast<const float *>(data->data[ch]);
		if (!samples) {
			continue;
		}

		float peak = 0.0f;
		for (uint32_t i = 0; i < data->frames; i++) {
			peak = std::max(peak, std::fabs(samples[i]));
		}

		float db = peak > 0.0f ? 20.0f * std::log10(peak) : METER_FLOOR_DB;

		float current = bus->pendingPeak[ch].load();
		while (db > current && !bus->pendingPeak[ch].compare_exchange_weak(current, db)) {
		}
	}
}

void SwitcherBusMeters::UpdateMeters()
{
	for (Bus &bus : buses) {
		for (int ch = 0; ch < 2; ch++) {
			float peak = bus.pendingPeak[ch].exchange(METER_FLOOR_DB);
			bus.displayPeak[ch] = std::max({peak, bus.displayPeak[ch] - METER_DECAY_DB, METER_FLOOR_DB});
		}

		bus.meter->SetLevels(bus.displayPeak[0], bus.displayPeak[1]);
	}
}

float SwitcherBusMeters::MasterLevel() const
{
	return std::max(buses[0].displayPeak[0], buses[0].displayPeak[1]);
}
