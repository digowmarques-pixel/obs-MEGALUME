#include "SwitcherInputGrid.hpp"
#include "FlowLayout.hpp"

#include <OBSApp.hpp>
#include <utility/display-helpers.hpp>
#include <widgets/OBSQTDisplay.hpp>

#include <qt-wrappers.hpp>

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

#include "moc_SwitcherInputGrid.cpp"

#define TILE_WIDTH 176
#define THUMBNAIL_HEIGHT 99

SwitcherInputTile::SwitcherInputTile(obs_source_t *source, QWidget *parent)
	: QFrame(parent),
	  weakSource(OBSGetWeakRef(source))
{
	setObjectName("switcherInputTile");
	setFixedWidth(TILE_WIDTH);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	/* Number and name, colored by live state */
	header = new QFrame(this);
	header->setObjectName("switcherInputHeader");

	QHBoxLayout *headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(4, 2, 4, 2);
	headerLayout->setSpacing(6);

	numberLabel = new QLabel(header);
	numberLabel->setObjectName("switcherInputNumber");

	nameLabel = new QLabel(header);
	nameLabel->setObjectName("switcherInputName");
	nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	headerLayout->addWidget(numberLabel);
	headerLayout->addWidget(nameLabel, 1);

	/* Live thumbnail */
	thumbnail = new OBSQTDisplay(this);
	thumbnail->setObjectName("switcherInputThumbnail");
	thumbnail->setFixedSize(TILE_WIDTH, THUMBNAIL_HEIGHT);
	thumbnail->SetDisplayBackgroundColor(Qt::black);
	thumbnail->installEventFilter(this);
	header->installEventFilter(this);

	connect(thumbnail.data(), &OBSQTDisplay::DisplayCreated, this, [this]() {
		obs_display_add_draw_callback(thumbnail->GetDisplay(), SwitcherInputTile::DrawThumbnail, this);
	});

	/* Quick actions */
	QHBoxLayout *buttons = new QHBoxLayout();
	buttons->setContentsMargins(2, 2, 2, 2);
	buttons->setSpacing(2);

	auto addButton = [&](const char *text, const char *name, void (SwitcherInputTile::*signal)(OBSSource)) {
		QPushButton *button = new QPushButton(QTStr(text), this);
		button->setObjectName(name);
		button->setProperty("class", "switcher-input-button");
		button->setFocusPolicy(Qt::NoFocus);
		connect(button, &QPushButton::clicked, this, [this, signal]() {
			OBSSource source = GetSource();
			if (source) {
				emit(this->*signal)(source);
			}
		});
		buttons->addWidget(button);
	};

	addButton("Switcher.Input.Close", "switcherInputClose", &SwitcherInputTile::CloseRequested);
	addButton("Switcher.QuickPlay", "switcherInputQuickPlay", &SwitcherInputTile::QuickPlayRequested);
	addButton("Switcher.Input.Cut", "switcherInputCut", &SwitcherInputTile::CutRequested);

	layout->addWidget(header);
	layout->addWidget(thumbnail);
	layout->addLayout(buttons);

	/* Keep the scene's sources running so the thumbnail stays live */
	obs_source_inc_showing(source);

	UpdateName();
}

SwitcherInputTile::~SwitcherInputTile()
{
	if (thumbnail && thumbnail->GetDisplay()) {
		obs_display_remove_draw_callback(thumbnail->GetDisplay(), SwitcherInputTile::DrawThumbnail, this);
	}

	OBSSource source = GetSource();
	if (source) {
		obs_source_dec_showing(source);
	}
}

void SwitcherInputTile::DrawThumbnail(void *data, uint32_t cx, uint32_t cy)
{
	SwitcherInputTile *tile = static_cast<SwitcherInputTile *>(data);

	OBSSourceAutoRelease source = obs_weak_source_get_source(tile->weakSource);
	if (!source) {
		return;
	}

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

bool SwitcherInputTile::eventFilter(QObject *obj, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
		if (mouseEvent->button() == Qt::LeftButton) {
			OBSSource source = GetSource();
			if (source) {
				emit PreviewRequested(source);
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

void SwitcherInputTile::SetLiveState(bool preview, bool program)
{
	/* Program wins when the same input is in both */
	QString state = program ? "state-program" : (preview ? "state-preview" : "");
	setClasses(header, state);
	setClasses(this, state);
}

SwitcherInputGrid::SwitcherInputGrid(QWidget *parent) : QScrollArea(parent)
{
	setObjectName("switcherInputGrid");
	setWidgetResizable(true);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setFrameShape(QFrame::NoFrame);

	contents = new QWidget();
	contents->setObjectName("switcherInputGridContents");
	flowLayout = new FlowLayout(contents, 4, 4, 4);

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
		connect(tile, &SwitcherInputTile::MenuRequested, this, &SwitcherInputGrid::MenuRequested);
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

	/* Re-add everything in scene order */
	while (QLayoutItem *item = flowLayout->takeAt(0)) {
		delete item;
	}

	tiles = std::move(newTiles);

	int number = 1;
	for (QPointer<SwitcherInputTile> &tile : tiles) {
		tile->SetNumber(number++);
		tile->UpdateName();
		flowLayout->addWidget(tile);
		tile->show();
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
