/**
 * @brief The game's console: running commands and reading console variables.
 *
 * Nothing here is part of the SCS SDK; see docs/RESEARCH.md.
 */

#pragma once

#include <stddef.h>

namespace game {

/**
 * @brief Finds the console code in the running game. False if the game build is not supported.
 */
bool console_attach(void);

/**
 * @brief Runs a console command, such as "g_show_tutorial_hints 0".
 */
bool console_run(const char *command);

/**
 * @brief Reads the current value of a console variable as text.
 */
bool console_variable(const char *name, char *value, size_t capacity);

} // namespace game
