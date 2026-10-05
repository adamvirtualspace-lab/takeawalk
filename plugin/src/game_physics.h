/**
 * @brief Queries against the game's collision world (PhysX 3.4, linked into the game).
 *
 * Nothing here is part of the SCS SDK; see docs/RESEARCH.md.
 */

#pragma once

namespace game {

/**
 * @brief Finds the physics code in the running game. False if the game build is not supported.
 */
bool physics_attach(void);

/**
 * @brief Works out where the physics world's origin is, using the truck as a landmark.
 *
 * Physics coordinates are world coordinates minus a whole number of map chunks. The
 * offset can change as the player travels, so call this again from time to time.
 *
 * @param yaw Truck heading (rad).
 */
bool physics_locate(double truck_x, double truck_y, double truck_z, float yaw);

/**
 * @brief Looks for static geometry straight below a world position.
 *
 * @param from_y Height the search starts at.
 * @param distance How far down to search (m).
 * @param[out] height Height of the first surface found.
 */
bool physics_ground_height(double x, double z, double from_y, float distance, float &height);

} // namespace game
