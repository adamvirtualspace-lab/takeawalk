/**
 * @brief Settings read from takeawalk.ini next to the plugin. Every setting is optional.
 */

#pragma once

#include <stddef.h>

struct config_t
{
	// [walk]

	/**
	 * @brief Furthest the walker can go from the truck (m). Zero for no limit.
	 */
	float max_distance;

	/**
	 * @brief Sway the view with each step.
	 */
	bool head_bob;

	bool footsteps;

	/**
	 * @brief Loudness of the footsteps, 0 to 1.
	 */
	float footstep_volume;

	/**
	 * @brief Hide the speedometer and the tutorial hints while on foot.
	 */
	bool hide_hud;

	/**
	 * @brief Walk through the invisible walls behind the X symbols.
	 */
	bool ignore_barriers;

	// [debug]

	/**
	 * @brief Log when and from where the game adds static collision to the physics scene.
	 */
	bool trace_collision;

	/**
	 * @brief Log every few seconds how many static actors exist around the truck and the walker.
	 */
	bool collision_census;

	/**
	 * @brief P while on foot reports what solid object is straight ahead.
	 */
	bool probe_key;
};

extern config_t config;

void config_load(void);

/**
 * @brief Path of a file in the folder this plugin was loaded from.
 */
bool config_file_path(const wchar_t *name, wchar_t *path, size_t capacity);
