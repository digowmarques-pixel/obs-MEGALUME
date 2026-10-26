/******************************************************************************
    Copyright (C) 2026 by MEGALUME

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

/* Switcher Mode: a broadcast switcher style layout built on top of Studio
 * Mode. Preview (orange) and Program (green) sit side by side with a
 * transition column between them, the scene/source/mixer docks fill the
 * bottom of the window and the output controls move to a bottom bar. */

#include "OBSBasic.hpp"
#include "OBSBasicControls.hpp"

#include <qt-wrappers.hpp>

#include <QDateTime>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#define SWITCHER_THEME_ID "com.obsproject.Yami.Megalume"

void OBSBasic::InitSwitcherMode()
{
	bool enabled = config_get_bool(App()->GetUserConfig(), "BasicWindow", "SwitcherMode");

	const char *prevState = config_get_string(App()->GetUserConfig(), "BasicWindow", "SwitcherPrevDockState");
	if (prevState) {
		switcherPrevDockState = QByteArray::fromBase64(QByteArray(prevState));
	}

	/* Keep the on-air timer in sync with the outputs */
	auto startOnAir = [this]() {
		if (!switcherOnAirStart) {
			switcherOnAirStart = QDateTime::currentMSecsSinceEpoch();
		}
	};
	auto stopOnAir = [this]() {
		if (!StreamingActive() && !RecordingActive()) {
			switcherOnAirStart = 0;
		}
	};

	connect(this, &OBSBasic::StreamingStarted, this, startOnAir);
	connect(this, &OBSBasic::RecordingStarted, this, startOnAir);
	connect(this, &OBSBasic::StreamingStopped, this, stopOnAir, Qt::QueuedConnection);
	connect(this, &OBSBasic::RecordingStopped, this, stopOnAir, Qt::QueuedConnection);

	QSignalBlocker blocker(ui->actionSwitcherMode);
	ui->actionSwitcherMode->setChecked(enabled);

	if (enabled) {
		/* Reuse the saved dock layout, if there is one */
		bool hasDockState = config_get_string(App()->GetUserConfig(), "BasicWindow", "DockState") != nullptr;
		SetSwitcherMode(true, !hasDockState);
	}
}

void OBSBasic::on_actionSwitcherMode_triggered(bool checked)
{
	if (checked) {
		const OBSTheme *theme = App()->GetTheme();
		if (theme && theme->id != SWITCHER_THEME_ID && App()->SetTheme(SWITCHER_THEME_ID)) {
			config_set_string(App()->GetUserConfig(), "BasicWindow", "SwitcherPrevTheme",
					  QT_TO_UTF8(theme->id));
			config_set_string(App()->GetUserConfig(), "Appearance", "Theme", SWITCHER_THEME_ID);
		}
	}

	SetSwitcherMode(checked, true);

	if (!checked) {
		const char *prevTheme = config_get_string(App()->GetUserConfig(), "BasicWindow", "SwitcherPrevTheme");
		if (prevTheme && *prevTheme && App()->SetTheme(prevTheme)) {
			config_set_string(App()->GetUserConfig(), "Appearance", "Theme", prevTheme);
		}
		config_remove_value(App()->GetUserConfig(), "BasicWindow", "SwitcherPrevTheme");
	}

	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
}

void OBSBasic::SetSwitcherMode(bool enable, bool applyLayout)
{
	if (switcherMode == enable) {
		return;
	}

	if (enable && applyLayout) {
		switcherPrevDockState = saveState();
		config_set_string(App()->GetUserConfig(), "BasicWindow", "SwitcherPrevDockState",
				  switcherPrevDockState.toBase64().constData());
	}

	switcherMode = enable;
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SwitcherMode", enable);

	{
		QSignalBlocker blocker(ui->actionSwitcherMode);
		ui->actionSwitcherMode->setChecked(enable);
	}

	/* The controls and transitions docks are replaced by the bottom bar
	 * and the transition column, so they can't be toggled meanwhile */
	controlsDock->toggleViewAction()->setEnabled(!enable);
	ui->transitionsDock->toggleViewAction()->setEnabled(!enable);

	if (enable) {
		CreateSwitcherBar();

		if (!IsPreviewProgramMode()) {
			SetPreviewProgramMode(true);
		} else {
			ApplySwitcherProgramOptions();
		}

		controlsDock->setVisible(false);
		ui->transitionsDock->setVisible(false);

		if (applyLayout) {
			ApplySwitcherDockLayout();
		} else {
			setDockCornersVertical(false);
		}

		if (!config_get_bool(App()->GetUserConfig(), "BasicWindow", "VerticalVolumeControl")) {
			toggleMixerLayout();
		}
	} else {
		DestroySwitcherBar();

		if (applyLayout && !switcherPrevDockState.isEmpty()) {
			restoreState(switcherPrevDockState);
		} else {
			controlsDock->setVisible(true);
			ui->transitionsDock->setVisible(true);
		}

		setDockCornersVertical(ui->sideDocks->isChecked());

		/* Studio Mode stays on, only the switcher extras are removed so
		 * nothing changes on air */
		if (programOptions) {
			delete programOptions->findChild<QWidget *>("switcherClockFrame");
			programOptions->setObjectName(QString());
			programOptions->setMinimumWidth(0);
			programOptions->setMaximumWidth(QWIDGETSIZE_MAX);

			if (QLabel *label = programOptions->findChild<QLabel *>("quickTransitionsLabel")) {
				label->setText(QTStr("QuickTransitions"));
			}
		}

		if (transitionButton) {
			transitionButton->setText(QTStr("Transition"));
			setClasses(transitionButton, "");
		}
	}

	if (programLabel) {
		setClasses(programLabel, enable ? "label-preview-title label-switcher-program" : "label-preview-title");
	}
	setClasses(ui->previewLabel, enable ? "label-preview-title label-switcher-preview" : "label-preview-title");

	UpdatePreviewProgramIndicators();
}

void OBSBasic::CreateSwitcherBar()
{
	if (switcherBar) {
		return;
	}

	switcherBar = new QToolBar(QTStr("Switcher.BottomBar"), this);
	switcherBar->setObjectName("switcherBottomBar");
	switcherBar->setMovable(false);
	switcherBar->setFloatable(false);
	switcherBar->toggleViewAction()->setVisible(false);
	switcherBar->setContextMenuPolicy(Qt::PreventContextMenu);

	/* Add Input: new scenes are the switcher's inputs */
	QToolButton *addInput = new QToolButton(switcherBar);
	addInput->setObjectName("switcherAddInput");
	addInput->setText(QTStr("Switcher.AddInput"));
	addInput->setPopupMode(QToolButton::InstantPopup);
	addInput->setToolButtonStyle(Qt::ToolButtonTextOnly);

	QMenu *addInputMenu = new QMenu(addInput);
	addInputMenu->addAction(QTStr("Switcher.AddInput.Scene"), this, &OBSBasic::on_actionAddScene_triggered);
	addInputMenu->addAction(QTStr("Switcher.AddInput.Source"), this, &OBSBasic::AddSourceDialog);
	addInput->setMenu(addInputMenu);

	switcherBar->addWidget(addInput);
	switcherBar->addSeparator();

	/* Reuse the existing controls so their state stays in sync */
	switcherControls = qobject_cast<OBSBasicControls *>(controlsDock->widget());
	if (switcherControls) {
		controlsDock->setWidget(nullptr);
		switcherControls->SetHorizontalLayout(true);
		switcherBar->addWidget(switcherControls);
	}

	addToolBar(Qt::BottomToolBarArea, switcherBar);
	switcherBar->show();
}

void OBSBasic::DestroySwitcherBar()
{
	if (!switcherBar) {
		return;
	}

	if (switcherControls) {
		switcherControls->SetHorizontalLayout(false);
		switcherControls->setParent(nullptr);
		controlsDock->setWidget(switcherControls);
		switcherControls = nullptr;
	}

	removeToolBar(switcherBar);
	switcherBar->deleteLater();
	switcherBar = nullptr;
}

void OBSBasic::ApplySwitcherDockLayout()
{
	QList<QDockWidget *> inputDocks{ui->scenesDock, ui->sourcesDock, ui->mixerDock};

	for (QDockWidget *dock : inputDocks) {
		dock->setFloating(false);
		dock->setVisible(true);
	}

	/* Inputs on the left, sources next to them and the mixer on the right,
	 * all spanning the full width under preview and program */
	setDockCornersVertical(false);
	addDockWidget(Qt::BottomDockWidgetArea, ui->scenesDock);
	splitDockWidget(ui->scenesDock, ui->sourcesDock, Qt::Horizontal);
	splitDockWidget(ui->sourcesDock, ui->mixerDock, Qt::Horizontal);

	int cx = width();
	int bottomHeight = height() * 42 / 100;

	resizeDocks(inputDocks, {bottomHeight, bottomHeight, bottomHeight}, Qt::Vertical);
	resizeDocks(inputDocks, {cx * 50 / 100, cx * 18 / 100, cx * 32 / 100}, Qt::Horizontal);

	if (!ui->scenes->GetGridMode()) {
		on_actionSceneGridMode_triggered();
	}
}

void OBSBasic::ApplySwitcherProgramOptions()
{
	if (!programOptions) {
		return;
	}

	programOptions->setObjectName("switcherTransitionColumn");
	programOptions->setFixedWidth(140);

	if (transitionButton) {
		transitionButton->setText(QTStr("Switcher.QuickPlay"));
		setClasses(transitionButton, "switcher-quickplay");
	}

	if (QLabel *label = programOptions->findChild<QLabel *>("quickTransitionsLabel")) {
		label->setText(QTStr("Switcher.Transitions"));
	}

	if (programOptions->findChild<QWidget *>("switcherClockFrame")) {
		return;
	}

	/* Clock and on-air timer under the T-Bar */
	QFrame *clockFrame = new QFrame();
	clockFrame->setObjectName("switcherClockFrame");

	QVBoxLayout *clockLayout = new QVBoxLayout(clockFrame);
	clockLayout->setContentsMargins(4, 4, 4, 4);
	clockLayout->setSpacing(0);

	QLabel *onAirLabel = new QLabel(clockFrame);
	onAirLabel->setObjectName("switcherOnAirTime");
	onAirLabel->setAlignment(Qt::AlignCenter);

	QLabel *clockLabel = new QLabel(clockFrame);
	clockLabel->setObjectName("switcherClock");
	clockLabel->setAlignment(Qt::AlignCenter);

	clockLayout->addWidget(onAirLabel);
	clockLayout->addWidget(clockLabel);

	QVBoxLayout *layout = static_cast<QVBoxLayout *>(programOptions->layout());
	layout->insertWidget(layout->count() - 1, clockFrame);

	QTimer *clockTimer = new QTimer(clockFrame);
	connect(clockTimer, &QTimer::timeout, this, &OBSBasic::UpdateSwitcherOnAirTimer);
	clockTimer->start(250);

	UpdateSwitcherOnAirTimer();
}

void OBSBasic::UpdateSwitcherOnAirTimer()
{
	if (!programOptions) {
		return;
	}

	QLabel *onAirLabel = programOptions->findChild<QLabel *>("switcherOnAirTime");
	QLabel *clockLabel = programOptions->findChild<QLabel *>("switcherClock");
	if (!onAirLabel || !clockLabel) {
		return;
	}

	qint64 elapsed = switcherOnAirStart ? (QDateTime::currentMSecsSinceEpoch() - switcherOnAirStart) / 1000 : 0;
	QString onAir = QString::asprintf("%02lld:%02lld:%02lld", elapsed / 3600, (elapsed / 60) % 60, elapsed % 60);

	if (onAirLabel->text() != onAir) {
		onAirLabel->setText(onAir);
		setClasses(onAirLabel, switcherOnAirStart ? "state-active" : "");
	}

	QString clock = QTime::currentTime().toString("HH:mm:ss");
	if (clockLabel->text() != clock) {
		clockLabel->setText(clock);
	}
}
