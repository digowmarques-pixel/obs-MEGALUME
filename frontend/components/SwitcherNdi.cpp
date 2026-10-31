#include "SwitcherNdi.hpp"

#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include "moc_SwitcherNdi.cpp"

namespace {

constexpr const char *Section = "SwitcherNdi";
constexpr const char *FilterName = "MEGALUME NDI";

QString ProfileString(const char *key, const QString &fallback)
{
	const char *value = config_get_string(OBSBasic::Get()->Config(), Section, key);
	return value && *value ? QT_UTF8(value) : fallback;
}

bool ProfileBool(const char *key)
{
	return config_get_bool(OBSBasic::Get()->Config(), Section, key);
}

OBSOutputAutoRelease CreateNdiOutput(const char *outputName, const QString &ndiName)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "ndi_name", QT_TO_UTF8(ndiName));
	obs_data_set_string(settings, "ndi_groups", "");
	return obs_output_create("ndi_output", outputName, settings, nullptr);
}

} // namespace

SwitcherNdi::SwitcherNdi(QObject *parent) : QObject(parent) {}

SwitcherNdi::~SwitcherNdi()
{
	StopAll();
}

bool SwitcherNdi::OutputAvailable()
{
	return (obs_get_output_flags("ndi_output") & OBS_OUTPUT_VIDEO) != 0;
}

bool SwitcherNdi::InputAvailable()
{
	return obs_get_source_output_flags("ndi_source") != 0;
}

bool SwitcherNdi::FilterAvailable()
{
	return obs_get_source_output_flags("ndi_filter") != 0;
}

SwitcherNdi::Settings SwitcherNdi::Load() const
{
	Settings settings;
	settings.program = ProfileBool("Program");
	settings.programName = ProfileString("ProgramName", "MEGALUME Program");
	settings.preview = ProfileBool("Preview");
	settings.previewName = ProfileString("PreviewName", "MEGALUME Preview");
	return settings;
}

void SwitcherNdi::Save(const Settings &settings)
{
	config_t *config = OBSBasic::Get()->Config();
	config_set_bool(config, Section, "Program", settings.program);
	config_set_string(config, Section, "ProgramName", QT_TO_UTF8(settings.programName.trimmed()));
	config_set_bool(config, Section, "Preview", settings.preview);
	config_set_string(config, Section, "PreviewName", QT_TO_UTF8(settings.previewName.trimmed()));
	config_save_safe(config, "tmp", nullptr);
}

void SwitcherNdi::Apply()
{
	Settings settings = Load();
	bool available = OutputAvailable();

	/* A renamed output has to restart to announce the new name */
	if (!settings.program || !available || settings.programName != programRunningName) {
		StopProgram();
	}
	if (settings.program && available && !programOutput) {
		StartProgram(settings.programName);
	}

	if (!settings.preview || !available || settings.previewName != previewRunningName) {
		StopPreview();
	}
	if (settings.preview && available && !previewOutput) {
		StartPreview(settings.previewName);
	}
}

void SwitcherNdi::StopAll()
{
	StopProgram();
	StopPreview();
}

bool SwitcherNdi::StartProgram(const QString &name)
{
	/* Without media set, an output takes the main video and audio, which
	 * is what Program shows and plays */
	programOutput = CreateNdiOutput("MEGALUME NDI Program", name);
	if (!programOutput || !obs_output_start(programOutput)) {
		const char *error = programOutput ? obs_output_get_last_error(programOutput) : nullptr;
		blog(LOG_WARNING, "Switcher NDI: Program output did not start: %s", error ? error : "?");
		programOutput = nullptr;
		return false;
	}

	programRunningName = name;
	blog(LOG_INFO, "Switcher NDI: sending Program as '%s'", QT_TO_UTF8(name));
	return true;
}

bool SwitcherNdi::StartPreview(const QString &name)
{
	/* Preview renders in a view of its own at the canvas size */
	previewView = obs_view_create();
	obs_video_info ovi;
	obs_get_video_info(&ovi);
	previewVideo = obs_view_add2(previewView, &ovi);
	if (!previewVideo) {
		obs_view_destroy(previewView);
		previewView = nullptr;
		return false;
	}

	OBSBasic *main = OBSBasic::Get();
	obs_view_set_source(previewView, 0, main->IsPreviewProgramMode() ? main->GetCurrentSceneSource() : nullptr);

	previewOutput = CreateNdiOutput("MEGALUME NDI Preview", name);
	if (previewOutput) {
		obs_output_set_media(previewOutput, previewVideo, obs_get_audio());
	}
	if (!previewOutput || !obs_output_start(previewOutput)) {
		const char *error = previewOutput ? obs_output_get_last_error(previewOutput) : nullptr;
		blog(LOG_WARNING, "Switcher NDI: Preview output did not start: %s", error ? error : "?");
		StopPreview();
		return false;
	}

	previewRunningName = name;
	blog(LOG_INFO, "Switcher NDI: sending Preview as '%s'", QT_TO_UTF8(name));
	return true;
}

void SwitcherNdi::StopProgram()
{
	if (programOutput) {
		obs_output_stop(programOutput);
		programOutput = nullptr;
		blog(LOG_INFO, "Switcher NDI: Program output stopped");
	}
	programRunningName.clear();
}

void SwitcherNdi::StopPreview()
{
	/* Output first, then the view whose video it uses */
	if (previewOutput) {
		obs_output_stop(previewOutput);
		previewOutput = nullptr;
		blog(LOG_INFO, "Switcher NDI: Preview output stopped");
	}
	if (previewView) {
		obs_view_remove(previewView);
		obs_view_set_source(previewView, 0, nullptr);
		obs_view_destroy(previewView);
		previewView = nullptr;
		previewVideo = nullptr;
	}
	previewRunningName.clear();
}

bool SwitcherNdi::ProgramActive() const
{
	return programOutput && obs_output_active(programOutput);
}

bool SwitcherNdi::PreviewActive() const
{
	return previewOutput && obs_output_active(previewOutput);
}

void SwitcherNdi::SetPreviewSource(obs_source_t *scene)
{
	if (previewView) {
		obs_view_set_source(previewView, 0, scene);
	}
}

QString SwitcherNdi::InputNdiName(obs_source_t *scene)
{
	return QString("MEGALUME - %1").arg(QT_UTF8(obs_source_get_name(scene)));
}

bool SwitcherNdi::InputSending(obs_source_t *scene)
{
	OBSSourceAutoRelease filter = obs_source_get_filter_by_name(scene, FilterName);
	return filter != nullptr;
}

void SwitcherNdi::SetInputSending(obs_source_t *scene, bool send)
{
	OBSSourceAutoRelease existing = obs_source_get_filter_by_name(scene, FilterName);
	if (!send) {
		if (existing) {
			obs_source_filter_remove(scene, existing);
		}
		return;
	}
	if (existing || !FilterAvailable()) {
		return;
	}

	/* DistroAV's NDI filter sends whatever the input shows and plays */
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "ndi_filter_ndiname", QT_TO_UTF8(InputNdiName(scene)));
	obs_data_set_string(settings, "ndi_filter_ndigroups", "");
	OBSSourceAutoRelease filter = obs_source_create("ndi_filter", FilterName, settings, nullptr);
	if (filter) {
		obs_source_filter_add(scene, filter);
	}
}
