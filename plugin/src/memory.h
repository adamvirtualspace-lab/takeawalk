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
 * @brief A game function redirected to one of the plugin's.
 */
struct hook_t
{
	uint8_t *function;
	uint8_t  saved[32];
	size_t   moved_size;

	/**
	 * @brief Calls the game's function: its first instructions, moved here, then a jump back into it.
	 */
	void    *original;
};

/**
 * @brief Makes a game function jump to a replacement.
 *
 * @param moved_size Length of the whole instructions at the start of the function which
 * get overwritten: at least 14 bytes, at most 32, and none of them may depend on where
 * it runs (no relative jumps, calls or RIP-relative operands).
 */
bool hook_install(hook_t &hook, uint8_t *function, size_t moved_size, const void *replacement);

/**
 * @brief Puts the function's first instructions back. The moved copy is kept, as a
 * thread may still be running it.
 */
void hook_remove(hook_t &hook);

/**
 * @brief Writes a hex dump of game memory to the log.
 */
void dump(const char *label, const uint8_t *address, size_t size);

} // namespace memory
