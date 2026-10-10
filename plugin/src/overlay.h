/**
 * @brief Short messages and a reminder of keys shown on top of the game.
 *
 * Drawn in a separate transparent window over the game's, so it needs the game to run
 * windowed or borderless (or with Windows' full-screen optimisations).
 */

#pragma once

namespace overlay {

bool init(void);
void shutdown(void);

/**
 * @brief Shows a line of text near the bottom of the game window for a few seconds.
 *
 * Replaces the message currently shown, if any.
 */
void show(const char *text);

/**
 * @brief Removes the message at once.
 */
void hide(void);

/**
 * @brief Shows a reminder of keys in the bottom left corner until it is changed or taken away.
 *
 * @param text Lines separated by line feeds, each "[key] what it does". A line starting
 * with '~' is shown faded, for a key which does nothing at the moment. NULL or empty
 * takes the reminder away.
 */
void set_hint(const char *text);

} // namespace overlay
