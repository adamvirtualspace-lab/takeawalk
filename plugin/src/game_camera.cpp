#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game_camera.h"
#include "log.h"

namespace game {
namespace {

// mov rax, [rip + camera_manager]; dec r14d
const char *const MANAGER_SIGNATURE = "48 8B 05 ?? ?? ?? ?? 41 FF CE";

// Start of the function which updates the free camera from the player's input.
const char *const FREE_CAMERA_TICK_SIGNATURE = "40 53 48 83 EC ?? 48 83 B9 ?? ?? ?? ?? 00 48 8B D9 0F 29 74 24";

// camera_manager_u
const size_t MANAGER_CURRENT_CAMERA = 0x10;	// u32
const size_t MANAGER_TARGET_CAMERA  = 0x14;	// u32
const size_t MANAGER_CAMERAS_ITEMS  = 0x38;	// core_camera_u **
const size_t MANAGER_CAMERAS_COUNT  = 0x40;	// u64

// core_camera_u
const size_t CAMERA_PLACEMENT = 0x40;	// placement_t

const uint32_t FREE_CAMERA_INDEX = 0;
const uint64_t MAX_CAMERA_COUNT = 64;

const uint8_t OPCODE_RET = 0xC3;

uint8_t **manager_global = NULL;
uint8_t  *free_camera_tick = NULL;
uint8_t   free_camera_tick_original = 0;

bool     taken = false;
uint32_t previous_camera = 0;
bool     layout_dumped = false;

/**
 * @brief Copies memory which might not be mapped. False if it faulted.
 */
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

bool parse_signature(const char *text, uint8_t *const bytes, bool *const wildcard, size_t &length)
{
	length = 0;
	while (*text) {
		if (*text == ' ') {
			++text;
			continue;
		}
		if (length == 64) {
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

/**
 * @brief Finds a signature in the code section of the game executable.
 *
 * Returns NULL unless there is exactly one match, so a signature which became
 * ambiguous in a new game build is treated as missing.
 */
uint8_t *find_signature(const char *const signature)
{
	uint8_t bytes[64];
	bool wildcard[64];
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

uint8_t *manager(void)
{
	uint8_t *result = NULL;
	if (! read(reinterpret_cast<const uint8_t *>(manager_global), result)) {
		return NULL;
	}
	return result;
}

uint8_t *camera(uint8_t *const camera_manager, const uint32_t index)
{
	uint8_t **items = NULL;
	uint64_t count = 0;
	if (! read(camera_manager + MANAGER_CAMERAS_ITEMS, items) || ! read(camera_manager + MANAGER_CAMERAS_COUNT, count)) {
		return NULL;
	}
	if (! items || (count == 0) || (count > MAX_CAMERA_COUNT) || (index >= count)) {
		return NULL;
	}
	uint8_t *result = NULL;
	if (! read(reinterpret_cast<const uint8_t *>(items + index), result)) {
		return NULL;
	}
	return result;
}

bool plausible(const placement_t &placement)
{
	for (int i = 0; i < 3; ++i) {
		if (! isfinite(placement.position[i]) || (fabsf(placement.position[i]) > 1.0e6f)) {
			return false;
		}
	}
	float norm = 0.0f;
	for (int i = 0; i < 4; ++i) {
		if (! isfinite(placement.rotation[i])) {
			return false;
		}
		norm += placement.rotation[i] * placement.rotation[i];
	}
	return (norm > 0.96f) && (norm < 1.04f);
}

bool set_tick_patched(const bool patched)
{
	DWORD protection = 0;
	if (! VirtualProtect(free_camera_tick, 1, PAGE_EXECUTE_READWRITE, &protection)) {
		return false;
	}
	*free_camera_tick = patched ? OPCODE_RET : free_camera_tick_original;
	VirtualProtect(free_camera_tick, 1, protection, &protection);
	FlushInstructionCache(GetCurrentProcess(), free_camera_tick, 1);
	return true;
}

} // namespace

bool camera_attach(void)
{
	manager_global = NULL;
	free_camera_tick = NULL;
	taken = false;
	layout_dumped = false;

	uint8_t *const manager_instruction = find_signature(MANAGER_SIGNATURE);
	uint8_t *const tick = find_signature(FREE_CAMERA_TICK_SIGNATURE);
	if (! manager_instruction || ! tick) {
		log_message(
			SCS_LOG_TYPE_error,
			"this game build is not supported (camera manager %s, free camera tick %s)",
			manager_instruction ? "found" : "missing",
			tick ? "found" : "missing"
		);
		return false;
	}

	// The instruction addresses its operand relative to the next instruction.

	int32_t displacement = 0;
	memcpy(&displacement, manager_instruction + 3, sizeof(displacement));
	manager_global = reinterpret_cast<uint8_t **>(manager_instruction + 7 + displacement);

	free_camera_tick = tick;
	free_camera_tick_original = *tick;
	return true;
}

bool camera_take(placement_t &reference)
{
	if (! manager_global || taken) {
		return false;
	}

	uint8_t *const camera_manager = manager();
	uint32_t current = 0;
	if (! camera_manager || ! read(camera_manager + MANAGER_CURRENT_CAMERA, current)) {
		log_message(SCS_LOG_TYPE_error, "camera manager is not available");
		return false;
	}

	uint8_t *const current_camera = camera(camera_manager, current);
	uint8_t *const free_camera = camera(camera_manager, FREE_CAMERA_INDEX);

	if (! layout_dumped) {
		layout_dumped = true;
		log_message(SCS_LOG_TYPE_message, "current camera index %u", current);
		dump("camera manager", camera_manager, 0xA0);
		if (current_camera) {
			dump("current camera", current_camera, 0xC0);
		}
		if (free_camera) {
			dump("free camera", free_camera, 0xC0);
		}
	}

	if (! current_camera || ! free_camera || ! read(current_camera + CAMERA_PLACEMENT, reference) || ! plausible(reference)) {
		log_message(SCS_LOG_TYPE_error, "camera layout does not look as expected, not touching it");
		return false;
	}

	if (! set_tick_patched(true)) {
		log_message(SCS_LOG_TYPE_error, "unable to patch the free camera update");
		return false;
	}

	previous_camera = current;
	write(camera_manager + MANAGER_TARGET_CAMERA, FREE_CAMERA_INDEX);
	taken = true;
	return true;
}

void camera_set(const placement_t &placement)
{
	if (! taken) {
		return;
	}
	uint8_t *const camera_manager = manager();
	if (! camera_manager) {
		return;
	}
	uint8_t *const free_camera = camera(camera_manager, FREE_CAMERA_INDEX);
	if (free_camera) {
		write(free_camera + CAMERA_PLACEMENT, placement);
	}
}

void camera_release(void)
{
	if (! taken) {
		return;
	}
	taken = false;
	set_tick_patched(false);

	uint8_t *const camera_manager = manager();
	if (camera_manager) {
		write(camera_manager + MANAGER_TARGET_CAMERA, previous_camera);
	}
}

} // namespace game
