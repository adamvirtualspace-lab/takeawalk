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

const int MAX_STACKS = 24;
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

stack_t          stacks[MAX_STACKS];
int              stack_count = 0;
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
void note(void *const actor, const bool removal)
{
	void *frames[STACK_DEPTH];
	ULONG hash = 0;
	const USHORT depth = RtlCaptureStackBackTrace(1, STACK_DEPTH, frames, &hash);

	uint8_t *px_actor = NULL;
	float position[3];
	double distance = 0.0;
	const bool located =
		memory::read(static_cast<const uint8_t *>(actor) + ACTOR_PX_ACTOR, px_actor) && px_actor &&
		actor_position(px_actor, position) &&
		distance_to(position, reference_x, reference_z, distance)
	;

	EnterCriticalSection(&lock);
	if (located) {
		record(removal ? removed : added, distance);
	}
	int index = 0;
	while ((index < stack_count) && ((stacks[index].hash != hash) || (stacks[index].removal != removal))) {
		++index;
	}
	if ((index == stack_count) && (stack_count < MAX_STACKS)) {
		stack_t &stack = stacks[stack_count++];
		stack.hash = hash;
		stack.depth = depth;
		memcpy(stack.frames, frames, sizeof(frames));
		stack.removal = removal;
		stack.logged = false;
		stack.calls = 0;
	}
	if (index < stack_count) {
		++stacks[index].calls;
	}
	LeaveCriticalSection(&lock);
}

void *hooked_add(void *a, void *b, void *c, void *d)
{
	void *const result = original_add(a, b, c, d);
	note(a, false);
	return result;
}

void *hooked_remove(void *a, void *b, void *c, void *d)
{
	note(a, true);
	return original_remove(a, b, c, d);
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

	const bool add_hooked = replace_slot(ADD_SLOT_RVA, ADD_FUNCTION_RVA, hooked_add, original_add);
	const bool remove_hooked = replace_slot(REMOVE_SLOT_RVA, REMOVE_FUNCTION_RVA, hooked_remove, original_remove);
	log_message(SCS_LOG_TYPE_message, "trace: watching static actors being added (%s) and removed (%s)", add_hooked ? "yes" : "no", remove_hooked ? "yes" : "no");
}

void trace_stop(void)
{
	if (! started) {
		return;
	}
	restore_slot(ADD_SLOT_RVA, original_add);
	restore_slot(REMOVE_SLOT_RVA, original_remove);
	started = false;
	DeleteCriticalSection(&lock);
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
		log_message(SCS_LOG_TYPE_message, "trace: %s call path %d:%s", stack.removal ? "removal" : "addition", i, text);
	}
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
