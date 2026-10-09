#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "memory.h"

namespace memory {
namespace {

const size_t MAX_SIGNATURE_LENGTH = 128;

bool parse_signature(const char *text, uint8_t *const bytes, bool *const wildcard, size_t &length)
{
	length = 0;
	while (*text) {
		if (*text == ' ') {
			++text;
			continue;
		}
		if (length == MAX_SIGNATURE_LENGTH) {
			return false;
		}
		if (*text == '?') {
			wildcard[length] = true;
			bytes[length] = 0;
			while (*text == '?') {
				++text;
			}
		}
		else {
			unsigned value = 0;
			if (sscanf(text, "%2x", &value) != 1) {
				return false;
			}
			wildcard[length] = false;
			bytes[length] = static_cast<uint8_t>(value);
			text += 2;
		}
		++length;
	}
	return length > 0;
}

} // namespace

uint8_t *find_signature(const char *const signature)
{
	uint8_t bytes[MAX_SIGNATURE_LENGTH];
	bool wildcard[MAX_SIGNATURE_LENGTH];
	size_t length = 0;
	if (! parse_signature(signature, bytes, wildcard, length)) {
		return NULL;
	}

	uint8_t *const base = reinterpret_cast<uint8_t *>(GetModuleHandleW(NULL));
	const IMAGE_DOS_HEADER *const dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	const IMAGE_NT_HEADERS *const nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
	const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);

	uint8_t *found = NULL;
	for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
		if (! (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
			continue;
		}
		uint8_t *const begin = base + section->VirtualAddress;
		const size_t size = section->Misc.VirtualSize;
		if (size < length) {
			continue;
		}
		for (size_t offset = 0; offset <= size - length; ++offset) {
			size_t matched = 0;
			while ((matched < length) && (wildcard[matched] || (begin[offset + matched] == bytes[matched]))) {
				++matched;
			}
			if (matched != length) {
				continue;
			}
			if (found) {
				return NULL;
			}
			found = begin + offset;
		}
	}
	return found;
}

uint8_t *resolve_relative(uint8_t *const instruction, const size_t operand_offset, const size_t instruction_size)
{
	int32_t displacement = 0;
	memcpy(&displacement, instruction + operand_offset, sizeof(displacement));
	return instruction + instruction_size + displacement;
}

bool safe_copy(void *const destination, const void *const source, const size_t size)
{
	__try {
		memcpy(destination, source, size);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

namespace {

// jmp [rip + 0] followed by the address to jump to.
const uint8_t JUMP_ABSOLUTE[] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
const size_t JUMP_SIZE = sizeof(JUMP_ABSOLUTE) + sizeof(void *);

const uint8_t OPCODE_NOP = 0x90;

void write_jump(uint8_t *const at, const void *const target)
{
	memcpy(at, JUMP_ABSOLUTE, sizeof(JUMP_ABSOLUTE));
	memcpy(at + sizeof(JUMP_ABSOLUTE), &target, sizeof(target));
}

bool overwrite_code(uint8_t *const at, const uint8_t *const bytes, const size_t size)
{
	DWORD protection = 0;
	if (! VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &protection)) {
		return false;
	}
	memcpy(at, bytes, size);
	VirtualProtect(at, size, protection, &protection);
	FlushInstructionCache(GetCurrentProcess(), at, size);
	return true;
}

} // namespace

bool hook_install(hook_t &hook, uint8_t *const function, const size_t moved_size, const void *const replacement)
{
	if (hook.function || ! function || (moved_size < JUMP_SIZE) || (moved_size > sizeof(hook.saved))) {
		return false;
	}

	uint8_t *const moved = static_cast<uint8_t *>(VirtualAlloc(NULL, moved_size + JUMP_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (! moved) {
		return false;
	}
	memcpy(hook.saved, function, moved_size);
	memcpy(moved, function, moved_size);
	write_jump(moved + moved_size, function + moved_size);

	uint8_t patch[sizeof(hook.saved)];
	memset(patch, OPCODE_NOP, sizeof(patch));
	write_jump(patch, replacement);
	if (! overwrite_code(function, patch, moved_size)) {
		return false;
	}
	hook.function = function;
	hook.moved_size = moved_size;
	hook.original = moved;
	return true;
}

void hook_remove(hook_t &hook)
{
	if (! hook.function) {
		return;
	}
	overwrite_code(hook.function, hook.saved, hook.moved_size);
	hook.function = NULL;
}

void dump(const char *const label, const uint8_t *const address, const size_t size)
{
	log_message(SCS_LOG_TYPE_message, "dump %s at %p", label, address);
	for (size_t offset = 0; offset < size; offset += 32) {
		uint8_t row[32];
		if (! safe_copy(row, address + offset, sizeof(row))) {
			log_message(SCS_LOG_TYPE_message, "  %03zX: unreadable", offset);
			return;
		}
		char text[32 * 3 + 1];
		for (size_t i = 0; i < sizeof(row); ++i) {
			snprintf(text + i * 3, 4, "%02X ", row[i]);
		}
		log_message(SCS_LOG_TYPE_message, "  %03zX: %s", offset, text);
	}
}

} // namespace memory
