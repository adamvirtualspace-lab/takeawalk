/**
 * @brief Finding code in the game executable and reading game memory defensively.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace memory {

/**
 * @brief Finds a signature such as "48 8B 05 ?? ?? ?? ?? 41 FF CE" in the game's code.
 *
 * Returns NULL unless there is exactly one match, so a signature which became
 * ambiguous in a new game build is treated as missing.
 */
uint8_t *find_signature(const char *signature);

/**
 * @brief Address referenced by an instruction with a RIP-relative operand.
 *
 * @param instruction Start of the instruction.
 * @param operand_offset Offset of the 32-bit displacement inside the instruction.
 * @param instruction_size Length of the whole instruction.
 */
uint8_t *resolve_relative(uint8_t *instruction, size_t operand_offset, size_t instruction_size);

/**
 * @brief Copies memory which might not be mapped. False if it faulted.
 */
bool safe_copy(void *destination, const void *source, size_t size);

template <typename T>
bool read(const uint8_t *const address, T &value)
{
	return address && safe_copy(&value, address, sizeof(T));
}

template <typename T>
bool write(uint8_t *const address, const T &value)
{
	return address && safe_copy(address, &value, sizeof(T));
}

/**
 * @brief Writes a hex dump of game memory to the log.
 */
void dump(const char *label, const uint8_t *address, size_t size);

} // namespace memory
