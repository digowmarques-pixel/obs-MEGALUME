#include "SwitcherInputSelect.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <properties-view.hpp>
#include <qt-wrappers.hpp>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <cstring>
#include <string>

#include "moc_SwitcherInputSelect.cpp"

SwitcherInputSelect::SwitcherInputSelect(OBSBasic *main) : QDialog(main)
{
	setObjectName("switcherInputSelect");
	setWindowTitle(QTStr("Switcher.InputSelect"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(1000, 640);

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	typeList = new QListWidget(this);
	typeList->setObjectName("switcherInputTypes");
	typeList->setFixedWidth(230);
	typeList->setIconSize(QSize(20, 20));
	layout->addWidget(typeList);

	QFrame *right = new QFrame(this);
	right->setObjectName("switcherInputSelectPage");
	QVBoxLayout *rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(8, 8, 8, 8);

	QHBoxLayout *nameLayout = new QHBoxLayout();
	nameLayout->addWidget(new QLabel(QTStr("Name"), right));
	nameEdit = new QLineEdit(right);
	nameLayout->addWidget(nameEdit, 1);
	rightLayout->addLayout(nameLayout);

	connect(nameEdit.data(), &QLineEdit::textEdited, this, [this]() { nameEdited = true; });

	propertiesLayout = new QVBoxLayout();
	rightLayout->addLayout(propertiesLayout, 1);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, right);
	rightLayout->addWidget(buttons);
	layout->addWidget(right, 1);

	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		CreateInput();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	LoadTypes();

	for (const InputType &type : types) {
		typeList->addItem(new QListWidgetItem(main->GetSourceIcon(type.id.c_str()), QTStr(type.labelKey)));
	}

	connect(typeList.data(), &QListWidget::currentRowChanged, this, &SwitcherInputSelect::SelectType);

	if (!types.empty()) {
		typeList->setCurrentRow(0);
	}
}

SwitcherInputSelect::~SwitcherInputSelect()
{
	ReleaseDraft();
}

void SwitcherInputSelect::LoadTypes()
{
	/* vMix input types and the OBS sources that provide them, best first.
	 * Only the ones available on this system and build are listed. */
	std::vector<InputType> wanted = {
		{"Switcher.InputType.Video", {"ffmpeg_source"}, "{\"is_local_file\":true}", {}},
		{"Switcher.InputType.List", {"vlc_source"}, nullptr, {}},
		{"Switcher.InputType.Camera",
		 {"dshow_input", "v4l2_input", "macos-avcapture", "av_capture_input"},
		 nullptr,
		 {}},
		{"Switcher.InputType.ScreenCapture",
		 {"monitor_capture", "screen_capture", "pipewire-screen-capture-source", "xshm_input_v2", "xshm_input"},
		 nullptr,
		 {}},
		{"Switcher.InputType.WindowCapture",
		 {"window_capture", "pipewire-window-capture-source", "xcomposite_input"},
		 nullptr,
		 {}},
		{"Switcher.InputType.GameCapture", {"game_capture"}, nullptr, {}},
		{"Switcher.InputType.NDI", {"ndi_source"}, nullptr, {}},
		{"Switcher.InputType.Stream", {"ffmpeg_source"}, "{\"is_local_file\":false}", {}},
		{"Switcher.InputType.Image", {"image_source"}, nullptr, {}},
		{"Switcher.InputType.Photos", {"slideshow"}, nullptr, {}},
		{"Switcher.InputType.Colour", {"color_source"}, nullptr, {}},
		{"Switcher.InputType.Audio", {"ffmpeg_source"}, "{\"is_local_file\":true}", {}},
		{"Switcher.InputType.AudioInput",
		 {"wasapi_input_capture", "coreaudio_input_capture", "pulse_input_capture", "alsa_input_capture"},
		 nullptr,
		 {}},
		{"Switcher.InputType.DesktopAudio",
		 {"wasapi_output_capture", "coreaudio_output_capture", "pulse_output_capture"},
		 nullptr,
		 {}},
		{"Switcher.InputType.Title", {"text_gdiplus", "text_ft2_source"}, nullptr, {}},
		{"Switcher.InputType.WebBrowser", {"browser_source"}, nullptr, {}},
	};

	auto findType = [](const char *wantedId) -> std::string {
		const char *id;
		const char *unversioned;

		for (size_t i = 0; obs_enum_input_types2(i, &id, &unversioned); i++) {
			if (strcmp(unversioned, wantedId) != 0) {
				continue;
			}

			uint32_t flags = obs_get_source_output_flags(id);
			if ((flags & OBS_SOURCE_DEPRECATED) || (flags & OBS_SOURCE_CAP_DISABLED)) {
				continue;
			}

			return id;
		}

		return {};
	};

	for (InputType &type : wanted) {
		for (const char *candidate : type.candidates) {
			type.id = findType(candidate);
			if (!type.id.empty()) {
				break;
			}
		}

		if (!type.id.empty()) {
			types.push_back(type);
		}
	}
}

void SwitcherInputSelect::ReleaseDraft()
{
	delete view;
	draft = nullptr;
}

void SwitcherInputSelect::SelectType(int index)
{
	if (index < 0 || index >= (int)types.size() || index == currentType) {
		return;
	}

	ReleaseDraft();
	currentType = index;

	const InputType &type = types[index];

	OBSDataAutoRelease settings = type.presetJson ? obs_data_create_from_json(type.presetJson) : obs_data_create();
	draft = OBSSourceAutoRelease(obs_source_create_private(type.id.c_str(), "Switcher Input Draft", settings));
	if (!draft) {
		return;
	}

	if (!nameEdited) {
		nameEdit->setText(QString::fromStdString(UniqueSourceName(QT_TO_UTF8(QTStr(type.labelKey)))));
	}

	OBSDataAutoRelease draftSettings = obs_source_get_settings(draft);
	view = new OBSPropertiesView(draftSettings.Get(), draft.Get(), (PropertiesReloadCallback)obs_source_properties,
				     (PropertiesUpdateCallback) nullptr, (PropertiesVisualUpdateCb)obs_source_update);
	propertiesLayout->addWidget(view);
}

std::string SwitcherInputSelect::UniqueSourceName(const std::string &base)
{
	std::string name = base;

	for (int i = 2;; i++) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing) {
			return name;
		}
		name = base + " " + std::to_string(i);
	}
}

void SwitcherInputSelect::CreateInput()
{
	if (!draft || currentType < 0) {
		return;
	}

	const InputType &type = types[currentType];
	std::string id = type.id;
	OBSDataAutoRelease settings = obs_source_get_settings(draft);

	/* Let go of the draft first so devices it opened are free again */
	ReleaseDraft();

	std::string name = QT_TO_UTF8(nameEdit->text().trimmed());
	if (name.empty()) {
		name = QT_TO_UTF8(QTStr(type.labelKey));
	}

	/* Scenes and sources share one namespace, so the source gets the
	 * input type appended to its name */
	std::string sceneName = UniqueSourceName(name);
	std::string sourceName = UniqueSourceName(name + " (" + QT_TO_UTF8(QTStr(type.labelKey)) + ")");

	OBSSourceAutoRelease source = obs_source_create(id.c_str(), sourceName.c_str(), settings, nullptr);
	if (!source) {
		return;
	}

	OBSSceneAutoRelease scene = obs_scene_create(sceneName.c_str());
	obs_sceneitem_t *item = obs_scene_add(scene, source);

	/* Fit video inputs to the canvas, like a switcher input fills its output */
	if (item && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) {
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

	blog(LOG_INFO, "Switcher input '%s' created with %s source '%s'", sceneName.c_str(), id.c_str(),
	     sourceName.c_str());

	emit InputCreated(OBSSource(obs_scene_get_source(scene)));
}
