#pragma once

#include <components/SwitcherBusMeters.hpp>

#include <QDialog>
#include <QPointer>

class OBSBasic;
class QCheckBox;
class QComboBox;
class QListWidget;
class QSpinBox;
class QStackedWidget;

/* Switcher settings, laid out like vMix's Settings window: Display,
 * Options, Audio Outputs and Alerts. Settings that OBS already has (stream
 * and recording confirmations, monitoring device, transition duration) are
 * edited in place rather than duplicated. */
class SwitcherSettings : public QDialog {
	Q_OBJECT

	OBSBasic *main;

	QPointer<QListWidget> nav;
	QPointer<QStackedWidget> pages;

	/* Display */
	QPointer<QComboBox> tileSize;

	/* Options */
	QPointer<QCheckBox> confirmClose;
	QPointer<QCheckBox> confirmStartStream;
	QPointer<QCheckBox> confirmStopStream;
	QPointer<QCheckBox> confirmStopRecord;
	QPointer<QCheckBox> previewDoubleClick;
	QPointer<QSpinBox> transitionDuration;

	/* Audio Outputs */
	QPointer<QComboBox> monitoringDevice;
	QPointer<QCheckBox> defaultBus[SWITCHER_BUS_COUNT];
	QPointer<QComboBox> busOutput[SWITCHER_BUS_COUNT];

	/* Alerts */
	QPointer<QCheckBox> alertVolume;
	QPointer<QSpinBox> alertVolumeDb;
	QPointer<QSpinBox> alertVolumeSeconds;
	QPointer<QCheckBox> alertFtb;
	QPointer<QCheckBox> alertNotRecording;

	QWidget *CreateDisplayPage();
	QWidget *CreateOptionsPage();
	QWidget *CreateAudioPage();
	QWidget *CreateAlertsPage();

	void Save();

public:
	SwitcherSettings(OBSBasic *main);

signals:
	void Applied(bool tileSizeChanged);
	void OpenObsSettings();
};
