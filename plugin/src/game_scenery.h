/**
 * @brief Makes signs, poles and other roadside furniture solid around the walker.
 *
 * The game checks each of these every frame and gives it a physics body only while
 * it is within 35 m of the player's truck. This makes the walker count as well.
 *
 * Nothing here is part of the SCS SDK; see docs/RESEARCH.md.
 */

#pragma once

namespace game {

/**
 * @brief Hooks the game's checks. False if the game build is not supported.
 */
bool scenery_attach(void);

void scenery_detach(void);

/**
 * @brief Tells the checks where the walker is. Call every frame while on foot.
 *
 * @param radius Distance from the walker within which things are made solid (m). The
 * game's own limit of 35 m is the most this can do.
 */
void scenery_set_walker(double x, double y, double z, double radius);

/**
 * @brief The walker is back in the truck: only the truck counts again.
 */
void scenery_clear_walker(void);

} // namespace game
