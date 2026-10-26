#pragma once

#include <obs.hpp>

#include <QDialog>
#include <QPointer>

#include <vector>

class OBSBasic;
class OBSPropertiesView;
class QLineEdit;
class QListWidget;
class QVBoxLayout;

/* vMix-style "Add Input" window: pick an input type on the left, configure
 * it on the right. Accepting creates a new switcher input, a scene holding
 * the configured source fitted to the canvas. */
class SwitcherInputSelect : public QDialog {
	Q_OBJECT

	struct InputType {
		const char *labelKey;
		std::vector<const char *> candidates;
		const char *presetJson;
		std::string id;
	};

	std::vector<InputType> types;

	QPointer<QListWidget> typeList;
	QPointer<QLineEdit> nameEdit;
	QPointer<QVBoxLayout> propertiesLayout;
	QPointer<OBSPropertiesView> view;

	/* Private source the properties view edits until the user accepts */
	OBSSource draft;
	int currentType = -1;
	bool nameEdited = false;

	void LoadTypes();
	void SelectType(int index);
	void ReleaseDraft();
	void CreateInput();

public:
	SwitcherInputSelect(OBSBasic *main);
	~SwitcherInputSelect();

	static std::string UniqueSourceName(const std::string &base);

signals:
	void InputCreated(OBSSource scene);
};
