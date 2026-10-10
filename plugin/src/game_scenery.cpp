#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <math.h>

#include "game_camera.h"
#include "game_scenery.h"
#include "log.h"
#include "memory.h"

namespace game {
namespace {

// Start of the function of the world object which gives the position of the player:
//   player_position_t *player_position(world, player_position_t *result)
// A twin which gives the orientation starts with the same bytes, so the signature runs
// up to the call which tells them apart. The first four instructions (16 bytes) can be moved.
const char *const PLAYER_POSITION_SIGNATURE =
	"40 53 48 83 EC 30 48 8B 89 ?? ?? 00 00 48 8B DA 48 85 C9 75 0E 88 0A 48 8B C2 88 4A 04 "
	"48 83 C4 30 5B C3 48 8B 49 18 48 89 7C 24 40 33 FF 48 85 C9 74 10 48 8B 01 48 8D 54 24 20 "
	"FF 90 00 01 00 00"
;
const size_t PLAYER_POSITION_MOVED = 16;

// Start of the function which the game calls every frame for a sign it is showing:
//   void update(sign)
// It creates the sign's physics body within 35 m of the player position and removes it beyond.
// The first five instructions (15 bytes) can be moved.
const char *const SIGN_UPDATE_SIGNATURE =
	"48 89 5C 24 08 57 48 83 EC 70 8B 51 38 33 FF 8B C2 44 8B C2 C1 E8 08 48 8B D9 41 C1 E8 10 "
	"F6 D0 24 01 41 80 E0 01 8B CA 41 3A C0 0F 84"
;
const size_t SIGN_UPDATE_MOVED = 15;

// The same for a compound, a group of signs and models stored as one map item.
// The first four instructions (16 bytes) can be moved.
const char *const COMPOUND_UPDATE_SIGNATURE =
	"48 89 7C 24 18 4C 89 74 24 20 55 48 8D 6C 24 A9 48 81 EC A0 00 00 00 45 33 F6 48 8B F9 "
	"4C 39 B1 ?? ?? 00 00 74 ?? 8B 41 38"
;
const size_t COMPOUND_UPDATE_MOVED = 16;

// Sign
const size_t SIGN_POSITION = 0x7C;	// float position[3], int16 chunk_x, int16 chunk_z

const size_t SIGN_FLAGS = 0x38;		// u32
const size_t SIGN_COLLISION = 0xA8;	// collision object *, NULL while the sign is not solid

// Sign flags: bit 18 has to be set and bits 16 (knocked over) and 22 clear for the game to make it solid.
const uint32_t SIGN_CAN_BE_SOLID = 0x40000;
const uint32_t SIGN_NEVER_SOLID = 0x410000;

// Map item
const size_t ITEM_UPDATE_SLOT = 0x190;	// in its vtable: the function called every frame

// World object: the items which get that call every frame.
const size_t WORLD_UPDATED_ITEMS = 0x658;	// item **
const size_t WORLD_UPDATED_COUNT = 0x660;	// u64

// More items than this in the list means it was misread.
const uint64_t MAX_UPDATED_ITEMS = 100000;

// The game's own limit (m).
const double TRUCK_RADIUS = 35.0;

// Compound
const size_t COMPOUND_NODE = 0x50;	// node *, which starts with its position as int32[3] in 1/256 m

const double FIXED_POINT_UNIT = 1.0 / 256.0;

#pragma pack(push, 1)

struct position_t
{
	float   position[3];
	int16_t chunk_x;
	int16_t chunk_z;
};

struct player_position_t
{
	uint8_t    known;
	uint8_t    unused[3];
	position_t position;
};

struct fixed_position_t
{
	int32_t position[3];
};

#pragma pack(pop)

typedef player_position_t *(*player_position_function_t)(void *world, player_position_t *result);
typedef void (*update_t)(void *item);

memory::hook_t player_position_hook = {};
memory::hook_t sign_update_hook = {};
memory::hook_t compound_update_hook = {};

std::atomic<bool>   walker_active(false);
std::atomic<double> walker_x(0.0);
std::atomic<double> walker_y(0.0);
std::atomic<double> walker_z(0.0);
std::atomic<double> walker_radius(0.0);

// Thread which is updating something near the walker and so gets the walker's position
// when it asks for the player's. Everybody else who asks still gets the truck.
std::atomic<DWORD> walker_thread(0);

bool near_walker(const double x, const double y, const double z)
{
	const double dx = x - walker_x;
	const double dy = y - walker_y;
	const double dz = z - walker_z;
	const double radius = walker_radius;
	return dx * dx + dy * dy + dz * dz < radius * radius;
}

player_position_t *player_or_walker_position(void *const world, player_position_t *const result)
{
	if (walker_thread != GetCurrentThreadId()) {
		return reinterpret_cast<player_position_function_t>(player_position_hook.original)(world, result);
	}
	placement_t placement;
	set_world_position(placement, walker_x, walker_y, walker_z);
	result->known = 1;
	result->position.position[0] = placement.position[0];
	result->position.position[1] = placement.position[1];
	result->position.position[2] = placement.position[2];
	result->position.chunk_x = placement.chunk_x;
	result->position.chunk_z = placement.chunk_z;
	return result;
}

/**
 * @brief Runs the game's update, with the walker as the player when the thing is near the walker.
 *
 * Things near the truck but not near the walker are judged by the truck as before, so
 * nothing around the parked truck changes.
 */
void update(const memory::hook_t &hook, void *const item, const bool by_walker)
{
	if (! by_walker) {
		reinterpret_cast<update_t>(hook.original)(item);
		return;
	}
	const DWORD previous = walker_thread.exchange(GetCurrentThreadId());
	reinterpret_cast<update_t>(hook.original)(item);
	walker_thread = previous;
}

void update_sign(void *const item)
{
	position_t position;
	const bool by_walker =
		walker_active &&
		memory::read(static_cast<const uint8_t *>(item) + SIGN_POSITION, position) &&
		near_walker(position.chunk_x * CHUNK_SIZE + position.position[0], position.position[1], position.chunk_z * CHUNK_SIZE + position.position[2])
	;
	update(sign_update_hook, item, by_walker);
}

void update_compound(void *const item)
{
	// Chunk and offset are packed into one integer there, which read as a whole is
	// simply the world coordinate.

	const uint8_t *node = NULL;
	fixed_position_t position;
	const bool by_walker =
		walker_active &&
		memory::read(static_cast<const uint8_t *>(item) + COMPOUND_NODE, node) &&
		memory::read(node, position) &&
		near_walker(position.position[0] * FIXED_POINT_UNIT, position.position[1] * FIXED_POINT_UNIT, position.position[2] * FIXED_POINT_UNIT)
	;
	update(compound_update_hook, item, by_walker);
}

bool install(memory::hook_t &hook, const char *const signature, const size_t moved_size, const void *const replacement)
{
	uint8_t *const function = memory::find_signature(signature);
	return function && memory::hook_install(hook, function, moved_size, replacement);
}

} // namespace

bool scenery_attach(void)
{
	if (player_position_hook.function) {
		return true;
	}

	// Without the position hook the other two would change nothing, so it goes first.

	if (! install(player_position_hook, PLAYER_POSITION_SIGNATURE, PLAYER_POSITION_MOVED, reinterpret_cast<const void *>(&player_or_walker_position))) {
		log_message(SCS_LOG_TYPE_warning, "unable to hook the player position, signs and poles will only be solid near the truck");
		return false;
	}
	const bool signs = install(sign_update_hook, SIGN_UPDATE_SIGNATURE, SIGN_UPDATE_MOVED, reinterpret_cast<const void *>(&update_sign));
	const bool compounds = install(compound_update_hook, COMPOUND_UPDATE_SIGNATURE, COMPOUND_UPDATE_MOVED, reinterpret_cast<const void *>(&update_compound));
	if (! signs || ! compounds) {
		log_message(SCS_LOG_TYPE_warning, "unable to hook the update of %s, those will only be solid near the truck", signs ? "compounds" : (compounds ? "signs" : "signs and compounds"));
	}
	return signs && compounds;
}

void scenery_detach(void)
{
	walker_active = false;
	memory::hook_remove(sign_update_hook);
	memory::hook_remove(compound_update_hook);
	memory::hook_remove(player_position_hook);
}

bool scenery_census(void *const world, const double *const truck, const double *const walker, scenery_census_t &census)
{
	census = scenery_census_t();
	const uint8_t *const *items = NULL;
	uint64_t count = 0;
	if (
		! world || ! sign_update_hook.function ||
		! memory::read(static_cast<const uint8_t *>(world) + WORLD_UPDATED_ITEMS, items) ||
		! memory::read(static_cast<const uint8_t *>(world) + WORLD_UPDATED_COUNT, count) ||
		(count > MAX_UPDATED_ITEMS)
	) {
		return false;
	}
	const double radius = walker_radius;
	for (uint64_t i = 0; i < count; ++i) {
		const uint8_t *item = NULL;
		const uint8_t *vtable = NULL;
		const uint8_t *update_function = NULL;
		position_t position;
		uint32_t flags = 0;
		const uint8_t *collision = NULL;
		if (
			! memory::read(reinterpret_cast<const uint8_t *>(items + i), item) ||
			! memory::read(item, vtable) ||
			! memory::read(vtable + ITEM_UPDATE_SLOT, update_function) ||
			(update_function != sign_update_hook.function) ||
			! memory::read(item + SIGN_POSITION, position) ||
			! memory::read(item + SIGN_FLAGS, flags) ||
			! memory::read(item + SIGN_COLLISION, collision)
		) {
			continue;
		}
		++census.shown;
		const double x = position.chunk_x * CHUNK_SIZE + position.position[0];
		const double y = position.position[1];
		const double z = position.chunk_z * CHUNK_SIZE + position.position[2];
		const double from_truck = sqrt((x - truck[0]) * (x - truck[0]) + (y - truck[1]) * (y - truck[1]) + (z - truck[2]) * (z - truck[2]));
		const bool solid = collision != NULL;
		const bool by_walker =
			walker &&
			((x - walker[0]) * (x - walker[0]) + (y - walker[1]) * (y - walker[1]) + (z - walker[2]) * (z - walker[2]) < radius * radius)
		;
		if (by_walker) {
			++census.near_walker;
			census.near_walker_solid += solid;
		}
		else if (from_truck < TRUCK_RADIUS) {
			++census.near_truck;
			census.near_truck_solid += solid;
		}
		else {
			++census.elsewhere;
			census.elsewhere_solid += solid;
		}

		const bool can_be_solid = (flags & SIGN_CAN_BE_SOLID) && ! (flags & SIGN_NEVER_SOLID);
		if (can_be_solid && (from_truck > TRUCK_RADIUS + radius) && (! census.far_sign_found || (from_truck < census.far_sign_from_truck))) {
			census.far_sign_found = true;
			census.far_sign[0] = x;
			census.far_sign[1] = y;
			census.far_sign[2] = z;
			census.far_sign_from_truck = from_truck;
			census.far_sign_solid = solid;
		}
	}
	return true;
}

void scenery_set_walker(const double x, const double y, const double z, const double radius)
{
	walker_x = x;
	walker_y = y;
	walker_z = z;
	walker_radius = radius;
	walker_active = true;
}

void scenery_clear_walker(void)
{
	walker_active = false;
}

} // namespace game
