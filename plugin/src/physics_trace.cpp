#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game_camera.h"
#include "game_physics.h"
#include "log.h"
#include "memory.h"
#include "physics_trace.h"

namespace game {
namespace {

// physics_static_actor_physx_t vtable entries, as offsets from the start of the exe.
const size_t ADD_SLOT_RVA = 0x2448848;		// _add_actor_to_scene
const size_t REMOVE_SLOT_RVA = 0x2448850;	// the entry after it, assumed to be the removal
const size_t ADD_FUNCTION_RVA = 0x1639EE0;
const size_t REMOVE_FUNCTION_RVA = 0x163A1A0;

// physics_static_actor_physx_t
const size_t ACTOR_PX_ACTOR = 0x98;	// PxRigidStatic *

// Virtual function offsets in PhysX 3.4 as built into the game.
const size_t SCENE_GET_ACTOR_COUNT = 0x90;	// PxScene::getNbActors
const size_t SCENE_GET_ACTORS = 0x98;		// PxScene::getActors
const size_t ACTOR_GET_GLOBAL_POSE = 0xA0;	// PxRigidActor::getGlobalPose

const uint16_t ACTOR_TYPE_STATIC = 1 << 0;

const int MAX_STACKS = 64;
const int STACK_DEPTH = 14;
const uint32_t MAX_CENSUS_ACTORS = 16384;

const double FLUSH_INTERVAL = 1.0;	// s

struct transform_t
{
	float rotation[4];
	float position[3];
};

struct stack_t
{
	ULONG  hash;
	USHORT depth;
	void  *frames[STACK_DEPTH];
	bool   removal;
	bool   dynamic;
	bool   logged;
	unsigned calls;
};

struct distance_stats_t
{
	unsigned count;
	double   minimum;
	double   maximum;
	double   sum;
};

typedef void *(*actor_function_t)(void *, void *, void *, void *);
typedef uint32_t (*get_actor_count_t)(void *scene, const uint16_t *types);
typedef uint32_t (*get_actors_t)(void *scene, const uint16_t *types, void **buffer, uint32_t capacity, uint32_t start);
typedef transform_t *(*get_global_pose_t)(void *actor, transform_t *result);

CRITICAL_SECTION lock;
bool             started = false;
actor_function_t original_add = NULL;
actor_function_t original_remove = NULL;
actor_function_t original_add_dynamic = NULL;

stack_t          stacks[MAX_STACKS];
int              stack_count = 0;

// Which call path added each of the most recently added actors (the game's actor objects).
struct origin_t
{
	void *actor;
	int   stack;
};

// The game's dynamic actor class (parked cars and other vehicles get one of these):
// its virtual function which adds the actor to the scene.
const size_t ADD_DYNAMIC_SLOT_RVA = 0x24489F0;
const size_t ADD_DYNAMIC_FUNCTION_RVA = 0x163F500;

const int MAX_ORIGINS = 8192;
origin_t  origins[MAX_ORIGINS];
int       next_origin = 0;

// Traffic vehicles (parked cars included) get a physics body by changing state.
// The class's virtual function which does that, as offsets from the start of the exe,
// and where a vehicle keeps its state.
const size_t STATE_SLOT_RVA = 0x2320C48;
const size_t STATE_FUNCTION_RVA = 0xB8EBA0;
const size_t VEHICLE_STATE = 0x18;	// u32

struct state_path_t
{
	ULONG    hash;
	uint32_t from;
	uint32_t to;
	USHORT   depth;
	void    *frames[STACK_DEPTH];
	bool     logged;
};

struct state_change_t
{
	void *vehicle;
	int   path;
};

const int MAX_STATE_PATHS = 64;
const int MAX_PENDING_CHANGES = 64;

// Most state changes written to the log in one session.
const int MAX_LOGGED_CHANGES = 400;

// Signs and poles do not get their collision through the item visitor. While shown they
// are in a list of the world object whose items get a call every frame, and that call
// builds or drops their collision. This function takes an item off that list (it was
// first taken for the one which puts items on it, hence "queue" in the names below).
// Its first four instructions (18 bytes) do not depend on where they run.
const size_t QUEUE_FUNCTION_RVA = 0x47BA60;
const uint8_t QUEUE_FUNCTION_START[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8D, 0x99, 0x78, 0x06, 0x00, 0x00, 0x48, 0x89, 0x54, 0x24, 0x20 };

// Map item: its type as in the map format (the sign class has its vtable at RVA 0x229A4B0).
const size_t ITEM_TYPE = 0x0A;	// u8

struct queue_path_t
{
	ULONG          hash;
	const uint8_t *item_class;
	uint8_t        item_type;
	USHORT         depth;
	void          *frames[STACK_DEPTH];
	bool           logged;
	unsigned       calls;
};

typedef void (*queue_function_t)(void *world, void *item);

const int MAX_QUEUE_PATHS = 128;

memory::hook_t queue_hook = {};
queue_path_t   queue_paths[MAX_QUEUE_PATHS];
int            queue_path_count = 0;

actor_function_t original_set_state = NULL;
state_path_t     state_paths[MAX_STATE_PATHS];
int              state_path_count = 0;
state_change_t   pending_changes[MAX_PENDING_CHANGES];
int              pending_change_count = 0;
int              logged_change_count = 0;
distance_stats_t added;
distance_stats_t removed;

double reference_x = 0.0;
double reference_z = 0.0;
double last_flush = 0.0;

void *census_buffer[MAX_CENSUS_ACTORS];

uint8_t *exe_base(void)
{
	return reinterpret_cast<uint8_t *>(GetModuleHandleW(NULL));
}

double seconds(void)
{
	return static_cast<double>(GetTickCount64()) / 1000.0;
}

void *virtual_function(void *const object, const size_t offset)
{
	uint8_t *table = NULL;
	void *function = NULL;
	if (! memory::read(static_cast<const uint8_t *>(object), table) || ! memory::read(table + offset, function)) {
		return NULL;
	}
	return function;
}

/**
 * @brief Position of a PhysX actor in physics coordinates. False if the call failed.
 */
bool actor_position(void *const px_actor, float *const position)
{
	const get_global_pose_t get_pose = reinterpret_cast<get_global_pose_t>(virtual_function(px_actor, ACTOR_GET_GLOBAL_POSE));
	if (! get_pose) {
		return false;
	}
	__try {
		transform_t transform;
		get_pose(px_actor, &transform);
		memcpy(position, transform.position, sizeof(transform.position));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

/**
 * @brief Distance in the X/Z plane between a point in physics coordinates and one in world coordinates.
 */
bool distance_to(const float *const physics_position, const double world_x, const double world_z, double &distance)
{
	int chunk_x = 0;
	int chunk_z = 0;
	if (! physics_origin(chunk_x, chunk_z)) {
		return false;
	}
	const double dx = physics_position[0] + chunk_x * CHUNK_SIZE - world_x;
	const double dz = physics_position[2] + chunk_z * CHUNK_SIZE - world_z;
	distance = sqrt(dx * dx + dz * dz);
	return true;
}

void record(distance_stats_t &stats, const double distance)
{
	if ((stats.count == 0) || (distance < stats.minimum)) {
		stats.minimum = distance;
	}
	if ((stats.count == 0) || (distance > stats.maximum)) {
		stats.maximum = distance;
	}
	stats.sum += distance;
	++stats.count;
}

/**
 * @brief Notes one addition or removal of a static actor: who asked for it and how far from the truck it is.
 */
void note(void *const actor, const bool removal, const bool dynamic)
{
	void *frames[STACK_DEPTH];
	ULONG hash = 0;
	const USHORT depth = RtlCaptureStackBackTrace(1, STACK_DEPTH, frames, &hash);

	// Only the static actor class is known to keep its PhysX actor at this offset.

	uint8_t *px_actor = NULL;
	float position[3];
	double distance = 0.0;
	const bool located =
		! dynamic &&
		memory::read(static_cast<const uint8_t *>(actor) + ACTOR_PX_ACTOR, px_actor) && px_actor &&
		actor_position(px_actor, position) &&
		distance_to(position, reference_x, reference_z, distance)
	;

	EnterCriticalSection(&lock);
	if (located) {
		record(removal ? removed : added, distance);
	}
	int index = 0;
	while ((index < stack_count) && ((stacks[index].hash != hash) || (stacks[index].removal != removal) || (stacks[index].dynamic != dynamic))) {
		++index;
	}
	if ((index == stack_count) && (stack_count < MAX_STACKS)) {
		stack_t &stack = stacks[stack_count++];
		stack.hash = hash;
		stack.depth = depth;
		memcpy(stack.frames, frames, sizeof(frames));
		stack.removal = removal;
		stack.dynamic = dynamic;
		stack.logged = false;
		stack.calls = 0;
	}
	if (index < stack_count) {
		++stacks[index].calls;
		if (! removal) {
			origins[next_origin].actor = actor;
			origins[next_origin].stack = index;
			next_origin = (next_origin + 1) % MAX_ORIGINS;
		}
	}
	LeaveCriticalSection(&lock);
}

void *hooked_add(void *a, void *b, void *c, void *d)
{
	void *const result = original_add(a, b, c, d);
	note(a, false, false);
	return result;
}

void *hooked_remove(void *a, void *b, void *c, void *d)
{
	note(a, true, false);
	return original_remove(a, b, c, d);
}

void *hooked_add_dynamic(void *a, void *b, void *c, void *d)
{
	note(a, false, true);
	return original_add_dynamic(a, b, c, d);
}

/**
 * @brief Notes a traffic vehicle changing state: from what to what, and who asked for it.
 */
void note_state(void *const vehicle, const uint32_t new_state)
{
	uint32_t old_state = 0;
	if (! memory::read(static_cast<const uint8_t *>(vehicle) + VEHICLE_STATE, old_state) || (old_state == new_state)) {
		return;
	}
	void *frames[STACK_DEPTH];
	ULONG hash = 0;
	const USHORT depth = RtlCaptureStackBackTrace(1, STACK_DEPTH, frames, &hash);

	EnterCriticalSection(&lock);
	int index = 0;
	while ((index < state_path_count) && ((state_paths[index].hash != hash) || (state_paths[index].from != old_state) || (state_paths[index].to != new_state))) {
		++index;
	}
	if ((index == state_path_count) && (state_path_count < MAX_STATE_PATHS)) {
		state_path_t &path = state_paths[state_path_count++];
		path.hash = hash;
		path.from = old_state;
		path.to = new_state;
		path.depth = depth;
		memcpy(path.frames, frames, sizeof(frames));
		path.logged = false;
	}
	if ((index < state_path_count) && (pending_change_count < MAX_PENDING_CHANGES)) {
		state_change_t &change = pending_changes[pending_change_count++];
		change.vehicle = vehicle;
		change.path = index;
	}
	LeaveCriticalSection(&lock);
}

/**
 * @brief Notes an item being taken off the per-frame update list, and who did it.
 */
void note_queue(void *const item)
{
	// Other kinds of item go through the same queue, so the class and type are noted too.

	const uint8_t *item_class = NULL;
	uint8_t item_type = 0;
	if (! memory::read(static_cast<const uint8_t *>(item), item_class) || ! memory::read(static_cast<const uint8_t *>(item) + ITEM_TYPE, item_type)) {
		return;
	}
	void *frames[STACK_DEPTH];
	ULONG hash = 0;
	const USHORT depth = RtlCaptureStackBackTrace(1, STACK_DEPTH, frames, &hash);

	EnterCriticalSection(&lock);
	int index = 0;
	while ((index < queue_path_count) && ((queue_paths[index].hash != hash) || (queue_paths[index].item_class != item_class))) {
		++index;
	}
	if ((index == queue_path_count) && (queue_path_count < MAX_QUEUE_PATHS)) {
		queue_path_t &path = queue_paths[queue_path_count++];
		path.hash = hash;
		path.item_class = item_class;
		path.item_type = item_type;
		path.depth = depth;
		memcpy(path.frames, frames, sizeof(frames));
		path.logged = false;
		path.calls = 0;
	}
	if (index < queue_path_count) {
		++queue_paths[index].calls;
	}
	LeaveCriticalSection(&lock);
}

void hooked_queue(void *world, void *item)
{
	note_queue(item);
	reinterpret_cast<queue_function_t>(queue_hook.original)(world, item);
}

void *hooked_set_state(void *a, void *b, void *c, void *d)
{
	note_state(a, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b)));
	return original_set_state(a, b, c, d);
}

/**
 * @brief Replaces a vtable entry, if it holds the function it is expected to hold.
 */
bool replace_slot(const size_t slot_rva, const size_t expected_rva, const actor_function_t replacement, actor_function_t &original)
{
	void **const slot = reinterpret_cast<void **>(exe_base() + slot_rva);
	void *current = NULL;
	if (! memory::read(reinterpret_cast<const uint8_t *>(slot), current) || (current != exe_base() + expected_rva)) {
		return false;
	}
	DWORD protection = 0;
	if (! VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
		return false;
	}
	original = reinterpret_cast<actor_function_t>(current);
	*slot = reinterpret_cast<void *>(replacement);
	VirtualProtect(slot, sizeof(void *), protection, &protection);
	return true;
}

void restore_slot(const size_t slot_rva, actor_function_t &original)
{
	if (! original) {
		return;
	}
	void **const slot = reinterpret_cast<void **>(exe_base() + slot_rva);
	DWORD protection = 0;
	if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
		*slot = reinterpret_cast<void *>(original);
		VirtualProtect(slot, sizeof(void *), protection, &protection);
	}
	original = NULL;
}

void log_stats(const char *const what, distance_stats_t &stats)
{
	if (stats.count == 0) {
		return;
	}
	log_message(
		SCS_LOG_TYPE_message,
		"trace: %u static actors %s, %.0f to %.0f m from the truck (average %.0f)",
		stats.count, what, stats.minimum, stats.maximum, stats.sum / stats.count
	);
	memset(&stats, 0, sizeof(stats));
}

/**
 * @brief Static actors of one scene. Returns how many were written to the buffer.
 */
uint32_t static_actors(void *const scene)
{
	const get_actor_count_t get_count = reinterpret_cast<get_actor_count_t>(virtual_function(scene, SCENE_GET_ACTOR_COUNT));
	const get_actors_t get_actors = reinterpret_cast<get_actors_t>(virtual_function(scene, SCENE_GET_ACTORS));
	if (! get_count || ! get_actors) {
		return 0;
	}
	const uint16_t types = ACTOR_TYPE_STATIC;
	__try {
		uint32_t count = get_count(scene, &types);
		if (count > MAX_CENSUS_ACTORS) {
			count = MAX_CENSUS_ACTORS;
		}
		return get_actors(scene, &types, census_buffer, count, 0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

} // namespace

void trace_start(void)
{
	if (started) {
		return;
	}
	InitializeCriticalSection(&lock);
	stack_count = 0;
	memset(&added, 0, sizeof(added));
	memset(&removed, 0, sizeof(removed));
	started = true;

	state_path_count = 0;
	pending_change_count = 0;
	logged_change_count = 0;

	const bool add_hooked = replace_slot(ADD_SLOT_RVA, ADD_FUNCTION_RVA, hooked_add, original_add);
	const bool remove_hooked = replace_slot(REMOVE_SLOT_RVA, REMOVE_FUNCTION_RVA, hooked_remove, original_remove);
	const bool state_hooked = replace_slot(STATE_SLOT_RVA, STATE_FUNCTION_RVA, hooked_set_state, original_set_state);
	const bool dynamic_hooked = replace_slot(ADD_DYNAMIC_SLOT_RVA, ADD_DYNAMIC_FUNCTION_RVA, hooked_add_dynamic, original_add_dynamic);

	queue_path_count = 0;
	uint8_t *const queue_function = exe_base() + QUEUE_FUNCTION_RVA;
	uint8_t start[sizeof(QUEUE_FUNCTION_START)];
	const bool queue_hooked =
		memory::safe_copy(start, queue_function, sizeof(start)) &&
		(memcmp(start, QUEUE_FUNCTION_START, sizeof(start)) == 0) &&
		memory::hook_install(queue_hook, queue_function, sizeof(QUEUE_FUNCTION_START), reinterpret_cast<const void *>(&hooked_queue))
	;
	log_message(
		SCS_LOG_TYPE_message,
		"trace: watching static actors being added (%s) and removed (%s), dynamic actors being added (%s), traffic vehicles changing state (%s), items leaving the update list (%s)",
		add_hooked ? "yes" : "no", remove_hooked ? "yes" : "no", dynamic_hooked ? "yes" : "no", state_hooked ? "yes" : "no", queue_hooked ? "yes" : "no"
	);
}

void trace_stop(void)
{
	if (! started) {
		return;
	}
	restore_slot(ADD_SLOT_RVA, original_add);
	restore_slot(REMOVE_SLOT_RVA, original_remove);
	restore_slot(STATE_SLOT_RVA, original_set_state);
	restore_slot(ADD_DYNAMIC_SLOT_RVA, original_add_dynamic);
	memory::hook_remove(queue_hook);
	started = false;
	DeleteCriticalSection(&lock);
}

int trace_creation_path(void *const actor)
{
	if (! started || ! actor) {
		return -1;
	}
	int result = -1;
	EnterCriticalSection(&lock);

	// Addresses get reused, so the most recent addition of this one counts.

	for (int age = 1; age <= MAX_ORIGINS; ++age) {
		const origin_t &origin = origins[(next_origin - age + MAX_ORIGINS) % MAX_ORIGINS];
		if (origin.actor == actor) {
			result = origin.stack;
			break;
		}
	}
	LeaveCriticalSection(&lock);
	return result;
}

void trace_flush(const double truck_x, const double truck_z)
{
	if (! started) {
		return;
	}
	reference_x = truck_x;
	reference_z = truck_z;

	const double now = seconds();
	if (now - last_flush < FLUSH_INTERVAL) {
		return;
	}
	last_flush = now;

	EnterCriticalSection(&lock);
	log_stats("added", added);
	log_stats("removed", removed);
	for (int i = 0; i < stack_count; ++i) {
		stack_t &stack = stacks[i];
		if (stack.logged) {
			continue;
		}
		stack.logged = true;

		char text[STACK_DEPTH * 10 + 1] = "";
		size_t length = 0;
		for (USHORT frame = 0; frame < stack.depth; ++frame) {
			const size_t rva = static_cast<size_t>(static_cast<uint8_t *>(stack.frames[frame]) - exe_base());
			length += snprintf(text + length, sizeof(text) - length, " %zX", rva);
		}
		log_message(SCS_LOG_TYPE_message, "trace: %s%s call path %d:%s", stack.dynamic ? "dynamic " : "", stack.removal ? "removal" : "addition", i, text);
	}

	for (int i = 0; i < queue_path_count; ++i) {
		queue_path_t &path = queue_paths[i];
		if (path.logged) {
			continue;
		}
		path.logged = true;

		char text[STACK_DEPTH * 10 + 1] = "";
		size_t length = 0;
		for (USHORT frame = 0; frame < path.depth; ++frame) {
			const size_t rva = static_cast<size_t>(static_cast<uint8_t *>(path.frames[frame]) - exe_base());
			length += snprintf(text + length, sizeof(text) - length, " %zX", rva);
		}
		log_message(
			SCS_LOG_TYPE_message,
			"trace: item of class %zX type %u taken off the update list, call path %d:%s",
			static_cast<size_t>(path.item_class - exe_base()), path.item_type, i, text
		);
	}

	for (int i = 0; i < state_path_count; ++i) {
		state_path_t &path = state_paths[i];
		if (path.logged) {
			continue;
		}
		path.logged = true;

		char text[STACK_DEPTH * 10 + 1] = "";
		size_t length = 0;
		for (USHORT frame = 0; frame < path.depth; ++frame) {
			const size_t rva = static_cast<size_t>(static_cast<uint8_t *>(path.frames[frame]) - exe_base());
			length += snprintf(text + length, sizeof(text) - length, " %zX", rva);
		}
		log_message(SCS_LOG_TYPE_message, "trace: vehicle state %u -> %u call path %d:%s", path.from, path.to, i, text);
	}
	for (int i = 0; (i < pending_change_count) && (logged_change_count < MAX_LOGGED_CHANGES); ++i, ++logged_change_count) {
		const state_change_t &change = pending_changes[i];
		const state_path_t &path = state_paths[change.path];
		log_message(SCS_LOG_TYPE_message, "trace: vehicle %p state %u -> %u by path %d", change.vehicle, path.from, path.to, change.path);
	}
	pending_change_count = 0;
	LeaveCriticalSection(&lock);
}

void trace_census(const char *const label, const double truck_x, const double truck_z, const double walker_x, const double walker_z)
{
	const double LIMITS[] = { 10.0, 25.0, 50.0, 100.0, 250.0, 1000.0 };
	const int LIMIT_COUNT = sizeof(LIMITS) / sizeof(LIMITS[0]);

	void *scene_list[4];
	const size_t scene_count = physics_scenes(scene_list, 4);
	for (size_t s = 0; s < scene_count; ++s) {
		const uint32_t count = static_actors(scene_list[s]);

		unsigned buckets[LIMIT_COUNT + 1] = {};
		unsigned near_walker = 0;
		double nearest_to_walker = -1.0;
		unsigned located = 0;
		for (uint32_t i = 0; i < count; ++i) {
			float position[3];
			double from_truck = 0.0;
			double from_walker = 0.0;
			if (! actor_position(census_buffer[i], position) || ! distance_to(position, truck_x, truck_z, from_truck) || ! distance_to(position, walker_x, walker_z, from_walker)) {
				continue;
			}
			++located;
			int bucket = 0;
			while ((bucket < LIMIT_COUNT) && (from_truck >= LIMITS[bucket])) {
				++bucket;
			}
			++buckets[bucket];
			if (from_walker < 25.0) {
				++near_walker;
			}
			if ((nearest_to_walker < 0.0) || (from_walker < nearest_to_walker)) {
				nearest_to_walker = from_walker;
			}
		}
		log_message(
			SCS_LOG_TYPE_message,
			"census (%s): %u static actors (%u located). From the truck: <10 m %u, <25 m %u, <50 m %u, <100 m %u, <250 m %u, <1 km %u, further %u. Within 25 m of the walker: %u, nearest %.1f m",
			label, count, located,
			buckets[0], buckets[1], buckets[2], buckets[3], buckets[4], buckets[5], buckets[6],
			near_walker, nearest_to_walker
		);
	}
}

} // namespace game
