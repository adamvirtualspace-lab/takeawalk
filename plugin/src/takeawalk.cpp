/**
 * Take a Walk - ETS2 plugin.
 *
 * F9 while parked steps out of the truck: the camera is placed next to the driver's
 * door and moved with WASD and the mouse. F9 again returns to the cab.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "scssdk_telemetry.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"

#include "game_camera.h"
#include "game_physics.h"
#include "input.h"
#include "log.h"

#define TAKEAWALK_VERSION "0.4"

namespace {

const int   TOGGLE_KEY = VK_F9;

// The truck counts as stopped below this speed (m/s).
const float STOPPED_SPEED = 0.1f;

const float TWO_PI = 6.2831853071795864769252867665590058f;

const float WALK_SPEED = 1.5f;			// m/s
const float RUN_SPEED = 4.0f;			// m/s
const float EYE_HEIGHT = 1.75f;			// m above the ground
const float MOUSE_SENSITIVITY = 0.0025f;	// rad per mouse count
const float MAX_PITCH = 1.48f;			// rad, just short of straight up

// How far outside the driver's head the walk starts (m). About a metre clear of the door.
const float EXIT_SIDE_STEP = 1.5f;

// Driver's head in vehicle space when the truck does not report it (m): left-hand drive.
const scs_value_fvector_t DEFAULT_HEAD_POSITION = { -0.7f, 2.0f, -1.5f };

// Highest ledge that can be walked onto, and how far down the ground is looked for (m).
const float STEP_HEIGHT = 0.6f;
const float GROUND_SEARCH_DEPTH = 4.0f;

// How fast the feet follow the ground up a step and down a drop (m/s).
const float RISE_SPEED = 4.0f;
const float FALL_SPEED = 8.0f;

// How often the physics world's origin is checked against the truck (s).
const float PHYSICS_LOCATE_INTERVAL = 2.0f;

// Longest simulated step (s), so a hitch does not turn into a jump.
const float MAX_FRAME_TIME = 0.1f;

struct telemetry_state_t
{
	// Configuration.

	/**
	 * @brief Default position of the driver's head in vehicle space.
	 */
	scs_value_fvector_t    head_position;

	// Channels.

	scs_value_dplacement_t truck_placement;
	float                  speed;
	bool                   parking_brake;
};

struct walk_state_t
{
	bool                active;
	double              position[3];	// World space, at the feet.
	float               yaw;		// rad, 0 = north, counter-clockwise from above
	float               pitch;		// rad, positive = up
	game::placement_t   placement;
	LARGE_INTEGER       last_update;
	float               time_since_locate;
};

scs_log_t         game_log = NULL;
telemetry_state_t telemetry;
walk_state_t      walk;
bool              camera_supported = false;
bool              physics_supported = false;
bool              game_paused = true;
bool              toggle_key_was_down = false;

bool can_leave_truck(void)
{
	return (fabsf(telemetry.speed) < STOPPED_SPEED) && telemetry.parking_brake;
}

/**
 * @brief Stores the walk's position and view direction in the camera placement.
 *
 * The rotation is yaw around Y followed by pitch around X.
 */
void update_placement(void)
{
	game::set_world_position(walk.placement, walk.position[0], walk.position[1] + EYE_HEIGHT, walk.position[2]);

	const float cos_yaw = cosf(walk.yaw * 0.5f);
	const float sin_yaw = sinf(walk.yaw * 0.5f);
	const float cos_pitch = cosf(walk.pitch * 0.5f);
	const float sin_pitch = sinf(walk.pitch * 0.5f);

	walk.placement.rotation[0] = cos_yaw * cos_pitch;
	walk.placement.rotation[1] = cos_yaw * sin_pitch;
	walk.placement.rotation[2] = sin_yaw * cos_pitch;
	walk.placement.rotation[3] = -sin_yaw * sin_pitch;
}

/**
 * @brief Finds where the physics world's origin currently is. Needed before the ground can be queried.
 */
void locate_physics(void)
{
	walk.time_since_locate = 0.0f;
	if (! physics_supported) {
		return;
	}
	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	game::physics_locate(truck.x, truck.y, truck.z, telemetry.truck_placement.orientation.heading * TWO_PI);
}

/**
 * @brief Moves the feet towards the ground below them. Without ground they stay at their height.
 *
 * @param frame_time Time to simulate (s). Zero puts the feet on the ground at once.
 */
void follow_ground(const float frame_time)
{
	float ground = 0.0f;
	if (! physics_supported || ! game::physics_ground_height(walk.position[0], walk.position[2], walk.position[1] + STEP_HEIGHT, STEP_HEIGHT + GROUND_SEARCH_DEPTH, ground)) {
		return;
	}

	const double difference = ground - walk.position[1];
	const double limit = ((difference > 0.0) ? RISE_SPEED : FALL_SPEED) * frame_time;
	if ((frame_time <= 0.0f) || (fabs(difference) <= limit)) {
		walk.position[1] = ground;
	}
	else {
		walk.position[1] += (difference > 0.0) ? limit : -limit;
	}
}

void start_walk(void)
{
	game::placement_t reference;
	if (! game::camera_take(reference)) {
		return;
	}

	// Step out sideways from the driver's seat, on whichever side the driver sits,
	// facing the way the truck faces. In vehicle space X is right and Z is backward.

	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	const float truck_yaw = telemetry.truck_placement.orientation.heading * TWO_PI;
	const float sin_yaw = sinf(truck_yaw);
	const float cos_yaw = cosf(truck_yaw);

	const float side = (telemetry.head_position.x > 0.0f) ? 1.0f : -1.0f;
	const float exit_x = telemetry.head_position.x + side * EXIT_SIDE_STEP;
	const float exit_z = telemetry.head_position.z;

	walk.position[0] = truck.x + exit_x * cos_yaw + exit_z * sin_yaw;
	walk.position[1] = truck.y;
	walk.position[2] = truck.z - exit_x * sin_yaw + exit_z * cos_yaw;

	locate_physics();
	follow_ground(0.0f);
	log_message(SCS_LOG_TYPE_message, "stepping out at height %.2f, truck is at %.2f", walk.position[1], truck.y);

	walk.placement = reference;
	walk.yaw = truck_yaw;
	walk.pitch = 0.0f;
	update_placement();

	input::capture_start();
	QueryPerformanceCounter(&walk.last_update);
	walk.active = true;
	game::camera_set(walk.placement);

	log_message(SCS_LOG_TYPE_message, "on foot: WASD to move, Shift to run, mouse to look, F9 to get back in");
}

void stop_walk(void)
{
	if (! walk.active) {
		return;
	}
	walk.active = false;
	input::capture_stop();
	game::camera_release();
	log_message(SCS_LOG_TYPE_message, "back in the truck");
}

void update_walk(void)
{
	LARGE_INTEGER now, frequency;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&frequency);
	float frame_time = static_cast<float>(now.QuadPart - walk.last_update.QuadPart) / static_cast<float>(frequency.QuadPart);
	walk.last_update = now;
	if (frame_time > MAX_FRAME_TIME) {
		frame_time = MAX_FRAME_TIME;
	}

	long mouse_x = 0;
	long mouse_y = 0;
	input::take_mouse_delta(mouse_x, mouse_y);

	if (! game_paused && input::game_has_focus()) {
		walk.yaw -= static_cast<float>(mouse_x) * MOUSE_SENSITIVITY;
		walk.pitch -= static_cast<float>(mouse_y) * MOUSE_SENSITIVITY;
		if (walk.pitch > MAX_PITCH) {
			walk.pitch = MAX_PITCH;
		}
		if (walk.pitch < -MAX_PITCH) {
			walk.pitch = -MAX_PITCH;
		}

		float forward = 0.0f;
		float right = 0.0f;
		if (input::key_down('W')) { forward += 1.0f; }
		if (input::key_down('S')) { forward -= 1.0f; }
		if (input::key_down('D')) { right += 1.0f; }
		if (input::key_down('A')) { right -= 1.0f; }

		const float length = sqrtf(forward * forward + right * right);
		if (length > 0.0f) {
			const float step = (input::key_down(VK_SHIFT) ? RUN_SPEED : WALK_SPEED) * frame_time / length;
			const float sin_yaw = sinf(walk.yaw);
			const float cos_yaw = cosf(walk.yaw);

			// Forward is (-sin, -cos) and right is (cos, -sin) in the X/Z plane.

			walk.position[0] += (-sin_yaw * forward + cos_yaw * right) * step;
			walk.position[2] += (-cos_yaw * forward - sin_yaw * right) * step;
		}

		walk.time_since_locate += frame_time;
		if (walk.time_since_locate >= PHYSICS_LOCATE_INTERVAL) {
			locate_physics();
		}
		follow_ground(frame_time);
		update_placement();
	}

	game::camera_set(walk.placement);
}

void on_toggle_pressed(void)
{
	if (walk.active) {
		stop_walk();
		return;
	}
	if (game_paused) {
		return;
	}
	if (! camera_supported) {
		log_message(SCS_LOG_TYPE_warning, "walking is not available on this game build");
		return;
	}
	if (! can_leave_truck()) {
		log_message(SCS_LOG_TYPE_warning, "stop and set the parking brake before getting out");
		return;
	}
	start_walk();
}

// Events.

SCSAPI_VOID telemetry_frame_end(const scs_event_t, const void *const, const scs_context_t)
{
	const bool down = input::game_has_focus() && input::key_down(TOGGLE_KEY);
	if (down && ! toggle_key_was_down) {
		on_toggle_pressed();
	}
	toggle_key_was_down = down;

	if (walk.active) {
		update_walk();
	}
}

SCSAPI_VOID telemetry_pause(const scs_event_t event, const void *const, const scs_context_t)
{
	game_paused = (event == SCS_TELEMETRY_EVENT_paused);
}

/**
 * @brief Finds a vector attribute in the configuration. NULL if it is missing.
 */
const scs_value_fvector_t *find_fvector(const scs_telemetry_configuration_t &configuration, const char *const name)
{
	for (const scs_named_value_t *current = configuration.attributes; current->name; ++current) {
		if ((current->index == SCS_U32_NIL) && (strcmp(current->name, name) == 0) && (current->value.type == SCS_VALUE_TYPE_fvector)) {
			return &current->value.value_fvector;
		}
	}
	return NULL;
}

SCSAPI_VOID telemetry_configuration(const scs_event_t, const void *const event_info, const scs_context_t)
{
	const scs_telemetry_configuration_t *const info = static_cast<const scs_telemetry_configuration_t *>(event_info);
	if (strcmp(info->id, SCS_TELEMETRY_CONFIG_truck) != 0) {
		return;
	}

	// The head position is relative to the cabin, which trucks without a separate cabin do not report.

	const scs_value_fvector_t *const head = find_fvector(*info, SCS_TELEMETRY_CONFIG_ATTRIBUTE_head_position);
	const scs_value_fvector_t *const cabin = find_fvector(*info, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cabin_position);
	if (! head) {
		telemetry.head_position = DEFAULT_HEAD_POSITION;
		return;
	}
	telemetry.head_position = *head;
	if (cabin) {
		telemetry.head_position.x += cabin->x;
		telemetry.head_position.y += cabin->y;
		telemetry.head_position.z += cabin->z;
	}
}

// Channels.

SCSAPI_VOID telemetry_store_dplacement(const scs_string_t, const scs_u32_t, const scs_value_t *const value, const scs_context_t context)
{
	if (value && (value->type == SCS_VALUE_TYPE_dplacement)) {
		*static_cast<scs_value_dplacement_t *>(context) = value->value_dplacement;
	}
}

SCSAPI_VOID telemetry_store_float(const scs_string_t, const scs_u32_t, const scs_value_t *const value, const scs_context_t context)
{
	if (value && (value->type == SCS_VALUE_TYPE_float)) {
		*static_cast<float *>(context) = value->value_float.value;
	}
}

SCSAPI_VOID telemetry_store_bool(const scs_string_t, const scs_u32_t, const scs_value_t *const value, const scs_context_t context)
{
	if (value && (value->type == SCS_VALUE_TYPE_bool)) {
		*static_cast<bool *>(context) = (value->value_bool.value != 0);
	}
}

} // namespace

void log_message(const scs_log_type_t type, const char *const format, ...)
{
	if (! game_log) {
		return;
	}
	char text[512];
	const int prefix = snprintf(text, sizeof(text), "[takeawalk] ");
	va_list args;
	va_start(args, format);
	vsnprintf(text + prefix, sizeof(text) - prefix, format, args);
	va_end(args);
	game_log(type, text);
}

/**
 * @brief Telemetry API initialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t *const params)
{
	if (version != SCS_TELEMETRY_VERSION_1_00) {
		return SCS_RESULT_unsupported;
	}
	const scs_telemetry_init_params_v100_t *const version_params = static_cast<const scs_telemetry_init_params_v100_t *>(params);

	if (strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) != 0) {
		version_params->common.log(SCS_LOG_TYPE_error, "[takeawalk] unsupported game, plugin disabled");
		return SCS_RESULT_unsupported;
	}

	const bool events_registered =
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_end, telemetry_frame_end, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_paused, telemetry_pause, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_started, telemetry_pause, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_configuration, telemetry_configuration, NULL) == SCS_RESULT_ok)
	;
	if (! events_registered) {
		version_params->common.log(SCS_LOG_TYPE_error, "[takeawalk] unable to register event callbacks");
		return SCS_RESULT_generic_error;
	}

	memset(&telemetry, 0, sizeof(telemetry));
	memset(&walk, 0, sizeof(walk));
	telemetry.head_position = DEFAULT_HEAD_POSITION;
	game_paused = true;
	toggle_key_was_down = false;

	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL, SCS_VALUE_TYPE_dplacement, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_dplacement, &telemetry.truck_placement);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_float, &telemetry.speed);
	version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_parking_brake, SCS_U32_NIL, SCS_VALUE_TYPE_bool, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_bool, &telemetry.parking_brake);

	game_log = version_params->common.log;

	camera_supported = game::camera_attach() && input::init();
	physics_supported = camera_supported && game::physics_attach();
	log_message(
		SCS_LOG_TYPE_message,
		"version " TAKEAWALK_VERSION " loaded, walking %s, ground following %s. Park, set the parking brake and press F9",
		camera_supported ? "available" : "NOT available on this game build",
		physics_supported ? "available" : "not available"
	);
	return SCS_RESULT_ok;
}

/**
 * @brief Telemetry API deinitialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_VOID scs_telemetry_shutdown(void)
{
	stop_walk();
	input::shutdown();
	game_log = NULL;
}
