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

#include "config.h"
#include "game_camera.h"
#include "game_console.h"
#include "game_physics.h"
#include "game_scenery.h"
#include "game_traffic.h"
#include "hud.h"
#include "input.h"
#include "log.h"
#include "overlay.h"
#include "physics_trace.h"
#include "sound.h"

#define TAKEAWALK_VERSION "0.14"

namespace {

const int   TOGGLE_KEY = VK_F9;

// How close to the spot beside the driver's door the walker has to be to get in (m).
const float ENTER_DISTANCE = 2.5f;

// Holding the toggle key this long puts the walker back in the cab from anywhere (ms).
// The way out when stuck or lost.
const ULONGLONG FORCE_RETURN_HOLD_MS = 1500;

// How often the diagnostics count the static collision in the scene (ms).
const ULONGLONG CENSUS_INTERVAL_MS = 5000;

// The truck counts as stopped below this speed (m/s).
const float STOPPED_SPEED = 0.1f;

const float TWO_PI = 6.2831853071795864769252867665590058f;

const float WALK_SPEED = 1.5f;			// m/s
const float RUN_SPEED = 4.0f;			// m/s
const float CROUCH_SPEED = 0.8f;		// m/s
const float EYE_HEIGHT = 1.75f;			// m above the ground
const float CROUCH_EYE_HEIGHT = 1.0f;		// m above the ground
const float MOUSE_SENSITIVITY = 0.0025f;	// rad per mouse count
const float MAX_PITCH = 1.48f;			// rad, just short of straight up

const int RUN_KEY = VK_SHIFT;
const int CROUCH_KEY = VK_CONTROL;
const int JUMP_KEY = VK_SPACE;

// Diagnostics, when enabled in the ini file: reports the solid object straight ahead.
const int PROBE_KEY = 'P';
const float PROBE_DISTANCE = 30.0f;	// m

// With the probe key enabled, this key puts the walker in front of a sign which is out of
// the truck's reach, to check that signs become solid around the walker.
const int SIGN_KEY = 'O';
const double SIGN_STAND_OFF = 3.0;	// m

// How fast the eyes move between standing and crouching (m/s).
const float CROUCH_RATE = 3.5f;

// Upward speed at the start of a jump (m/s). Reaches about half a metre.
const float JUMP_SPEED = 3.2f;

// A landing faster than this is heard (m/s).
const float LANDING_SOUND_SPEED = 2.0f;

// Distance covered by one step (m).
const float WALK_STEP_LENGTH = 0.75f;
const float RUN_STEP_LENGTH = 1.25f;

// How far the eyes move up and down with each step (m).
const float WALK_BOB_HEIGHT = 0.025f;
const float RUN_BOB_HEIGHT = 0.045f;

// How quickly the head bob fades in and out when starting and stopping (1/s).
const float BOB_FADE_RATE = 7.0f;

// Moving slower than this is standing still as far as steps are concerned (m/s).
const float STEP_MIN_SPEED = 0.3f;

const float PI = 3.14159265358979323846f;

// How far outside the driver's head the walk starts (m). About a metre clear of the door.
const float EXIT_SIDE_STEP = 1.5f;

// Driver's head in vehicle space when the truck does not report it (m): left-hand drive.
const scs_value_fvector_t DEFAULT_HEAD_POSITION = { -0.7f, 2.0f, -1.5f };

// Highest ledge that can be walked onto, and how far down the ground is looked for (m).
// Ground further down than that is ignored: where the map has no collision, the next
// surface below can be far underneath what is visible.
const float STEP_HEIGHT = 0.45f;
const float GROUND_SEARCH_DEPTH = 8.0f;

// How far down the log reports what is there when the ground is lost (m).
const float GROUND_REPORT_DEPTH = 200.0f;

// How fast the feet follow the ground up and down while walking on it (m/s).
const float GROUND_FOLLOW_SPEED = 4.0f;

// A drop larger than this is a fall instead of a step down (m).
const float FALL_THRESHOLD = 0.35f;

const float GRAVITY = 9.81f;	// m/s^2

// Half the width of the body: how close the walker gets to a wall (m).
const float BODY_RADIUS = 0.3f;

// Heights above the feet at which obstacles are looked for (m). The lowest is above
// STEP_HEIGHT, and the gaps are small enough not to miss a guard rail.
const float PROBE_HEIGHTS[] = { 0.5f, 0.75f, 1.0f, 1.3f, 1.65f };

// Surfaces whose normal points up more than this are ground to walk on, not walls.
const float WALKABLE_NORMAL_Y = 0.6f;

// The map is made solid within this distance of the walker (m).
const float ACTIVATION_RADIUS = 25.0f;

// Number of directions checked around the body to keep it clear of walls.
const int CLEARANCE_DIRECTIONS = 8;

// Most "ground lost / found" lines written to the log during one walk.
const int MAX_GROUND_REPORTS = 30;

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
	float               vertical_speed;	// m/s, upward
	bool                airborne;		// Jumping or falling.
	bool                ground_found;
	int                 ground_reports;
	float               eye_height;		// m above the feet, lower when crouching
	float               step_phase;		// rad, a step is taken every PI
	float               bob_strength;	// 0 standing still to 1 walking
	float               bob_offset;		// m, added to the eye height
	bool                jump_key_was_down;
	bool                probe_key_was_down;
	bool                sign_key_was_down;
};

scs_log_t         game_log = NULL;
telemetry_state_t telemetry;
walk_state_t      walk;
bool              camera_supported = false;
bool              physics_supported = false;
bool              game_paused = true;
bool              toggle_key_was_down = false;

// Set while the toggle key is held after a refused attempt to get in.
bool              force_return_armed = false;
ULONGLONG         toggle_pressed_at = 0;

// Whether HUD settings left over from an earlier session were dealt with.
bool              hud_recovered = false;

bool can_leave_truck(void)
{
	return (fabsf(telemetry.speed) < STOPPED_SPEED) && telemetry.parking_brake;
}

/**
 * @brief Tells the player why something did not happen.
 *
 * The message goes on screen and to the log.
 */
void refuse(const char *const format, ...)
{
	char text[256];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	log_message(SCS_LOG_TYPE_warning, "%s", text);
	overlay::show(text);
}

/**
 * @brief World position of the spot on the ground beside the driver's door.
 *
 * It is sideways from the driver's seat, on whichever side the driver sits. In
 * vehicle space X is right and Z is backward.
 */
void door_position(double *const position)
{
	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	const float truck_yaw = telemetry.truck_placement.orientation.heading * TWO_PI;
	const float sin_yaw = sinf(truck_yaw);
	const float cos_yaw = cosf(truck_yaw);

	const float side = (telemetry.head_position.x > 0.0f) ? 1.0f : -1.0f;
	const float exit_x = telemetry.head_position.x + side * EXIT_SIDE_STEP;
	const float exit_z = telemetry.head_position.z;

	position[0] = truck.x + exit_x * cos_yaw + exit_z * sin_yaw;
	position[1] = truck.y;
	position[2] = truck.z - exit_x * sin_yaw + exit_z * cos_yaw;
}

/**
 * @brief Distance in the X/Z plane from the walker to a world position (m).
 */
double distance_to(const double x, const double z)
{
	const double dx = walk.position[0] - x;
	const double dz = walk.position[2] - z;
	return sqrt(dx * dx + dz * dz);
}

/**
 * @brief Stores the walk's position and view direction in the camera placement.
 *
 * The rotation is yaw around Y followed by pitch around X.
 */
void update_placement(void)
{
	game::set_world_position(walk.placement, walk.position[0], walk.position[1] + walk.eye_height + walk.bob_offset, walk.position[2]);

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
 * @brief Asks the game for the collision of the map around the walker.
 *
 * Without this only what the truck touches is solid.
 */
void activate_surroundings(void)
{
	if (physics_supported) {
		game::physics_activate(walk.position[0], walk.position[1], walk.position[2], ACTIVATION_RADIUS);

		// Vehicles are a separate matter: the game makes them solid near the truck, and
		// now near this position too.

		game::traffic_set_walker(walk.position[0], walk.position[1], walk.position[2]);

		// So are signs and poles.

		game::scenery_set_walker(walk.position[0], walk.position[1], walk.position[2], ACTIVATION_RADIUS);
	}
}

/**
 * @brief Notes in the log when the ground under the walker appears or disappears.
 *
 * The collision world does not cover the whole map; the distance from the truck
 * shows where it ends.
 */
void report_ground(const bool found)
{
	if ((found == walk.ground_found) || (walk.ground_reports >= MAX_GROUND_REPORTS)) {
		return;
	}
	walk.ground_found = found;
	++walk.ground_reports;

	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	const double distance = sqrt((walk.position[0] - truck.x) * (walk.position[0] - truck.x) + (walk.position[2] - truck.z) * (walk.position[2] - truck.z));
	log_message(
		SCS_LOG_TYPE_message,
		"ground %s %.1f m from the truck, at (%.1f, %.2f, %.1f)",
		found ? "found" : "lost", distance, walk.position[0], walk.position[1], walk.position[2]
	);

	float deep = 0.0f;
	if (! found && game::physics_ground_height(walk.position[0], walk.position[2], walk.position[1] + STEP_HEIGHT, GROUND_REPORT_DEPTH, deep)) {
		log_message(SCS_LOG_TYPE_message, "  next surface below is at height %.2f", deep);
	}
}

/**
 * @brief Moves the feet towards the ground below them. Without ground they stay at their height.
 *
 * @param frame_time Time to simulate (s). Zero puts the feet on the ground at once.
 */
void follow_ground(const float frame_time)
{
	float ground = 0.0f;
	const bool found = physics_supported && game::physics_ground_height(walk.position[0], walk.position[2], walk.position[1] + STEP_HEIGHT, STEP_HEIGHT + GROUND_SEARCH_DEPTH, ground);
	report_ground(found);
	if (! found) {
		walk.vertical_speed = 0.0f;
		walk.airborne = false;
		return;
	}
	if (frame_time <= 0.0f) {
		walk.position[1] = ground;
		walk.vertical_speed = 0.0f;
		walk.airborne = false;
		return;
	}

	// Close to the ground the feet follow it; further above it they fall.

	const double drop = walk.position[1] - ground;
	if (! walk.airborne) {
		if (drop <= FALL_THRESHOLD) {
			const double limit = GROUND_FOLLOW_SPEED * frame_time;
			walk.position[1] = (fabs(drop) <= limit) ? ground : (walk.position[1] + ((drop > 0.0) ? -limit : limit));
			return;
		}
		walk.airborne = true;
		walk.vertical_speed = 0.0f;
	}

	walk.vertical_speed -= GRAVITY * frame_time;
	walk.position[1] += walk.vertical_speed * frame_time;
	if (walk.position[1] <= ground) {
		if (config.footsteps && (walk.vertical_speed < -LANDING_SOUND_SPEED)) {
			sound::landing();
		}
		walk.position[1] = ground;
		walk.vertical_speed = 0.0f;
		walk.airborne = false;
	}
}

/**
 * @brief Starts a jump if the walker is standing on something.
 */
void jump(void)
{
	if (! walk.airborne && walk.ground_found && physics_supported) {
		walk.airborne = true;
		walk.vertical_speed = JUMP_SPEED;
	}
}

/**
 * @brief Moves the eyes between standing and crouching height.
 */
void update_crouch(const bool crouching, const float frame_time)
{
	const float target = crouching ? CROUCH_EYE_HEIGHT : EYE_HEIGHT;
	const float limit = CROUCH_RATE * frame_time;
	const float difference = target - walk.eye_height;
	walk.eye_height = (fabsf(difference) <= limit) ? target : (walk.eye_height + ((difference > 0.0f) ? limit : -limit));
}

/**
 * @brief Advances the walking cycle: head bob and the sound of each step.
 *
 * @param distance How far the walker moved over the ground this frame (m).
 */
void update_steps(const float distance, const bool running, const float frame_time)
{
	const bool stepping = ! walk.airborne && (frame_time > 0.0f) && (distance / frame_time > STEP_MIN_SPEED);
	if (stepping) {
		const float previous_phase = walk.step_phase;
		walk.step_phase += distance / (running ? RUN_STEP_LENGTH : WALK_STEP_LENGTH) * PI;
		if (config.footsteps && (floorf(walk.step_phase / PI) != floorf(previous_phase / PI))) {
			sound::footstep();
		}
	}

	const float target = stepping ? 1.0f : 0.0f;
	walk.bob_strength += (target - walk.bob_strength) * fminf(BOB_FADE_RATE * frame_time, 1.0f);

	// The eyes are lowest when a foot lands (phase a multiple of PI) and highest in between.

	const float height = running ? RUN_BOB_HEIGHT : WALK_BOB_HEIGHT;
	walk.bob_offset = config.head_bob ? (-fabsf(cosf(walk.step_phase)) * height * walk.bob_strength) : 0.0f;
}

/**
 * @brief Finds the nearest wall in the way of the body moving in a horizontal direction.
 *
 * Ground steep enough to walk on is not a wall, and neither is anything the walker
 * is already inside of: that would trap them.
 */
bool find_wall(const float direction_x, const float direction_z, const float distance, game::obstacle_t &nearest)
{
	bool found = false;
	for (const float height : PROBE_HEIGHTS) {
		game::obstacle_t obstacle;
		if (! game::physics_obstacle(walk.position[0], walk.position[1] + height, walk.position[2], direction_x, direction_z, distance, obstacle)) {
			continue;
		}
		if ((obstacle.distance <= 0.0f) || (obstacle.normal[1] > WALKABLE_NORMAL_Y)) {
			continue;
		}
		if (! found || (obstacle.distance < nearest.distance)) {
			found = true;
			nearest = obstacle;
		}
	}
	return found;
}

/**
 * @brief Moves the walker horizontally, stopping at walls and sliding along them.
 */
void move(const float x, const float z)
{
	const float length = sqrtf(x * x + z * z);
	if (length <= 0.0f) {
		return;
	}
	const float direction_x = x / length;
	const float direction_z = z / length;

	game::obstacle_t wall;
	if (! physics_supported || ! find_wall(direction_x, direction_z, length + BODY_RADIUS, wall)) {
		walk.position[0] += x;
		walk.position[2] += z;
		return;
	}

	// Go as far as the wall allows, then spend the rest of the step along the wall.

	float advance = wall.distance - BODY_RADIUS;
	if (advance < 0.0f) {
		advance = 0.0f;
	}
	walk.position[0] += direction_x * advance;
	walk.position[2] += direction_z * advance;

	const float normal_length = sqrtf(wall.normal[0] * wall.normal[0] + wall.normal[2] * wall.normal[2]);
	if (normal_length < 0.001f) {
		return;
	}
	const float normal_x = wall.normal[0] / normal_length;
	const float normal_z = wall.normal[2] / normal_length;

	const float remaining = length - advance;
	const float into_wall = (direction_x * normal_x + direction_z * normal_z) * remaining;
	const float slide_x = direction_x * remaining - normal_x * into_wall;
	const float slide_z = direction_z * remaining - normal_z * into_wall;
	const float slide_length = sqrtf(slide_x * slide_x + slide_z * slide_z);
	if (slide_length < 0.0001f) {
		return;
	}

	game::obstacle_t other_wall;
	if (! find_wall(slide_x / slide_length, slide_z / slide_length, slide_length + BODY_RADIUS, other_wall)) {
		walk.position[0] += slide_x;
		walk.position[2] += slide_z;
	}
}

/**
 * @brief Keeps the walker within the configured distance of the truck.
 */
void apply_leash(void)
{
	if (config.max_distance <= 0.0f) {
		return;
	}
	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	const double distance = distance_to(truck.x, truck.z);
	if (distance <= config.max_distance) {
		return;
	}
	const double scale = config.max_distance / distance;
	walk.position[0] = truck.x + (walk.position[0] - truck.x) * scale;
	walk.position[2] = truck.z + (walk.position[2] - truck.z) * scale;
}

/**
 * @brief Pushes the walker away from walls closer than the body radius in any direction.
 *
 * Moving only checks ahead, so walking along a wall or turning next to one could
 * otherwise put the eyes inside it.
 */
void keep_clear_of_walls(void)
{
	if (! physics_supported) {
		return;
	}
	for (int i = 0; i < CLEARANCE_DIRECTIONS; ++i) {
		const float angle = TWO_PI * static_cast<float>(i) / static_cast<float>(CLEARANCE_DIRECTIONS);
		const float direction_x = cosf(angle);
		const float direction_z = sinf(angle);

		game::obstacle_t wall;
		if (find_wall(direction_x, direction_z, BODY_RADIUS, wall)) {
			const float overlap = BODY_RADIUS - wall.distance;
			walk.position[0] -= direction_x * overlap;
			walk.position[2] -= direction_z * overlap;
		}
	}
}

void start_walk(void)
{
	game::placement_t reference;
	if (! game::camera_take(reference)) {
		return;
	}

	// Step out beside the driver's door, facing the way the truck faces.

	const float truck_yaw = telemetry.truck_placement.orientation.heading * TWO_PI;
	door_position(walk.position);

	walk.vertical_speed = 0.0f;
	walk.airborne = false;
	walk.ground_found = true;
	walk.ground_reports = 0;
	walk.eye_height = EYE_HEIGHT;
	walk.step_phase = 0.0f;
	walk.bob_strength = 0.0f;
	walk.bob_offset = 0.0f;
	walk.jump_key_was_down = false;
	walk.probe_key_was_down = false;
	walk.sign_key_was_down = false;
	locate_physics();
	activate_surroundings();
	follow_ground(0.0f);

	walk.placement = reference;
	walk.yaw = truck_yaw;
	walk.pitch = 0.0f;
	update_placement();

	input::capture_start();
	QueryPerformanceCounter(&walk.last_update);
	walk.active = true;
	game::camera_set(walk.placement);
	hud::hide();

	log_message(SCS_LOG_TYPE_message, "on foot");
	overlay::show("On foot.  WASD move  \xC2\xB7  Shift run  \xC2\xB7  Ctrl crouch  \xC2\xB7  Space jump  \xC2\xB7  F9 at the door to get in");
}

void stop_walk(const bool game_closing)
{
	if (! walk.active) {
		return;
	}
	walk.active = false;
	input::capture_stop();
	game::camera_release();
	game::traffic_clear_walker();
	game::scenery_clear_walker();
	hud::restore(game_closing);
	overlay::hide();
	log_message(SCS_LOG_TYPE_message, "back in the truck");
}

void probe(void);
void go_to_sign(void);

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

	// Menus need the keyboard and the mouse, so the game has them while it is paused.

	const bool controllable = ! game_paused && input::game_has_focus();
	if (controllable) {
		input::capture_start();
	}
	else {
		input::capture_stop();
	}

	long mouse_x = 0;
	long mouse_y = 0;
	input::take_mouse_delta(mouse_x, mouse_y);

	if (controllable) {
		activate_surroundings();

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

		const bool crouching = input::key_down(CROUCH_KEY);
		const bool running = ! crouching && input::key_down(RUN_KEY);
		update_crouch(crouching, frame_time);

		const bool jump_key_down = input::key_down(JUMP_KEY);
		if (jump_key_down && ! walk.jump_key_was_down) {
			jump();
		}
		walk.jump_key_was_down = jump_key_down;

		const bool probe_key_down = config.probe_key && input::key_down(PROBE_KEY);
		if (probe_key_down && ! walk.probe_key_was_down) {
			probe();
		}
		walk.probe_key_was_down = probe_key_down;

		const bool sign_key_down = config.probe_key && input::key_down(SIGN_KEY);
		if (sign_key_down && ! walk.sign_key_was_down) {
			go_to_sign();
		}
		walk.sign_key_was_down = sign_key_down;

		const double start_x = walk.position[0];
		const double start_z = walk.position[2];

		const float length = sqrtf(forward * forward + right * right);
		if (length > 0.0f) {
			const float speed = crouching ? CROUCH_SPEED : (running ? RUN_SPEED : WALK_SPEED);
			const float step = speed * frame_time / length;
			const float sin_yaw = sinf(walk.yaw);
			const float cos_yaw = cosf(walk.yaw);

			// Forward is (-sin, -cos) and right is (cos, -sin) in the X/Z plane.

			move((-sin_yaw * forward + cos_yaw * right) * step, (-cos_yaw * forward - sin_yaw * right) * step);
			apply_leash();
		}

		walk.time_since_locate += frame_time;
		if (walk.time_since_locate >= PHYSICS_LOCATE_INTERVAL) {
			locate_physics();
		}
		keep_clear_of_walls();
		follow_ground(frame_time);
		update_steps(static_cast<float>(distance_to(start_x, start_z)), running, frame_time);
		update_placement();
	}

	game::camera_set(walk.placement);
}

void on_toggle_pressed(void)
{
	if (walk.active) {
		double door[3];
		door_position(door);
		const double distance = distance_to(door[0], door[2]);
		if (distance <= ENTER_DISTANCE) {
			stop_walk(false);
		}
		else {
			force_return_armed = true;
			refuse("Walk back to the driver's door to get in (%.0f m away), or hold F9 to be put back in the cab.", distance);
		}
		return;
	}

	if (game_paused) {
		refuse("The game is paused.");
	}
	else if (! camera_supported) {
		refuse("Walking is not available on this game version.");
	}
	else if (! can_leave_truck()) {
		refuse("Stop and set the parking brake before getting out.");
	}
	else {
		start_walk();
	}
}

/**
 * @brief Diagnostics: logs how many of the signs being shown are solid, by where they are.
 */
bool count_signs(game::scenery_census_t &census)
{
	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	const double truck_position[3] = { truck.x, truck.y, truck.z };
	if (! game::scenery_census(game::physics_world(), truck_position, walk.position, census)) {
		log_message(SCS_LOG_TYPE_message, "signs: unable to read the game's list");
		return false;
	}
	log_message(
		SCS_LOG_TYPE_message,
		"signs: %u shown. Within %.0f m of the walker: %u, solid %u. Within 35 m of the truck: %u, solid %u. Elsewhere: %u, solid %u",
		census.shown, ACTIVATION_RADIUS, census.near_walker, census.near_walker_solid, census.near_truck, census.near_truck_solid, census.elsewhere, census.elsewhere_solid
	);
	return true;
}

/**
 * @brief Diagnostics: shows and logs what solid object the walker is looking at.
 */
void probe(void)
{
	const float cos_pitch = cosf(walk.pitch);
	const float direction[3] = { -sinf(walk.yaw) * cos_pitch, sinf(walk.pitch), -cosf(walk.yaw) * cos_pitch };

	char text[256];
	void *actor = NULL;
	game::physics_describe(walk.position[0], walk.position[1] + walk.eye_height, walk.position[2], direction, PROBE_DISTANCE, text, sizeof(text), actor);

	// With the collision trace on, this ties the object to the game code which created it.

	const int path = game::trace_creation_path(actor);
	if (path >= 0) {
		const size_t length = strlen(text);
		snprintf(text + length, sizeof(text) - length, ", added by call path %d", path);
	}

	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	log_message(
		SCS_LOG_TYPE_message,
		"probe from (%.1f, %.2f, %.1f), %.0f m from the truck: %s",
		walk.position[0], walk.position[1], walk.position[2], distance_to(truck.x, truck.z), text
	);
	overlay::show(text);

	game::scenery_census_t census;
	count_signs(census);
}

/**
 * @brief Diagnostics: puts the walker in front of the nearest sign which the truck does not make solid, facing it.
 */
void go_to_sign(void)
{
	game::scenery_census_t census;
	if (! count_signs(census)) {
		return;
	}
	if (! census.far_sign_found) {
		refuse("No sign out of the truck's reach is being shown here.");
		return;
	}

	// Stand on the truck's side of it.

	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	const double dx = truck.x - census.far_sign[0];
	const double dz = truck.z - census.far_sign[2];
	const double length = sqrt(dx * dx + dz * dz);
	walk.position[0] = census.far_sign[0] + dx / length * SIGN_STAND_OFF;
	walk.position[1] = census.far_sign[1] + 0.5;
	walk.position[2] = census.far_sign[2] + dz / length * SIGN_STAND_OFF;
	walk.yaw = static_cast<float>(atan2(dx, dz));
	walk.pitch = 0.0f;
	log_message(
		SCS_LOG_TYPE_message,
		"moved to a sign at (%.1f, %.2f, %.1f), %.0f m from the truck, which was %s",
		census.far_sign[0], census.far_sign[1], census.far_sign[2], census.far_sign_from_truck, census.far_sign_solid ? "solid" : "not solid"
	);
	overlay::show("Moved to a sign out of the truck's reach. Press P to check it.");
}

/**
 * @brief Optional diagnostics of the game's collision world, switched on in the ini file.
 */
void update_trace(void)
{
	if (! physics_supported || (! config.trace_collision && ! config.collision_census)) {
		return;
	}
	const scs_value_dvector_t &truck = telemetry.truck_placement.position;
	game::trace_flush(truck.x, truck.z);

	static ULONGLONG last_census = 0;
	const ULONGLONG now = GetTickCount64();
	if (! config.collision_census || game_paused || (now - last_census < CENSUS_INTERVAL_MS)) {
		return;
	}
	last_census = now;
	if (walk.active) {
		game::trace_census("walking", truck.x, truck.z, walk.position[0], walk.position[2]);
	}
	else {
		// Keeps the physics origin known while driving, so distances can be measured.

		locate_physics();
		game::trace_census("driving", truck.x, truck.z, truck.x, truck.z);
	}
}

// Events.

SCSAPI_VOID telemetry_frame_end(const scs_event_t, const void *const, const scs_context_t)
{
	const bool down = input::game_has_focus() && input::key_down(TOGGLE_KEY);
	const ULONGLONG now = GetTickCount64();
	if (! down) {
		force_return_armed = false;
	}
	else if (! toggle_key_was_down) {
		toggle_pressed_at = now;
		on_toggle_pressed();
	}
	else if (force_return_armed && walk.active && (now - toggle_pressed_at >= FORCE_RETURN_HOLD_MS)) {
		force_return_armed = false;
		stop_walk(false);
	}
	toggle_key_was_down = down;

	if (! hud_recovered && ! game_paused) {
		hud_recovered = true;
		hud::recover();
	}

	update_trace();
	if (walk.active) {
		update_walk();
	}
}

SCSAPI_VOID telemetry_pause(const scs_event_t event, const void *const, const scs_context_t)
{
	game_paused = (event == SCS_TELEMETRY_EVENT_paused);

	// Frames might not be reported while paused, so the menu gets its input back here.

	if (game_paused && walk.active) {
		input::capture_stop();
	}
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
	config_load();

	camera_supported = game::camera_attach() && input::init();
	physics_supported = camera_supported && game::physics_attach();
	game::physics_ignore_barriers(config.ignore_barriers);
	if (physics_supported) {
		game::traffic_attach();
		game::scenery_attach();
	}
	game::console_attach();
	overlay::init();
	sound::init();
	hud_recovered = false;
	log_message(
		SCS_LOG_TYPE_message,
		"version " TAKEAWALK_VERSION " loaded, walking %s, ground following %s. Park, set the parking brake and press F9",
		camera_supported ? "available" : "NOT available on this game build",
		physics_supported ? "available" : "not available"
	);
	if (physics_supported && config.trace_collision) {
		game::trace_start();
	}
	return SCS_RESULT_ok;
}

/**
 * @brief Telemetry API deinitialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_VOID scs_telemetry_shutdown(void)
{
	stop_walk(true);
	game::traffic_detach();
	game::scenery_detach();
	game::trace_stop();
	sound::shutdown();
	overlay::shutdown();
	input::shutdown();
	game_log = NULL;
}
