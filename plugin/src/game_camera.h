/**
 * @brief Access to the game's camera manager and its free camera.
 *
 * Nothing here is part of the SCS SDK. The layout was reverse engineered by the
 * community (see docs/RESEARCH.md) and is checked at runtime before any write.
 */

#pragma once

#include <stdint.h>

namespace game {

#pragma pack(push, 1)

/**
 * @brief Camera position and orientation as the engine stores it.
 *
 * X and Z are relative to a map chunk, Y is the world height.
 */
struct placement_t
{
	float   position[3];
	int16_t chunk_x;
	int16_t chunk_z;
	float   rotation[4];	// Quaternion: w, x, y, z.
};

#pragma pack(pop)

/**
 * @brief Finds the camera code in the running game. False if the game build is not supported.
 */
bool camera_attach(void);

/**
 * @brief Switches the game to the free camera and stops the engine from moving it.
 *
 * @param[out] reference Placement of the camera which was active before the switch.
 */
bool camera_take(placement_t &reference);

/**
 * @brief Moves the free camera. Call every frame while the camera is taken.
 */
void camera_set(const placement_t &placement);

/**
 * @brief Gives the camera back to the game and restores the previous view.
 */
void camera_release(void);

} // namespace game
