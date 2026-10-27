#pragma once

#include <obs.hpp>

#include <QString>
#include <QStringList>

/* Per-input automation saved with the input, like vMix's List inputs and
 * Triggers: a list of video files played one after another, and what to do
 * when the input's video finishes while it is on air. */

QStringList SwitcherGetList(obs_source_t *scene);
void SwitcherSetList(obs_source_t *scene, const QStringList &files);
bool SwitcherGetListLoop(obs_source_t *scene);
void SwitcherSetListLoop(obs_source_t *scene, bool loop);
int SwitcherGetListIndex(obs_source_t *scene);
/* Loads list item `index` into the input's video and plays it */
bool SwitcherPlayListItem(obs_source_t *scene, int index);
/* Moves to the next item; false at the end of a list that does not loop */
bool SwitcherAdvanceList(obs_source_t *scene);

enum SwitcherEndAction {
	SWITCHER_END_NOTHING = 0,
	SWITCHER_END_NEXT_INPUT = 1,
	SWITCHER_END_INPUT = 2,
	SWITCHER_END_PREVIOUS = 3,
};

struct SwitcherEndTrigger {
	int action = SWITCHER_END_NOTHING;
	QString targetUuid;
	bool cut = false;
};

SwitcherEndTrigger SwitcherGetEndTrigger(obs_source_t *scene);
void SwitcherSetEndTrigger(obs_source_t *scene, const SwitcherEndTrigger &trigger);
