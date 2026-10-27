#pragma once

#include <QFrame>
#include <QPointer>

#include <atomic>

class SwitcherVuMeter;
struct audio_data;

#define SWITCHER_BUS_COUNT 5

/* Output meters for the switcher's audio buses. Master and A-D are OBS
 * audio tracks 1-5, the same tracks the M and A-D mixer buttons route to. */
class SwitcherBusMeters : public QFrame {
	Q_OBJECT

	struct Bus {
		SwitcherBusMeters *owner = nullptr;
		size_t mixIdx = 0;
		QPointer<SwitcherVuMeter> meter;

		/* Written from the audio thread, read and reset by the UI */
		std::atomic<float> pendingPeak[2];
		float displayPeak[2] = {-60.0f, -60.0f};
	};

	Bus buses[SWITCHER_BUS_COUNT];

	static void AudioMixed(void *param, size_t mixIdx, struct audio_data *data);

	void UpdateMeters();

public:
	SwitcherBusMeters(QWidget *parent = nullptr);
	~SwitcherBusMeters();

	/* Current Master level in dB, as shown on its meter */
	float MasterLevel() const;
};
