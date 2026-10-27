#pragma once

#include <obs.hpp>

#include <QDialog>
#include <QPointer>

#include <array>

class OBSBasic;
class OBSQTDisplay;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QVBoxLayout;
class QStackedWidget;

#define SWITCHER_LAYER_COUNT 10

/* Per-input settings window, like vMix's input settings: General (name,
 * category, click and GO actions) and Layers (other inputs stacked on top
 * of this one as nested scenes, with layout presets), plus a live preview. */
class SwitcherInputSettings : public QDialog {
	Q_OBJECT

	struct LayerRow {
		QPointer<QCheckBox> visible;
		QPointer<QComboBox> input;
	};

	OBSBasic *main;
	OBSWeakSource weakScene;

	QPointer<QListWidget> nav;
	QPointer<QStackedWidget> pages;
	QPointer<OBSQTDisplay> preview;
	QPointer<QLineEdit> nameEdit;
	QPointer<QButtonGroup> categoryGroup;
	QPointer<QComboBox> clickAction;
	QPointer<QComboBox> goAction;
	std::array<LayerRow, SWITCHER_LAYER_COUNT> layers;
	bool updatingLayers = false;

	/* List */
	QPointer<QListWidget> listFiles;
	QPointer<QCheckBox> listLoop;
	QPointer<QWidget> listControls;
	QPointer<QLabel> listNoVideo;

	/* Triggers */
	QPointer<QComboBox> endAction;
	QPointer<QComboBox> endTarget;
	QPointer<QComboBox> endTransition;

	/* Colour */
	struct ColourSlider {
		const char *setting;
		double scale;
		QPointer<QSlider> slider;
		QPointer<QLabel> value;
	};
	std::vector<ColourSlider> colourSliders;
	bool updatingColour = false;

	/* Title */
	QPointer<QVBoxLayout> titleFields;

	static void DrawPreview(void *data, uint32_t cx, uint32_t cy);

	QWidget *CreateGeneralPage();
	QWidget *CreateLayersPage();
	QWidget *CreateListPage();
	QWidget *CreateTriggersPage();
	QWidget *CreateColourPage();
	QWidget *CreateTitlePage();

	void RefreshList();
	void SaveList();
	void AddListFiles();
	void SaveEndTrigger();
	OBSSource ColourFilter(bool create);
	void ApplyColour();
	void RefreshTitles();
	void AddTitle();

	void Rename();
	void RefreshLayers();
	void SetLayerInput(int layer, const QString &uuid);
	void ApplyLayout(int preset);
	void EditLayer(int layer);

	OBSSceneItem GetLayerItem(int layer) const;
	void ReorderLayers();

public:
	SwitcherInputSettings(OBSBasic *main, obs_source_t *scene);
	~SwitcherInputSettings();

	OBSSource GetScene() const;

	enum Page { PageGeneral, PageLayers, PageList, PageTriggers, PageColour, PageTitle };
	void ShowPage(Page page);

signals:
	void Changed();
};
