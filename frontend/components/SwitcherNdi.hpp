#pragma once

#include <obs.hpp>

#include <QObject>
#include <QString>

/* Program and Preview sent over NDI, like vMix's NDI outputs. The sending
 * itself is DistroAV's "ndi_output"; this decides what feeds it and keeps
 * it running. Settings are saved in the profile. */
class SwitcherNdi : public QObject {
	Q_OBJECT

public:
	struct Settings {
		bool program = false;
		QString programName;
		bool preview = false;
		QString previewName;
	};

private:
	OBSOutputAutoRelease programOutput;
	QString programRunningName;

	OBSOutputAutoRelease previewOutput;
	QString previewRunningName;
	obs_view_t *previewView = nullptr;
	video_t *previewVideo = nullptr;

	void StopProgram();
	void StopPreview();
	bool StartProgram(const QString &name);
	bool StartPreview(const QString &name);

public:
	SwitcherNdi(QObject *parent = nullptr);
	~SwitcherNdi();

	/* Whether DistroAV is loaded, so NDI can be sent and received */
	static bool OutputAvailable();
	static bool InputAvailable();
	static bool FilterAvailable();

	Settings Load() const;
	void Save(const Settings &settings);

	/* Starts or stops the outputs to match the saved settings */
	void Apply();
	void StopAll();

	bool ProgramActive() const;
	bool PreviewActive() const;

	/* What the Preview output shows; follows the switcher's Preview */
	void SetPreviewSource(obs_source_t *scene);

	/* Per input: send this input as its own NDI source */
	static bool InputSending(obs_source_t *scene);
	static void SetInputSending(obs_source_t *scene, bool send);
	static QString InputNdiName(obs_source_t *scene);
};
