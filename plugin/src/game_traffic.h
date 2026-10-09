/**
 * @brief Makes traffic vehicles, parked cars included, solid around the walker.
 *
 * The game gives a traffic vehicle a physics body only while it is within 25 m of the
 * player's truck or trailer, and takes it away again beyond 35 m. This adds the walker
 * to what counts as near.
 *
 * Nothing here is part of the SCS SDK; see docs/RESEARCH.md.
 */

#pragma once

namespace game {

/**
 * @brief Hooks the game's proximity test. False if the game build is not supported.
 */
bool traffic_attach(void);

void traffic_detach(void);

/**
 * @brief Tells the proximity test where the walker is. Call every frame while on foot.
 */
void traffic_set_walker(double x, double y, double z);

/**
 * @brief The walker is back in the truck: only the truck counts again.
 */
void traffic_clear_walker(void);

} // namespace game
