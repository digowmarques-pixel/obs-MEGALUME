#include "SwitcherInputGrid.hpp"
#include "FlowLayout.hpp"

#include <OBSApp.hpp>
#include <utility/display-helpers.hpp>
#include <widgets/OBSQTDisplay.hpp>

#include <qt-wrappers.hpp>

#include <dialogs/NameDialog.hpp>

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "moc_SwitcherInputGrid.cpp"

#define VU_WIDTH 10

/* Tile width follows the Display setting; the thumbnail keeps 16:9 */
static int TileWidth()
{
	config_t *config = App()->GetUserConfig();
	if (!config_has_user_value(config, "BasicWindow", "SwitcherTileSize")) {
		return 212;
	}

	switch (config_get_int(config, "BasicWindow", "SwitcherTileSize")) {
	case 0:
		return 196;
	case 2:
		return 256;
	default:
		return 212;
	}
}

#define METER_FLOOR_DB -60.0f
#define METER_DECAY_DB 1.2f

/* ------------------------------------------------------------------------- */

QColor SwitcherCategoryColor(int category)
{
	static const QColor colors[SWITCHER_CATEGORY_COUNT] = {
		QColor(196, 36, 48),  QColor(46, 150, 62),  QColor(226, 128, 20),
		QColor(146, 58, 176), QColor(40, 110, 206), QColor(26, 150, 150),
	};

	if (category < 1 || category > SWITCHER_CATEGORY_COUNT) {
		return QColor();
	}

	return colors[category - 1];
}

QString SwitcherCategoryName(int category)
{
	std::string key = "SwitcherCategory" + std::to_string(category);
	const char *name = config_get_string(App()->GetUserConfig(), "BasicWindow", key.c_str());

	if (name && *name) {
		return QT_UTF8(name);
	}

	std::string defaultKey = "Switcher.Category." + std::to_string(category);
	return QTStr(defaultKey.c_str());
}

void SwitcherSetCategoryName(int category, const QString &name)
{
	std::string key = "SwitcherCategory" + std::to_string(category);
	config_set_string(App()->GetUserConfig(), "BasicWindow", key.c_str(), QT_TO_UTF8(name));
}

int SwitcherGetCategory(obs_source_t *scene)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	return (int)obs_data_get_int(settings, "switcher_category");
}

void SwitcherSetCategory(obs_source_t *scene, int category)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	obs_data_set_int(settings, "switcher_category", category);
}

int SwitcherGetClickAction(obs_source_t *scene)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	return (int)obs_data_get_int(settings, "switcher_click_action");
}

void SwitcherSetClickAction(obs_source_t *scene, int action)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	obs_data_set_int(settings, "switcher_click_action", action);
}

int SwitcherGetGoAction(obs_source_t *scene)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	return (int)obs_data_get_int(settings, "switcher_go_action");
}

void SwitcherSetGoAction(obs_source_t *scene, int action)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	obs_data_set_int(settings, "switcher_go_action", action);
}

void SwitcherRenderSourceFit(obs_source_t *source, uint32_t cx, uint32_t cy)
{
	uint32_t sourceCX = std::max(obs_source_get_width(source), 1u);
	uint32_t sourceCY = std::max(obs_source_get_height(source), 1u);

	int x, y;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);

	int newCX = int(scale * float(sourceCX));
	int newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	const bool previous = gs_set_linear_srgb(true);

	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(source);

	gs_set_linear_srgb(previous);
	gs_projection_pop();
	gs_viewport_pop();
}

/* ------------------------------------------------------------------------- */

SwitcherVuMeter::SwitcherVuMeter(QWidget *parent) : QWidget(parent)
{
	setObjectName("switcherInputMeter");
}

void SwitcherVuMeter::SetLevels(float left, float right)
{
	if (left == levels[0] && right == levels[1]) {
		return;
	}

	levels[0] = left;
	levels[1] = right;
	update();
}

void SwitcherVuMeter::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.fillRect(rect(), palette().window());
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(8, 9, 12));
	painter.drawRoundedRect(QRectF(rect()).adjusted(1, 0, 0, 0), 3, 3);

	int barWidth = (width() - 4) / 2;
	int h = height() - 4;

	QLinearGradient gradient(0, h, 0, 0);
	gradient.setColorAt(0.0, QColor(40, 190, 70));
	gradient.setColorAt(0.75, QColor(40, 190, 70));
	gradient.setColorAt(0.85, QColor(230, 200, 40));
	gradient.setColorAt(1.0, QColor(225, 50, 50));

	for (int i = 0; i < 2; i++) {
		float level = std::clamp(levels[i], METER_FLOOR_DB, 0.0f);
		int barHeight = int(float(h) * (level - METER_FLOOR_DB) / -METER_FLOOR_DB);
		int x = 2 + i * (barWidth + 1);

		painter.setBrush(gradient);
		painter.drawRoundedRect(QRectF(x, 2 + h - barHeight, barWidth, barHeight), 1.5, 1.5);
	}
}

/* ------------------------------------------------------------------------- */

static void CollectSources(obs_scene_t *scene, std::vector<obs_source_t *> &media, std::vector<obs_source_t *> &audio,
			   int depth);

struct CollectContext {
	std::vector<obs_source_t *> &media;
	std::vector<obs_source_t *> &audio;
	int depth;
};

static bool CollectItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	CollectContext *ctx = static_cast<CollectContext *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);

	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, CollectItem, param);
		return true;
	}

	/* Nested scenes contribute their own media and audio */
	if (obs_scene_t *nested = obs_scene_from_source(source)) {
		if (ctx->depth < 4) {
			CollectSources(nested, ctx->media, ctx->audio, ctx->depth + 1);
		}
		return true;
	}

	uint32_t flags = obs_source_get_output_flags(source);
	auto addUnique = [source](std::vector<obs_source_t *> &list) {
		if (std::find(list.begin(), list.end(), source) == list.end()) {
			list.push_back(source);
		}
	};

	if (flags & OBS_SOURCE_CONTROLLABLE_MEDIA) {
		addUnique(ctx->media);
	}
	if (flags & OBS_SOURCE_AUDIO) {
		addUnique(ctx->audio);
	}

	return true;
}

static void CollectSources(obs_scene_t *scene, std::vector<obs_source_t *> &media, std::vector<obs_source_t *> &audio,
			   int depth)
{
	CollectContext ctx{media, audio, depth};
	obs_scene_enum_items(scene, CollectItem, &ctx);
}

OBSSource SwitcherFindMediaSource(obs_source_t *scene)
{
	obs_scene_t *sceneData = obs_scene_from_source(scene);
	if (!sceneData) {
		return nullptr;
	}

	std::vector<obs_source_t *> media;
	std::vector<obs_source_t *> audio;
	CollectSources(sceneData, media, audio, 0);

	return media.empty() ? nullptr : OBSSource(media.front());
}

static const char *LoopSettingName(obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);

	if (strcmp(id, "ffmpeg_source") == 0) {
		return "looping";
	}
	if (strcmp(id, "vlc_source") == 0) {
		return "loop";
	}

	return nullptr;
}

static bool IsLooping(obs_source_t *source)
{
	const char *setting = LoopSettingName(source);
	if (!setting) {
		return false;
	}

	OBSDataAutoRelease settings = obs_source_get_settings(source);
	return obs_data_get_bool(settings, setting);
}

static bool IsPlaying(obs_source_t *source)
{
	enum obs_media_state state = obs_source_media_get_state(source);
	return state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_OPENING ||
	       state == OBS_MEDIA_STATE_BUFFERING;
}

/* ------------------------------------------------------------------------- */

SwitcherInputTile::SwitcherInputTile(obs_source_t *source, QWidget *parent)
	: QFrame(parent),
	  weakSource(OBSGetWeakRef(source))
{
	pendingPeak[0] = METER_FLOOR_DB;
	pendingPeak[1] = METER_FLOOR_DB;

	setObjectName("switcherInputTile");
	const int tileWidth = TileWidth();
	const int thumbnailWidth = tileWidth - VU_WIDTH;
	const int thumbnailHeight = thumbnailWidth * 9 / 16;

	setFixedWidth(tileWidth);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	/* Number and name, colored by live state */
	header = new QFrame(this);
	header->setObjectName("switcherInputHeader");

	QHBoxLayout *headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(4, 1, 1, 1);
	headerLayout->setSpacing(6);

	categoryMark = new QFrame(header);
	categoryMark->setObjectName("switcherInputCategory");
	categoryMark->setFixedSize(9, 9);
	categoryMark->setVisible(false);

	numberLabel = new QLabel(header);
	numberLabel->setObjectName("switcherInputNumber");

	nameLabel = new QLabel(header);
	nameLabel->setObjectName("switcherInputName");
	nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	headerLayout->addWidget(categoryMark);
	headerLayout->addWidget(numberLabel);
	headerLayout->addWidget(nameLabel, 1);

	QPushButton *closeButton = new QPushButton(QStringLiteral("✕"), header);
	closeButton->setObjectName("switcherInputClose");
	closeButton->setToolTip(QTStr("Switcher.Input.Close"));
	closeButton->setFocusPolicy(Qt::NoFocus);
	closeButton->setFixedSize(18, 16);
	connect(closeButton, &QPushButton::clicked, this, [this]() {
		OBSSource source = GetSource();
		if (source) {
			emit CloseRequested(source);
		}
	});
	headerLayout->addWidget(closeButton);

	/* Live thumbnail with the audio meter on its right */
	QHBoxLayout *viewLayout = new QHBoxLayout();
	viewLayout->setContentsMargins(0, 0, 0, 0);
	viewLayout->setSpacing(0);

	thumbnail = new OBSQTDisplay(this);
	thumbnail->setObjectName("switcherInputThumbnail");
	thumbnail->setFixedSize(thumbnailWidth, thumbnailHeight);
	thumbnail->SetDisplayBackgroundColor(Qt::black);
	thumbnail->installEventFilter(this);
	header->installEventFilter(this);

	connect(thumbnail.data(), &OBSQTDisplay::DisplayCreated, this, [this]() {
		obs_display_add_draw_callback(thumbnail->GetDisplay(), SwitcherInputTile::DrawThumbnail, this);
	});

	vuMeter = new SwitcherVuMeter(this);
	vuMeter->setFixedSize(VU_WIDTH, thumbnailHeight);

	viewLayout->addWidget(thumbnail);
	viewLayout->addWidget(vuMeter);

	/* Two rows of actions, as on a hardware switcher's input strip */
	auto makeButton = [this](const QString &text, const char *name) {
		QPushButton *button = new QPushButton(text, this);
		button->setObjectName(name);
		button->setProperty("class", "switcher-input-button");
		button->setFocusPolicy(Qt::NoFocus);
		button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		return button;
	};

	auto emitWithSource = [this](auto signal) {
		return [this, signal]() {
			OBSSource source = GetSource();
			if (source) {
				emit(this->*signal)(source);
			}
		};
	};

	/* Two rows of actions, laid out like a vMix input strip:
	 * 1 2 3 4 GO Cut Loop Play / 5 6 7 8 Audio Projector Settings */
	QHBoxLayout *firstRow = new QHBoxLayout();
	firstRow->setContentsMargins(4, 4, 4, 0);
	firstRow->setSpacing(2);

	QHBoxLayout *secondRow = new QHBoxLayout();
	secondRow->setContentsMargins(4, 2, 4, 4);
	secondRow->setSpacing(2);

	for (int i = 0; i < SWITCHER_OVERLAY_COUNT; i++) {
		QPushButton *button = makeButton(QString::number(i + 1), "switcherInputOverlay");
		button->setFixedWidth(20);
		button->setToolTip(QTStr("Switcher.Input.Overlay").arg(i + 1));
		connect(button, &QPushButton::clicked, this, [this, i]() {
			OBSSource source = GetSource();
			if (source) {
				emit OverlayRequested(source, i);
			}
		});
		overlayButtons[i] = button;
		(i < SWITCHER_OVERLAY_COUNT / 2 ? firstRow : secondRow)->addWidget(button);
	}

	QPushButton *goButton = makeButton(QTStr("Switcher.Input.Go"), "switcherInputGo");
	goButton->setToolTip(QTStr("Switcher.Input.GoTT"));
	connect(goButton, &QPushButton::clicked, this, [this]() {
		OBSSource source = GetSource();
		if (!source) {
			return;
		}

		if (SwitcherGetGoAction(source) == SWITCHER_GO_CUT) {
			emit CutRequested(source);
		} else {
			emit QuickPlayRequested(source);
		}
	});

	QPushButton *cutButton = makeButton(QTStr("Switcher.Input.Cut"), "switcherInputCut");
	loopButton = makeButton(QTStr("Switcher.Input.Loop"), "switcherInputLoop");
	playButton = makeButton(QString(), "switcherInputPlay");
	playButton->setProperty("class", "switcher-input-button icon-media-play");
	playButton->setFixedWidth(22);

	connect(cutButton, &QPushButton::clicked, this, emitWithSource(&SwitcherInputTile::CutRequested));
	connect(loopButton.data(), &QPushButton::clicked, this, &SwitcherInputTile::ToggleLoop);
	connect(playButton.data(), &QPushButton::clicked, this, &SwitcherInputTile::TogglePlay);

	firstRow->addWidget(goButton, 1);
	firstRow->addWidget(cutButton, 1);
	firstRow->addWidget(loopButton);
	firstRow->addWidget(playButton);

	audioButton = makeButton(QTStr("Switcher.Input.Audio"), "switcherInputAudio");
	audioButton->setToolTip(QTStr("Switcher.Input.AudioTT"));
	connect(audioButton.data(), &QPushButton::clicked, this, &SwitcherInputTile::ToggleAudio);

	QPushButton *projectorButton = makeButton(QString(), "switcherInputProjector");
	projectorButton->setProperty("class", "switcher-input-button icon-switcher-projector");
	projectorButton->setToolTip(QTStr("Projector.Open.Scene"));
	projectorButton->setFixedWidth(22);
	connect(projectorButton, &QPushButton::clicked, this, emitWithSource(&SwitcherInputTile::ProjectorRequested));

	QPushButton *settingsButton = makeButton(QString(), "switcherInputSettings");
	settingsButton->setProperty("class", "switcher-input-button icon-gear");
	settingsButton->setToolTip(QTStr("Switcher.Input.Settings"));
	settingsButton->setFixedWidth(22);
	connect(settingsButton, &QPushButton::clicked, this, emitWithSource(&SwitcherInputTile::SettingsRequested));

	secondRow->addWidget(audioButton, 1);
	secondRow->addWidget(projectorButton);
	secondRow->addWidget(settingsButton);

	layout->addWidget(header);
	layout->addLayout(viewLayout);
	layout->addLayout(firstRow);
	layout->addLayout(secondRow);

	/* The meter animates quickly, the media and audio state can lag a bit */
	QTimer *meterTimer = new QTimer(this);
	connect(meterTimer, &QTimer::timeout, this, &SwitcherInputTile::UpdateMeter);
	meterTimer->start(33);

	QTimer *stateTimer = new QTimer(this);
	connect(stateTimer, &QTimer::timeout, this, [this]() {
		RescanSources();
		UpdateButtonStates();
	});
	stateTimer->start(500);

	UpdateName();
	UpdateCategory();
	RescanSources();
	UpdateButtonStates();
}

SwitcherInputTile::~SwitcherInputTile()
{
	if (thumbnail && thumbnail->GetDisplay()) {
		obs_display_remove_draw_callback(thumbnail->GetDisplay(), SwitcherInputTile::DrawThumbnail, this);
	}

	ClearAudioSources();

	OBSSource source = GetSource();
	if (source && showing) {
		obs_source_dec_showing(source);
	}
}

void SwitcherInputTile::ActivateLater(int delayMs)
{
	/* Showing the scene keeps its sources running so the thumbnail is
	 * live. Cameras, captures and browsers can be slow to start, so a
	 * full grid wakes up a tile at a time instead of all at once. */
	QTimer::singleShot(delayMs, this, [this]() {
		OBSSource source = GetSource();
		if (source && !showing) {
			obs_source_inc_showing(source);
			showing = true;
		}
	});
}

void SwitcherInputTile::DrawThumbnail(void *data, uint32_t cx, uint32_t cy)
{
	SwitcherInputTile *tile = static_cast<SwitcherInputTile *>(data);

	OBSSourceAutoRelease source = obs_weak_source_get_source(tile->weakSource);
	if (source) {
		SwitcherRenderSourceFit(source, cx, cy);
	}
}

void SwitcherInputTile::VolmeterUpdated(void *data, const float[MAX_AUDIO_CHANNELS],
					const float peak[MAX_AUDIO_CHANNELS], const float[MAX_AUDIO_CHANNELS])
{
	SwitcherInputTile *tile = static_cast<SwitcherInputTile *>(data);

	/* Mono sources only report the first channel */
	float levels[2] = {peak[0], std::isfinite(peak[1]) ? peak[1] : peak[0]};

	for (int i = 0; i < 2; i++) {
		if (!std::isfinite(levels[i])) {
			continue;
		}

		/* Several sources share the meter, so keep the loudest */
		float current = tile->pendingPeak[i].load();
		while (levels[i] > current && !tile->pendingPeak[i].compare_exchange_weak(current, levels[i])) {
		}
	}
}

void SwitcherInputTile::ClearAudioSources()
{
	for (std::unique_ptr<AudioSource> &audio : audioSources) {
		obs_volmeter_remove_callback(audio->volmeter, SwitcherInputTile::VolmeterUpdated, this);
		obs_volmeter_destroy(audio->volmeter);
	}

	audioSources.clear();
}

void SwitcherInputTile::RescanSources()
{
	OBSSource source = GetSource();
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		return;
	}

	std::vector<obs_source_t *> media;
	std::vector<obs_source_t *> audio;
	CollectSources(scene, media, audio, 0);

	mediaSources.clear();
	for (obs_source_t *mediaSource : media) {
		mediaSources.push_back(OBSGetWeakRef(mediaSource));
	}

	/* Volume meters are only rebuilt when the set of audio sources changes */
	bool audioChanged = audio.size() != audioSources.size();
	for (size_t i = 0; !audioChanged && i < audio.size(); i++) {
		audioChanged = !obs_weak_source_references_source(audioSources[i]->source, audio[i]);
	}

	if (!audioChanged) {
		return;
	}

	ClearAudioSources();

	for (obs_source_t *audioSource : audio) {
		auto entry = std::make_unique<AudioSource>();
		entry->source = OBSGetWeakRef(audioSource);
		entry->volmeter = obs_volmeter_create(OBS_FADER_LOG);
		obs_volmeter_attach_source(entry->volmeter, audioSource);
		obs_volmeter_add_callback(entry->volmeter, SwitcherInputTile::VolmeterUpdated, this);
		audioSources.push_back(std::move(entry));
	}
}

void SwitcherInputTile::UpdateButtonStates()
{
	bool hasMedia = false;
	bool anyPlaying = false;
	bool anyLooping = false;

	for (const OBSWeakSource &weak : mediaSources) {
		OBSSource media = OBSGetStrongRef(weak);
		if (!media) {
			continue;
		}

		hasMedia = true;
		anyPlaying = anyPlaying || IsPlaying(media);
		anyLooping = anyLooping || IsLooping(media);
	}

	loopButton->setEnabled(hasMedia);
	playButton->setEnabled(hasMedia);
	setClasses(loopButton, anyLooping ? "switcher-input-button state-active" : "switcher-input-button");
	setClasses(playButton,
		   anyPlaying ? "switcher-input-button icon-media-pause" : "switcher-input-button icon-media-play");

	bool hasAudio = false;
	bool anyUnmuted = false;

	for (const std::unique_ptr<AudioSource> &entry : audioSources) {
		OBSSource audio = OBSGetStrongRef(entry->source);
		if (!audio) {
			continue;
		}

		hasAudio = true;
		anyUnmuted = anyUnmuted || !obs_source_muted(audio);
	}

	audioButton->setEnabled(hasAudio);
	setClasses(audioButton, anyUnmuted ? "switcher-input-button state-active" : "switcher-input-button");
}

void SwitcherInputTile::UpdateMeter()
{
	for (int i = 0; i < 2; i++) {
		float peak = pendingPeak[i].exchange(METER_FLOOR_DB);
		displayPeak[i] = std::max({peak, displayPeak[i] - METER_DECAY_DB, METER_FLOOR_DB});
	}

	vuMeter->SetLevels(displayPeak[0], displayPeak[1]);
}

void SwitcherInputTile::ToggleLoop()
{
	bool anyLooping = false;
	for (const OBSWeakSource &weak : mediaSources) {
		OBSSource media = OBSGetStrongRef(weak);
		anyLooping = anyLooping || (media && IsLooping(media));
	}

	for (const OBSWeakSource &weak : mediaSources) {
		OBSSource media = OBSGetStrongRef(weak);
		const char *setting = media ? LoopSettingName(media) : nullptr;
		if (!setting) {
			continue;
		}

		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_bool(settings, setting, !anyLooping);
		obs_source_update(media, settings);
	}

	UpdateButtonStates();
}

void SwitcherInputTile::TogglePlay()
{
	bool anyPlaying = false;
	for (const OBSWeakSource &weak : mediaSources) {
		OBSSource media = OBSGetStrongRef(weak);
		anyPlaying = anyPlaying || (media && IsPlaying(media));
	}

	for (const OBSWeakSource &weak : mediaSources) {
		OBSSource media = OBSGetStrongRef(weak);
		if (!media) {
			continue;
		}

		if (anyPlaying) {
			obs_source_media_play_pause(media, true);
			continue;
		}

		enum obs_media_state state = obs_source_media_get_state(media);
		if (state == OBS_MEDIA_STATE_ENDED || state == OBS_MEDIA_STATE_STOPPED) {
			obs_source_media_restart(media);
		} else {
			obs_source_media_play_pause(media, false);
		}
	}

	UpdateButtonStates();
}

void SwitcherInputTile::ToggleAudio()
{
	bool anyUnmuted = false;
	for (const std::unique_ptr<AudioSource> &entry : audioSources) {
		OBSSource audio = OBSGetStrongRef(entry->source);
		anyUnmuted = anyUnmuted || (audio && !obs_source_muted(audio));
	}

	for (const std::unique_ptr<AudioSource> &entry : audioSources) {
		OBSSource audio = OBSGetStrongRef(entry->source);
		if (audio) {
			obs_source_set_muted(audio, anyUnmuted);
		}
	}

	UpdateButtonStates();
}

bool SwitcherInputTile::eventFilter(QObject *obj, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
		if (mouseEvent->button() == Qt::LeftButton) {
			OBSSource source = GetSource();
			if (!source) {
				return true;
			}

			switch (SwitcherGetClickAction(source)) {
			case SWITCHER_CLICK_CUT:
				emit CutRequested(source);
				break;
			case SWITCHER_CLICK_QUICKPLAY:
				emit QuickPlayRequested(source);
				break;
			case SWITCHER_CLICK_NOTHING:
				break;
			default:
				emit PreviewRequested(source);
				break;
			}
			return true;
		}
	}

	return QFrame::eventFilter(obj, event);
}

void SwitcherInputTile::contextMenuEvent(QContextMenuEvent *event)
{
	OBSSource source = GetSource();
	if (source) {
		emit MenuRequested(source, event->globalPos());
	}
}

OBSSource SwitcherInputTile::GetSource() const
{
	return OBSGetStrongRef(weakSource);
}

bool SwitcherInputTile::IsSource(obs_source_t *source) const
{
	return source && obs_weak_source_references_source(weakSource, source);
}

void SwitcherInputTile::SetNumber(int number)
{
	numberLabel->setText(QString::number(number));
}

void SwitcherInputTile::UpdateName()
{
	OBSSource source = GetSource();
	QString name = source ? QT_UTF8(obs_source_get_name(source)) : QString();

	if (nameLabel->text() != name) {
		nameLabel->setText(name);
		nameLabel->setToolTip(name);
	}
}

void SwitcherInputTile::UpdateCategory()
{
	OBSSource source = GetSource();
	category = source ? SwitcherGetCategory(source) : 0;

	QColor color = SwitcherCategoryColor(category);
	categoryMark->setVisible(color.isValid());
	if (color.isValid()) {
		categoryMark->setStyleSheet(
			QString("background: %1; border: 1px solid rgba(255,255,255,0.85); border-radius: 4px;")
				.arg(color.name()));
		categoryMark->setToolTip(SwitcherCategoryName(category));
	}
}

void SwitcherInputTile::showEvent(QShowEvent *event)
{
	/* Tiles filtered out by a tab stop drawing their thumbnail */
	if (thumbnail && thumbnail->GetDisplay()) {
		obs_display_set_enabled(thumbnail->GetDisplay(), true);
	}

	QFrame::showEvent(event);
}

void SwitcherInputTile::hideEvent(QHideEvent *event)
{
	if (thumbnail && thumbnail->GetDisplay()) {
		obs_display_set_enabled(thumbnail->GetDisplay(), false);
	}

	QFrame::hideEvent(event);
}

void SwitcherInputTile::SetLiveState(bool preview, bool program)
{
	/* Program wins when the same input is in both */
	QString state = program ? "state-program" : (preview ? "state-preview" : "");
	setClasses(header, state);
	setClasses(this, state);
}

void SwitcherInputTile::SetOverlayStates(const std::array<OBSSource, SWITCHER_OVERLAY_COUNT> &overlays)
{
	for (int i = 0; i < SWITCHER_OVERLAY_COUNT; i++) {
		bool active = IsSource(overlays[i]);
		setClasses(overlayButtons[i], active ? "switcher-input-button state-active" : "switcher-input-button");
	}
}

/* ------------------------------------------------------------------------- */

SwitcherInputGrid::SwitcherInputGrid(QWidget *parent) : QScrollArea(parent)
{
	setObjectName("switcherInputGrid");
	setWidgetResizable(true);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setFrameShape(QFrame::NoFrame);

	contents = new QWidget();
	contents->setObjectName("switcherInputGridContents");
	flowLayout = new FlowLayout(contents, 8, 8, 8);

	setWidget(contents);
}

SwitcherInputGrid::~SwitcherInputGrid()
{
	/* Tiles must remove their draw callbacks before the displays go */
	for (QPointer<SwitcherInputTile> &tile : tiles) {
		delete tile;
	}
}

void SwitcherInputGrid::SetScenes(const std::vector<OBSSource> &scenes)
{
	std::vector<QPointer<SwitcherInputTile>> newTiles;
	newTiles.reserve(scenes.size());

	/* On the first fill, stagger thumbnail activation after the window is
	 * up; inputs added later go live right away */
	bool firstFill = tiles.empty();
	int activationDelay = firstFill ? 300 : 0;

	for (const OBSSource &scene : scenes) {
		auto it = std::find_if(tiles.begin(), tiles.end(), [&](const QPointer<SwitcherInputTile> &tile) {
			return tile && tile->IsSource(scene);
		});

		if (it != tiles.end()) {
			newTiles.push_back(*it);
			tiles.erase(it);
			continue;
		}

		SwitcherInputTile *tile = new SwitcherInputTile(scene, contents);
		connect(tile, &SwitcherInputTile::PreviewRequested, this, &SwitcherInputGrid::PreviewRequested);
		connect(tile, &SwitcherInputTile::QuickPlayRequested, this, &SwitcherInputGrid::QuickPlayRequested);
		connect(tile, &SwitcherInputTile::CutRequested, this, &SwitcherInputGrid::CutRequested);
		connect(tile, &SwitcherInputTile::CloseRequested, this, &SwitcherInputGrid::CloseRequested);
		connect(tile, &SwitcherInputTile::OverlayRequested, this, &SwitcherInputGrid::OverlayRequested);
		connect(tile, &SwitcherInputTile::ProjectorRequested, this, &SwitcherInputGrid::ProjectorRequested);
		connect(tile, &SwitcherInputTile::SettingsRequested, this, &SwitcherInputGrid::SettingsRequested);
		connect(tile, &SwitcherInputTile::MenuRequested, this, &SwitcherInputGrid::MenuRequested);
		tile->ActivateLater(activationDelay);
		if (firstFill) {
			activationDelay += 120;
		}
		newTiles.push_back(tile);
	}

	/* Whatever is left belongs to scenes that are gone. This may run from
	 * one of the tile's own signals, so let Qt delete it later. */
	for (QPointer<SwitcherInputTile> &tile : tiles) {
		if (tile) {
			flowLayout->removeWidget(tile);
			tile->hide();
			tile->deleteLater();
		}
	}

	tiles = std::move(newTiles);

	int number = 1;
	for (QPointer<SwitcherInputTile> &tile : tiles) {
		tile->SetNumber(number++);
		tile->UpdateName();
		tile->UpdateCategory();
	}

	RebuildLayout();
}

void SwitcherInputGrid::SetFilter(int category)
{
	if (filter == category) {
		return;
	}

	filter = category;
	RebuildLayout();
}

void SwitcherInputGrid::RebuildLayout()
{
	/* Hidden tiles would still take up spacing in the flow layout, so only
	 * the ones in the current tab are added, in scene order */
	while (QLayoutItem *item = flowLayout->takeAt(0)) {
		delete item;
	}

	for (QPointer<SwitcherInputTile> &tile : tiles) {
		bool visible = filter == 0 || tile->Category() == filter;
		if (visible) {
			flowLayout->addWidget(tile);
		}
		tile->setVisible(visible);
	}

	contents->updateGeometry();
}

void SwitcherInputGrid::SetLiveStates(obs_source_t *preview, obs_source_t *program)
{
	for (QPointer<SwitcherInputTile> &tile : tiles) {
		if (tile) {
			tile->SetLiveState(tile->IsSource(preview), tile->IsSource(program));
		}
	}
}

void SwitcherInputGrid::SetOverlayStates(const std::array<OBSSource, SWITCHER_OVERLAY_COUNT> &overlays)
{
	for (QPointer<SwitcherInputTile> &tile : tiles) {
		if (tile) {
			tile->SetOverlayStates(overlays);
		}
	}
}

/* ------------------------------------------------------------------------- */

SwitcherInputTabs::SwitcherInputTabs(QWidget *parent) : QFrame(parent)
{
	setObjectName("switcherInputTabs");

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(8, 6, 8, 2);
	layout->setSpacing(6);

	group = new QButtonGroup(this);
	group->setExclusive(true);

	for (int i = 0; i <= SWITCHER_CATEGORY_COUNT; i++) {
		QPushButton *button = new QPushButton(this);
		button->setCheckable(true);
		button->setFocusPolicy(Qt::NoFocus);
		button->setProperty("class", "switcher-tab");

		QColor color = SwitcherCategoryColor(i);
		if (color.isValid()) {
			/* Category colors are data, not theme, so they are set here */
			button->setStyleSheet(
				QString("QPushButton { background: %1; border: 1px solid %1; color: white;"
					" border-radius: 11px; padding: 0px 12px; }"
					"QPushButton:hover { border-color: rgba(255,255,255,0.5); }"
					"QPushButton:checked { border: 2px solid white; }")
					.arg(color.name()));
			button->setContextMenuPolicy(Qt::CustomContextMenu);
			connect(button, &QPushButton::customContextMenuRequested, this,
				[this, i]() { RenameCategory(i); });
			button->setToolTip(QTStr("Switcher.Category.RenameTT"));
		}

		group->addButton(button, i);
		buttons.push_back(button);
		layout->addWidget(button);
	}

	layout->addStretch(1);

	int saved = (int)config_get_int(App()->GetUserConfig(), "BasicWindow", "SwitcherCategoryFilter");
	if (saved < 0 || saved > SWITCHER_CATEGORY_COUNT) {
		saved = 0;
	}
	buttons[saved]->setChecked(true);

	connect(group, &QButtonGroup::idClicked, this, [this](int id) {
		config_set_int(App()->GetUserConfig(), "BasicWindow", "SwitcherCategoryFilter", id);
		emit FilterChanged(id);
	});

	UpdateNames();
}

void SwitcherInputTabs::UpdateNames()
{
	buttons[0]->setText(QTStr("Switcher.Category.All"));
	for (int i = 1; i <= SWITCHER_CATEGORY_COUNT; i++) {
		buttons[i]->setText(SwitcherCategoryName(i));
	}
}

int SwitcherInputTabs::CurrentFilter() const
{
	return std::max(group->checkedId(), 0);
}

void SwitcherInputTabs::RenameCategory(int category)
{
	std::string name = QT_TO_UTF8(SwitcherCategoryName(category));
	if (!NameDialog::AskForName(this, QTStr("Switcher.Category.Rename"), QTStr("Switcher.Category.RenameText"),
				    name, QT_UTF8(name.c_str())) ||
	    name.empty()) {
		return;
	}

	SwitcherSetCategoryName(category, QT_UTF8(name.c_str()));
	UpdateNames();
	emit CategoriesRenamed();
}
