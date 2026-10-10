#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <intrin.h>
#include <string.h>

#include "game_camera.h"
#include "game_driver.h"
#include "log.h"
#include "memory.h"

namespace game {
namespace {

// Where the per-frame update of the player's vehicle updates its driver:
//   lea rcx, [rsi + driver]; call update_driver; mov rcx, [rsi+18h]; test rcx, rcx; jz ...
// The same function is called for other vehicles from elsewhere; the address this call
// returns to tells the player's driver apart.
const char *const PLAYER_DRIVER_UPDATE_SIGNATURE =
	"48 8D 8E ?? ?? 00 00 E8 ?? ?? ?? ?? 48 8B 4E 18 48 85 C9 0F 84 ?? ?? ?? ?? 44 39 B6"
;
const size_t UPDATE_CALL = 7;
const size_t UPDATE_RETURN = 12;

// Start of update_driver(driver): four instructions (14 bytes) which can be moved.
const uint8_t UPDATE_DRIVER_START[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x83, 0x79, 0x08, 0x00, 0x48, 0x8B, 0xD9 };

// set_local_placement(node, placement): moves a node of the scene relative to its parent.
const char *const SET_PLACEMENT_SIGNATURE =
	"48 83 EC 28 48 8B 41 58 4C 8B C1 C7 41 68 00 00 00 00 48 85 C0 74 11 48 8B C8 E8 ?? ?? ?? ?? "
	"48 8B 40 50 48 85 C0 75 EF 0F 10 02 41 0F 11 40 30"
;

// Driver of a vehicle
const size_t DRIVER_NODE = 0x08;	// node *, the figure; NULL if the vehicle shows none

// Scene node
const size_t NODE_PLACEMENT = 0x30;	// placement_t relative to the parent, here the seat

// The figure is not switched off but put this far below the seat (m), where nothing shows it.
const float HIDING_DEPTH = 10000.0f;

typedef void (*update_driver_t)(void *driver);
typedef void (*set_placement_t)(void *node, const placement_t *placement);

memory::hook_t  update_hook = {};
const void     *player_return_address = NULL;
set_placement_t set_placement = NULL;

std::atomic<bool> hidden_wanted(false);

// Only used by the thread which updates the player's vehicle.
const uint8_t *hidden_node = NULL;
placement_t    seat_placement;

/**
 * @brief Moves the player's driver out of sight or back to the seat, as wanted.
 */
void apply(const uint8_t *const driver)
{
	const uint8_t *node = NULL;
	if (! memory::read(driver + DRIVER_NODE, node) || ! node) {
		hidden_node = NULL;
		return;
	}
	if (node != hidden_node) {

		// Another figure than the one which was hidden (the truck was changed): it sits where it should.

		hidden_node = NULL;
	}

	const bool wanted = hidden_wanted;
	if (wanted) {
		placement_t current;
		if (! memory::read(node + NODE_PLACEMENT, current)) {
			return;
		}

		// The game may put the figure back on the seat at any time, so this is checked every frame.

		if (current.position[1] > -HIDING_DEPTH * 0.5f) {
			seat_placement = current;
			current.position[1] -= HIDING_DEPTH;
			set_placement(const_cast<uint8_t *>(node), &current);
			if (hidden_node != node) {
				log_message(SCS_LOG_TYPE_message, "the driver's figure is taken out of the cab");
			}
			hidden_node = node;
		}
	}
	else if (hidden_node) {
		set_placement(const_cast<uint8_t *>(node), &seat_placement);
		hidden_node = NULL;
		log_message(SCS_LOG_TYPE_message, "the driver's figure is back on the seat, %.2f m from where it is attached", seat_placement.position[1]);
	}
}

void update_driver(void *const driver)
{
	reinterpret_cast<update_driver_t>(update_hook.original)(driver);
	if (_ReturnAddress() == player_return_address) {
		apply(static_cast<const uint8_t *>(driver));
	}
}

} // namespace

bool driver_attach(void)
{
	if (update_hook.function) {
		return true;
	}
	uint8_t *const site = memory::find_signature(PLAYER_DRIVER_UPDATE_SIGNATURE);
	set_placement = reinterpret_cast<set_placement_t>(memory::find_signature(SET_PLACEMENT_SIGNATURE));
	uint8_t *const function = site ? memory::resolve_relative(site + UPDATE_CALL, 1, 5) : NULL;
	uint8_t start[sizeof(UPDATE_DRIVER_START)];
	const bool found =
		function && set_placement &&
		memory::safe_copy(start, function, sizeof(start)) &&
		(memcmp(start, UPDATE_DRIVER_START, sizeof(start)) == 0)
	;
	if (found) {
		player_return_address = site + UPDATE_RETURN;
	}
	if (! found || ! memory::hook_install(update_hook, function, sizeof(UPDATE_DRIVER_START), reinterpret_cast<const void *>(&update_driver))) {
		log_message(SCS_LOG_TYPE_warning, "unable to hook the driver's update, the driver will stay visible in the cab");
		return false;
	}
	return true;
}

void driver_detach(void)
{
	hidden_wanted = false;
	memory::hook_remove(update_hook);
}

void driver_set_hidden(const bool hidden)
{
	hidden_wanted = hidden;
}

} // namespace game
