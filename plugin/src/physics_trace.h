/**
 * @brief Diagnostics: records when and from where the game adds static collision to
 * the physics scene, and how much of it there is around the truck.
 *
 * Temporary. Only works on game 1.61.1.1, whose addresses it checks before use.
 */

#pragma once

namespace game {

void trace_start(void);
void trace_stop(void);

/**
 * @brief Writes what was recorded since the last call to the log. Call every frame.
 */
void trace_flush(double truck_x, double truck_z);

/**
 * @brief Logs how many static actors the scene holds and how far they are from the truck and the walker.
 */
void trace_census(const char *label, double truck_x, double truck_z, double walker_x, double walker_z);

} // namespace game
