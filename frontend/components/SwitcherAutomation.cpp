#include "SwitcherAutomation.hpp"
#include "SwitcherInputGrid.hpp"

#include <qt-wrappers.hpp>

#include <cstring>

QStringList SwitcherGetList(obs_source_t *scene)
{
	QStringList files;
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	OBSDataArrayAutoRelease array = obs_data_get_array(settings, "switcher_list");

	size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease item = obs_data_array_item(array, i);
		files << QT_UTF8(obs_data_get_string(item, "file"));
	}
	return files;
}

void SwitcherSetList(obs_source_t *scene, const QStringList &files)
{
	OBSDataArrayAutoRelease array = obs_data_array_create();
	for (const QString &file : files) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "file", QT_TO_UTF8(file));
		obs_data_array_push_back(array, item);
	}

	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	obs_data_set_array(settings, "switcher_list", array);
}

bool SwitcherGetListLoop(obs_source_t *scene)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	return obs_data_get_bool(settings, "switcher_list_loop");
}

void SwitcherSetListLoop(obs_source_t *scene, bool loop)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	obs_data_set_bool(settings, "switcher_list_loop", loop);
}

int SwitcherGetListIndex(obs_source_t *scene)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	return (int)obs_data_get_int(settings, "switcher_list_index");
}

bool SwitcherPlayListItem(obs_source_t *scene, int index)
{
	QStringList files = SwitcherGetList(scene);
	OBSSource media = SwitcherFindMediaSource(scene);
	if (index < 0 || index >= files.size() || !media ||
	    strcmp(obs_source_get_unversioned_id(media), "ffmpeg_source") != 0) {
		return false;
	}

	OBSDataAutoRelease privateSettings = obs_source_get_private_settings(scene);
	obs_data_set_int(privateSettings, "switcher_list_index", index);

	/* Changing the file restarts the video by itself; restarting it as
	 * well would open the file twice at once.
	 *
	 * Clearing on media end makes the media source preload the first frame
	 * of each file, and libobs can resize the video texture for that frame
	 * between a tick and a render of the previous file, which crashes when
	 * the files differ in size. Keeping the last frame avoids the preload,
	 * and holds the picture between videos like a switcher should. */
	OBSDataAutoRelease current = obs_source_get_settings(media);
	if (files[index] == QT_UTF8(obs_data_get_string(current, "local_file")) &&
	    obs_data_get_bool(current, "is_local_file") && !obs_data_get_bool(current, "clear_on_media_end")) {
		obs_source_media_restart(media);
	} else {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_bool(settings, "is_local_file", true);
		obs_data_set_bool(settings, "clear_on_media_end", false);
		obs_data_set_string(settings, "local_file", QT_TO_UTF8(files[index]));
		obs_source_update(media, settings);
	}
	return true;
}

bool SwitcherAdvanceList(obs_source_t *scene)
{
	QStringList files = SwitcherGetList(scene);
	if (files.isEmpty()) {
		return false;
	}

	int next = SwitcherGetListIndex(scene) + 1;
	if (next >= files.size()) {
		if (!SwitcherGetListLoop(scene)) {
			return false;
		}
		next = 0;
	}
	return SwitcherPlayListItem(scene, next);
}

SwitcherEndTrigger SwitcherGetEndTrigger(obs_source_t *scene)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);

	SwitcherEndTrigger trigger;
	trigger.action = (int)obs_data_get_int(settings, "switcher_end_action");
	trigger.targetUuid = QT_UTF8(obs_data_get_string(settings, "switcher_end_target"));
	trigger.cut = obs_data_get_bool(settings, "switcher_end_cut");
	return trigger;
}

void SwitcherSetEndTrigger(obs_source_t *scene, const SwitcherEndTrigger &trigger)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene);
	obs_data_set_int(settings, "switcher_end_action", trigger.action);
	obs_data_set_string(settings, "switcher_end_target", QT_TO_UTF8(trigger.targetUuid));
	obs_data_set_bool(settings, "switcher_end_cut", trigger.cut);
}
