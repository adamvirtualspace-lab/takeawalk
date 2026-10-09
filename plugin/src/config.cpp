#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <wchar.h>

#include "config.h"

config_t config;

namespace {

const wchar_t *const FILE_NAME = L"takeawalk.ini";

float read_float(const wchar_t *const path, const wchar_t *const section, const wchar_t *const key, const float fallback)
{
	wchar_t text[32];
	if (GetPrivateProfileStringW(section, key, L"", text, 32, path) == 0) {
		return fallback;
	}
	return static_cast<float>(wcstod(text, NULL));
}

bool read_bool(const wchar_t *const path, const wchar_t *const section, const wchar_t *const key, const bool fallback)
{
	return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, path) != 0;
}

} // namespace

bool config_file_path(const wchar_t *const name, wchar_t *const path, const size_t capacity)
{
	HMODULE module = NULL;
	const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
	if (! GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&config_load), &module)) {
		return false;
	}
	const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(capacity));
	if ((length == 0) || (length >= capacity)) {
		return false;
	}
	wchar_t *const separator = wcsrchr(path, L'\\');
	if (! separator || (static_cast<size_t>(separator - path) + 1 + wcslen(name) >= capacity)) {
		return false;
	}
	wcscpy(separator + 1, name);
	return true;
}

void config_load(void)
{
	config.max_distance = 0.0f;
	config.head_bob = true;
	config.footsteps = true;
	config.footstep_volume = 0.2f;
	config.hide_hud = true;
	config.ignore_barriers = true;
	config.trace_collision = false;
	config.collision_census = false;
	config.probe_key = false;

	wchar_t path[MAX_PATH];
	if (! config_file_path(FILE_NAME, path, MAX_PATH)) {
		return;
	}
	config.max_distance = read_float(path, L"walk", L"max_distance", config.max_distance);
	config.head_bob = read_bool(path, L"walk", L"head_bob", config.head_bob);
	config.footsteps = read_bool(path, L"walk", L"footsteps", config.footsteps);
	config.footstep_volume = read_float(path, L"walk", L"footstep_volume", config.footstep_volume);
	config.hide_hud = read_bool(path, L"walk", L"hide_hud", config.hide_hud);
	config.ignore_barriers = read_bool(path, L"walk", L"ignore_barriers", config.ignore_barriers);
	config.trace_collision = read_bool(path, L"debug", L"trace_collision", config.trace_collision);
	config.collision_census = read_bool(path, L"debug", L"collision_census", config.collision_census);
	config.probe_key = read_bool(path, L"debug", L"probe_key", config.probe_key);

	if (config.footstep_volume < 0.0f) {
		config.footstep_volume = 0.0f;
	}
	if (config.footstep_volume > 1.0f) {
		config.footstep_volume = 1.0f;
	}
}
