#include "SwitcherInputSettings.hpp"
#include "OBSBasicTransform.hpp"

#include <OBSApp.hpp>
#include <components/SwitcherInputGrid.hpp>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSQTDisplay.hpp>

#include <obs-frontend-api.h>
#include <qt-wrappers.hpp>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "moc_SwitcherInputSettings.cpp"

namespace {

/* Layout presets place layers 1, 2, 3... in these canvas-relative rects.
 * Crop fills each rect, otherwise the layer is letterboxed inside it. */
struct LayoutPreset {
	const char *nameKey;
	std::vector<QRectF> rects;
	bool crop;
};

const std::vector<LayoutPreset> &LayoutPresets()
{
	static const std::vector<LayoutPreset> presets = {
		{"Switcher.Layout.Full", {{0, 0, 1, 1}}, false},
		{"Switcher.Layout.SideBySide", {{0, 0.25, 0.5, 0.5}, {0.5, 0.25, 0.5, 0.5}}, false},
		{"Switcher.Layout.SplitCrop", {{0, 0, 0.5, 1}, {0.5, 0, 0.5, 1}}, true},
		{"Switcher.Layout.Stacked", {{0.25, 0, 0.5, 0.5}, {0.25, 0.5, 0.5, 0.5}}, false},
		{"Switcher.Layout.PipBottomRight", {{0, 0, 1, 1}, {0.66, 0.64, 0.3, 0.3}}, false},
		{"Switcher.Layout.PipTopLeft", {{0, 0, 1, 1}, {0.04, 0.06, 0.3, 0.3}}, false},
		{"Switcher.Layout.OnePlusThree",
		 {{0, 0.125, 0.75, 0.75},
		  {0.75, 0, 0.25, 1.0 / 3},
		  {0.75, 1.0 / 3, 0.25, 1.0 / 3},
		  {0.75, 2.0 / 3, 0.25, 1.0 / 3}},
		 false},
		{"Switcher.Layout.Grid2x2",
		 {{0, 0, 0.5, 0.5}, {0.5, 0, 0.5, 0.5}, {0, 0.5, 0.5, 0.5}, {0.5, 0.5, 0.5, 0.5}},
		 false},
		{"Switcher.Layout.Grid3x3",
		 {{0, 0, 1.0 / 3, 1.0 / 3},
		  {1.0 / 3, 0, 1.0 / 3, 1.0 / 3},
		  {2.0 / 3, 0, 1.0 / 3, 1.0 / 3},
		  {0, 1.0 / 3, 1.0 / 3, 1.0 / 3},
		  {1.0 / 3, 1.0 / 3, 1.0 / 3, 1.0 / 3},
		  {2.0 / 3, 1.0 / 3, 1.0 / 3, 1.0 / 3},
		  {0, 2.0 / 3, 1.0 / 3, 1.0 / 3},
		  {1.0 / 3, 2.0 / 3, 1.0 / 3, 1.0 / 3},
		  {2.0 / 3, 2.0 / 3, 1.0 / 3, 1.0 / 3}},
		 false},
	};

	return presets;
}

QPixmap LayoutPixmap(const LayoutPreset &preset)
{
	static const QColor colors[] = {QColor(10, 40, 230),  QColor(230, 20, 20),   QColor(240, 150, 20),
					QColor(30, 160, 60),  QColor(150, 50, 180),  QColor(220, 200, 30),
					QColor(20, 160, 160), QColor(130, 130, 130), QColor(230, 90, 160)};

	QPixmap pixmap(128, 72);
	pixmap.fill(Qt::black);

	QPainter painter(&pixmap);
	QFont font = painter.font();
	font.setBold(true);
	font.setPixelSize(12);
	painter.setFont(font);

	for (size_t i = 0; i < preset.rects.size(); i++) {
		const QRectF &r = preset.rects[i];
		QRectF rect(r.x() * 128, r.y() * 72, r.width() * 128, r.height() * 72);
		painter.fillRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), colors[i % 9]);
		painter.setPen(Qt::white);
		painter.drawText(rect.adjusted(4, 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, QString::number(i + 1));
	}

	return pixmap;
}

bool SceneContains(obs_scene_t *scene, obs_source_t *target, int depth)
{
	struct Context {
		obs_source_t *target;
		int depth;
		bool found;
	} ctx{target, depth, false};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			Context *c = static_cast<Context *>(param);
			obs_source_t *source = obs_sceneitem_get_source(item);

			if (source == c->target) {
				c->found = true;
			} else if (obs_scene_t *nested = obs_scene_from_source(source)) {
				c->found = c->depth < 8 && SceneContains(nested, c->target, c->depth + 1);
			}

			return !c->found;
		},
		&ctx);

	return ctx.found;
}

} // namespace

SwitcherInputSettings::SwitcherInputSettings(OBSBasic *main_, obs_source_t *scene)
	: QDialog(main_),
	  main(main_),
	  weakScene(OBSGetWeakRef(scene))
{
	setObjectName("switcherInputSettings");
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QTStr("Switcher.InputSettings").arg(QT_UTF8(obs_source_get_name(scene))));
	resize(1100, 720);

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	nav = new QListWidget(this);
	nav->setObjectName("switcherSettingsNav");
	nav->setFixedWidth(170);
	nav->addItem(QTStr("Switcher.InputSettings.General"));
	nav->addItem(QTStr("Switcher.InputSettings.Layers"));
	nav->addItem(QTStr("Switcher.InputSettings.List"));
	nav->addItem(QTStr("Switcher.InputSettings.Triggers"));
	nav->addItem(QTStr("Switcher.InputSettings.Colour"));
	nav->addItem(QTStr("Switcher.InputSettings.Title"));
	layout->addWidget(nav);

	QVBoxLayout *right = new QVBoxLayout();
	right->setContentsMargins(8, 8, 8, 8);

	pages = new QStackedWidget(this);
	pages->addWidget(CreateGeneralPage());
	pages->addWidget(CreateLayersPage());
	pages->addWidget(CreateListPage());
	pages->addWidget(CreateTriggersPage());
	pages->addWidget(CreateColourPage());
	pages->addWidget(CreateTitlePage());
	right->addWidget(pages);

	/* Live preview of the input under the settings */
	preview = new OBSQTDisplay(this);
	preview->setMinimumHeight(260);
	preview->SetDisplayBackgroundColor(Qt::black);
	right->addWidget(preview, 1);

	connect(preview.data(), &OBSQTDisplay::DisplayCreated, this, [this]() {
		obs_display_add_draw_callback(preview->GetDisplay(), SwitcherInputSettings::DrawPreview, this);
	});

	layout->addLayout(right, 1);

	connect(nav.data(), &QListWidget::currentRowChanged, this, [this](int row) {
		pages->setCurrentIndex(row);
		if (row == PageLayers) {
			RefreshLayers();
		} else if (row == PageList) {
			RefreshList();
		} else if (row == PageTitle) {
			RefreshTitles();
		}
	});
	nav->setCurrentRow(0);

	obs_source_inc_showing(scene);
}

SwitcherInputSettings::~SwitcherInputSettings()
{
	if (preview && preview->GetDisplay()) {
		obs_display_remove_draw_callback(preview->GetDisplay(), SwitcherInputSettings::DrawPreview, this);
	}

	OBSSource scene = GetScene();
	if (scene) {
		obs_source_dec_showing(scene);
	}
}

OBSSource SwitcherInputSettings::GetScene() const
{
	return OBSGetStrongRef(weakScene);
}

void SwitcherInputSettings::ShowPage(Page page)
{
	nav->setCurrentRow(page);
}

void SwitcherInputSettings::DrawPreview(void *data, uint32_t cx, uint32_t cy)
{
	SwitcherInputSettings *window = static_cast<SwitcherInputSettings *>(data);

	OBSSourceAutoRelease scene = obs_weak_source_get_source(window->weakScene);
	if (scene) {
		SwitcherRenderSourceFit(scene, cx, cy);
	}
}

/* ------------------------------------------------------------------------- */

QWidget *SwitcherInputSettings::CreateGeneralPage()
{
	OBSSource scene = GetScene();

	QWidget *page = new QWidget(this);
	QFormLayout *form = new QFormLayout(page);

	nameEdit = new QLineEdit(QT_UTF8(obs_source_get_name(scene)), page);
	connect(nameEdit.data(), &QLineEdit::editingFinished, this, &SwitcherInputSettings::Rename);
	form->addRow(QTStr("Name"), nameEdit);

	/* Category swatches: none, then each category color */
	QHBoxLayout *swatches = new QHBoxLayout();
	swatches->setSpacing(4);
	categoryGroup = new QButtonGroup(this);
	int current = SwitcherGetCategory(scene);

	for (int i = 0; i <= SWITCHER_CATEGORY_COUNT; i++) {
		QPushButton *swatch = new QPushButton(page);
		swatch->setCheckable(true);
		swatch->setFixedSize(26, 26);
		swatch->setToolTip(i == 0 ? QTStr("Switcher.Category.None") : SwitcherCategoryName(i));

		QColor color = i == 0 ? QColor(45, 45, 45) : SwitcherCategoryColor(i);
		swatch->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid rgba(255,255,255,0.25);"
					      " border-radius: 13px; }"
					      "QPushButton:checked { border: 3px solid white; }")
					      .arg(color.name()));
		swatch->setChecked(i == current);
		categoryGroup->addButton(swatch, i);
		swatches->addWidget(swatch);
	}
	swatches->addStretch(1);
	form->addRow(QTStr("Switcher.Category"), swatches);

	connect(categoryGroup.data(), &QButtonGroup::idClicked, this, [this](int id) {
		OBSSource source = GetScene();
		if (source) {
			SwitcherSetCategory(source, id);
			emit Changed();
		}
	});

	clickAction = new QComboBox(page);
	clickAction->addItem(QTStr("Switcher.ClickAction.Preview"), SWITCHER_CLICK_PREVIEW);
	clickAction->addItem(QTStr("Switcher.ClickAction.Cut"), SWITCHER_CLICK_CUT);
	clickAction->addItem(QTStr("Switcher.ClickAction.QuickPlay"), SWITCHER_CLICK_QUICKPLAY);
	clickAction->addItem(QTStr("Switcher.ClickAction.Nothing"), SWITCHER_CLICK_NOTHING);
	clickAction->setCurrentIndex(clickAction->findData(SwitcherGetClickAction(scene)));
	form->addRow(QTStr("Switcher.ClickAction"), clickAction);

	connect(clickAction.data(), &QComboBox::currentIndexChanged, this, [this]() {
		OBSSource source = GetScene();
		if (source) {
			SwitcherSetClickAction(source, clickAction->currentData().toInt());
		}
	});

	goAction = new QComboBox(page);
	goAction->addItem(QTStr("Switcher.QuickPlay"), SWITCHER_GO_QUICKPLAY);
	goAction->addItem(QTStr("Switcher.Input.Cut"), SWITCHER_GO_CUT);
	goAction->setCurrentIndex(goAction->findData(SwitcherGetGoAction(scene)));
	form->addRow(QTStr("Switcher.GoAction"), goAction);

	connect(goAction.data(), &QComboBox::currentIndexChanged, this, [this]() {
		OBSSource source = GetScene();
		if (source) {
			SwitcherSetGoAction(source, goAction->currentData().toInt());
		}
	});

	obs_video_info ovi;
	obs_get_video_info(&ovi);
	form->addRow(QTStr("Switcher.InputSettings.Resolution"),
		     new QLabel(QString("%1x%2").arg(ovi.base_width).arg(ovi.base_height), page));

	/* The input's own source and filters, through the regular OBS windows */
	QHBoxLayout *actions = new QHBoxLayout();
	QPushButton *properties = new QPushButton(QTStr("Switcher.InputSettings.SourceProperties"), page);
	QPushButton *filters = new QPushButton(QTStr("Switcher.InputSettings.Filters"), page);
	actions->addWidget(properties);
	actions->addWidget(filters);
	actions->addStretch(1);
	form->addRow(QString(), actions);

	connect(properties, &QPushButton::clicked, this, [this]() {
		OBSSource source = GetScene();
		obs_scene_t *sceneData = obs_scene_from_source(source);
		if (!sceneData) {
			return;
		}

		/* The first item that is not a nested input is the input's source */
		obs_source_t *target = nullptr;
		obs_scene_enum_items(
			sceneData,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				obs_source_t *itemSource = obs_sceneitem_get_source(item);
				if (!obs_scene_from_source(itemSource)) {
					*static_cast<obs_source_t **>(param) = itemSource;
					return false;
				}
				return true;
			},
			&target);

		if (target) {
			main->CreatePropertiesWindow(target);
		}
	});

	connect(filters, &QPushButton::clicked, this, [this]() {
		OBSSource source = GetScene();
		if (source) {
			main->CreateFiltersWindow(source);
		}
	});

	return page;
}

void SwitcherInputSettings::Rename()
{
	OBSSource scene = GetScene();
	if (!scene) {
		return;
	}

	std::string name = QT_TO_UTF8(nameEdit->text().trimmed());
	if (name.empty() || name == obs_source_get_name(scene)) {
		nameEdit->setText(QT_UTF8(obs_source_get_name(scene)));
		return;
	}

	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
	if (existing) {
		OBSMessageBox::warning(this, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
		nameEdit->setText(QT_UTF8(obs_source_get_name(scene)));
		return;
	}

	obs_source_set_name(scene, name.c_str());
	setWindowTitle(QTStr("Switcher.InputSettings").arg(QT_UTF8(name.c_str())));
	emit Changed();
}

/* ------------------------------------------------------------------------- */

QWidget *SwitcherInputSettings::CreateLayersPage()
{
	QWidget *page = new QWidget(this);
	QHBoxLayout *layout = new QHBoxLayout(page);

	QGridLayout *grid = new QGridLayout();
	grid->setHorizontalSpacing(4);
	grid->setVerticalSpacing(2);

	for (int i = 0; i < SWITCHER_LAYER_COUNT; i++) {
		QLabel *number = new QLabel(QString::number(i + 1), page);
		number->setObjectName("switcherLayerNumber");
		number->setAlignment(Qt::AlignCenter);
		number->setFixedWidth(22);

		LayerRow &row = layers[i];
		row.visible = new QCheckBox(page);
		row.visible->setToolTip(QTStr("Switcher.Layer.Visible"));
		row.input = new QComboBox(page);
		row.input->setMinimumWidth(220);

		QPushButton *edit = new QPushButton(QTStr("Switcher.Layer.Edit"), page);

		grid->addWidget(number, i, 0);
		grid->addWidget(row.visible, i, 1);
		grid->addWidget(row.input, i, 2);
		grid->addWidget(edit, i, 3);

		connect(row.input.data(), &QComboBox::currentIndexChanged, this, [this, i]() {
			if (!updatingLayers) {
				SetLayerInput(i + 1, layers[i].input->currentData().toString());
			}
		});

		connect(row.visible.data(), &QCheckBox::toggled, this, [this, i](bool checked) {
			if (updatingLayers) {
				return;
			}

			OBSSceneItem item = GetLayerItem(i + 1);
			if (item) {
				obs_sceneitem_set_visible(item, checked);
			}
		});

		connect(edit, &QPushButton::clicked, this, [this, i]() { EditLayer(i + 1); });
	}

	layout->addLayout(grid);

	/* Layout presets, drawn like vMix's numbered layer diagrams */
	QVBoxLayout *presetsLayout = new QVBoxLayout();
	QListWidget *presets = new QListWidget(page);
	presets->setObjectName("switcherLayoutPresets");
	presets->setViewMode(QListView::IconMode);
	presets->setIconSize(QSize(128, 72));
	presets->setResizeMode(QListView::Adjust);
	presets->setMovement(QListView::Static);
	presets->setSpacing(6);

	for (const LayoutPreset &preset : LayoutPresets()) {
		QListWidgetItem *item = new QListWidgetItem(QIcon(LayoutPixmap(preset)), QTStr(preset.nameKey));
		item->setToolTip(QTStr(preset.nameKey));
		presets->addItem(item);
	}

	QLabel *hint = new QLabel(QTStr("Switcher.Layout.Hint"), page);
	hint->setWordWrap(true);

	presetsLayout->addWidget(presets, 1);
	presetsLayout->addWidget(hint);
	layout->addLayout(presetsLayout, 1);

	connect(presets, &QListWidget::itemActivated, this,
		[this, presets](QListWidgetItem *item) { ApplyLayout(presets->row(item)); });
	connect(presets, &QListWidget::itemClicked, this,
		[this, presets](QListWidgetItem *item) { ApplyLayout(presets->row(item)); });

	return page;
}

OBSSceneItem SwitcherInputSettings::GetLayerItem(int layer) const
{
	OBSSource source = GetScene();
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		return nullptr;
	}

	struct Context {
		int layer;
		obs_sceneitem_t *item;
	} ctx{layer, nullptr};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			Context *c = static_cast<Context *>(param);
			OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
			if (obs_data_get_int(settings, "switcher_layer") == c->layer) {
				c->item = item;
				return false;
			}
			return true;
		},
		&ctx);

	return OBSSceneItem(ctx.item);
}

void SwitcherInputSettings::RefreshLayers()
{
	OBSSource self = GetScene();
	if (!self) {
		return;
	}

	updatingLayers = true;

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	for (int i = 0; i < SWITCHER_LAYER_COUNT; i++) {
		LayerRow &row = layers[i];
		row.input->clear();
		row.input->addItem(QTStr("None"), QString());

		for (size_t j = 0; j < scenes.sources.num; j++) {
			obs_source_t *scene = scenes.sources.array[j];
			if (scene == self.Get()) {
				continue;
			}
			row.input->addItem(QT_UTF8(obs_source_get_name(scene)), QT_UTF8(obs_source_get_uuid(scene)));
		}

		OBSSceneItem item = GetLayerItem(i + 1);
		obs_source_t *layerSource = item ? obs_sceneitem_get_source(item) : nullptr;
		int index = layerSource ? row.input->findData(QT_UTF8(obs_source_get_uuid(layerSource))) : 0;

		row.input->setCurrentIndex(std::max(index, 0));
		row.visible->setChecked(item ? obs_sceneitem_visible(item) : true);
		row.visible->setEnabled(item != nullptr);
	}

	obs_frontend_source_list_free(&scenes);
	updatingLayers = false;
}

void SwitcherInputSettings::SetLayerInput(int layer, const QString &uuid)
{
	OBSSource self = GetScene();
	obs_scene_t *scene = obs_scene_from_source(self);
	if (!scene) {
		return;
	}

	OBSSceneItem old = GetLayerItem(layer);
	if (old) {
		obs_sceneitem_remove(old);
	}

	if (!uuid.isEmpty()) {
		OBSSourceAutoRelease input = obs_get_source_by_uuid(QT_TO_UTF8(uuid));
		obs_scene_t *inputScene = obs_scene_from_source(input);

		/* An input that already contains this one would loop forever */
		if (!input || (inputScene && SceneContains(inputScene, self, 0))) {
			OBSMessageBox::warning(this, QTStr("Switcher.Layer.LoopTitle"),
					       QTStr("Switcher.Layer.LoopText"));
			RefreshLayers();
			return;
		}

		obs_sceneitem_t *item = obs_scene_add(scene, input);
		if (item) {
			OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
			obs_data_set_int(settings, "switcher_layer", layer);

			obs_video_info ovi;
			obs_get_video_info(&ovi);

			obs_transform_info info;
			obs_sceneitem_get_info2(item, &info);
			info.pos = {0.0f, 0.0f};
			info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
			info.bounds_type = OBS_BOUNDS_SCALE_INNER;
			info.bounds_alignment = OBS_ALIGN_CENTER;
			info.bounds = {float(ovi.base_width), float(ovi.base_height)};
			obs_sceneitem_set_info2(item, &info);
		}
	}

	ReorderLayers();
	RefreshLayers();
	emit Changed();
}

void SwitcherInputSettings::ReorderLayers()
{
	/* Layers stack above the input's own sources, layer 10 on top */
	for (int i = 1; i <= SWITCHER_LAYER_COUNT; i++) {
		OBSSceneItem item = GetLayerItem(i);
		if (item) {
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
		}
	}
}

void SwitcherInputSettings::ApplyLayout(int preset)
{
	const std::vector<LayoutPreset> &presets = LayoutPresets();
	if (preset < 0 || preset >= (int)presets.size()) {
		return;
	}

	obs_video_info ovi;
	obs_get_video_info(&ovi);
	float width = float(ovi.base_width);
	float height = float(ovi.base_height);

	const LayoutPreset &layout = presets[preset];
	for (size_t i = 0; i < layout.rects.size(); i++) {
		OBSSceneItem item = GetLayerItem(int(i) + 1);
		if (!item) {
			continue;
		}

		const QRectF &r = layout.rects[i];

		obs_transform_info info;
		obs_sceneitem_get_info2(item, &info);
		info.pos = {float(r.x()) * width, float(r.y()) * height};
		info.rot = 0.0f;
		info.scale = {1.0f, 1.0f};
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		info.bounds_type = layout.crop ? OBS_BOUNDS_SCALE_OUTER : OBS_BOUNDS_SCALE_INNER;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		info.bounds = {float(r.width()) * width, float(r.height()) * height};
		info.crop_to_bounds = layout.crop;
		obs_sceneitem_set_info2(item, &info);

		obs_sceneitem_crop crop = {};
		obs_sceneitem_set_crop(item, &crop);
	}
}

void SwitcherInputSettings::EditLayer(int layer)
{
	OBSSceneItem item = GetLayerItem(layer);
	if (!item) {
		return;
	}

	OBSBasicTransform *transform = new OBSBasicTransform(item, main);
	transform->setAttribute(Qt::WA_DeleteOnClose);
	transform->show();
}
