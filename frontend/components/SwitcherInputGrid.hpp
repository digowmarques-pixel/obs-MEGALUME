#pragma once

#include <obs.hpp>

#include <QFrame>
#include <QPointer>
#include <QScrollArea>

#include <vector>

class FlowLayout;
class OBSQTDisplay;
class QLabel;

/* One input of the switcher: a live thumbnail of a scene with its number,
 * name and quick actions, colored by whether it is in Preview or Program. */
class SwitcherInputTile : public QFrame {
	Q_OBJECT

	OBSWeakSource weakSource;
	QPointer<OBSQTDisplay> thumbnail;
	QPointer<QLabel> numberLabel;
	QPointer<QLabel> nameLabel;
	QPointer<QFrame> header;

	static void DrawThumbnail(void *data, uint32_t cx, uint32_t cy);

protected:
	bool eventFilter(QObject *obj, QEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

public:
	SwitcherInputTile(obs_source_t *source, QWidget *parent = nullptr);
	~SwitcherInputTile();

	OBSSource GetSource() const;
	bool IsSource(obs_source_t *source) const;

	void SetNumber(int number);
	void UpdateName();
	void SetLiveState(bool preview, bool program);

signals:
	void PreviewRequested(OBSSource source);
	void QuickPlayRequested(OBSSource source);
	void CutRequested(OBSSource source);
	void CloseRequested(OBSSource source);
	void MenuRequested(OBSSource source, const QPoint &globalPos);
};

/* The switcher's input area: every scene as a tile, side by side, wrapping
 * to new rows as needed. */
class SwitcherInputGrid : public QScrollArea {
	Q_OBJECT

	QPointer<QWidget> contents;
	FlowLayout *flowLayout = nullptr;
	std::vector<QPointer<SwitcherInputTile>> tiles;

public:
	SwitcherInputGrid(QWidget *parent = nullptr);
	~SwitcherInputGrid();

	void SetScenes(const std::vector<OBSSource> &scenes);
	void SetLiveStates(obs_source_t *preview, obs_source_t *program);

signals:
	void PreviewRequested(OBSSource source);
	void QuickPlayRequested(OBSSource source);
	void CutRequested(OBSSource source);
	void CloseRequested(OBSSource source);
	void MenuRequested(OBSSource source, const QPoint &globalPos);
};
