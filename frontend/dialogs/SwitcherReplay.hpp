#pragma once

#include <QDialog>
#include <QPointer>

class OBSBasic;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

/* vMix-style Replay window: keeps the replay buffer running, saves the
 * last seconds as a clip and plays it in the REPLAY input, in slow motion
 * if wanted, then goes back to the input that was on air. */
class SwitcherReplay : public QDialog {
	Q_OBJECT

	OBSBasic *main;

	QPointer<QLabel> status;
	QPointer<QPushButton> bufferButton;
	QPointer<QComboBox> speed;
	QPointer<QCheckBox> returnAfter;
	QPointer<QListWidget> clips;

	void Refresh();
	void RefreshClips();

public:
	SwitcherReplay(OBSBasic *main);
};
