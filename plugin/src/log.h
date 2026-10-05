/**
 * @brief Logging to the game console and game.log.txt.
 */

#pragma once

#include "scssdk.h"

void log_message(const scs_log_type_t type, const char *const format, ...);
