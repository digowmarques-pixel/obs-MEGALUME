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
#include "OBSProjector.hpp"

#include <components/SceneTree.hpp>
#include <components/SwitcherInputGrid.hpp>
#include <dialogs/NameDialog.hpp>

#include <qt-wrappers.hpp>

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>

#define SWITCHER_THEME_ID "com.obsproject.Yami.Megalume"

/* Overlays use their own output channels, drawn on top of Program. Channels
 * 0-6 belong to OBS and low numbers are popular with plugins. */
#define SWITCHER_OVERLAY_CHANNEL 40

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
		CreateSwitcherInputGrid();

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
		DestroySwitcherInputGrid();

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

	/* QToolBar manages the visibility of widgets added to it directly,
	 * so everything goes into one container instead */
	QWidget *barContents = new QWidget(switcherBar);
	barContents->setObjectName("switcherBarContents");
	barContents->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	QHBoxLayout *barLayout = new QHBoxLayout(barContents);
	barLayout->setContentsMargins(0, 0, 0, 0);
	barLayout->setSpacing(4);

	/* Add Input: new scenes are the switcher's inputs */
	QToolButton *addInput = new QToolButton(barContents);
	addInput->setObjectName("switcherAddInput");
	addInput->setText(QTStr("Switcher.AddInput"));
	addInput->setPopupMode(QToolButton::InstantPopup);
	addInput->setToolButtonStyle(Qt::ToolButtonTextOnly);

	QMenu *addInputMenu = new QMenu(addInput);
	addInputMenu->addAction(QTStr("Switcher.AddInput.Scene"), this, &OBSBasic::on_actionAddScene_triggered);
	addInputMenu->addAction(QTStr("Switcher.AddInput.Source"), this, &OBSBasic::AddSourceDialog);
	addInput->setMenu(addInputMenu);

	barLayout->addWidget(addInput);

	/* Reuse the existing controls so their state stays in sync */
	switcherControls = qobject_cast<OBSBasicControls *>(controlsDock->widget());
	if (switcherControls) {
		controlsDock->setWidget(nullptr);
		switcherControls->SetHorizontalLayout(true);
		barLayout->addWidget(switcherControls, 1);
		switcherControls->setVisible(true);
	}

	switcherBar->addWidget(barContents);
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
}

void OBSBasic::ApplySwitcherProgramOptions()
{
	if (!programOptions) {
		return;
	}

	programOptions->setObjectName("switcherTransitionColumn");
	programOptions->setFixedWidth(190);

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

void OBSBasic::CreateSwitcherInputGrid()
{
	if (switcherInputGrid) {
		return;
	}

	/* The grid takes the place of the scene list inside the scenes dock,
	 * so the dock keeps its saved position and toolbar */
	switcherInputGrid = new SwitcherInputGrid(ui->scenesFrame);
	ui->verticalLayout_12->insertWidget(0, switcherInputGrid, 1);
	ui->scenes->setVisible(false);
	ui->scenesDock->setWindowTitle(QTStr("Switcher.Inputs"));

	connect(switcherInputGrid.data(), &SwitcherInputGrid::PreviewRequested, this,
		[this](OBSSource source) { SetCurrentScene(source); });

	connect(switcherInputGrid.data(), &SwitcherInputGrid::QuickPlayRequested, this,
		[this](OBSSource source) { TransitionToScene(source); });

	connect(switcherInputGrid.data(), &SwitcherInputGrid::CutRequested, this, [this](OBSSource source) {
		/* A forced transition never reports stopping, so refresh the
			 * Program title here */
		TransitionToScene(source, true);
		UpdatePreviewProgramIndicators();
	});

	connect(switcherInputGrid.data(), &SwitcherInputGrid::CloseRequested, this, [this](OBSSource source) {
		SetCurrentScene(source);
		RemoveSelectedScene();
	});

	connect(switcherInputGrid.data(), &SwitcherInputGrid::OverlayRequested, this, &OBSBasic::ToggleSwitcherOverlay);

	connect(switcherInputGrid.data(), &SwitcherInputGrid::ProjectorRequested, this,
		[this](OBSSource source) { OpenProjector(source, -1, ProjectorType::Scene); });

	connect(switcherInputGrid.data(), &SwitcherInputGrid::MenuRequested, this,
		[this](OBSSource source, const QPoint &globalPos) {
			SetCurrentScene(source);

			QMenu menu(this);
			menu.addAction(QTStr("Rename"), this, [this, source]() {
				std::string name = obs_source_get_name(source);
				if (!NameDialog::AskForName(this, QTStr("Rename"), QTStr("Basic.Main.AddSceneDlg.Text"),
							    name, QT_UTF8(name.c_str())) ||
				    name.empty() || name == obs_source_get_name(source)) {
					return;
				}

				OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
				if (existing) {
					OBSMessageBox::warning(this, QTStr("NameExists.Title"),
							       QTStr("NameExists.Text"));
					return;
				}

				obs_source_set_name(source, name.c_str());
			});
			menu.addAction(QTStr("Filters"), this, &OBSBasic::OpenSceneFilters);
			menu.addAction(QTStr("Projector.Open.Scene"), this, &OBSBasic::OpenSceneWindow);
			menu.addSeparator();
			menu.addAction(QTStr("Remove"), this, &OBSBasic::RemoveSelectedScene);
			menu.exec(globalPos);
		});

	/* Follow the scene list: adds, removes, renames, reorders and
	 * scene collection changes */
	QAbstractItemModel *model = ui->scenes->model();
	connect(model, &QAbstractItemModel::rowsInserted, switcherInputGrid,
		[this]() { QueueSwitcherInputsRefresh(); });
	connect(model, &QAbstractItemModel::rowsRemoved, switcherInputGrid, [this]() { QueueSwitcherInputsRefresh(); });
	connect(model, &QAbstractItemModel::rowsMoved, switcherInputGrid, [this]() { QueueSwitcherInputsRefresh(); });
	connect(model, &QAbstractItemModel::dataChanged, switcherInputGrid, [this]() { QueueSwitcherInputsRefresh(); });
	connect(model, &QAbstractItemModel::modelReset, switcherInputGrid, [this]() { QueueSwitcherInputsRefresh(); });

	RefreshSwitcherInputs();
}

void OBSBasic::DestroySwitcherInputGrid()
{
	if (!switcherInputGrid) {
		return;
	}

	delete switcherInputGrid;
	ui->scenes->setVisible(true);
	ui->scenesDock->setWindowTitle(QTStr("Basic.Main.Scenes"));
}

void OBSBasic::QueueSwitcherInputsRefresh()
{
	if (switcherInputsRefreshQueued) {
		return;
	}

	/* Batch bursts of list changes and stay out of tile signal handlers */
	switcherInputsRefreshQueued = true;
	QTimer::singleShot(0, this, [this]() {
		switcherInputsRefreshQueued = false;
		RefreshSwitcherInputs();
	});
}

void OBSBasic::RefreshSwitcherInputs()
{
	if (!switcherInputGrid) {
		return;
	}

	std::vector<OBSSource> scenes;
	scenes.reserve(ui->scenes->count());

	for (int i = 0; i < ui->scenes->count(); i++) {
		OBSScene scene = GetOBSRef<OBSScene>(ui->scenes->item(i));
		if (scene) {
			scenes.emplace_back(obs_scene_get_source(scene));
		}
	}

	switcherInputGrid->SetScenes(scenes);

	/* An overlay whose scene was removed has nothing left to control it */
	for (int i = 0; i < SWITCHER_OVERLAY_COUNT; i++) {
		OBSSourceAutoRelease overlay = obs_get_output_source(SWITCHER_OVERLAY_CHANNEL + i);
		if (overlay && std::find(scenes.begin(), scenes.end(), overlay.Get()) == scenes.end()) {
			obs_set_output_source(SWITCHER_OVERLAY_CHANNEL + i, nullptr);
		}
	}

	UpdateSwitcherInputStates();
}

void OBSBasic::UpdateSwitcherInputStates()
{
	if (!switcherInputGrid) {
		return;
	}

	OBSSource preview = GetCurrentSceneSource();
	OBSSource program = IsPreviewProgramMode() ? GetProgramSource() : preview;

	switcherInputGrid->SetLiveStates(preview, program);

	std::array<OBSSource, SWITCHER_OVERLAY_COUNT> overlays;
	for (int i = 0; i < SWITCHER_OVERLAY_COUNT; i++) {
		OBSSourceAutoRelease overlay = obs_get_output_source(SWITCHER_OVERLAY_CHANNEL + i);
		overlays[i] = overlay.Get();
	}

	switcherInputGrid->SetOverlayStates(overlays);
}

void OBSBasic::ToggleSwitcherOverlay(OBSSource source, int index)
{
	int channel = SWITCHER_OVERLAY_CHANNEL + index;
	OBSSourceAutoRelease current = obs_get_output_source(channel);

	obs_set_output_source(channel, current.Get() == source.Get() ? nullptr : source.Get());
	blog(LOG_INFO, "Switcher overlay %d %s '%s'", index + 1, current.Get() == source.Get() ? "off" : "on",
	     obs_source_get_name(source));

	UpdateSwitcherInputStates();
}
