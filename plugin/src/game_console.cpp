#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

#include "game_console.h"
#include "log.h"
#include "memory.h"

namespace game {
namespace {

// Start of the function the game itself uses to run a command line:
//   push rbx; sub rsp, 0C40h; mov rbx, rcx; cmp edx, -1; jne ...; mov rdx, [rcx]; xor eax, eax; mov r8b, 20h
const char *const RUN_SIGNATURE = "40 53 48 81 EC 40 0C 00 00 48 8B D9 83 FA FF 0F 85 ?? ?? ?? ?? 48 8B 11 33 C0 41 B0 20";

// Runs the command at once instead of adding it to a named queue.
const int QUEUE_NONE = -1;

// A console variable is a global structure. Offsets are from its start; the name is
// what it is found by. The value is text, either the default or what was set, and a
// variable can be replaced by another one (a profile's setting over the global one).
const size_t VARIABLE_NAME = 0x08;
const size_t VARIABLE_NAME_SIZE = 0x20;
const size_t VARIABLE_DEFAULT_VALUE = 0x28;
const size_t VARIABLE_USES_DEFAULT = 0xA1;	// bool
const size_t VARIABLE_VALUE = 0xB1;
const size_t VARIABLE_VALUE_SIZE = 0x65;
const size_t VARIABLE_REPLACEMENT = 0x120;	// variable *
const size_t VARIABLE_SIZE = 0x140;

// Longest chain of replacements followed.
const int MAX_REPLACEMENTS = 8;

/**
 * @param text Address of a pointer to the command line.
 */
typedef bool (*run_function_t)(const char *const *text, int queue);

run_function_t run_function = NULL;

bool guarded_run(const char *const command)
{
	__try {
		run_function(&command, QUEUE_NONE);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

/**
 * @brief Finds the structure of a console variable in the game's writable data.
 */
const uint8_t *find_variable(const char *const name)
{
	const size_t length = strlen(name);
	if ((length == 0) || (length >= VARIABLE_NAME_SIZE)) {
		return NULL;
	}

	const uint8_t *const base = reinterpret_cast<const uint8_t *>(GetModuleHandleW(NULL));
	const IMAGE_DOS_HEADER *const dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	const IMAGE_NT_HEADERS *const nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
	const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);

	for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
		if (! (section->Characteristics & IMAGE_SCN_MEM_WRITE)) {
			continue;
		}
		const uint8_t *const begin = base + section->VirtualAddress;
		const size_t size = section->Misc.VirtualSize;
		if (size < VARIABLE_SIZE) {
			continue;
		}

		// The structures are aligned, which also skips other text ending in the same name.

		const size_t last = size - VARIABLE_SIZE;
		for (size_t offset = 0; offset <= last; offset += 8) {
			const uint8_t *const candidate = begin + offset;
			const uint8_t *const candidate_name = candidate + VARIABLE_NAME;
			if ((candidate_name[0] != static_cast<uint8_t>(name[0])) || (memcmp(candidate_name, name, length + 1) != 0)) {
				continue;
			}
			return candidate;
		}
	}
	return NULL;
}

} // namespace

bool console_attach(void)
{
	run_function = reinterpret_cast<run_function_t>(memory::find_signature(RUN_SIGNATURE));
	if (! run_function) {
		log_message(SCS_LOG_TYPE_warning, "the game's console was not found, the HUD will stay visible while walking");
		return false;
	}
	return true;
}

bool console_run(const char *const command)
{
	if (! run_function) {
		return false;
	}
	if (! guarded_run(command)) {
		run_function = NULL;
		log_message(SCS_LOG_TYPE_error, "the game's console crashed on \"%s\", it will not be used again", command);
		return false;
	}
	return true;
}

bool console_variable(const char *const name, char *const value, const size_t capacity)
{
	const uint8_t *variable = find_variable(name);
	if (! variable || (capacity == 0)) {
		return false;
	}

	// The value in effect is the one of the last replacement.

	for (int i = 0; i < MAX_REPLACEMENTS; ++i) {
		const uint8_t *replacement = NULL;
		if (! memory::read(variable + VARIABLE_REPLACEMENT, replacement)) {
			return false;
		}
		if (! replacement) {
			break;
		}
		variable = replacement;
	}

	char text[VARIABLE_VALUE_SIZE + 1] = {};
	bool uses_default = false;
	const bool readable =
		memory::read(variable + VARIABLE_USES_DEFAULT, uses_default) &&
		memory::safe_copy(text, variable + (uses_default ? VARIABLE_DEFAULT_VALUE : VARIABLE_VALUE), VARIABLE_VALUE_SIZE)
	;
	const size_t length = strlen(text);
	if (! readable || (length >= capacity)) {
		return false;
	}
	memcpy(value, text, length + 1);
	return true;
}

} // namespace game
