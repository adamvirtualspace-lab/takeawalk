/**
 * @brief Keyboard and mouse input while on foot.
 */

#pragma once

namespace input {

/**
 * @brief Starts the mouse listener. Call once when the plugin is initialized.
 */
bool init(void);

void shutdown(void);

/**
 * @brief Starts collecting mouse movement and hides the walk keys from the game.
 */
void capture_start(void);

void capture_stop(void);

/**
 * @brief Mouse movement in counts since the previous call.
 */
void take_mouse_delta(long &x, long &y);

bool key_down(int virtual_key);

/**
 * @brief Whether the game window is the one receiving keyboard input.
 */
bool game_has_focus(void);

} // namespace input
