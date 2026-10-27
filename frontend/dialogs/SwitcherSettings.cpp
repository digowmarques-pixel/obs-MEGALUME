#include "SwitcherSettings.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "moc_SwitcherSettings.cpp"

namespace {

bool UserBool(const char *key, bool defaultValue)
{
	config_t *config = App()->GetUserConfig();
	return config_has_user_value(config, "BasicWindow", key) ? config_get_bool(config, "BasicWindow", key)
								 : defaultValue;
}

int UserInt(const char *key, int defaultValue)
{
	config_t *config = App()->GetUserConfig();
	return config_has_user_value(config, "BasicWindow", key) ? (int)config_get_int(config, "BasicWindow", key)
								 : defaultValue;
}

QLabel *Hint(const QString &text, QWidget *parent)
{
	QLabel *label = new QLabel(text, parent);
	label->setObjectName("switcherSettingsHint");
	label->setWordWrap(true);
	return label;
}

} // namespace

SwitcherSettings::SwitcherSettings(OBSBasic *main_) : QDialog(main_), main(main_)
{
	setObjectName("switcherSettings");
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QTStr("Switcher.Settings.Title"));
	resize(820, 520);

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	nav = new QListWidget(this);
	nav->setObjectName("switcherSettingsNav");
	nav->setFixedWidth(190);
	nav->addItem(QTStr("Switcher.Settings.Display"));
	nav->addItem(QTStr("Switcher.Settings.Options"));
	nav->addItem(QTStr("Switcher.Settings.AudioOutputs"));
	nav->addItem(QTStr("Switcher.Settings.Alerts"));
	layout->addWidget(nav);

	QVBoxLayout *right = new QVBoxLayout();
	right->setContentsMargins(16, 16, 16, 12);

	pages = new QStackedWidget(this);
	pages->addWidget(CreateDisplayPage());
	pages->addWidget(CreateOptionsPage());
	pages->addWidget(CreateAudioPage());
	pages->addWidget(CreateAlertsPage());
	right->addWidget(pages, 1);

	QHBoxLayout *buttons = new QHBoxLayout();
	QPushButton *obsSettings = new QPushButton(QTStr("Switcher.Settings.ObsSettings"), this);
	QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->addWidget(obsSettings);
	buttons->addStretch(1);
	buttons->addWidget(box);
	right->addLayout(buttons);

	layout->addLayout(right, 1);

	connect(nav.data(), &QListWidget::currentRowChanged, pages.data(), &QStackedWidget::setCurrentIndex);
	nav->setCurrentRow(0);

	connect(box, &QDialogButtonBox::accepted, this, [this]() {
		Save();
		accept();
	});
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(obsSettings, &QPushButton::clicked, this, &SwitcherSettings::OpenObsSettings);
}

QWidget *SwitcherSettings::CreateDisplayPage()
{
	QWidget *page = new QWidget(this);
	QFormLayout *form = new QFormLayout(page);

	tileSize = new QComboBox(page);
	tileSize->addItem(QTStr("Switcher.Settings.TileSmall"), 0);
	tileSize->addItem(QTStr("Switcher.Settings.TileMedium"), 1);
	tileSize->addItem(QTStr("Switcher.Settings.TileLarge"), 2);
	tileSize->setCurrentIndex(tileSize->findData(UserInt("SwitcherTileSize", 1)));
	form->addRow(QTStr("Switcher.Settings.TileSize"), tileSize);

	form->addRow(QString(), Hint(QTStr("Switcher.Settings.ThemeHint"), page));
	return page;
}

QWidget *SwitcherSettings::CreateOptionsPage()
{
	QWidget *page = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(page);

	config_t *config = App()->GetUserConfig();

	confirmClose = new QCheckBox(QTStr("Switcher.Settings.ConfirmClose"), page);
	confirmClose->setChecked(UserBool("SwitcherConfirmClose", true));

	confirmStartStream = new QCheckBox(QTStr("Switcher.Settings.ConfirmStartStream"), page);
	confirmStartStream->setChecked(config_get_bool(config, "BasicWindow", "WarnBeforeStartingStream"));

	confirmStopStream = new QCheckBox(QTStr("Switcher.Settings.ConfirmStopStream"), page);
	confirmStopStream->setChecked(config_get_bool(config, "BasicWindow", "WarnBeforeStoppingStream"));

	confirmStopRecord = new QCheckBox(QTStr("Switcher.Settings.ConfirmStopRecord"), page);
	confirmStopRecord->setChecked(config_get_bool(config, "BasicWindow", "WarnBeforeStoppingRecord"));

	previewDoubleClick = new QCheckBox(QTStr("Switcher.Settings.PreviewDoubleClick"), page);
	previewDoubleClick->setChecked(UserBool("SwitcherPreviewDoubleClick", false));

	for (QCheckBox *check : {confirmClose.data(), confirmStartStream.data(), confirmStopStream.data(),
				 confirmStopRecord.data(), previewDoubleClick.data()}) {
		layout->addWidget(check);
	}

	QFormLayout *form = new QFormLayout();
	transitionDuration = new QSpinBox(page);
	transitionDuration->setRange(50, 20000);
	transitionDuration->setSingleStep(50);
	transitionDuration->setSuffix(" ms");
	transitionDuration->setValue(main->GetTransitionDuration());
	form->addRow(QTStr("Switcher.Settings.TransitionDuration"), transitionDuration);
	layout->addSpacing(8);
	layout->addLayout(form);
	layout->addStretch(1);

	return page;
}

QWidget *SwitcherSettings::CreateAudioPage()
{
	QWidget *page = new QWidget(this);
	QFormLayout *form = new QFormLayout(page);

	/* Headphones: the monitoring device of the current profile */
	monitoringDevice = new QComboBox(page);
	monitoringDevice->addItem(QTStr("Basic.Settings.Advanced.Audio.MonitoringDevice.Default"), "default");
	obs_enum_audio_monitoring_devices(
		[](void *data, const char *name, const char *id) {
			static_cast<QComboBox *>(data)->addItem(QT_UTF8(name), QT_UTF8(id));
			return true;
		},
		monitoringDevice.data());

	const char *currentId = config_get_string(main->Config(), "Audio", "MonitoringDeviceId");
	int index = monitoringDevice->findData(QT_UTF8(currentId ? currentId : "default"));
	monitoringDevice->setCurrentIndex(std::max(index, 0));
	monitoringDevice->setEnabled(obs_audio_monitoring_available());
	form->addRow(QTStr("Switcher.Settings.Headphones"), monitoringDevice);

	/* Buses new audio sources are routed to */
	QHBoxLayout *buses = new QHBoxLayout();
	int mask = UserInt("SwitcherDefaultBuses", 0x3F);
	const char *names[SWITCHER_BUS_COUNT] = {"Switcher.Bus.MasterName", "Switcher.Bus.AName", "Switcher.Bus.BName",
						 "Switcher.Bus.CName", "Switcher.Bus.DName"};
	for (int i = 0; i < SWITCHER_BUS_COUNT; i++) {
		defaultBus[i] = new QCheckBox(QTStr(names[i]), page);
		defaultBus[i]->setChecked((mask & (1 << i)) != 0);
		buses->addWidget(defaultBus[i]);
	}
	buses->addStretch(1);
	form->addRow(QTStr("Switcher.Settings.DefaultBuses"), buses);

	form->addRow(QString(), Hint(QTStr("Switcher.Settings.BusesHint"), page));
	return page;
}

QWidget *SwitcherSettings::CreateAlertsPage()
{
	QWidget *page = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(page);

	QHBoxLayout *volumeRow = new QHBoxLayout();
	alertVolume = new QCheckBox(QTStr("Switcher.Settings.AlertVolume"), page);
	alertVolume->setChecked(UserBool("SwitcherAlertVolume", false));

	alertVolumeDb = new QSpinBox(page);
	alertVolumeDb->setRange(-60, 0);
	alertVolumeDb->setSuffix(" dB");
	alertVolumeDb->setValue(UserInt("SwitcherAlertVolumeDb", -50));

	alertVolumeSeconds = new QSpinBox(page);
	alertVolumeSeconds->setRange(1, 120);
	alertVolumeSeconds->setSuffix(" s");
	alertVolumeSeconds->setValue(UserInt("SwitcherAlertVolumeSeconds", 5));

	volumeRow->addWidget(alertVolume);
	volumeRow->addWidget(alertVolumeDb);
	volumeRow->addWidget(new QLabel(QTStr("Switcher.Settings.AlertFor"), page));
	volumeRow->addWidget(alertVolumeSeconds);
	volumeRow->addStretch(1);
	layout->addLayout(volumeRow);

	alertFtb = new QCheckBox(QTStr("Switcher.Settings.AlertFtb"), page);
	alertFtb->setChecked(UserBool("SwitcherAlertFtb", false));
	layout->addWidget(alertFtb);

	alertNotRecording = new QCheckBox(QTStr("Switcher.Settings.AlertNotRecording"), page);
	alertNotRecording->setChecked(UserBool("SwitcherAlertNotRecording", false));
	layout->addWidget(alertNotRecording);

	layout->addWidget(Hint(QTStr("Switcher.Settings.AlertsHint"), page));
	layout->addStretch(1);
	return page;
}

void SwitcherSettings::Save()
{
	config_t *config = App()->GetUserConfig();

	int oldTileSize = UserInt("SwitcherTileSize", 1);
	int newTileSize = tileSize->currentData().toInt();
	config_set_int(config, "BasicWindow", "SwitcherTileSize", newTileSize);

	config_set_bool(config, "BasicWindow", "SwitcherConfirmClose", confirmClose->isChecked());
	config_set_bool(config, "BasicWindow", "WarnBeforeStartingStream", confirmStartStream->isChecked());
	config_set_bool(config, "BasicWindow", "WarnBeforeStoppingStream", confirmStopStream->isChecked());
	config_set_bool(config, "BasicWindow", "WarnBeforeStoppingRecord", confirmStopRecord->isChecked());
	config_set_bool(config, "BasicWindow", "SwitcherPreviewDoubleClick", previewDoubleClick->isChecked());

	if (transitionDuration->value() != main->GetTransitionDuration()) {
		main->SetTransitionDuration(transitionDuration->value());
	}

	if (monitoringDevice->isEnabled()) {
		std::string name = QT_TO_UTF8(monitoringDevice->currentText());
		std::string id = QT_TO_UTF8(monitoringDevice->currentData().toString());
		const char *oldId = config_get_string(main->Config(), "Audio", "MonitoringDeviceId");

		if (!oldId || id != oldId) {
			config_set_string(main->Config(), "Audio", "MonitoringDeviceName", name.c_str());
			config_set_string(main->Config(), "Audio", "MonitoringDeviceId", id.c_str());
			obs_set_audio_monitoring_device(name.c_str(), id.c_str());
			config_save_safe(main->Config(), "tmp", nullptr);
		}
	}

	/* Only tracks 1-5 are shown as buses; track 6 keeps the OBS default */
	int mask = UserInt("SwitcherDefaultBuses", 0x3F) & ~((1 << SWITCHER_BUS_COUNT) - 1);
	for (int i = 0; i < SWITCHER_BUS_COUNT; i++) {
		if (defaultBus[i]->isChecked()) {
			mask |= 1 << i;
		}
	}
	config_set_int(config, "BasicWindow", "SwitcherDefaultBuses", mask);

	config_set_bool(config, "BasicWindow", "SwitcherAlertVolume", alertVolume->isChecked());
	config_set_int(config, "BasicWindow", "SwitcherAlertVolumeDb", alertVolumeDb->value());
	config_set_int(config, "BasicWindow", "SwitcherAlertVolumeSeconds", alertVolumeSeconds->value());
	config_set_bool(config, "BasicWindow", "SwitcherAlertFtb", alertFtb->isChecked());
	config_set_bool(config, "BasicWindow", "SwitcherAlertNotRecording", alertNotRecording->isChecked());

	config_save_safe(config, "tmp", nullptr);

	emit Applied(oldTileSize != newTileSize);
}
