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

#include <components/MediaControls.hpp>
#include <components/SceneTree.hpp>
#include <components/SwitcherBusMeters.hpp>
#include <components/SwitcherInputGrid.hpp>
#include <dialogs/NameDialog.hpp>
#include <dialogs/SwitcherInputSelect.hpp>
#include <dialogs/SwitcherInputSettings.hpp>
#include <dialogs/SwitcherMultiCorder.hpp>
#include <dialogs/SwitcherSettings.hpp>
#include <dialogs/SwitcherStreamSettings.hpp>
#include <utility/BasicOutputHandler.hpp>

#include <qt-wrappers.hpp>

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>

#define SWITCHER_THEME_ID "com.obsproject.Yami.Megalume"

/* Overlays use their own output channels, drawn on top of Program. Channels
 * 0-6 belong to OBS and low numbers are popular with plugins. */
#define SWITCHER_OVERLAY_CHANNEL 40

namespace {

/* Calls back whenever a watched widget is resized */
class SwitcherResizeWatcher : public QObject {
	std::function<void()> callback;

public:
	SwitcherResizeWatcher(QObject *parent, std::function<void()> callback_)
		: QObject(parent),
		  callback(std::move(callback_))
	{
	}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override
	{
		if (event->type() == QEvent::Resize) {
			callback();
		}
		return QObject::eventFilter(obj, event);
	}
};

/* Runs a callback on double-click; consumes the click when it says so */
class SwitcherDoubleClickWatcher : public QObject {
	std::function<bool()> callback;

public:
	SwitcherDoubleClickWatcher(QObject *parent, std::function<bool()> callback_)
		: QObject(parent),
		  callback(std::move(callback_))
	{
	}

protected:
	bool eventFilter(QObject *obj, QEvent *event) override
	{
		if (event->type() == QEvent::MouseButtonDblClick && callback()) {
			return true;
		}
		return QObject::eventFilter(obj, event);
	}
};

/* Width a display needs to show the canvas edge to edge at its height */
int CanvasFitWidth(QWidget *display)
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || !ovi.base_height) {
		return QWIDGETSIZE_MAX;
	}

	/* The display keeps a margin for transform handles, in device pixels */
	qreal edge = PREVIEW_EDGE_SIZE / display->devicePixelRatioF();
	qreal height = display->height() - edge * 2;
	if (height <= 0) {
		return QWIDGETSIZE_MAX;
	}

	return int(std::ceil(height * ovi.base_width / ovi.base_height + edge * 2));
}

} // namespace

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

	/* New audio sources go to the default buses from the switcher settings */
	switcherSourceCreateSignal.Connect(obs_get_signal_handler(), "source_create", OBSBasic::SwitcherSourceCreated,
					   this);

	switcherMultiCorder = new SwitcherMultiCorder(this);

	/* Extra stream destinations share the main stream's encoders */
	switcherMultistream = new SwitcherMultistream(this, [this]() -> OBSOutputAutoRelease {
		if (!outputHandler || !outputHandler->StreamingActive()) {
			return nullptr;
		}
		return outputHandler->StreamingOutput();
	});

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
	emit userSettingChanged("BasicWindow", "SwitcherMode");

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
		CreateSwitcherBusMeters();

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

		/* Preview and Program shrink to the video's width, so there are no
		 * empty bars beside it */
		switcherResizeWatcher = new SwitcherResizeWatcher(this, [this]() { UpdateSwitcherDisplayWidths(); });
		ui->preview->installEventFilter(switcherResizeWatcher);
		if (program) {
			program->installEventFilter(switcherResizeWatcher);
		}
		QTimer::singleShot(0, this, &OBSBasic::UpdateSwitcherDisplayWidths);

		/* The switcher preview always fits the window, without zoom
		 * controls or scroll bars */
		setPreviewScalingWindow();
		ui->previewXContainer->setVisible(false);
		ui->previewYScrollBar->setVisible(false);

		/* Optionally, double-clicking Preview takes it to air */
		switcherDoubleClickWatcher = new SwitcherDoubleClickWatcher(this, [this]() {
			if (!config_get_bool(App()->GetUserConfig(), "BasicWindow", "SwitcherPreviewDoubleClick")) {
				return false;
			}
			TransitionClicked();
			return true;
		});
		ui->preview->installEventFilter(switcherDoubleClickWatcher);

		if (!config_get_bool(App()->GetUserConfig(), "BasicWindow", "VerticalVolumeControl")) {
			toggleMixerLayout();
		}
	} else {
		DestroySwitcherBar();
		DestroySwitcherInputGrid();
		DestroySwitcherBusMeters();

		delete switcherResizeWatcher;
		delete switcherDoubleClickWatcher;
		UpdateSwitcherDisplayWidths();

		delete switcherPreviewMedia;
		delete switcherProgramMedia;

		ui->previewXContainer->setVisible(true);
		ui->previewYScrollBar->setVisible(true);

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

		if (programOptions) {
			delete programOptions->findChild<QWidget *>("switcherColumnOverlays");
			if (QLabel *label = programOptions->findChild<QLabel *>("quickTransitionsLabel")) {
				label->setVisible(true);
			}
		}

		if (IsPreviewProgramMode() && tBar) {
			tBar->setOrientation(Qt::Horizontal);
			tBar->setInvertedAppearance(false);
			tBar->setMinimumHeight(0);
			tBar->setMaximumHeight(QWIDGETSIZE_MAX);
			tBar->setMinimumWidth(0);
			tBar->setMaximumWidth(QWIDGETSIZE_MAX);
			static_cast<QVBoxLayout *>(programOptions->layout())->setAlignment(tBar, Qt::Alignment());
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
	addInput->setPopupMode(QToolButton::MenuButtonPopup);
	addInput->setToolButtonStyle(Qt::ToolButtonTextOnly);

	/* Clicking opens Input Select, the arrow keeps the quick options */
	QMenu *addInputMenu = new QMenu(addInput);
	addInputMenu->addAction(QTStr("Switcher.InputSelect") + "...", this, &OBSBasic::OpenSwitcherInputSelect);
	addInputMenu->addSeparator();
	addInputMenu->addAction(QTStr("Switcher.AddInput.Scene"), this, &OBSBasic::on_actionAddScene_triggered);
	addInputMenu->addAction(QTStr("Switcher.AddInput.Source"), this, &OBSBasic::AddSourceDialog);
	addInput->setMenu(addInputMenu);
	connect(addInput, &QToolButton::clicked, this, &OBSBasic::OpenSwitcherInputSelect);

	barLayout->addWidget(addInput);

	/* Reuse the existing controls so their state stays in sync */
	switcherControls = qobject_cast<OBSBasicControls *>(controlsDock->widget());
	if (switcherControls) {
		controlsDock->setWidget(nullptr);
		switcherControls->SetHorizontalLayout(true);
		barLayout->addWidget(switcherControls, 1);
		switcherControls->setVisible(true);
	}

	QToolButton *destinations = new QToolButton(barContents);
	destinations->setObjectName("switcherDestinations");
	destinations->setText(QTStr("Switcher.Stream.Destinations"));
	destinations->setToolButtonStyle(Qt::ToolButtonTextOnly);
	connect(destinations, &QToolButton::clicked, this, &OBSBasic::OpenSwitcherStreamSettings);
	barLayout->addWidget(destinations);

	QToolButton *multiCorder = new QToolButton(barContents);
	multiCorder->setObjectName("switcherMultiCorderButton");
	multiCorder->setText(QTStr("Switcher.MultiCorder.Button"));
	multiCorder->setToolButtonStyle(Qt::ToolButtonTextOnly);
	connect(multiCorder, &QToolButton::clicked, this, &OBSBasic::OpenSwitcherMultiCorder);
	barLayout->addWidget(multiCorder);

	QToolButton *settings = new QToolButton(barContents);
	settings->setObjectName("switcherSettingsButton");
	settings->setText(QTStr("Switcher.Settings.Button"));
	settings->setToolButtonStyle(Qt::ToolButtonTextOnly);
	connect(settings, &QToolButton::clicked, this, &OBSBasic::OpenSwitcherSettings);
	barLayout->addWidget(settings);

	/* Alerts show up as a red pill in the middle of the bar */
	switcherAlert = new QLabel(barContents);
	switcherAlert->setObjectName("switcherAlert");
	switcherAlert->setVisible(false);
	barLayout->insertWidget(1, switcherAlert);

	QTimer *alertTimer = new QTimer(barContents);
	connect(alertTimer, &QTimer::timeout, this, &OBSBasic::UpdateSwitcherAlerts);
	alertTimer->start(1000);

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

	if (switcherResizeWatcher && program) {
		program->installEventFilter(switcherResizeWatcher);
		QTimer::singleShot(0, this, &OBSBasic::UpdateSwitcherDisplayWidths);
	}

	/* Studio Mode centers the column horizontally, which hands it spare
	 * width; centered vertically only, the spare width goes to the
	 * displays instead */
	ui->previewLayout->setAlignment(programOptions, Qt::AlignVCenter);

	programOptions->setObjectName("switcherTransitionColumn");
	programOptions->setFixedWidth(190);

	if (transitionButton) {
		transitionButton->setText(QTStr("Switcher.QuickPlay"));
		setClasses(transitionButton, "switcher-quickplay");
	}

	if (QLabel *label = programOptions->findChild<QLabel *>("quickTransitionsLabel")) {
		label->setText(QTStr("Switcher.Transitions"));
		label->setVisible(false);
	}

	/* Vertical T-Bar, pulled down to take, like a hardware switcher */
	if (tBar) {
		tBar->setOrientation(Qt::Vertical);
		tBar->setInvertedAppearance(true);
		tBar->setFixedHeight(90);
		tBar->setFixedWidth(26);

		static_cast<QVBoxLayout *>(programOptions->layout())->setAlignment(tBar, Qt::AlignHCenter);

		/* The column was renamed after the T-Bar got its style */
		tBar->style()->unpolish(tBar);
		tBar->style()->polish(tBar);
	}

	/* Overlays 1-8 put the Preview input on top of Program */
	if (!programOptions->findChild<QWidget *>("switcherColumnOverlays")) {
		QWidget *overlayGrid = new QWidget();
		overlayGrid->setObjectName("switcherColumnOverlays");
		QGridLayout *grid = new QGridLayout(overlayGrid);
		grid->setContentsMargins(0, 6, 0, 6);
		grid->setSpacing(3);

		for (int i = 0; i < SWITCHER_OVERLAY_COUNT; i++) {
			QPushButton *button = new QPushButton(QString::number(i + 1), overlayGrid);
			button->setProperty("class", "switcher-column-overlay");
			button->setToolTip(QTStr("Switcher.ColumnOverlayTT").arg(i + 1));
			button->setFocusPolicy(Qt::NoFocus);
			grid->addWidget(button, i / 4, i % 4);
			switcherColumnOverlays[i] = button;

			connect(button, &QPushButton::clicked, this, [this, i]() {
				/* A live overlay goes off, otherwise Preview goes on */
				OBSSource current = OBSGetStrongRef(switcherOverlayScenes[i]);
				OBSSource target = current ? current : GetCurrentSceneSource();
				if (target) {
					ToggleSwitcherOverlay(target, i);
				}
			});
		}

		QVBoxLayout *columnLayout = static_cast<QVBoxLayout *>(programOptions->layout());
		int tBarIndex = columnLayout->indexOf(tBar);
		columnLayout->insertWidget(tBarIndex >= 0 ? tBarIndex : columnLayout->count() - 1, overlayGrid);
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
	switcherInputTabs = new SwitcherInputTabs(ui->scenesFrame);
	ui->verticalLayout_12->insertWidget(0, switcherInputTabs);

	switcherInputGrid = new SwitcherInputGrid(ui->scenesFrame);
	switcherInputGrid->SetFilter(switcherInputTabs->CurrentFilter());
	ui->verticalLayout_12->insertWidget(1, switcherInputGrid, 1);

	connect(switcherInputTabs.data(), &SwitcherInputTabs::FilterChanged, switcherInputGrid.data(),
		&SwitcherInputGrid::SetFilter);
	connect(switcherInputTabs.data(), &SwitcherInputTabs::CategoriesRenamed, this,
		&OBSBasic::RefreshSwitcherInputs);
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

	connect(switcherInputGrid.data(), &SwitcherInputGrid::CloseRequested, this, &OBSBasic::RemoveSwitcherInput);

	connect(switcherInputGrid.data(), &SwitcherInputGrid::OverlayRequested, this, &OBSBasic::ToggleSwitcherOverlay);

	connect(switcherInputGrid.data(), &SwitcherInputGrid::ProjectorRequested, this,
		[this](OBSSource source) { OpenProjector(source, -1, ProjectorType::Scene); });

	connect(switcherInputGrid.data(), &SwitcherInputGrid::SettingsRequested, this,
		&OBSBasic::OpenSwitcherInputSettings);

	connect(switcherInputGrid.data(), &SwitcherInputGrid::MenuRequested, this,
		[this](OBSSource source, const QPoint &globalPos) {
			/* Only the actions that work on the selected scene select it,
			 * so opening the menu leaves Preview alone */
			QMenu menu(this);
			menu.addAction(QTStr("Switcher.Input.Settings") + "...", this,
				       [this, source]() { OpenSwitcherInputSettings(source); });
			menu.addSeparator();
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
			QMenu *categoryMenu = menu.addMenu(QTStr("Switcher.Category"));
			int currentCategory = SwitcherGetCategory(source);
			for (int i = 0; i <= SWITCHER_CATEGORY_COUNT; i++) {
				QString name = i == 0 ? QTStr("Switcher.Category.None") : SwitcherCategoryName(i);
				QAction *action = categoryMenu->addAction(name, this, [this, source, i]() {
					SwitcherSetCategory(source, i);
					RefreshSwitcherInputs();
				});
				action->setCheckable(true);
				action->setChecked(i == currentCategory);

				QColor color = SwitcherCategoryColor(i);
				if (color.isValid()) {
					QPixmap swatch(12, 12);
					swatch.fill(color);
					action->setIcon(QIcon(swatch));
				}
			}

			menu.addAction(QTStr("Filters"), this, [this, source]() {
				SetCurrentScene(source);
				OpenSceneFilters();
			});
			menu.addAction(QTStr("Projector.Open.Scene"), this,
				       [this, source]() { OpenProjector(source, -1, ProjectorType::Scene); });
			menu.addSeparator();
			menu.addAction(QTStr("Remove"), this, [this, source]() {
				SetCurrentScene(source);
				RemoveSelectedScene();
			});
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
	delete switcherInputTabs;
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
		OBSSource overlay = OBSGetStrongRef(switcherOverlayScenes[i]);
		if (overlay && std::find(scenes.begin(), scenes.end(), overlay.Get()) == scenes.end()) {
			ToggleSwitcherOverlay(overlay, i);
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
		overlays[i] = OBSGetStrongRef(switcherOverlayScenes[i]);
	}

	switcherInputGrid->SetOverlayStates(overlays);

	for (int i = 0; i < SWITCHER_OVERLAY_COUNT; i++) {
		if (switcherColumnOverlays[i]) {
			setClasses(switcherColumnOverlays[i],
				   overlays[i] ? "switcher-column-overlay state-active" : "switcher-column-overlay");
		}
	}

	UpdateSwitcherMediaBars();
}

void OBSBasic::ToggleSwitcherOverlay(OBSSource source, int index)
{
	int channel = SWITCHER_OVERLAY_CHANNEL + index;
	OBSSource &transition = switcherOverlayTransitions[index];

	if (!transition) {
		std::string name = "Switcher Overlay " + std::to_string(index + 1);
		transition = OBSSourceAutoRelease(obs_source_create_private("fade_transition", name.c_str(), nullptr));
		if (!transition) {
			return;
		}
	}

	/* The channel can be emptied behind our back, e.g. by a collection load */
	OBSSourceAutoRelease current = obs_get_output_source(channel);
	if (current.Get() != transition.Get()) {
		obs_set_output_source(channel, transition);
	}

	bool turnOff = obs_weak_source_references_source(switcherOverlayScenes[index], source);
	obs_source_t *target = turnOff ? nullptr : source.Get();

	if (!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, GetTransitionDuration(), target)) {
		obs_transition_set(transition, target);
	}

	switcherOverlayScenes[index] = turnOff ? OBSWeakSource() : OBSGetWeakRef(source);
	blog(LOG_INFO, "Switcher overlay %d %s '%s'", index + 1, turnOff ? "off" : "on", obs_source_get_name(source));

	UpdateSwitcherInputStates();
}

void OBSBasic::ClearSwitcherOverlays()
{
	for (int i = 0; i < SWITCHER_OVERLAY_COUNT; i++) {
		if (switcherOverlayTransitions[i]) {
			OBSSourceAutoRelease current = obs_get_output_source(SWITCHER_OVERLAY_CHANNEL + i);
			if (current.Get() == switcherOverlayTransitions[i].Get()) {
				obs_set_output_source(SWITCHER_OVERLAY_CHANNEL + i, nullptr);
			}
		}

		switcherOverlayTransitions[i] = nullptr;
		switcherOverlayScenes[i] = nullptr;
	}

	UpdateSwitcherInputStates();
}

void OBSBasic::CreateSwitcherBusMeters()
{
	if (switcherMixerContainer) {
		return;
	}

	/* Master/A/B output meters sit to the left of the source mixer */
	switcherMixer = ui->mixerDock->widget();
	switcherMixerContainer = new QWidget();
	switcherMixerContainer->setObjectName("switcherMixerContainer");

	QHBoxLayout *layout = new QHBoxLayout(switcherMixerContainer);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	switcherBusMeters = new SwitcherBusMeters(switcherMixerContainer);
	layout->addWidget(switcherBusMeters);

	if (switcherMixer) {
		layout->addWidget(switcherMixer, 1);
	}

	ui->mixerDock->setWidget(switcherMixerContainer);

	if (switcherMixer) {
		switcherMixer->show();
	}
	switcherMixerContainer->show();
}

void OBSBasic::DestroySwitcherBusMeters()
{
	if (!switcherMixerContainer) {
		return;
	}

	if (switcherMixer) {
		switcherMixer->setParent(nullptr);
		ui->mixerDock->setWidget(switcherMixer);
		switcherMixer->show();
	}

	delete switcherMixerContainer;
	switcherMixer = nullptr;
}

void OBSBasic::OpenSwitcherInputSelect()
{
	SwitcherInputSelect *dialog = new SwitcherInputSelect(this);
	connect(dialog, &SwitcherInputSelect::InputCreated, this, [this](OBSSource scene) {
		/* New inputs land in Preview, ready to take */
		SetCurrentScene(scene);
	});
	dialog->show();
}

void OBSBasic::OpenSwitcherInputSettings(OBSSource scene)
{
	/* One settings window per input; asking again brings it forward */
	for (SwitcherInputSettings *open : findChildren<SwitcherInputSettings *>()) {
		if (open->GetScene() == scene) {
			open->raise();
			open->activateWindow();
			return;
		}
	}

	SwitcherInputSettings *dialog = new SwitcherInputSettings(this, scene);
	connect(dialog, &SwitcherInputSettings::Changed, this, &OBSBasic::QueueSwitcherInputsRefresh);
	dialog->show();
}

void OBSBasic::OpenSwitcherStreamSettings()
{
	if (SwitcherStreamSettings *open = findChild<SwitcherStreamSettings *>()) {
		open->raise();
		open->activateWindow();
		return;
	}

	SwitcherStreamSettings *dialog = new SwitcherStreamSettings(this, switcherMultistream);
	connect(dialog, &SwitcherStreamSettings::OpenMainStreamSettings, this, &OBSBasic::on_action_Settings_triggered);
	dialog->show();
}

void OBSBasic::UpdateSwitcherDisplayWidths()
{
	if (!switcherMode) {
		ui->previewContainer->setMaximumWidth(QWIDGETSIZE_MAX);
		if (programWidget) {
			programWidget->setMaximumWidth(QWIDGETSIZE_MAX);
		}
		return;
	}

	/* Width only depends on height, so this settles after one pass */
	int previewExtra = ui->previewContainer->width() - ui->preview->width();
	ui->previewContainer->setMaximumWidth(CanvasFitWidth(ui->preview) + previewExtra);

	if (programWidget && program) {
		int programExtra = programWidget->width() - program->width();
		programWidget->setMaximumWidth(CanvasFitWidth(program) + programExtra);
	}
}

void OBSBasic::EnsureSwitcherMediaBars()
{
	auto makeBar = [](QPointer<MediaControls> &controls) {
		QFrame *bar = new QFrame();
		bar->setObjectName("switcherMediaBar");

		QHBoxLayout *barLayout = new QHBoxLayout(bar);
		barLayout->setContentsMargins(10, 2, 10, 2);

		/* Never ask for width, so the bar cannot reshape the displays */
		controls = new MediaControls(bar);
		controls->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		barLayout->addWidget(controls);
		return bar;
	};

	if (!switcherPreviewMedia) {
		switcherPreviewMedia = makeBar(switcherPreviewControls);
		switcherPreviewMedia->setVisible(false);
		ui->previewTextLayout->addWidget(switcherPreviewMedia);
	}

	/* The program side is rebuilt with Studio Mode, so it is added once
	 * its layout exists */
	if (!switcherProgramMedia && programLayout) {
		switcherProgramMedia = makeBar(switcherProgramControls);
		switcherProgramMedia->setVisible(false);
		programLayout->addWidget(switcherProgramMedia);
	}
}

void OBSBasic::UpdateSwitcherMediaBars()
{
	if (!switcherMode) {
		return;
	}

	EnsureSwitcherMediaBars();

	/* Show the transport only for inputs that hold a video or audio file,
	 * so it can be checked in Preview before it goes to air */
	auto update = [](QFrame *bar, MediaControls *controls, obs_source_t *scene) {
		if (!bar || !controls) {
			return;
		}

		OBSSource media = scene ? SwitcherFindMediaSource(scene) : nullptr;
		if (controls->GetSource() != media) {
			controls->SetSource(media);
		}
		bar->setVisible(media != nullptr);
	};

	update(switcherPreviewMedia, switcherPreviewControls, GetCurrentSceneSource());
	update(switcherProgramMedia, switcherProgramControls, IsPreviewProgramMode() ? GetProgramSource() : nullptr);
}

void OBSBasic::OpenSwitcherSettings()
{
	if (SwitcherSettings *open = findChild<SwitcherSettings *>()) {
		open->raise();
		open->activateWindow();
		return;
	}

	SwitcherSettings *dialog = new SwitcherSettings(this);
	connect(dialog, &SwitcherSettings::OpenObsSettings, this, &OBSBasic::on_action_Settings_triggered);
	connect(dialog, &SwitcherSettings::Applied, this, [this](bool tileSizeChanged) {
		/* Tiles take their size when created, so rebuild the grid */
		if (tileSizeChanged && switcherInputGrid) {
			DestroySwitcherInputGrid();
			CreateSwitcherInputGrid();
		}
		UpdateSwitcherAlerts();
	});
	dialog->show();
}

void OBSBasic::RemoveSwitcherInput(OBSSource scene)
{
	config_t *config = App()->GetUserConfig();
	bool confirm = !config_has_user_value(config, "BasicWindow", "SwitcherConfirmClose") ||
		       config_get_bool(config, "BasicWindow", "SwitcherConfirmClose");

	if (confirm) {
		SetCurrentScene(scene);
		RemoveSelectedScene();
	} else {
		/* Same as removing the scene without the question */
		blog(LOG_INFO, "Switcher input '%s' closed", obs_source_get_name(scene));
		obs_source_remove(scene);
	}
}

void OBSBasic::SwitcherSourceCreated(void *, calldata_t *calldata)
{
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(calldata, "source"));
	if (!source || obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT || obs_obj_is_private(source) ||
	    !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) {
		return;
	}

	/* Sources loaded from a scene collection get their saved tracks
	 * restored right after creation, so this only affects new ones */
	config_t *config = App()->GetUserConfig();
	if (config_has_user_value(config, "BasicWindow", "SwitcherDefaultBuses")) {
		obs_source_set_audio_mixers(source,
					    (uint32_t)config_get_int(config, "BasicWindow", "SwitcherDefaultBuses"));
	}
}

void OBSBasic::UpdateSwitcherAlerts()
{
	if (!switcherAlert) {
		return;
	}

	config_t *config = App()->GetUserConfig();
	QStringList alerts;

	if (config_get_bool(config, "BasicWindow", "SwitcherAlertFtb") && prevFTBSource) {
		alerts << QTStr("Switcher.Alert.Ftb");
	}

	if (config_get_bool(config, "BasicWindow", "SwitcherAlertNotRecording") && !RecordingActive()) {
		alerts << QTStr("Switcher.Alert.NotRecording");
	}

	/* Quiet Master only matters while something is going out */
	bool live = StreamingActive() || RecordingActive();
	if (config_get_bool(config, "BasicWindow", "SwitcherAlertVolume") && live && switcherBusMeters) {
		int threshold = config_has_user_value(config, "BasicWindow", "SwitcherAlertVolumeDb")
					? (int)config_get_int(config, "BasicWindow", "SwitcherAlertVolumeDb")
					: -50;
		int seconds = config_has_user_value(config, "BasicWindow", "SwitcherAlertVolumeSeconds")
				      ? (int)config_get_int(config, "BasicWindow", "SwitcherAlertVolumeSeconds")
				      : 5;

		qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (switcherBusMeters->MasterLevel() < threshold) {
			if (!switcherQuietSince) {
				switcherQuietSince = now;
			}
			if (now - switcherQuietSince >= seconds * 1000) {
				alerts << QTStr("Switcher.Alert.Volume");
			}
		} else {
			switcherQuietSince = 0;
		}
	} else {
		switcherQuietSince = 0;
	}

	switcherAlert->setText(alerts.join("  \u2022  "));
	switcherAlert->setVisible(!alerts.isEmpty());
}

void OBSBasic::OpenSwitcherMultiCorder()
{
	if (SwitcherMultiCorderWindow *open = findChild<SwitcherMultiCorderWindow *>()) {
		open->raise();
		open->activateWindow();
		return;
	}

	(new SwitcherMultiCorderWindow(this, switcherMultiCorder))->show();
}
