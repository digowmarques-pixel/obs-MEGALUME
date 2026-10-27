#include "SwitcherOutputs.hpp"

#include <OBSApp.hpp>
#include <components/Multiview.hpp>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSProjector.hpp>

#include <qt-wrappers.hpp>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include "moc_SwitcherOutputs.cpp"

namespace {

/* Combo item data: one of these keywords, or "scene:<uuid>" for an input */
const char *OutputNone = "none";
const char *OutputProgram = "program";
const char *OutputPreview = "preview";
const char *OutputMultiview = "multiview";
const char *ScenePrefix = "scene:";

QString MonitorLabel(int index, QScreen *screen)
{
	QSize size = screen->size() * screen->devicePixelRatio();
	return QTStr("Switcher.Outputs.Monitor").arg(index + 1).arg(screen->name()).arg(size.width()).arg(size.height());
}

} // namespace

SwitcherOutputs::SwitcherOutputs(OBSBasic *main_) : QDialog(main_), main(main_)
{
	setObjectName("switcherOutputs");
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QTStr("Switcher.Outputs.Title"));
	setMinimumWidth(560);

	QVBoxLayout *layout = new QVBoxLayout(this);

	QGroupBox *fullscreen = new QGroupBox(QTStr("Switcher.Outputs.Fullscreen"), this);
	QFormLayout *fullscreenForm = new QFormLayout(fullscreen);
	QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size() && i < 10; i++) {
		QComboBox *combo = new QComboBox(fullscreen);
		PopulateSources(combo);
		connect(combo, &QComboBox::activated, this,
			[this, i, combo](int) { SetOutput(i, combo->currentData().toString()); });
		fullscreenForm->addRow(MonitorLabel(i, screens[i]), combo);
		monitors.emplace_back(combo);
	}
	QLabel *hint = new QLabel(QTStr("Switcher.Outputs.FullscreenHint"), fullscreen);
	hint->setObjectName("switcherSettingsHint");
	hint->setWordWrap(true);
	fullscreenForm->addRow(hint);
	layout->addWidget(fullscreen);

	QGroupBox *multiview = new QGroupBox(QTStr("Switcher.Outputs.Multiview"), this);
	QFormLayout *multiviewForm = new QFormLayout(multiview);
	multiviewLayout = new QComboBox(multiview);
	const std::pair<const char *, MultiviewLayout> layouts[] = {
		{"Horizontal.Top", MultiviewLayout::HORIZONTAL_TOP_8_SCENES},
		{"Horizontal.Bottom", MultiviewLayout::HORIZONTAL_BOTTOM_8_SCENES},
		{"Vertical.Left", MultiviewLayout::VERTICAL_LEFT_8_SCENES},
		{"Vertical.Right", MultiviewLayout::VERTICAL_RIGHT_8_SCENES},
		{"Horizontal.18Scene.Top", MultiviewLayout::HORIZONTAL_TOP_18_SCENES},
		{"Horizontal.Extended.Top", MultiviewLayout::HORIZONTAL_TOP_24_SCENES},
		{"4Scene", MultiviewLayout::SCENES_ONLY_4_SCENES},
		{"9Scene", MultiviewLayout::SCENES_ONLY_9_SCENES},
		{"16Scene", MultiviewLayout::SCENES_ONLY_16_SCENES},
		{"25Scene", MultiviewLayout::SCENES_ONLY_25_SCENES},
	};
	for (const auto &[key, value] : layouts) {
		multiviewLayout->addItem(QTStr((std::string("Basic.Settings.General.MultiviewLayout.") + key).c_str()),
					 static_cast<int>(value));
	}
	int current = (int)config_get_int(App()->GetUserConfig(), "BasicWindow", "MultiviewLayout");
	multiviewLayout->setCurrentIndex(std::max(0, multiviewLayout->findData(current)));
	connect(multiviewLayout, &QComboBox::activated, this, [this](int) {
		config_set_int(App()->GetUserConfig(), "BasicWindow", "MultiviewLayout",
			       multiviewLayout->currentData().toInt());
		OBSProjector::UpdateMultiviewProjectors();
	});
	multiviewForm->addRow(QTStr("Basic.Settings.General.MultiviewLayout"), multiviewLayout);
	layout->addWidget(multiview);

	QGroupBox *vcam = new QGroupBox(QTStr("Switcher.Outputs.VirtualCam"), this);
	QHBoxLayout *vcamLayout = new QHBoxLayout(vcam);
	virtualCam = new QPushButton(vcam);
	virtualCam->setEnabled(main->VCamEnabled());
	connect(virtualCam, &QPushButton::clicked, main, &OBSBasic::VirtualCamActionTriggered);
	vcamLayout->addWidget(virtualCam);
	vcamLayout->addStretch();
	layout->addWidget(vcam);

	QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::close);
	layout->addWidget(box);

	/* Projectors can also be closed from their own window, so keep the
	 * combos in step with what is really open */
	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &SwitcherOutputs::Refresh);
	timer->start(1000);
	Refresh();
}

void SwitcherOutputs::PopulateSources(QComboBox *combo)
{
	combo->addItem(QTStr("Switcher.Outputs.None"), OutputNone);
	combo->addItem(QTStr("StudioMode.Program"), OutputProgram);
	combo->addItem(QTStr("StudioMode.Preview"), OutputPreview);
	combo->addItem(QTStr("Switcher.Outputs.MultiviewItem"), OutputMultiview);
	combo->insertSeparator(combo->count());

	auto addScene = [](void *data, obs_source_t *scene) {
		QComboBox *combo = static_cast<QComboBox *>(data);
		combo->addItem(QT_UTF8(obs_source_get_name(scene)),
			       QString(ScenePrefix) + QT_UTF8(obs_source_get_uuid(scene)));
		return true;
	};
	obs_enum_scenes(addScene, combo);
}

QString SwitcherOutputs::CurrentOutput(int monitor) const
{
	for (OBSProjector *projector : main->projectors) {
		if (projector->GetMonitor() != monitor) {
			continue;
		}
		switch (projector->GetProjectorType()) {
		case ProjectorType::StudioProgram:
			return OutputProgram;
		case ProjectorType::Preview:
			return OutputPreview;
		case ProjectorType::Multiview:
			return OutputMultiview;
		case ProjectorType::Scene:
		case ProjectorType::Source:
			if (obs_source_t *source = projector->GetSource()) {
				return QString(ScenePrefix) + QT_UTF8(obs_source_get_uuid(source));
			}
			break;
		}
	}
	return OutputNone;
}

void SwitcherOutputs::SetOutput(int monitor, const QString &output)
{
	/* A monitor shows one thing at a time */
	for (size_t i = main->projectors.size(); i > 0; i--) {
		OBSProjector *projector = main->projectors[i - 1];
		if (projector->GetMonitor() == monitor) {
			main->DeleteProjector(projector);
		}
	}

	if (output == OutputProgram) {
		main->OpenProjector(nullptr, monitor,
				    main->IsPreviewProgramMode() ? ProjectorType::StudioProgram
								 : ProjectorType::Preview);
	} else if (output == OutputPreview) {
		main->OpenProjector(nullptr, monitor, ProjectorType::Preview);
	} else if (output == OutputMultiview) {
		main->OpenProjector(nullptr, monitor, ProjectorType::Multiview);
	} else if (output.startsWith(ScenePrefix)) {
		QByteArray uuid = output.mid((int)strlen(ScenePrefix)).toUtf8();
		OBSSourceAutoRelease scene = obs_get_source_by_uuid(uuid.constData());
		if (scene) {
			main->OpenProjector(scene, monitor, ProjectorType::Scene);
		}
	}

	activateWindow();
}

void SwitcherOutputs::Refresh()
{
	for (int i = 0; i < (int)monitors.size(); i++) {
		QComboBox *combo = monitors[i];
		if (!combo || combo->view()->isVisible()) {
			continue;
		}

		QString output = CurrentOutput(i);
		int index = combo->findData(output);
		if (index < 0 && output.startsWith(ScenePrefix)) {
			/* An input added since the window opened */
			combo->clear();
			PopulateSources(combo);
			index = combo->findData(output);
		}
		combo->setCurrentIndex(std::max(0, index));
	}

	if (virtualCam) {
		bool active = main->VirtualCamActive();
		virtualCam->setText(QTStr(active ? "Basic.Main.StopVirtualCam" : "Basic.Main.StartVirtualCam"));
		virtualCam->setProperty("active", active);
	}
}
