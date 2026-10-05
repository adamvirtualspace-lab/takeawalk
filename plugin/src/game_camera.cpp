#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <math.h>

#include "game_camera.h"
#include "log.h"
#include "memory.h"

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

uint8_t *manager(void)
{
	uint8_t *result = NULL;
	if (! memory::read(reinterpret_cast<const uint8_t *>(manager_global), result)) {
		return NULL;
	}
	return result;
}

uint8_t *camera(uint8_t *const camera_manager, const uint32_t index)
{
	uint8_t **items = NULL;
	uint64_t count = 0;
	if (! memory::read(camera_manager + MANAGER_CAMERAS_ITEMS, items) || ! memory::read(camera_manager + MANAGER_CAMERAS_COUNT, count)) {
		return NULL;
	}
	if (! items || (count == 0) || (count > MAX_CAMERA_COUNT) || (index >= count)) {
		return NULL;
	}
	uint8_t *result = NULL;
	if (! memory::read(reinterpret_cast<const uint8_t *>(items + index), result)) {
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

void set_world_position(placement_t &placement, const double x, const double y, const double z)
{
	// Offsets are centred on the chunk: world = chunk * size + offset, offset in [-size/2, size/2).

	const double chunk_x = floor(x / CHUNK_SIZE + 0.5);
	const double chunk_z = floor(z / CHUNK_SIZE + 0.5);
	placement.chunk_x = static_cast<int16_t>(chunk_x);
	placement.chunk_z = static_cast<int16_t>(chunk_z);
	placement.position[0] = static_cast<float>(x - chunk_x * CHUNK_SIZE);
	placement.position[1] = static_cast<float>(y);
	placement.position[2] = static_cast<float>(z - chunk_z * CHUNK_SIZE);
}

bool camera_attach(void)
{
	manager_global = NULL;
	free_camera_tick = NULL;
	taken = false;
	layout_dumped = false;

	uint8_t *const manager_instruction = memory::find_signature(MANAGER_SIGNATURE);
	uint8_t *const tick = memory::find_signature(FREE_CAMERA_TICK_SIGNATURE);
	if (! manager_instruction || ! tick) {
		log_message(
			SCS_LOG_TYPE_error,
			"this game build is not supported (camera manager %s, free camera tick %s)",
			manager_instruction ? "found" : "missing",
			tick ? "found" : "missing"
		);
		return false;
	}

	manager_global = reinterpret_cast<uint8_t **>(memory::resolve_relative(manager_instruction, 3, 7));
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
	if (! camera_manager || ! memory::read(camera_manager + MANAGER_CURRENT_CAMERA, current)) {
		log_message(SCS_LOG_TYPE_error, "camera manager is not available");
		return false;
	}

	uint8_t *const current_camera = camera(camera_manager, current);
	uint8_t *const free_camera = camera(camera_manager, FREE_CAMERA_INDEX);

	if (! current_camera || ! free_camera || ! memory::read(current_camera + CAMERA_PLACEMENT, reference) || ! plausible(reference)) {
		log_message(SCS_LOG_TYPE_error, "camera layout does not look as expected, not touching it");

		// Enough of the raw memory to work out the new layout after a game update.

		if (! layout_dumped) {
			layout_dumped = true;
			log_message(SCS_LOG_TYPE_message, "current camera index %u", current);
			memory::dump("camera manager", camera_manager, 0xA0);
			if (current_camera) {
				memory::dump("current camera", current_camera, 0xC0);
			}
			if (free_camera) {
				memory::dump("free camera", free_camera, 0xC0);
			}
		}
		return false;
	}

	if (! set_tick_patched(true)) {
		log_message(SCS_LOG_TYPE_error, "unable to patch the free camera update");
		return false;
	}

	previous_camera = current;
	memory::write(camera_manager + MANAGER_TARGET_CAMERA, FREE_CAMERA_INDEX);
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
		memory::write(free_camera + CAMERA_PLACEMENT, placement);
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
		memory::write(camera_manager + MANAGER_TARGET_CAMERA, previous_camera);
	}
}

} // namespace game
