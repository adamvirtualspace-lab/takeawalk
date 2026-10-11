#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "game_console.h"
#include "hud.h"
#include "log.h"

namespace hud {
namespace {

// Holds the commands which put the HUD back, for as long as it is hidden. The game
// saves console variables, so without it a crash while on foot would leave the HUD
// hidden for good.
const wchar_t *const RESTORE_FILE_NAME = L"takeawalk.restore";

struct setting_t
{
	const char *variable;
	const char *value_on_foot;

	/**
	 * @brief Walking does not work without it, so it is not the player's choice.
	 */
	bool        required;
	char        previous[64];
	bool        changed;
};

// The route advisor with its map stays: it is useful for finding the way back.
setting_t settings[] = {
	// The walker's camera is the game's free camera, which the game only switches to in
	// developer mode. Players have it off, so it is on just for the walk.
	{ "g_developer", "1", true, "", false },

	{ "g_adviser_widget_tachometer", "0", false, "", false },	// speedometer in the corner
	{ "g_show_tutorial_hints", "0", false, "", false },		// "Driving: use W and S..." panel
};

bool hidden = false;

bool restore_file_path(wchar_t *const path)
{
	return config_file_path(RESTORE_FILE_NAME, path, MAX_PATH);
}

void set(const char *const variable, const char *const value)
{
	char command[160];
	snprintf(command, sizeof(command), "%s %s", variable, value);
	game::console_run(command);
}

void write_restore_file(void)
{
	wchar_t path[MAX_PATH];
	if (! restore_file_path(path)) {
		return;
	}
	FILE *const file = _wfopen(path, L"wt");
	if (! file) {
		return;
	}
	for (const setting_t &setting : settings) {
		if (setting.changed) {
			fprintf(file, "%s %s\n", setting.variable, setting.previous);
		}
	}
	fclose(file);
}

void delete_restore_file(void)
{
	wchar_t path[MAX_PATH];
	if (restore_file_path(path)) {
		DeleteFileW(path);
	}
}

} // namespace

void recover(void)
{
	wchar_t path[MAX_PATH];
	if (! restore_file_path(path)) {
		return;
	}
	FILE *const file = _wfopen(path, L"rt");
	if (! file) {
		return;
	}
	log_message(SCS_LOG_TYPE_message, "putting back the settings an earlier session left changed");

	char line[160];
	while (fgets(line, sizeof(line), file)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0]) {
			game::console_run(line);
		}
	}
	fclose(file);
	DeleteFileW(path);
}

void hide(void)
{
	if (hidden) {
		return;
	}
	hidden = true;

	// A variable whose current value is unknown is left alone, as it could not be put
	// back, and so is one which already has the value wanted.

	for (setting_t &setting : settings) {
		setting.changed = false;
		if (! setting.required && ! config.hide_hud) {
			continue;
		}
		if (! game::console_variable(setting.variable, setting.previous, sizeof(setting.previous))) {
			log_message(SCS_LOG_TYPE_warning, "unable to read %s, leaving it as it is", setting.variable);
			continue;
		}
		setting.changed = strcmp(setting.previous, setting.value_on_foot) != 0;
	}
	write_restore_file();
	for (const setting_t &setting : settings) {
		if (setting.changed) {
			log_message(SCS_LOG_TYPE_message, "while on foot: %s set to %s, was %s", setting.variable, setting.value_on_foot, setting.previous);
			set(setting.variable, setting.value_on_foot);
		}
	}
}

void restore(const bool game_closing)
{
	if (! hidden) {
		return;
	}
	hidden = false;
	for (setting_t &setting : settings) {
		if (setting.changed) {
			set(setting.variable, setting.previous);
			setting.changed = false;
		}
	}

	// While the game closes it may already have saved its settings as they are on
	// foot. The file then stays, so the next session puts them right.

	if (! game_closing) {
		delete_restore_file();
	}
}

} // namespace hud
