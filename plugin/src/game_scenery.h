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

/**
 * @brief Diagnostics: the signs the game is showing, and which of them are solid.
 */
struct scenery_census_t
{
	unsigned shown;

	/**
	 * @brief Signs within the walker's radius, and how many of those have their collision.
	 */
	unsigned near_walker;
	unsigned near_walker_solid;

	/**
	 * @brief The same for signs within the game's 35 m of the truck and not near the walker.
	 */
	unsigned near_truck;
	unsigned near_truck_solid;

	/**
	 * @brief And for all the others, which are expected not to be solid.
	 */
	unsigned elsewhere;
	unsigned elsewhere_solid;

	/**
	 * @brief The sign nearest to the truck which is out of the truck's reach and able to be solid.
	 */
	bool   far_sign_found;
	double far_sign[3];
	double far_sign_from_truck;
	bool   far_sign_solid;
};

/**
 * @brief Counts the signs. Call from the game's main thread only.
 *
 * @param world The game's world object.
 * @param truck Position of the truck, three coordinates.
 * @param walker Position of the walker, NULL when nobody is on foot.
 */
bool scenery_census(void *world, const double *truck, const double *walker, scenery_census_t &census);

} // namespace game
