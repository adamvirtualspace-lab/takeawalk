#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>

#include "game_camera.h"
#include "game_traffic.h"
#include "log.h"
#include "memory.h"

namespace game {
namespace {

// Start of the function which tells whether a traffic vehicle (or anything it tows) is
// within a distance of one of the player's vehicles:
//   bool is_near_player(manager, vehicle, float distance_squared)
// The first three instructions (15 bytes) save registers on the stack and can be moved.
const char *const NEAR_PLAYER_SIGNATURE =
	"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 40 0F 29 74 24 30 0F 28 F2 "
	"48 8B F2 48 8B D9 48 85 D2 0F 84 ?? ?? ?? ?? F3 0F 10 2D"
;
const size_t MOVED_SIZE = 15;

// Traffic vehicle
const size_t VEHICLE_PLACEMENT = 0x28;	// float position[3], int16 chunk_x, int16 chunk_z
const size_t VEHICLE_TOWED = 0x68;	// vehicle *, the next one in the chain

// Longest chain of towed vehicles followed.
const int MAX_CHAIN = 8;

#pragma pack(push, 1)

struct vehicle_placement_t
{
	float   position[3];
	int16_t chunk_x;
	int16_t chunk_z;
};

#pragma pack(pop)

typedef bool (*near_player_t)(void *manager, void *vehicle, float distance_squared);

memory::hook_t near_player_hook = {};

// Read from whichever thread updates traffic.
std::atomic<bool>   walker_active(false);
std::atomic<double> walker_x(0.0);
std::atomic<double> walker_y(0.0);
std::atomic<double> walker_z(0.0);

/**
 * @brief Whether the vehicle or anything it tows is within the distance of the walker.
 */
bool near_walker(const uint8_t *vehicle, const float distance_squared)
{
	const double x = walker_x;
	const double y = walker_y;
	const double z = walker_z;
	for (int i = 0; vehicle && (i < MAX_CHAIN); ++i) {
		vehicle_placement_t placement;
		if (! memory::read(vehicle + VEHICLE_PLACEMENT, placement)) {
			return false;
		}
		const double dx = placement.chunk_x * CHUNK_SIZE + placement.position[0] - x;
		const double dy = placement.position[1] - y;
		const double dz = placement.chunk_z * CHUNK_SIZE + placement.position[2] - z;
		if (dx * dx + dy * dy + dz * dz < distance_squared) {
			return true;
		}
		if (! memory::read(vehicle + VEHICLE_TOWED, vehicle)) {
			return false;
		}
	}
	return false;
}

bool near_player_or_walker(void *const manager, void *const vehicle, const float distance_squared)
{
	if (reinterpret_cast<near_player_t>(near_player_hook.original)(manager, vehicle, distance_squared)) {
		return true;
	}
	return walker_active && near_walker(static_cast<const uint8_t *>(vehicle), distance_squared);
}

} // namespace

bool traffic_attach(void)
{
	if (near_player_hook.function) {
		return true;
	}
	uint8_t *const function = memory::find_signature(NEAR_PLAYER_SIGNATURE);
	if (! function || ! memory::hook_install(near_player_hook, function, MOVED_SIZE, reinterpret_cast<const void *>(&near_player_or_walker))) {
		log_message(SCS_LOG_TYPE_warning, "unable to hook the traffic proximity test, parked cars will only be solid near the truck");
		return false;
	}
	return true;
}

void traffic_detach(void)
{
	walker_active = false;
	memory::hook_remove(near_player_hook);
}

void traffic_set_walker(const double x, const double y, const double z)
{
	walker_x = x;
	walker_y = y;
	walker_z = z;
	walker_active = true;
}

void traffic_clear_walker(void)
{
	walker_active = false;
}

} // namespace game
