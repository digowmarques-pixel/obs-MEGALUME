#include "SwitcherMultiCorder.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>
#include <qt-wrappers.hpp>

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstring>

#include "moc_SwitcherMultiCorder.cpp"

#define CONFIG_SECTION "SwitcherMultiCorder"
#define PROGRAM_ROW_ID "__program__"

SwitcherMultiCorder::SwitcherMultiCorder(OBSBasic *main_) : QObject(main_), main(main_) {}

SwitcherMultiCorder::~SwitcherMultiCorder()
{
	for (auto &[uuid, recorder] : recorders) {
		recorder->stopSignal.Disconnect();
		if (recorder->output) {
			obs_output_force_stop(recorder->output);
		}
	}

	while (!recorders.empty()) {
		Release(recorders.begin()->first);
	}
}

SwitcherMultiCorder::Settings SwitcherMultiCorder::LoadSettings() const
{
	config_t *config = main->Config();
	Settings settings;

	const char *folder = config_get_string(config, CONFIG_SECTION, "Folder");
	const char *format = config_get_string(config, CONFIG_SECTION, "Format");

	settings.folder = folder && *folder ? QT_UTF8(folder)
					    : QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	settings.format = format && *format ? QT_UTF8(format) : QString("mkv");
	if (config_has_user_value(config, CONFIG_SECTION, "BitrateKbps")) {
		settings.bitrateKbps = (int)config_get_int(config, CONFIG_SECTION, "BitrateKbps");
	}

	return settings;
}

void SwitcherMultiCorder::SaveSettings(const Settings &settings)
{
	config_t *config = main->Config();
	config_set_string(config, CONFIG_SECTION, "Folder", QT_TO_UTF8(settings.folder));
	config_set_string(config, CONFIG_SECTION, "Format", QT_TO_UTF8(settings.format));
	config_set_int(config, CONFIG_SECTION, "BitrateKbps", settings.bitrateKbps);
	config_save_safe(config, "tmp", nullptr);
}

bool SwitcherMultiCorder::IsSelected(obs_source_t *scene)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	return obs_data_get_bool(settings, "switcher_multicorder");
}

void SwitcherMultiCorder::SetSelected(obs_source_t *scene, bool selected)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	obs_data_set_bool(settings, "switcher_multicorder", selected);
}

std::string SwitcherMultiCorder::PickVideoEncoder()
{
	/* Hardware first, x264 as the fallback that is always there */
	static const char *preferred[] = {"obs_nvenc_h264_tex", "jim_nvenc", "h264_texture_amf", "obs_qsv11_v2",
					  "obs_x264"};

	for (const char *wanted : preferred) {
		const char *id;
		for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
			if (strcmp(id, wanted) == 0 && !(obs_get_encoder_caps(id) & OBS_ENCODER_CAP_DEPRECATED)) {
				return id;
			}
		}
	}

	return "obs_x264";
}

static QString SafeFileName(QString name)
{
	static const QString invalid = "\\/:*?\"<>|";
	for (QChar &c : name) {
		if (invalid.contains(c)) {
			c = '_';
		}
	}
	return name.trimmed();
}

bool SwitcherMultiCorder::Start(obs_source_t *scene, const Settings &settings)
{
	std::string uuid = obs_source_get_uuid(scene);
	if (recorders.count(uuid)) {
		return true;
	}

	auto recorder = std::make_unique<Recorder>();
	recorder->owner = this;
	recorder->uuid = uuid;

	auto fail = [&](const QString &message) {
		recorders[uuid] = std::move(recorder);
		Release(uuid);
		lastStatus[uuid] = QTStr("Switcher.MultiCorder.Status.Error").arg(message);
		emit StateChanged();
		return false;
	};

	/* The input renders in its own view at the canvas size */
	obs_video_info ovi;
	obs_get_video_info(&ovi);

	recorder->view = obs_view_create();
	obs_view_set_source(recorder->view, 0, scene);
	video_t *video = obs_view_add2(recorder->view, &ovi);
	if (!video) {
		return fail("view");
	}

	std::string name = "switcher_multicorder_" + uuid;

	OBSDataAutoRelease videoSettings = obs_data_create();
	obs_data_set_int(videoSettings, "bitrate", settings.bitrateKbps);
	obs_data_set_string(videoSettings, "rate_control", "CBR");
	obs_data_set_int(videoSettings, "keyint_sec", 2);
	obs_data_set_string(videoSettings, "preset", "veryfast");

	std::string encoder = PickVideoEncoder();
	recorder->video = obs_video_encoder_create(encoder.c_str(), name.c_str(), videoSettings, nullptr);
	if (!recorder->video && encoder != "obs_x264") {
		encoder = "obs_x264";
		recorder->video = obs_video_encoder_create(encoder.c_str(), name.c_str(), videoSettings, nullptr);
	}
	if (!recorder->video) {
		return fail("video encoder");
	}
	obs_encoder_set_video(recorder->video, video);

	OBSDataAutoRelease audioSettings = obs_data_create();
	obs_data_set_int(audioSettings, "bitrate", 192);
	recorder->audio = obs_audio_encoder_create("ffmpeg_aac", name.c_str(), audioSettings, 0, nullptr);
	if (!recorder->audio) {
		return fail("audio encoder");
	}
	obs_encoder_set_audio(recorder->audio, obs_get_audio());

	QDir().mkpath(settings.folder);
	QString file = QString("%1 %2.%3")
			       .arg(SafeFileName(QT_UTF8(obs_source_get_name(scene))))
			       .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss"))
			       .arg(settings.format);
	recorder->path = QDir(settings.folder).filePath(file);

	OBSDataAutoRelease outputSettings = obs_data_create();
	obs_data_set_string(outputSettings, "path", QT_TO_UTF8(recorder->path));
	recorder->output = obs_output_create("ffmpeg_muxer", name.c_str(), outputSettings, nullptr);
	if (!recorder->output) {
		return fail("ffmpeg_muxer");
	}

	obs_output_set_video_encoder(recorder->output, recorder->video);
	obs_output_set_audio_encoder(recorder->output, recorder->audio, 0);
	recorder->stopSignal.Connect(obs_output_get_signal_handler(recorder->output), "stop", OutputStopped,
				     recorder.get());

	if (!obs_output_start(recorder->output)) {
		const char *error = obs_output_get_last_error(recorder->output);
		return fail(error ? QT_UTF8(error) : QString("start"));
	}

	blog(LOG_INFO, "MultiCorder recording '%s' with %s to %s", obs_source_get_name(scene), encoder.c_str(),
	     QT_TO_UTF8(recorder->path));

	recorder->started = QDateTime::currentMSecsSinceEpoch();
	lastStatus.erase(uuid);
	recorders[uuid] = std::move(recorder);
	emit StateChanged();
	return true;
}

void SwitcherMultiCorder::Stop(const std::string &uuid)
{
	auto it = recorders.find(uuid);
	if (it != recorders.end() && it->second->output) {
		obs_output_stop(it->second->output);
	}
}

void SwitcherMultiCorder::StopAll()
{
	for (auto &[uuid, recorder] : recorders) {
		if (recorder->output) {
			obs_output_stop(recorder->output);
		}
	}
}

void SwitcherMultiCorder::OutputStopped(void *data, calldata_t *calldata)
{
	Recorder *recorder = static_cast<Recorder *>(data);
	int code = (int)calldata_int(calldata, "code");
	const char *error = calldata_string(calldata, "last_error");

	QString status = code == OBS_OUTPUT_SUCCESS
				 ? QTStr("Switcher.MultiCorder.Status.Saved")
				 : QTStr("Switcher.MultiCorder.Status.Error")
					   .arg(error && *error ? QT_UTF8(error) : QString::number(code));

	/* Stop arrives on an output thread; clean up on the UI thread */
	QMetaObject::invokeMethod(
		recorder->owner,
		[owner = recorder->owner, uuid = recorder->uuid, status]() {
			owner->lastStatus[uuid] = status;
			owner->Release(uuid);
			emit owner->StateChanged();
		},
		Qt::QueuedConnection);
}

void SwitcherMultiCorder::Release(const std::string &uuid)
{
	auto it = recorders.find(uuid);
	if (it == recorders.end()) {
		return;
	}

	Recorder *recorder = it->second.get();
	recorder->stopSignal.Disconnect();

	if (!recorder->path.isEmpty()) {
		lastPath[uuid] = recorder->path;
	}

	/* Output first, then the encoders using the view's video, then the view */
	recorder->output = nullptr;
	recorder->video = nullptr;
	recorder->audio = nullptr;

	if (recorder->view) {
		obs_view_remove(recorder->view);
		obs_view_set_source(recorder->view, 0, nullptr);
		obs_view_destroy(recorder->view);
		recorder->view = nullptr;
	}

	recorders.erase(it);
}

bool SwitcherMultiCorder::IsRecording(const std::string &uuid) const
{
	auto it = recorders.find(uuid);
	return it != recorders.end() && it->second->output && obs_output_active(it->second->output);
}

bool SwitcherMultiCorder::AnyRecording() const
{
	return !recorders.empty();
}

qint64 SwitcherMultiCorder::StartedAt(const std::string &uuid) const
{
	auto it = recorders.find(uuid);
	return it != recorders.end() ? it->second->started : 0;
}

QString SwitcherMultiCorder::Status(const std::string &uuid) const
{
	if (recorders.count(uuid)) {
		return QTStr("Switcher.MultiCorder.Status.Recording");
	}

	auto it = lastStatus.find(uuid);
	return it != lastStatus.end() ? it->second : QString();
}

QString SwitcherMultiCorder::Path(const std::string &uuid) const
{
	auto it = recorders.find(uuid);
	if (it != recorders.end()) {
		return it->second->path;
	}

	auto last = lastPath.find(uuid);
	return last != lastPath.end() ? last->second : QString();
}

/* ------------------------------------------------------------------------- */

SwitcherMultiCorderWindow::SwitcherMultiCorderWindow(OBSBasic *main_, SwitcherMultiCorder *corder_)
	: QDialog(main_),
	  main(main_),
	  corder(corder_)
{
	setObjectName("switcherMultiCorder");
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QTStr("Switcher.MultiCorder.Title"));
	resize(860, 560);

	SwitcherMultiCorder::Settings settings = corder->LoadSettings();

	QVBoxLayout *layout = new QVBoxLayout(this);

	QHBoxLayout *top = new QHBoxLayout();
	QFormLayout *form = new QFormLayout();

	format = new QComboBox(this);
	format->addItem(QTStr("Switcher.MultiCorder.FormatMkv"), "mkv");
	format->addItem("MP4", "mp4");
	format->addItem("MOV", "mov");
	format->setCurrentIndex(std::max(format->findData(settings.format), 0));
	form->addRow(QTStr("Switcher.MultiCorder.Format"), format);

	quality = new QComboBox(this);
	for (int mbps : {6, 12, 20, 40}) {
		quality->addItem(QString("%1 Mbps").arg(mbps), mbps * 1000);
	}
	int qualityIndex = quality->findData(settings.bitrateKbps);
	quality->setCurrentIndex(qualityIndex >= 0 ? qualityIndex : 1);
	form->addRow(QTStr("Switcher.MultiCorder.Quality"), quality);

	QHBoxLayout *folderRow = new QHBoxLayout();
	folder = new QLineEdit(settings.folder, this);
	QPushButton *browse = new QPushButton(QTStr("Browse"), this);
	folderRow->addWidget(folder, 1);
	folderRow->addWidget(browse);
	form->addRow(QTStr("Switcher.MultiCorder.Folder"), folderRow);

	top->addLayout(form, 1);

	startButton = new QPushButton(this);
	startButton->setObjectName("switcherMultiCorderStart");
	startButton->setMinimumSize(150, 90);
	top->addWidget(startButton);
	layout->addLayout(top);

	list = new QTreeWidget(this);
	list->setRootIsDecorated(false);
	list->setColumnCount(4);
	list->setHeaderLabels({QTStr("Switcher.MultiCorder.Input"), QTStr("Switcher.MultiCorder.Duration"),
			       QTStr("Switcher.MultiCorder.StatusHeader"), QTStr("Switcher.MultiCorder.File")});
	list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	list->header()->setSectionResizeMode(3, QHeaderView::Stretch);
	layout->addWidget(list, 1);

	QLabel *note = new QLabel(QTStr("Switcher.MultiCorder.Note"), this);
	note->setObjectName("switcherSettingsHint");
	note->setWordWrap(true);
	layout->addWidget(note);

	connect(browse, &QPushButton::clicked, this, [this]() {
		QString dir =
			QFileDialog::getExistingDirectory(this, QTStr("Switcher.MultiCorder.Folder"), folder->text());
		if (!dir.isEmpty()) {
			folder->setText(dir);
		}
	});

	connect(startButton.data(), &QPushButton::clicked, this, &SwitcherMultiCorderWindow::StartStop);
	connect(list.data(), &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
		if (column != 0) {
			return;
		}

		std::string id = QT_TO_UTF8(item->data(0, Qt::UserRole).toString());
		bool checked = item->checkState(0) == Qt::Checked;

		if (id == PROGRAM_ROW_ID) {
			config_set_bool(main->Config(), CONFIG_SECTION, "Program", checked);
			return;
		}

		OBSSourceAutoRelease scene = obs_get_source_by_uuid(id.c_str());
		if (scene) {
			SwitcherMultiCorder::SetSelected(scene, checked);
		}
	});

	connect(corder, &SwitcherMultiCorder::StateChanged, this, &SwitcherMultiCorderWindow::UpdateRows);
	connect(main, &OBSBasic::RecordingStarted, this, &SwitcherMultiCorderWindow::UpdateRows);
	connect(main, &OBSBasic::RecordingStopped, this, &SwitcherMultiCorderWindow::UpdateRows);

	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &SwitcherMultiCorderWindow::UpdateRows);
	timer->start(1000);

	Populate();
	UpdateRows();
}

void SwitcherMultiCorderWindow::Populate()
{
	QSignalBlocker blocker(list);
	list->clear();

	/* The Program is the regular OBS recording */
	QTreeWidgetItem *program = new QTreeWidgetItem(list);
	program->setText(0, QTStr("Switcher.MultiCorder.Program"));
	program->setData(0, Qt::UserRole, QString(PROGRAM_ROW_ID));
	program->setFlags(program->flags() | Qt::ItemIsUserCheckable);
	program->setCheckState(0, config_get_bool(main->Config(), CONFIG_SECTION, "Program") ? Qt::Checked
											     : Qt::Unchecked);

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *scene = scenes.sources.array[i];
		QTreeWidgetItem *item = new QTreeWidgetItem(list);
		item->setText(0, QT_UTF8(obs_source_get_name(scene)));
		item->setData(0, Qt::UserRole, QT_UTF8(obs_source_get_uuid(scene)));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, SwitcherMultiCorder::IsSelected(scene) ? Qt::Checked : Qt::Unchecked);
	}

	obs_frontend_source_list_free(&scenes);
}

void SwitcherMultiCorderWindow::UpdateRows()
{
	QSignalBlocker blocker(list);
	qint64 now = QDateTime::currentMSecsSinceEpoch();

	auto duration = [now](qint64 started) {
		if (!started) {
			return QString();
		}
		qint64 seconds = (now - started) / 1000;
		return QString::asprintf("%02lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
	};

	for (int i = 0; i < list->topLevelItemCount(); i++) {
		QTreeWidgetItem *item = list->topLevelItem(i);
		std::string id = QT_TO_UTF8(item->data(0, Qt::UserRole).toString());

		if (id == PROGRAM_ROW_ID) {
			bool active = main->RecordingActive();
			item->setText(2, active ? QTStr("Switcher.MultiCorder.Status.Recording") : QString());
			continue;
		}

		item->setText(1, duration(corder->StartedAt(id)));
		item->setText(2, corder->Status(id));
		item->setText(3, corder->Path(id));
	}

	bool active = corder->AnyRecording() ||
		      (config_get_bool(main->Config(), CONFIG_SECTION, "Program") && main->RecordingActive());
	startButton->setText(active ? QTStr("Switcher.MultiCorder.Stop") : QTStr("Switcher.MultiCorder.Start"));
	setClasses(startButton, active ? "state-active" : "");
	format->setEnabled(!active);
	quality->setEnabled(!active);
	folder->setEnabled(!active);
}

SwitcherMultiCorder::Settings SwitcherMultiCorderWindow::CurrentSettings() const
{
	SwitcherMultiCorder::Settings settings;
	settings.folder = folder->text().trimmed();
	settings.format = format->currentData().toString();
	settings.bitrateKbps = quality->currentData().toInt();
	return settings;
}

void SwitcherMultiCorderWindow::StartStop()
{
	bool programChecked = false;
	std::vector<std::string> inputs;

	for (int i = 0; i < list->topLevelItemCount(); i++) {
		QTreeWidgetItem *item = list->topLevelItem(i);
		if (item->checkState(0) != Qt::Checked) {
			continue;
		}

		std::string id = QT_TO_UTF8(item->data(0, Qt::UserRole).toString());
		if (id == PROGRAM_ROW_ID) {
			programChecked = true;
		} else {
			inputs.push_back(id);
		}
	}

	if (corder->AnyRecording() || (programChecked && main->RecordingActive())) {
		corder->StopAll();
		if (programChecked && main->RecordingActive()) {
			main->StopRecording();
		}
		return;
	}

	SwitcherMultiCorder::Settings settings = CurrentSettings();
	corder->SaveSettings(settings);

	for (const std::string &id : inputs) {
		OBSSourceAutoRelease scene = obs_get_source_by_uuid(id.c_str());
		if (scene) {
			corder->Start(scene, settings);
		}
	}

	if (programChecked && !main->RecordingActive()) {
		main->StartRecording();
	}

	UpdateRows();
}
