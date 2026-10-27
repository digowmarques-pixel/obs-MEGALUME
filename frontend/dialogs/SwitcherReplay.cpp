#include "SwitcherReplay.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "moc_SwitcherReplay.cpp"

SwitcherReplay::SwitcherReplay(OBSBasic *main_) : QDialog(main_), main(main_)
{
	setObjectName("switcherReplay");
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QTStr("Switcher.Replay.Title"));
	resize(560, 480);

	QVBoxLayout *layout = new QVBoxLayout(this);

	/* Buffer */
	QHBoxLayout *bufferRow = new QHBoxLayout();
	status = new QLabel(this);
	bufferButton = new QPushButton(this);
	bufferRow->addWidget(status, 1);
	bufferRow->addWidget(bufferButton);
	layout->addLayout(bufferRow);

	connect(bufferButton.data(), &QPushButton::clicked, this, [this]() {
		if (main->ReplayBufferActive()) {
			main->StopReplayBuffer();
		} else {
			main->EnsureSwitcherReplayBuffer();
		}
		Refresh();
	});

	/* Playback */
	config_t *config = App()->GetUserConfig();
	QFormLayout *form = new QFormLayout();
	speed = new QComboBox(this);
	for (int percent : {100, 75, 50, 33, 25}) {
		speed->addItem(QString("%1%").arg(percent), percent);
	}
	int currentSpeed = config_has_user_value(config, "BasicWindow", "SwitcherReplaySpeed")
				   ? (int)config_get_int(config, "BasicWindow", "SwitcherReplaySpeed")
				   : 100;
	speed->setCurrentIndex(std::max(0, speed->findData(currentSpeed)));
	form->addRow(QTStr("Switcher.Replay.Speed"), speed);

	returnAfter = new QCheckBox(QTStr("Switcher.Replay.Return"), this);
	returnAfter->setChecked(!config_has_user_value(config, "BasicWindow", "SwitcherReplayReturn") ||
				config_get_bool(config, "BasicWindow", "SwitcherReplayReturn"));
	form->addRow(QString(), returnAfter);
	layout->addLayout(form);

	connect(speed.data(), &QComboBox::currentIndexChanged, this, [this]() {
		config_set_int(App()->GetUserConfig(), "BasicWindow", "SwitcherReplaySpeed",
			       speed->currentData().toInt());
	});
	connect(returnAfter.data(), &QCheckBox::toggled, this, [](bool checked) {
		config_set_bool(App()->GetUserConfig(), "BasicWindow", "SwitcherReplayReturn", checked);
	});

	/* Capture */
	QHBoxLayout *capture = new QHBoxLayout();
	QPushButton *toPreview = new QPushButton(QTStr("Switcher.Replay.ToPreview"), this);
	QPushButton *toAir = new QPushButton(QTStr("Switcher.Replay.ToAir"), this);
	toAir->setObjectName("switcherReplayAir");
	capture->addWidget(toPreview);
	capture->addWidget(toAir);
	layout->addLayout(capture);

	connect(toPreview, &QPushButton::clicked, this,
		[this]() { main->SwitcherReplayCapture(OBSBasic::SwitcherReplayTarget::Preview); });
	connect(toAir, &QPushButton::clicked, this,
		[this]() { main->SwitcherReplayCapture(OBSBasic::SwitcherReplayTarget::Program); });

	/* Clips saved in this session */
	layout->addWidget(new QLabel(QTStr("Switcher.Replay.Clips"), this));
	clips = new QListWidget(this);
	layout->addWidget(clips, 1);

	QHBoxLayout *clipButtons = new QHBoxLayout();
	QPushButton *clipPreview = new QPushButton(QTStr("Switcher.Replay.ClipPreview"), this);
	QPushButton *clipAir = new QPushButton(QTStr("Switcher.Replay.ClipAir"), this);
	clipButtons->addWidget(clipPreview);
	clipButtons->addWidget(clipAir);
	clipButtons->addStretch(1);
	layout->addLayout(clipButtons);

	auto loadClip = [this](OBSBasic::SwitcherReplayTarget target) {
		QListWidgetItem *item = clips->currentItem();
		if (item) {
			main->SwitcherReplayLoad(item->data(Qt::UserRole).toString(), target);
		}
	};
	connect(clipPreview, &QPushButton::clicked, this,
		[loadClip]() { loadClip(OBSBasic::SwitcherReplayTarget::Preview); });
	connect(clipAir, &QPushButton::clicked, this,
		[loadClip]() { loadClip(OBSBasic::SwitcherReplayTarget::Program); });
	connect(clips.data(), &QListWidget::itemDoubleClicked, this,
		[loadClip]() { loadClip(OBSBasic::SwitcherReplayTarget::Preview); });

	QLabel *hint = new QLabel(QTStr("Switcher.Replay.Hint"), this);
	hint->setObjectName("switcherSettingsHint");
	hint->setWordWrap(true);
	layout->addWidget(hint);

	QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::close);
	layout->addWidget(box);

	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &SwitcherReplay::Refresh);
	timer->start(500);
	Refresh();
}

void SwitcherReplay::Refresh()
{
	bool active = main->ReplayBufferActive();
	status->setText(QTStr(active ? "Switcher.Replay.BufferOn" : "Switcher.Replay.BufferOff"));
	status->setStyleSheet(active ? "color: #e5484d; font-weight: bold;" : QString());
	bufferButton->setText(QTStr(active ? "Switcher.Replay.StopBuffer" : "Switcher.Replay.StartBuffer"));

	if (clips->count() != main->switcherReplayClips.size()) {
		RefreshClips();
	}
}

void SwitcherReplay::RefreshClips()
{
	clips->clear();
	for (const QString &path : main->switcherReplayClips) {
		QListWidgetItem *item = new QListWidgetItem(QFileInfo(path).fileName(), clips);
		item->setData(Qt::UserRole, path);
		item->setToolTip(path);
	}
	clips->setCurrentRow(0);
}
