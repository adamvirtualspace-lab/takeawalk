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
	char        previous[64];
	bool        changed;
};

// The route advisor with its map stays: it is useful for finding the way back.
setting_t settings[] = {
	{ "g_adviser_widget_tachometer", "0", "", false },	// speedometer in the corner
	{ "g_show_tutorial_hints", "0", "", false },		// "Driving: use W and S..." panel
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
	log_message(SCS_LOG_TYPE_message, "putting back the HUD settings an earlier session left changed");

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
	if (hidden || ! config.hide_hud) {
		return;
	}
	hidden = true;

	// A variable whose current value is unknown is left alone, as it could not be put back.

	for (setting_t &setting : settings) {
		setting.changed = game::console_variable(setting.variable, setting.previous, sizeof(setting.previous));
	}
	write_restore_file();
	for (const setting_t &setting : settings) {
		if (setting.changed) {
			log_message(SCS_LOG_TYPE_message, "hiding HUD: %s was %s", setting.variable, setting.previous);
			set(setting.variable, setting.value_on_foot);
		}
		else {
			log_message(SCS_LOG_TYPE_warning, "unable to read %s, leaving it as it is", setting.variable);
		}
	}
}

void restore(void)
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
	delete_restore_file();
}

} // namespace hud
