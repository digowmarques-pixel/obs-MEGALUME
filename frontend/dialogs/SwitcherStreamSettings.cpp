#include "SwitcherStreamSettings.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <string>

#include "moc_SwitcherStreamSettings.cpp"

#define CONFIG_SECTION "SwitcherStream"

SwitcherMultistream::SwitcherMultistream(OBSBasic *main_, MainOutputGetter getter)
	: QObject(main_),
	  main(main_),
	  getMainOutput(std::move(getter))
{
	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		runtime[i].owner = this;
		runtime[i].index = i;
		runtime[i].status = QTStr("Switcher.Stream.Status.Off");
	}

	Load();

	/* Extra destinations follow the main stream */
	connect(main, &OBSBasic::StreamingStarted, this, [this]() {
		if (startWithMain) {
			StartEnabled();
		}
	});
	connect(main, &OBSBasic::StreamingStopping, this, &SwitcherMultistream::StopAll);
	connect(main, &OBSBasic::StreamingStopped, this, &SwitcherMultistream::StopAll);
}

SwitcherMultistream::~SwitcherMultistream()
{
	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		if (runtime[i].output) {
			obs_output_force_stop(runtime[i].output);
		}
		ReleaseOutput(i);
	}
}

void SwitcherMultistream::Load()
{
	/* Destinations belong to the profile, like the main stream settings */
	config_t *config = main->Config();
	startWithMain = config_has_user_value(config, CONFIG_SECTION, "StartWithMain")
				? config_get_bool(config, CONFIG_SECTION, "StartWithMain")
				: true;

	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		std::string prefix = "Dest" + std::to_string(i + 2);
		Destination &dest = destinations[i];

		dest.enabled = config_get_bool(config, CONFIG_SECTION, (prefix + "Enabled").c_str());
		const char *name = config_get_string(config, CONFIG_SECTION, (prefix + "Name").c_str());
		const char *server = config_get_string(config, CONFIG_SECTION, (prefix + "Server").c_str());
		const char *key = config_get_string(config, CONFIG_SECTION, (prefix + "Key").c_str());

		dest.name = name && *name ? QT_UTF8(name) : QTStr("Switcher.Stream.Destination").arg(i + 2);
		dest.server = QT_UTF8(server ? server : "");
		dest.key = QT_UTF8(key ? key : "");
	}
}

void SwitcherMultistream::Save()
{
	config_t *config = main->Config();
	config_set_bool(config, CONFIG_SECTION, "StartWithMain", startWithMain);

	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		std::string prefix = "Dest" + std::to_string(i + 2);
		const Destination &dest = destinations[i];

		config_set_bool(config, CONFIG_SECTION, (prefix + "Enabled").c_str(), dest.enabled);
		config_set_string(config, CONFIG_SECTION, (prefix + "Name").c_str(), QT_TO_UTF8(dest.name));
		config_set_string(config, CONFIG_SECTION, (prefix + "Server").c_str(), QT_TO_UTF8(dest.server));
		config_set_string(config, CONFIG_SECTION, (prefix + "Key").c_str(), QT_TO_UTF8(dest.key));
	}

	config_save_safe(config, "tmp", nullptr);
}

bool SwitcherMultistream::IsActive(int index) const
{
	return runtime[index].output && obs_output_active(runtime[index].output);
}

uint64_t SwitcherMultistream::TotalBytes(int index) const
{
	return runtime[index].output ? obs_output_get_total_bytes(runtime[index].output) : 0;
}

void SwitcherMultistream::SetStatus(int index, const QString &status)
{
	runtime[index].status = status;
	emit StateChanged(index);
}

bool SwitcherMultistream::Start(int index)
{
	Runtime &rt = runtime[index];
	const Destination &dest = destinations[index];

	if (rt.output) {
		if (obs_output_active(rt.output)) {
			return true;
		}
		/* Stopped, but its stop notification has not been handled yet */
		ReleaseOutput(index);
	}

	if (dest.server.trimmed().isEmpty()) {
		SetStatus(index, QTStr("Switcher.Stream.Status.NoServer"));
		return false;
	}

	OBSOutputAutoRelease mainOutput = getMainOutput();
	obs_encoder_t *video = mainOutput ? obs_output_get_video_encoder(mainOutput) : nullptr;
	obs_encoder_t *audio = mainOutput ? obs_output_get_audio_encoder(mainOutput, 0) : nullptr;

	if (!video || !audio) {
		SetStatus(index, QTStr("Switcher.Stream.Status.NeedsMain"));
		return false;
	}

	OBSDataAutoRelease serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "server", QT_TO_UTF8(dest.server.trimmed()));
	obs_data_set_string(serviceSettings, "key", QT_TO_UTF8(dest.key.trimmed()));
	obs_data_set_bool(serviceSettings, "use_auth", false);

	std::string name = "switcher_destination_" + std::to_string(index + 2);
	rt.service = obs_service_create_private("rtmp_custom", name.c_str(), serviceSettings);
	rt.output = obs_output_create("rtmp_output", name.c_str(), nullptr, nullptr);
	if (!rt.service || !rt.output) {
		ReleaseOutput(index);
		SetStatus(index, QTStr("Switcher.Stream.Status.Error").arg("rtmp_output"));
		return false;
	}

	obs_output_set_video_encoder(rt.output, video);
	obs_output_set_audio_encoder(rt.output, audio, 0);
	obs_output_set_service(rt.output, rt.service);

	signal_handler_t *handler = obs_output_get_signal_handler(rt.output);
	rt.startSignal.Connect(handler, "start", OutputStarted, &rt);
	rt.stopSignal.Connect(handler, "stop", OutputStopped, &rt);
	rt.reconnectSignal.Connect(handler, "reconnect", OutputReconnecting, &rt);
	rt.reconnectSuccessSignal.Connect(handler, "reconnect_success", OutputReconnected, &rt);

	SetStatus(index, QTStr("Switcher.Stream.Status.Connecting"));
	blog(LOG_INFO, "Switcher destination %d starting: %s", index + 2, QT_TO_UTF8(dest.server));

	if (!obs_output_start(rt.output)) {
		const char *error = obs_output_get_last_error(rt.output);
		QString message = QTStr("Switcher.Stream.Status.Error").arg(error ? QT_UTF8(error) : QString("?"));
		ReleaseOutput(index);
		SetStatus(index, message);
		return false;
	}

	return true;
}

void SwitcherMultistream::Stop(int index)
{
	if (runtime[index].output) {
		obs_output_stop(runtime[index].output);
	}
}

void SwitcherMultistream::StartEnabled()
{
	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		if (destinations[i].enabled) {
			Start(i);
		}
	}
}

void SwitcherMultistream::StopAll()
{
	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		Stop(i);
	}
}

void SwitcherMultistream::ReleaseOutput(int index)
{
	Runtime &rt = runtime[index];
	rt.startSignal.Disconnect();
	rt.stopSignal.Disconnect();
	rt.reconnectSignal.Disconnect();
	rt.reconnectSuccessSignal.Disconnect();
	rt.output = nullptr;
	rt.service = nullptr;
}

/* Output signals arrive on libobs threads; state changes happen on the UI */

void SwitcherMultistream::OutputStarted(void *data, calldata_t *)
{
	Runtime *rt = static_cast<Runtime *>(data);
	QMetaObject::invokeMethod(
		rt->owner,
		[owner = rt->owner, index = rt->index]() {
			owner->SetStatus(index, QTStr("Switcher.Stream.Status.Live"));
		},
		Qt::QueuedConnection);
}

void SwitcherMultistream::OutputStopped(void *data, calldata_t *calldata)
{
	Runtime *rt = static_cast<Runtime *>(data);
	int code = (int)calldata_int(calldata, "code");
	const char *error = calldata_string(calldata, "last_error");
	obs_output_t *stopped = static_cast<obs_output_t *>(calldata_ptr(calldata, "output"));
	QString message = code == OBS_OUTPUT_SUCCESS
				  ? QTStr("Switcher.Stream.Status.Off")
				  : QTStr("Switcher.Stream.Status.Error")
					    .arg(error && *error ? QT_UTF8(error) : QString::number(code));

	QMetaObject::invokeMethod(
		rt->owner,
		[owner = rt->owner, index = rt->index, message, stopped]() {
			/* A restart may already own a newer output by now */
			if (owner->runtime[index].output.Get() != stopped) {
				return;
			}
			blog(LOG_INFO, "Switcher destination %d stopped", index + 2);
			owner->ReleaseOutput(index);
			owner->SetStatus(index, message);
		},
		Qt::QueuedConnection);
}

void SwitcherMultistream::OutputReconnecting(void *data, calldata_t *)
{
	Runtime *rt = static_cast<Runtime *>(data);
	QMetaObject::invokeMethod(
		rt->owner,
		[owner = rt->owner, index = rt->index]() {
			owner->SetStatus(index, QTStr("Switcher.Stream.Status.Reconnecting"));
		},
		Qt::QueuedConnection);
}

void SwitcherMultistream::OutputReconnected(void *data, calldata_t *calldata)
{
	OutputStarted(data, calldata);
}

/* ------------------------------------------------------------------------- */

SwitcherStreamSettings::SwitcherStreamSettings(OBSBasic *main_, SwitcherMultistream *multistream_)
	: QDialog(main_),
	  main(main_),
	  multistream(multistream_)
{
	setObjectName("switcherStreamSettings");
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QTStr("Switcher.Stream.Title"));
	resize(980, 420);

	QVBoxLayout *layout = new QVBoxLayout(this);

	QGridLayout *grid = new QGridLayout();
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(6);

	const char *headers[] = {"#",
				 "Switcher.Stream.Enabled",
				 "Name",
				 "Switcher.Stream.Server",
				 "Switcher.Stream.Key",
				 "Switcher.Stream.StatusHeader",
				 ""};
	for (int c = 0; c < 7; c++) {
		QLabel *header = new QLabel(c == 0 || !*headers[c] ? QString(headers[c]) : QTStr(headers[c]), this);
		header->setObjectName("switcherStreamHeader");
		grid->addWidget(header, 0, c);
	}

	/* Destination 1 is the main OBS stream */
	QLabel *mainNumber = new QLabel("1", this);
	mainNumber->setObjectName("switcherStreamNumber");
	grid->addWidget(mainNumber, 1, 0);

	QLabel *mainInfo = new QLabel(QTStr("Switcher.Stream.MainInfo"), this);
	grid->addWidget(mainInfo, 1, 2, 1, 2);

	QPushButton *mainSettings = new QPushButton(QTStr("Switcher.Stream.MainSettings"), this);
	grid->addWidget(mainSettings, 1, 4);
	connect(mainSettings, &QPushButton::clicked, this, &SwitcherStreamSettings::OpenMainStreamSettings);

	mainStatus = new QLabel(this);
	grid->addWidget(mainStatus, 1, 5);

	mainToggle = new QPushButton(this);
	grid->addWidget(mainToggle, 1, 6);
	connect(mainToggle.data(), &QPushButton::clicked, this, [this]() {
		if (main->StreamingActive()) {
			main->StopStreaming();
		} else {
			SaveRows();
			main->StartStreaming();
		}
	});

	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		Row &row = rows[i];
		const SwitcherMultistream::Destination &dest = multistream->Get(i);
		int r = i + 2;

		QLabel *number = new QLabel(QString::number(i + 2), this);
		number->setObjectName("switcherStreamNumber");

		row.enabled = new QCheckBox(this);
		row.enabled->setChecked(dest.enabled);
		row.enabled->setToolTip(QTStr("Switcher.Stream.EnabledTT"));

		row.name = new QLineEdit(dest.name, this);
		row.server = new QLineEdit(dest.server, this);
		row.server->setPlaceholderText("rtmp://...");
		row.server->setMinimumWidth(260);
		row.key = new QLineEdit(dest.key, this);
		row.key->setEchoMode(QLineEdit::Password);

		row.status = new QLabel(this);
		row.status->setMinimumWidth(150);
		row.toggle = new QPushButton(this);

		grid->addWidget(number, r, 0);
		grid->addWidget(row.enabled, r, 1);
		grid->addWidget(row.name, r, 2);
		grid->addWidget(row.server, r, 3);
		grid->addWidget(row.key, r, 4);
		grid->addWidget(row.status, r, 5);
		grid->addWidget(row.toggle, r, 6);

		connect(row.toggle.data(), &QPushButton::clicked, this, [this, i]() {
			SaveRows();
			if (multistream->IsActive(i)) {
				multistream->Stop(i);
			} else {
				multistream->Start(i);
			}
			UpdateStates();
		});

		for (QLineEdit *edit : {row.name.data(), row.server.data(), row.key.data()}) {
			connect(edit, &QLineEdit::editingFinished, this, &SwitcherStreamSettings::SaveRows);
		}
		connect(row.enabled.data(), &QCheckBox::toggled, this, &SwitcherStreamSettings::SaveRows);
	}

	grid->setColumnStretch(3, 2);
	grid->setColumnStretch(4, 1);
	layout->addLayout(grid);

	startWithMain = new QCheckBox(QTStr("Switcher.Stream.StartWithMain"), this);
	startWithMain->setChecked(multistream->StartWithMain());
	connect(startWithMain.data(), &QCheckBox::toggled, this, &SwitcherStreamSettings::SaveRows);
	layout->addWidget(startWithMain);

	QLabel *note = new QLabel(QTStr("Switcher.Stream.Note"), this);
	note->setWordWrap(true);
	note->setObjectName("switcherStreamNote");
	layout->addWidget(note);

	layout->addStretch(1);

	QHBoxLayout *buttons = new QHBoxLayout();
	QPushButton *startAll = new QPushButton(QTStr("Switcher.Stream.StartAll"), this);
	QPushButton *stopAll = new QPushButton(QTStr("Switcher.Stream.StopAll"), this);
	QPushButton *close = new QPushButton(QTStr("Switcher.Stream.SaveClose"), this);
	buttons->addWidget(startAll);
	buttons->addWidget(stopAll);
	buttons->addStretch(1);
	buttons->addWidget(close);
	layout->addLayout(buttons);

	connect(startAll, &QPushButton::clicked, this, [this]() {
		SaveRows();
		if (main->StreamingActive()) {
			multistream->StartEnabled();
		} else {
			/* Extras follow once the main stream is up */
			multistream->SetStartWithMain(true);
			startWithMain->setChecked(true);
			main->StartStreaming();
		}
		UpdateStates();
	});
	connect(stopAll, &QPushButton::clicked, this, [this]() {
		multistream->StopAll();
		if (main->StreamingActive()) {
			main->StopStreaming();
		}
	});
	connect(close, &QPushButton::clicked, this, [this]() {
		SaveRows();
		accept();
	});

	connect(multistream, &SwitcherMultistream::StateChanged, this, &SwitcherStreamSettings::UpdateStates);
	connect(main, &OBSBasic::StreamingStarted, this, &SwitcherStreamSettings::UpdateStates);
	connect(main, &OBSBasic::StreamingStopped, this, &SwitcherStreamSettings::UpdateStates);

	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &SwitcherStreamSettings::UpdateBitrates);
	timer->start(1000);

	UpdateStates();
}

void SwitcherStreamSettings::SaveRows()
{
	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		SwitcherMultistream::Destination &dest = multistream->Get(i);
		dest.enabled = rows[i].enabled->isChecked();
		dest.name = rows[i].name->text().trimmed();
		dest.server = rows[i].server->text().trimmed();
		dest.key = rows[i].key->text().trimmed();
	}

	multistream->SetStartWithMain(startWithMain->isChecked());
	multistream->Save();
}

void SwitcherStreamSettings::UpdateStates()
{
	bool mainLive = main->StreamingActive();
	mainStatus->setText(mainLive ? QTStr("Switcher.Stream.Status.Live") : QTStr("Switcher.Stream.Status.Off"));
	setClasses(mainStatus, mainLive ? "text-success" : "");
	mainToggle->setText(mainLive ? QTStr("Switcher.Stream.Stop") : QTStr("Switcher.Stream.Start"));

	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		Row &row = rows[i];
		bool active = multistream->IsActive(i);

		QString status = multistream->Status(i);
		if (active && !row.bitrate.isEmpty()) {
			status += "  " + row.bitrate;
		}

		row.status->setText(status);
		row.toggle->setText(active ? QTStr("Switcher.Stream.Stop") : QTStr("Switcher.Stream.Start"));
		row.toggle->setEnabled(mainLive || active);
		row.server->setEnabled(!active);
		row.key->setEnabled(!active);
	}
}

void SwitcherStreamSettings::UpdateBitrates()
{
	/* Runs once a second, so the byte difference is the bitrate */
	for (int i = 0; i < SWITCHER_EXTRA_DESTINATIONS; i++) {
		Row &row = rows[i];
		uint64_t bytes = multistream->TotalBytes(i);
		row.bitrate = bytes > row.lastBytes ? QString("%1 kbps").arg((bytes - row.lastBytes) * 8 / 1000)
						    : QString();
		row.lastBytes = bytes;
	}

	UpdateStates();
}
