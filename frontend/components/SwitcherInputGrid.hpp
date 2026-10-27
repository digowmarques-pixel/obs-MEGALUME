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

#define SWITCHER_OVERLAY_COUNT 8
#define SWITCHER_CATEGORY_COUNT 6

class QButtonGroup;

/* Inputs can be tagged with one of a few colored categories, shown as tabs
 * above the grid. The category lives in the scene's private settings so it
 * is saved with the scene collection; 0 means none. */
QColor SwitcherCategoryColor(int category);
QString SwitcherCategoryName(int category);
void SwitcherSetCategoryName(int category, const QString &name);
int SwitcherGetCategory(obs_source_t *scene);
void SwitcherSetCategory(obs_source_t *scene, int category);

/* What clicking an input's thumbnail and its GO button do, per input */
enum SwitcherClickAction {
	SWITCHER_CLICK_PREVIEW,
	SWITCHER_CLICK_CUT,
	SWITCHER_CLICK_QUICKPLAY,
	SWITCHER_CLICK_NOTHING
};
enum SwitcherGoAction { SWITCHER_GO_QUICKPLAY, SWITCHER_GO_CUT };
int SwitcherGetClickAction(obs_source_t *scene);
void SwitcherSetClickAction(obs_source_t *scene, int action);
int SwitcherGetGoAction(obs_source_t *scene);
void SwitcherSetGoAction(obs_source_t *scene, int action);

/* Draws a source scaled to fit, for thumbnails and previews */
void SwitcherRenderSourceFit(obs_source_t *source, uint32_t cx, uint32_t cy);

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
	QPointer<QFrame> categoryMark;
	int category = 0;
	QPointer<QPushButton> loopButton;
	QPointer<QPushButton> playButton;
	QPointer<QPushButton> audioButton;
	std::array<QPointer<QPushButton>, SWITCHER_OVERLAY_COUNT> overlayButtons;
	bool showing = false;

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
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

public:
	SwitcherInputTile(obs_source_t *source, QWidget *parent = nullptr);
	~SwitcherInputTile();

	OBSSource GetSource() const;
	bool IsSource(obs_source_t *source) const;

	void ActivateLater(int delayMs);
	void SetNumber(int number);
	void UpdateName();
	void UpdateCategory();
	inline int Category() const { return category; }
	void SetLiveState(bool preview, bool program);
	void SetOverlayStates(const std::array<OBSSource, SWITCHER_OVERLAY_COUNT> &overlays);

signals:
	void PreviewRequested(OBSSource source);
	void QuickPlayRequested(OBSSource source);
	void CutRequested(OBSSource source);
	void CloseRequested(OBSSource source);
	void OverlayRequested(OBSSource source, int index);
	void ProjectorRequested(OBSSource source);
	void SettingsRequested(OBSSource source);
	void MenuRequested(OBSSource source, const QPoint &globalPos);
};

/* The switcher's input area: every scene as a tile, side by side, wrapping
 * to new rows as needed. */
class SwitcherInputGrid : public QScrollArea {
	Q_OBJECT

	QPointer<QWidget> contents;
	FlowLayout *flowLayout = nullptr;
	std::vector<QPointer<SwitcherInputTile>> tiles;
	int filter = 0;

	void RebuildLayout();

public:
	SwitcherInputGrid(QWidget *parent = nullptr);
	~SwitcherInputGrid();

	void SetFilter(int category);

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
	void SettingsRequested(OBSSource source);
	void MenuRequested(OBSSource source, const QPoint &globalPos);
};

/* Colored category tabs above the input grid: All, then one per category.
 * Right-click a category to rename it. */
class SwitcherInputTabs : public QFrame {
	Q_OBJECT

	QButtonGroup *group = nullptr;
	std::vector<QPushButton *> buttons;

	void RenameCategory(int category);

public:
	SwitcherInputTabs(QWidget *parent = nullptr);

	void UpdateNames();
	int CurrentFilter() const;

signals:
	void FilterChanged(int category);
	void CategoriesRenamed();
};
