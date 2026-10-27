/* List, Triggers, Colour and Title pages of the input settings window */

#include "SwitcherInputSettings.hpp"

#include <OBSApp.hpp>
#include <components/SwitcherAutomation.hpp>
#include <components/SwitcherInputGrid.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>

namespace {

constexpr const char *ColourFilterName = "Switcher Colour";

QLabel *PageHint(const QString &text, QWidget *parent)
{
	QLabel *label = new QLabel(text, parent);
	label->setObjectName("switcherSettingsHint");
	label->setWordWrap(true);
	return label;
}

bool IsTextSource(obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);
	return strcmp(id, "text_gdiplus") == 0 || strcmp(id, "text_ft2_source") == 0;
}

} // namespace

/* ------------------------------------------------------------------------- */
/* List                                                                      */

QWidget *SwitcherInputSettings::CreateListPage()
{
	QWidget *page = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(page);

	listNoVideo = new QLabel(QTStr("Switcher.List.NoVideo"), page);
	listNoVideo->setWordWrap(true);
	layout->addWidget(listNoVideo);

	listControls = new QWidget(page);
	QHBoxLayout *body = new QHBoxLayout(listControls);
	body->setContentsMargins(0, 0, 0, 0);

	listFiles = new QListWidget(listControls);
	listFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);
	listFiles->setMinimumHeight(150);
	body->addWidget(listFiles, 1);

	QVBoxLayout *buttons = new QVBoxLayout();
	auto addButton = [&](const char *key) {
		QPushButton *button = new QPushButton(QTStr(key), listControls);
		buttons->addWidget(button);
		return button;
	};
	QPushButton *add = addButton("Switcher.List.Add");
	QPushButton *remove = addButton("Switcher.List.Remove");
	QPushButton *up = addButton("Switcher.List.Up");
	QPushButton *down = addButton("Switcher.List.Down");
	buttons->addSpacing(8);
	QPushButton *play = addButton("Switcher.List.Play");
	QPushButton *next = addButton("Switcher.List.Next");
	buttons->addStretch(1);
	body->addLayout(buttons);
	layout->addWidget(listControls, 1);

	listLoop = new QCheckBox(QTStr("Switcher.List.Loop"), page);
	layout->addWidget(listLoop);
	layout->addWidget(PageHint(QTStr("Switcher.List.Hint"), page));

	connect(add, &QPushButton::clicked, this, &SwitcherInputSettings::AddListFiles);

	connect(remove, &QPushButton::clicked, this, [this]() {
		for (QListWidgetItem *item : listFiles->selectedItems()) {
			delete item;
		}
		SaveList();
	});

	auto move = [this](int offset) {
		int row = listFiles->currentRow();
		int target = row + offset;
		if (row < 0 || target < 0 || target >= listFiles->count()) {
			return;
		}
		QListWidgetItem *item = listFiles->takeItem(row);
		listFiles->insertItem(target, item);
		listFiles->setCurrentRow(target);
		SaveList();
	};
	connect(up, &QPushButton::clicked, this, [move]() { move(-1); });
	connect(down, &QPushButton::clicked, this, [move]() { move(1); });

	auto playRow = [this](int row) {
		OBSSource scene = GetScene();
		if (scene && SwitcherPlayListItem(scene, row)) {
			RefreshList();
		}
	};
	connect(play, &QPushButton::clicked, this, [this, playRow]() { playRow(listFiles->currentRow()); });
	connect(listFiles.data(), &QListWidget::itemDoubleClicked, this,
		[this, playRow](QListWidgetItem *item) { playRow(listFiles->row(item)); });
	connect(next, &QPushButton::clicked, this, [this]() {
		OBSSource scene = GetScene();
		if (scene && SwitcherAdvanceList(scene)) {
			RefreshList();
		}
	});

	connect(listLoop.data(), &QCheckBox::toggled, this, [this](bool checked) {
		if (OBSSource scene = GetScene()) {
			SwitcherSetListLoop(scene, checked);
		}
	});

	/* Follow the list as it moves on by itself */
	QTimer *timer = new QTimer(page);
	connect(timer, &QTimer::timeout, this, [this]() {
		if (pages->currentIndex() == PageList) {
			OBSSource scene = GetScene();
			int index = scene ? SwitcherGetListIndex(scene) : -1;
			for (int i = 0; i < listFiles->count(); i++) {
				QFont font = listFiles->item(i)->font();
				if (font.bold() != (i == index)) {
					RefreshList();
					break;
				}
			}
		}
	});
	timer->start(500);

	return page;
}

void SwitcherInputSettings::RefreshList()
{
	OBSSource scene = GetScene();
	if (!scene) {
		return;
	}

	OBSSource media = SwitcherFindMediaSource(scene);
	bool hasVideo = media && strcmp(obs_source_get_unversioned_id(media), "ffmpeg_source") == 0;
	listNoVideo->setVisible(!hasVideo);
	listControls->setEnabled(hasVideo);
	listLoop->setEnabled(hasVideo);

	QStringList files = SwitcherGetList(scene);
	int index = SwitcherGetListIndex(scene);
	int row = listFiles->currentRow();

	listFiles->clear();
	for (int i = 0; i < files.size(); i++) {
		QString label = QString("%1.  %2").arg(i + 1).arg(QFileInfo(files[i]).fileName());
		QListWidgetItem *item =
			new QListWidgetItem((i == index ? QString::fromUtf8("▶ ") : QString("   ")) + label);
		item->setData(Qt::UserRole, files[i]);
		item->setToolTip(files[i]);
		QFont font = item->font();
		font.setBold(i == index);
		item->setFont(font);
		listFiles->addItem(item);
	}
	listFiles->setCurrentRow(std::min(row, listFiles->count() - 1));

	QSignalBlocker block(listLoop);
	listLoop->setChecked(SwitcherGetListLoop(scene));
}

void SwitcherInputSettings::SaveList()
{
	OBSSource scene = GetScene();
	if (!scene) {
		return;
	}

	QString playing;
	QStringList oldFiles = SwitcherGetList(scene);
	int oldIndex = SwitcherGetListIndex(scene);
	if (oldIndex >= 0 && oldIndex < oldFiles.size()) {
		playing = oldFiles[oldIndex];
	}

	QStringList files;
	for (int i = 0; i < listFiles->count(); i++) {
		files << listFiles->item(i)->data(Qt::UserRole).toString();
	}
	SwitcherSetList(scene, files);

	/* Keep pointing at the video that is playing, wherever it moved */
	int index = files.indexOf(playing);
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	obs_data_set_int(settings, "switcher_list_index", std::max(index, 0));
	if (index < 0 && !files.isEmpty()) {
		SwitcherPlayListItem(scene, 0);
	}

	RefreshList();
}

void SwitcherInputSettings::AddListFiles()
{
	OBSSource scene = GetScene();
	if (!scene) {
		return;
	}

	QStringList files = QFileDialog::getOpenFileNames(
		this, QTStr("Switcher.List.Add"), QString(),
		QTStr("Switcher.List.Filter") +
			" (*.mp4 *.mov *.mkv *.avi *.webm *.flv *.ts *.m4v *.mpg *.mpeg *.mp3 *.wav *.aac *.m4a *.ogg)");
	if (files.isEmpty()) {
		return;
	}

	for (const QString &file : files) {
		QListWidgetItem *item = new QListWidgetItem(QFileInfo(file).fileName());
		item->setData(Qt::UserRole, file);
		listFiles->addItem(item);
	}
	SaveList();
}

/* ------------------------------------------------------------------------- */
/* Triggers                                                                  */

QWidget *SwitcherInputSettings::CreateTriggersPage()
{
	OBSSource scene = GetScene();
	SwitcherEndTrigger trigger = SwitcherGetEndTrigger(scene);

	QWidget *page = new QWidget(this);
	QFormLayout *form = new QFormLayout(page);

	endAction = new QComboBox(page);
	endAction->addItem(QTStr("Switcher.Trigger.Nothing"), SWITCHER_END_NOTHING);
	endAction->addItem(QTStr("Switcher.Trigger.NextInput"), SWITCHER_END_NEXT_INPUT);
	endAction->addItem(QTStr("Switcher.Trigger.Input"), SWITCHER_END_INPUT);
	endAction->addItem(QTStr("Switcher.Trigger.Previous"), SWITCHER_END_PREVIOUS);
	endAction->setCurrentIndex(std::max(0, endAction->findData(trigger.action)));
	form->addRow(QTStr("Switcher.Trigger.OnEnd"), endAction);

	endTarget = new QComboBox(page);
	obs_enum_scenes(
		[](void *param, obs_source_t *source) {
			auto *window = static_cast<SwitcherInputSettings *>(param);
			if (source != window->GetScene()) {
				window->endTarget->addItem(QT_UTF8(obs_source_get_name(source)),
							   QT_UTF8(obs_source_get_uuid(source)));
			}
			return true;
		},
		this);
	endTarget->setCurrentIndex(std::max(0, endTarget->findData(trigger.targetUuid)));
	form->addRow(QTStr("Switcher.Trigger.Target"), endTarget);

	endTransition = new QComboBox(page);
	endTransition->addItem(QTStr("Switcher.Trigger.Transition"), false);
	endTransition->addItem(QTStr("Switcher.Trigger.Cut"), true);
	endTransition->setCurrentIndex(trigger.cut ? 1 : 0);
	form->addRow(QTStr("Switcher.Trigger.How"), endTransition);

	form->addRow(QString(), PageHint(QTStr("Switcher.Trigger.Hint"), page));

	auto update = [this]() {
		endTarget->setEnabled(endAction->currentData().toInt() == SWITCHER_END_INPUT);
		endTransition->setEnabled(endAction->currentData().toInt() != SWITCHER_END_NOTHING);
	};
	update();

	for (QComboBox *combo : {endAction.data(), endTarget.data(), endTransition.data()}) {
		connect(combo, &QComboBox::currentIndexChanged, this, [this, update]() {
			update();
			SaveEndTrigger();
		});
	}

	return page;
}

void SwitcherInputSettings::SaveEndTrigger()
{
	OBSSource scene = GetScene();
	if (!scene) {
		return;
	}

	SwitcherEndTrigger trigger;
	trigger.action = endAction->currentData().toInt();
	trigger.targetUuid = endTarget->currentData().toString();
	trigger.cut = endTransition->currentData().toBool();
	SwitcherSetEndTrigger(scene, trigger);
}

/* ------------------------------------------------------------------------- */
/* Colour                                                                    */

QWidget *SwitcherInputSettings::CreateColourPage()
{
	QWidget *page = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(page);
	QFormLayout *form = new QFormLayout();

	struct Spec {
		const char *setting;
		const char *label;
		int min, max;
		double scale;
	};
	const Spec specs[] = {
		{"brightness", "Switcher.Colour.Brightness", -100, 100, 0.01},
		{"contrast", "Switcher.Colour.Contrast", -100, 100, 0.01},
		{"saturation", "Switcher.Colour.Saturation", -100, 200, 0.01},
		{"gamma", "Switcher.Colour.Gamma", -100, 100, 0.01},
		{"hue_shift", "Switcher.Colour.Hue", -180, 180, 1.0},
	};

	OBSSource filter = ColourFilter(false);
	OBSDataAutoRelease settings = filter ? obs_source_get_settings(filter) : nullptr;

	for (const Spec &spec : specs) {
		QHBoxLayout *row = new QHBoxLayout();
		QSlider *slider = new QSlider(Qt::Horizontal, page);
		slider->setRange(spec.min, spec.max);
		double current = settings ? obs_data_get_double(settings, spec.setting) : 0.0;
		slider->setValue(int(std::lround(current / spec.scale)));

		QLabel *value = new QLabel(QString::number(slider->value()), page);
		value->setMinimumWidth(40);
		value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

		row->addWidget(slider, 1);
		row->addWidget(value);
		form->addRow(QTStr(spec.label), row);

		colourSliders.push_back({spec.setting, spec.scale, slider, value});
		connect(slider, &QSlider::valueChanged, this, [this, value](int v) {
			value->setText(QString::number(v));
			ApplyColour();
		});
	}
	layout->addLayout(form);

	QHBoxLayout *buttons = new QHBoxLayout();
	QPushButton *reset = new QPushButton(QTStr("Switcher.Colour.Reset"), page);
	buttons->addWidget(reset);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addWidget(PageHint(QTStr("Switcher.Colour.Hint"), page));
	layout->addStretch(1);

	connect(reset, &QPushButton::clicked, this, [this]() {
		updatingColour = true;
		for (ColourSlider &colour : colourSliders) {
			colour.slider->setValue(0);
		}
		updatingColour = false;
		ApplyColour();
	});

	return page;
}

OBSSource SwitcherInputSettings::ColourFilter(bool create)
{
	OBSSource scene = GetScene();
	if (!scene) {
		return nullptr;
	}

	OBSSourceAutoRelease filter = obs_source_get_filter_by_name(scene, ColourFilterName);
	if (filter || !create) {
		return filter.Get();
	}

	OBSSourceAutoRelease created = obs_source_create("color_filter_v2", ColourFilterName, nullptr, nullptr);
	if (!created) {
		return nullptr;
	}
	obs_source_filter_add(scene, created);
	return created.Get();
}

void SwitcherInputSettings::ApplyColour()
{
	if (updatingColour) {
		return;
	}

	bool neutral = true;
	for (const ColourSlider &colour : colourSliders) {
		neutral = neutral && colour.slider->value() == 0;
	}

	/* A neutral image needs no filter at all */
	OBSSource filter = ColourFilter(!neutral);
	if (!filter) {
		return;
	}
	if (neutral) {
		obs_source_filter_remove(GetScene(), filter);
		return;
	}

	OBSDataAutoRelease settings = obs_data_create();
	for (const ColourSlider &colour : colourSliders) {
		obs_data_set_double(settings, colour.setting, colour.slider->value() * colour.scale);
	}
	obs_source_update(filter, settings);
}

/* ------------------------------------------------------------------------- */
/* Title                                                                     */

QWidget *SwitcherInputSettings::CreateTitlePage()
{
	QWidget *page = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(page);

	titleFields = new QVBoxLayout();
	layout->addLayout(titleFields);

	QHBoxLayout *buttons = new QHBoxLayout();
	QPushButton *add = new QPushButton(QTStr("Switcher.Title.Add"), page);
	buttons->addWidget(add);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	layout->addWidget(PageHint(QTStr("Switcher.Title.Hint"), page));
	layout->addStretch(1);

	connect(add, &QPushButton::clicked, this, &SwitcherInputSettings::AddTitle);
	return page;
}

void SwitcherInputSettings::RefreshTitles()
{
	while (QLayoutItem *item = titleFields->takeAt(0)) {
		delete item->widget();
		delete item;
	}

	OBSSource scene = GetScene();
	obs_scene_t *sceneData = obs_scene_from_source(scene);
	if (!sceneData) {
		return;
	}

	std::vector<OBSSource> texts;
	obs_scene_enum_items(
		sceneData,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			obs_source_t *source = obs_sceneitem_get_source(item);
			if (IsTextSource(source)) {
				static_cast<std::vector<OBSSource> *>(param)->emplace_back(source);
			}
			return true;
		},
		&texts);

	if (texts.empty()) {
		QLabel *none = new QLabel(QTStr("Switcher.Title.None"));
		titleFields->addWidget(none);
		return;
	}

	/* Items enumerate bottom to top; list them top to bottom */
	for (auto it = texts.rbegin(); it != texts.rend(); ++it) {
		OBSWeakSource weak = OBSGetWeakRef(*it);
		OBSDataAutoRelease settings = obs_source_get_settings(*it);

		QWidget *field = new QWidget();
		QVBoxLayout *fieldLayout = new QVBoxLayout(field);
		fieldLayout->setContentsMargins(0, 0, 0, 4);
		QHBoxLayout *header = new QHBoxLayout();
		header->addWidget(new QLabel(QT_UTF8(obs_source_get_name(*it)), field), 1);
		QPushButton *style = new QPushButton(QTStr("Switcher.Title.Style"), field);
		header->addWidget(style);
		fieldLayout->addLayout(header);

		connect(style, &QPushButton::clicked, this, [this, weak]() {
			OBSSource source = OBSGetStrongRef(weak);
			if (source) {
				main->CreatePropertiesWindow(source);
			}
		});

		QPlainTextEdit *edit = new QPlainTextEdit(QT_UTF8(obs_data_get_string(settings, "text")), field);
		edit->setFixedHeight(64);
		fieldLayout->addWidget(edit);
		titleFields->addWidget(field);

		/* Text goes live as it is typed */
		connect(edit, &QPlainTextEdit::textChanged, this, [weak, edit]() {
			OBSSource source = OBSGetStrongRef(weak);
			if (source) {
				OBSDataAutoRelease update = obs_data_create();
				obs_data_set_string(update, "text", QT_TO_UTF8(edit->toPlainText()));
				obs_source_update(source, update);
			}
		});
	}
}

void SwitcherInputSettings::AddTitle()
{
	OBSSource scene = GetScene();
	obs_scene_t *sceneData = obs_scene_from_source(scene);
	if (!sceneData) {
		return;
	}

#ifdef _WIN32
	const char *id = obs_get_latest_input_type_id("text_gdiplus");
#else
	const char *id = obs_get_latest_input_type_id("text_ft2_source");
#endif
	if (!id) {
		return;
	}

	obs_video_info ovi;
	obs_get_video_info(&ovi);

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "text", QT_TO_UTF8(QTStr("Switcher.Title.Default")));
	OBSDataAutoRelease font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_string(font, "style", "Bold");
	obs_data_set_int(font, "size", std::max(32, int(ovi.base_height / 14)));
	obs_data_set_obj(settings, "font", font);
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_bool(settings, "drop_shadow", true);

	std::string base = std::string(obs_source_get_name(scene)) + " - " + QT_TO_UTF8(QTStr("Switcher.Title.Name"));
	std::string name = base;
	for (int i = 2;; i++) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing) {
			break;
		}
		name = base + " " + std::to_string(i);
	}

	OBSSourceAutoRelease text = obs_source_create(id, name.c_str(), settings, nullptr);
	if (!text) {
		return;
	}

	/* Lower third: bottom left, inside the title safe area */
	obs_sceneitem_t *item = obs_scene_add(sceneData, text);
	if (item) {
		vec2 pos = {float(ovi.base_width) * 0.06f, float(ovi.base_height) * 0.78f};
		obs_sceneitem_set_pos(item, &pos);
	}

	RefreshTitles();
}
