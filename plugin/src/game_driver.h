/**
 * @brief Hides the driver's figure in the player's truck while the player is on foot.
 *
 * The game shows the driver at the wheel in every outside view, which the walker's
 * camera is.
 *
 * Nothing here is part of the SCS SDK; see docs/RESEARCH.md.
 */

#pragma once

namespace game {

/**
 * @brief Hooks the game's update of the driver. False if the game build is not supported.
 */
bool driver_attach(void);

void driver_detach(void);

/**
 * @brief Takes the driver out of the cab, or puts them back. Takes effect on the game's next frame.
 */
void driver_set_hidden(bool hidden);

} // namespace game
