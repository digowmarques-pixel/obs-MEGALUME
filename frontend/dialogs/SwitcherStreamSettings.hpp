#pragma once

#include <obs.hpp>

#include <QDialog>
#include <QPointer>

#include <array>
#include <functional>

class OBSBasic;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

/* Extra stream destinations next to the main OBS stream, like vMix's five
 * streaming slots. Destination 1 is the regular OBS stream; 2-5 are RTMP
 * outputs that reuse the main stream's encoders, so the video is encoded
 * once whatever the number of destinations. */
#define SWITCHER_EXTRA_DESTINATIONS 4

class SwitcherMultistream : public QObject {
	Q_OBJECT

public:
	struct Destination {
		bool enabled = false;
		QString name;
		QString server;
		QString key;
	};

	/* Returns the live main stream output, or nothing when it is off */
	using MainOutputGetter = std::function<OBSOutputAutoRelease()>;

	SwitcherMultistream(OBSBasic *main, MainOutputGetter getter);
	~SwitcherMultistream();

	void Load();
	void Save();

	Destination &Get(int index) { return destinations[index]; }
	bool StartWithMain() const { return startWithMain; }
	void SetStartWithMain(bool start) { startWithMain = start; }

	bool Start(int index);
	void Stop(int index);
	void StartEnabled();
	void StopAll();

	bool IsActive(int index) const;
	QString Status(int index) const { return runtime[index].status; }
	uint64_t TotalBytes(int index) const;

signals:
	void StateChanged(int index);

private:
	struct Runtime {
		SwitcherMultistream *owner = nullptr;
		int index = 0;
		OBSOutputAutoRelease output;
		OBSServiceAutoRelease service;
		OBSSignal startSignal;
		OBSSignal stopSignal;
		OBSSignal reconnectSignal;
		OBSSignal reconnectSuccessSignal;
		QString status;
	};

	OBSBasic *main;
	MainOutputGetter getMainOutput;
	std::array<Destination, SWITCHER_EXTRA_DESTINATIONS> destinations;
	std::array<Runtime, SWITCHER_EXTRA_DESTINATIONS> runtime;
	bool startWithMain = true;

	void SetStatus(int index, const QString &status);
	void ReleaseOutput(int index);

	static void OutputStarted(void *data, calldata_t *calldata);
	static void OutputStopped(void *data, calldata_t *calldata);
	static void OutputReconnecting(void *data, calldata_t *calldata);
	static void OutputReconnected(void *data, calldata_t *calldata);
};

/* The window: destination 1 (main stream) plus the extra destinations, with
 * per-destination start/stop and status. */
class SwitcherStreamSettings : public QDialog {
	Q_OBJECT

	struct Row {
		QPointer<QCheckBox> enabled;
		QPointer<QLineEdit> name;
		QPointer<QLineEdit> server;
		QPointer<QLineEdit> key;
		QPointer<QLabel> status;
		QPointer<QPushButton> toggle;
		uint64_t lastBytes = 0;
		QString bitrate;
	};

	OBSBasic *main;
	SwitcherMultistream *multistream;
	std::array<Row, SWITCHER_EXTRA_DESTINATIONS> rows;
	QPointer<QLabel> mainStatus;
	QPointer<QPushButton> mainToggle;
	QPointer<QCheckBox> startWithMain;

	void SaveRows();
	void UpdateStates();
	void UpdateBitrates();

public:
	SwitcherStreamSettings(OBSBasic *main, SwitcherMultistream *multistream);

signals:
	void OpenMainStreamSettings();
};
