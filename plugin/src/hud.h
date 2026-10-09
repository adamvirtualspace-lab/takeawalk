/**
 * @brief Hides the driving HUD while on foot and puts it back afterwards.
 */

#pragma once

namespace hud {

/**
 * @brief Call once the game is running. Undoes what a previous session left hidden,
 * for example after a crash while on foot.
 */
void recover(void);

void hide(void);

/**
 * @param game_closing The plugin is being shut down, usually because the game quits.
 */
void restore(bool game_closing);

} // namespace hud
