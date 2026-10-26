#pragma once

#include <obs.hpp>

#include <QFrame>
#include <QPointer>
#include <QScrollArea>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class FlowLayout;
class OBSQTDisplay;
class QLabel;
class QPushButton;

#define SWITCHER_OVERLAY_COUNT 4

/* Compact stereo peak meter drawn next to an input's thumbnail */
class SwitcherVuMeter : public QWidget {
	Q_OBJECT

	float levels[2] = {-60.0f, -60.0f};

protected:
	void paintEvent(QPaintEvent *event) override;

public:
	SwitcherVuMeter(QWidget *parent = nullptr);

	void SetLevels(float left, float right);
};

/* One input of the switcher: a live thumbnail of a scene with its number,
 * name, audio meter and quick actions, colored by whether it is in Preview
 * or Program. */
class SwitcherInputTile : public QFrame {
	Q_OBJECT

	struct AudioSource {
		OBSWeakSource source;
		obs_volmeter_t *volmeter = nullptr;
	};

	OBSWeakSource weakSource;
	QPointer<OBSQTDisplay> thumbnail;
	QPointer<SwitcherVuMeter> vuMeter;
	QPointer<QLabel> numberLabel;
	QPointer<QLabel> nameLabel;
	QPointer<QFrame> header;
	QPointer<QPushButton> loopButton;
	QPointer<QPushButton> playButton;
	QPointer<QPushButton> audioButton;
	std::array<QPointer<QPushButton>, SWITCHER_OVERLAY_COUNT> overlayButtons;

	std::vector<OBSWeakSource> mediaSources;
	std::vector<std::unique_ptr<AudioSource>> audioSources;

	/* Written from the audio thread, read and reset by the UI */
	std::atomic<float> pendingPeak[2];
	float displayPeak[2] = {-60.0f, -60.0f};

	static void DrawThumbnail(void *data, uint32_t cx, uint32_t cy);
	static void VolmeterUpdated(void *data, const float magnitude[MAX_AUDIO_CHANNELS],
				    const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);

	void ClearAudioSources();
	void RescanSources();
	void UpdateButtonStates();
	void UpdateMeter();

	void ToggleLoop();
	void TogglePlay();
	void ToggleAudio();

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

public:
	SwitcherInputTile(obs_source_t *source, QWidget *parent = nullptr);
	~SwitcherInputTile();

	OBSSource GetSource() const;
	bool IsSource(obs_source_t *source) const;

	void SetNumber(int number);
	void UpdateName();
	void SetLiveState(bool preview, bool program);
	void SetOverlayStates(const std::array<OBSSource, SWITCHER_OVERLAY_COUNT> &overlays);

signals:
	void PreviewRequested(OBSSource source);
	void QuickPlayRequested(OBSSource source);
	void CutRequested(OBSSource source);
	void CloseRequested(OBSSource source);
	void OverlayRequested(OBSSource source, int index);
	void ProjectorRequested(OBSSource source);
	void MenuRequested(OBSSource source, const QPoint &globalPos);
};

/* The switcher's input area: every scene as a tile, side by side, wrapping
 * to new rows as needed. */
class SwitcherInputGrid : public QScrollArea {
	Q_OBJECT

	QPointer<QWidget> contents;
	FlowLayout *flowLayout = nullptr;
	std::vector<QPointer<SwitcherInputTile>> tiles;

public:
	SwitcherInputGrid(QWidget *parent = nullptr);
	~SwitcherInputGrid();

	void SetScenes(const std::vector<OBSSource> &scenes);
	void SetLiveStates(obs_source_t *preview, obs_source_t *program);
	void SetOverlayStates(const std::array<OBSSource, SWITCHER_OVERLAY_COUNT> &overlays);

signals:
	void PreviewRequested(OBSSource source);
	void QuickPlayRequested(OBSSource source);
	void CutRequested(OBSSource source);
	void CloseRequested(OBSSource source);
	void OverlayRequested(OBSSource source, int index);
	void ProjectorRequested(OBSSource source);
	void MenuRequested(OBSSource source, const QPoint &globalPos);
};
