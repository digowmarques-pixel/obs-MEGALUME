#pragma once

#include <obs.hpp>

#include <QDialog>
#include <QPointer>

#include <map>
#include <memory>
#include <string>

class OBSBasic;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

/* Records individual inputs to their own files, like vMix's MultiCorder.
 * Each input renders in its own view at the canvas size and gets its own
 * video encoder; audio is the Master mix. The Program recording stays the
 * regular OBS recording. */
class SwitcherMultiCorder : public QObject {
	Q_OBJECT

	struct Recorder {
		SwitcherMultiCorder *owner = nullptr;
		std::string uuid;
		obs_view_t *view = nullptr;
		OBSEncoderAutoRelease video;
		OBSEncoderAutoRelease audio;
		OBSOutputAutoRelease output;
		OBSSignal stopSignal;
		QString path;
		QString status;
		qint64 started = 0;
	};

	OBSBasic *main;
	std::map<std::string, std::unique_ptr<Recorder>> recorders;
	/* What the last recording of each input ended with */
	std::map<std::string, QString> lastStatus;
	std::map<std::string, QString> lastPath;

	static void OutputStopped(void *data, calldata_t *calldata);
	void Release(const std::string &uuid);
	static std::string PickVideoEncoder();

public:
	struct Settings {
		QString folder;
		QString format;
		int bitrateKbps = 12000;
	};

	SwitcherMultiCorder(OBSBasic *main);
	~SwitcherMultiCorder();

	Settings LoadSettings() const;
	void SaveSettings(const Settings &settings);

	bool Start(obs_source_t *scene, const Settings &settings);
	void Stop(const std::string &uuid);
	void StopAll();
	/* Stops and releases every recorder right away, for when the scenes
	 * they record are about to go away */
	void ForceStopAll();

	bool IsRecording(const std::string &uuid) const;
	bool AnyRecording() const;
	qint64 StartedAt(const std::string &uuid) const;
	QString Status(const std::string &uuid) const;
	QString Path(const std::string &uuid) const;

	/* Whether an input is ticked for recording, saved with the input */
	static bool IsSelected(obs_source_t *scene);
	static void SetSelected(obs_source_t *scene, bool selected);

signals:
	void StateChanged();
};

class SwitcherMultiCorderWindow : public QDialog {
	Q_OBJECT

	OBSBasic *main;
	SwitcherMultiCorder *corder;

	QPointer<QComboBox> format;
	QPointer<QComboBox> quality;
	QPointer<QLineEdit> folder;
	QPointer<QTreeWidget> list;
	QPointer<QPushButton> startButton;

	void Populate();
	void UpdateRows();
	SwitcherMultiCorder::Settings CurrentSettings() const;
	void StartStop();

public:
	SwitcherMultiCorderWindow(OBSBasic *main, SwitcherMultiCorder *corder);
};
