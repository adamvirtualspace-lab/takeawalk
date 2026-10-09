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
void restore(void);

} // namespace hud
