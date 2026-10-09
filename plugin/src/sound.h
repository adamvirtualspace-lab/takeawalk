/**
 * @brief Sounds of the walker. They are generated when the plugin starts, so no audio files are needed.
 */

#pragma once

namespace sound {

void init(void);
void shutdown(void);

/**
 * @brief Plays one footstep, a slightly different one each time.
 */
void footstep(void);

/**
 * @brief Plays the heavier thud of landing after a jump or a fall.
 */
void landing(void);

} // namespace sound
