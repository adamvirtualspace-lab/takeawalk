/**
 * @brief Queries against the game's collision world (PhysX 3.4, linked into the game).
 *
 * Nothing here is part of the SCS SDK; see docs/RESEARCH.md.
 */

#pragma once

#include <stddef.h>

namespace game {

/**
 * @brief The game's PxScene objects.
 */
size_t physics_scenes(void **result, size_t capacity);

/**
 * @brief The game's world object, which owns the map items. NULL if it is not known.
 */
void *physics_world(void);

/**
 * @brief Chunk which is the origin of physics coordinates. False until physics_locate() succeeded.
 */
bool physics_origin(int &chunk_x, int &chunk_z);

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
 * @brief Makes the map around a world position solid.
 *
 * The game only keeps collision for map items a vehicle is touching. This asks for
 * the items within a square of the given half side as well. It has to be repeated
 * every frame, as the game drops collision nothing asks for.
 */
void physics_activate(double x, double y, double z, float radius);

/**
 * @brief Whether the walker passes through the invisible walls behind the X symbols.
 *
 * They exist to keep vehicles on the map. Nothing changes for vehicles: the walls stay
 * in the game, the queries below only stop seeing them.
 */
void physics_ignore_barriers(bool ignored);

/**
 * @brief Describes the first solid thing along a line from a world position, for diagnostics.
 *
 * @param direction Unit vector, three floats.
 * @param[out] actor The game's actor object behind what was hit, NULL if there is none.
 * @return False if there is nothing; the text says so.
 */
bool physics_describe(double x, double y, double z, const float *direction, float distance, char *text, size_t capacity, void *&actor);

/**
 * @brief Looks for static geometry straight below a world position.
 *
 * @param from_y Height the search starts at.
 * @param distance How far down to search (m).
 * @param[out] height Height of the first surface found.
 */
bool physics_ground_height(double x, double z, double from_y, float distance, float &height);

/**
 * @brief Something solid in the way of a horizontal move.
 */
struct obstacle_t
{
	/**
	 * @brief Distance to the surface (m). Zero if the start point is inside the obstacle.
	 */
	float distance;

	/**
	 * @brief Unit vector pointing away from the surface.
	 */
	float normal[3];
};

/**
 * @brief Looks for static or movable geometry along a horizontal line from a world position.
 *
 * @param direction_x,direction_z Unit vector in the X/Z plane.
 */
bool physics_obstacle(double x, double y, double z, float direction_x, float direction_z, float distance, obstacle_t &obstacle);

} // namespace game
