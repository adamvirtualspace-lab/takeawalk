/**
 * @brief Game settings which are changed while on foot and put back afterwards: the
 * driving HUD is hidden, and developer mode is switched on, without which the game
 * does not let the walker's camera be used.
 */

#pragma once

namespace hud {

/**
 * @brief Call once the game is running. Undoes what a previous session left hidden,
 * for example after a crash while on foot.
 */
void recover(void);

/**
 * @brief Changes the settings for walking. Call before taking the camera.
 */
void hide(void);

/**
 * @param game_closing The plugin is being shut down, usually because the game quits.
 */
void restore(bool game_closing);

} // namespace hud
