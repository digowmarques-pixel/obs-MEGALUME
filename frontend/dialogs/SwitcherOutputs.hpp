#pragma once

#include <QDialog>
#include <QPointer>

#include <vector>

class OBSBasic;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

/* vMix-style External Outputs window: choose what each monitor shows full
 * screen (Program, Preview, Multiview or any input), the Multiview layout,
 * and start or stop the virtual camera. Changes apply right away. */
class SwitcherOutputs : public QDialog {
	Q_OBJECT

	OBSBasic *main;

	std::vector<QPointer<QComboBox>> monitors;
	QPointer<QComboBox> multiviewLayout;
	QPointer<QPushButton> virtualCam;

	/* NDI */
	QPointer<QCheckBox> ndiProgram;
	QPointer<QLineEdit> ndiProgramName;
	QPointer<QCheckBox> ndiPreview;
	QPointer<QLineEdit> ndiPreviewName;
	QPointer<QLabel> ndiStatus;
	void ApplyNdi();

	void PopulateSources(QComboBox *combo);
	QString CurrentOutput(int monitor) const;
	void SetOutput(int monitor, const QString &output);
	void Refresh();

public:
	SwitcherOutputs(OBSBasic *main);
};
