#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <float.h>
#include <math.h>
#include <string.h>

#include "game_camera.h"
#include "game_physics.h"
#include "log.h"
#include "memory.h"

namespace game {
namespace {

// mov rdi, [rip + NpPhysics::mInstance]; mov rcx, [rbx + 8]; add rdi, 0A0h
const char *const INSTANCE_SIGNATURE = "48 8B 3D ?? ?? ?? ?? 48 8B 4B 08 48 81 C7 A0 00 00 00";

// Start of NpSceneQueries::raycast. Long, because NpVolumeCache::raycast starts the same way.
const char *const RAYCAST_SIGNATURE =
	"F3 0F 11 5C 24 20 4C 8B DC 53 48 83 EC 70 0F AE 9C 24 88 00 00 00 8B 9C 24 88 00 00 00 "
	"4C 8B D1 C7 84 24 88 00 00 00 C0 9F 00 00 0F AE 94 24 88 00 00 00 0F 18 02 41 0F 18 00 "
	"F3 41 0F 10 43 20 4D 8D 4B 20 48 8B 84 24 A8 00 00 00 49 89 53 C8 49 8D 53 C8 4D 89 43 D0"
;

// The place where the player's vehicle turns on the collision of the map around it:
//   call make_activation_box; mov rcx, [rip + world]; lea rdx, [rsp+58h]; xor r8d, r8d; call activate_collision; mov rcx, [rsi+...]
const char *const ACTIVATION_SIGNATURE = "E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 58 45 33 C0 E8 ?? ?? ?? ?? 48 8B 8E";
const size_t ACTIVATION_MAKE_BOX_CALL = 0;
const size_t ACTIVATION_WORLD_LOAD = 5;
const size_t ACTIVATION_ACTIVATE_CALL = 20;

// NpPhysics
const size_t PHYSICS_SCENES_ITEMS = 0x08;	// NpScene **
const size_t PHYSICS_SCENES_COUNT = 0x10;	// u32

const uint32_t MAX_SCENE_COUNT = 16;

// PxHitFlag
const uint16_t HIT_POSITION = 1 << 0;
const uint16_t HIT_NORMAL   = 1 << 1;
const uint16_t HIT_DISTANCE = 1 << 2;

// PxQueryFlag
const uint16_t QUERY_STATIC  = 1 << 0;
const uint16_t QUERY_DYNAMIC = 1 << 1;

// The structures below mirror PhysX 3.4 types (PxQueryReport.h, PxQueryFiltering.h),
// including their inheritance, so the compiler lays them out the same way.

struct vec3_t
{
	float x;
	float y;
	float z;
};

struct actor_shape_t
{
	void *actor;
	void *shape;
};

struct query_hit_t : actor_shape_t
{
	uint32_t face_index;
};

struct location_hit_t : query_hit_t
{
	uint16_t flags;
	vec3_t   position;
	vec3_t   normal;
	float    distance;
};

struct raycast_hit_t : location_hit_t
{
	float u;
	float v;
};

/**
 * @brief PxHitCallback<PxRaycastHit> without a touch buffer: reports the closest hit only.
 */
struct raycast_callback_t
{
	raycast_hit_t  block;
	bool           has_block;
	raycast_hit_t *touches;
	uint32_t       max_touch_count;
	uint32_t       touch_count;

	raycast_callback_t(void)
		: has_block(false)
		, touches(NULL)
		, max_touch_count(0)
		, touch_count(0)
	{
		memset(&block, 0, sizeof(block));
		block.face_index = 0xFFFFFFFF;
		block.distance = FLT_MAX;
	}

	virtual bool process_touches(const raycast_hit_t *, uint32_t) { return false; }
	virtual void finalize_query(void) {}
	virtual ~raycast_callback_t(void) {}
};

struct query_filter_t
{
	uint32_t words[4];
	uint16_t flags;
	uint8_t  client;
};

static_assert(sizeof(raycast_hit_t) == 64, "raycast hit layout differs from PhysX");
static_assert(sizeof(raycast_callback_t) == 96, "raycast callback layout differs from PhysX");
static_assert(sizeof(query_filter_t) == 20, "query filter layout differs from PhysX");

/**
 * @brief NpSceneQueries::raycast. The hit flags are passed by address because PxFlags has a copy constructor.
 */
typedef bool (*raycast_function_t)(
	void *scene,
	const vec3_t *origin,
	const vec3_t *direction,
	float distance,
	raycast_callback_t *callback,
	const uint16_t *hit_flags,
	const query_filter_t *filter,
	void *filter_callback,
	const void *cache
);

// How far around the truck's own chunk the origin is searched for (chunks).
const int LOCATE_RADIUS = 80;

// Vertical extent of the rays used to find the truck (m above and below its origin).
const float LOCATE_ABOVE = 8.0f;
const float LOCATE_BELOW = 4.0f;

// Distance between the points probed along the truck (m).
const float LOCATE_PROBE_SPACING = 1.5f;

/**
 * @brief Builds the box the game searches for map items in.
 *
 * The box is square: its half side is half the larger of size X and Z, plus a little.
 *
 * @param position Three floats and two 16-bit chunk indices, as at the start of placement_t.
 * @param size Three floats.
 */
typedef void (*make_activation_box_t)(void *unused, void *box, const void *position, const float *size);

/**
 * @brief Creates the collision of the map items inside the box, unless it exists already.
 *
 * The game calls this every frame for the player's vehicle; collision nothing asked
 * for is removed again.
 */
typedef void (*activate_collision_t)(void *world, const void *box, uint8_t flag);

uint8_t          **instance_global = NULL;
raycast_function_t raycast_function = NULL;

uint8_t             **world_global = NULL;
make_activation_box_t make_activation_box = NULL;
activate_collision_t  activate_collision = NULL;

bool faulted = false;
bool failure_logged = false;

bool origin_known = false;
int  origin_chunk_x = 0;
int  origin_chunk_z = 0;

size_t scenes(void **const result)
{
	uint8_t *instance = NULL;
	void **items = NULL;
	uint32_t count = 0;
	const bool readable =
		memory::read(reinterpret_cast<const uint8_t *>(instance_global), instance) &&
		memory::read(instance + PHYSICS_SCENES_ITEMS, items) &&
		memory::read(instance + PHYSICS_SCENES_COUNT, count)
	;
	if (! readable || ! instance || ! items || (count > MAX_SCENE_COUNT)) {
		return 0;
	}
	if (! memory::safe_copy(result, items, count * sizeof(void *))) {
		return 0;
	}
	return count;
}

/**
 * @brief Calls into the game. Returns 1 for a hit, 0 for a miss, -1 if the call crashed.
 */
int guarded_raycast(void *const scene, const vec3_t *const origin, const vec3_t *const direction, const float distance, raycast_callback_t *const callback, const query_filter_t *const filter)
{
	const uint16_t hit_flags = HIT_POSITION | HIT_NORMAL | HIT_DISTANCE;
	__try {
		return raycast_function(scene, origin, direction, distance, callback, &hit_flags, filter, NULL, NULL) ? 1 : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

/**
 * @brief Casts a ray in physics coordinates through every scene.
 *
 * @param direction Unit vector.
 * @param[out] closest The nearest hit.
 */
bool raycast(const vec3_t &origin, const vec3_t &direction, const float distance, const uint16_t kinds, raycast_hit_t &closest)
{
	if (! raycast_function || faulted) {
		return false;
	}

	void *scene_list[MAX_SCENE_COUNT];
	const size_t scene_count = scenes(scene_list);

	query_filter_t filter = {};
	filter.flags = kinds;

	bool hit = false;
	for (size_t i = 0; i < scene_count; ++i) {
		if (! scene_list[i]) {
			continue;
		}
		raycast_callback_t callback;
		const int result = guarded_raycast(scene_list[i], &origin, &direction, distance, &callback, &filter);
		if (result < 0) {
			faulted = true;
			log_message(SCS_LOG_TYPE_error, "the game's raycast crashed, collision is disabled");
			return false;
		}
		if ((result > 0) && callback.has_block && (! hit || (callback.block.distance < closest.distance))) {
			hit = true;
			closest = callback.block;
		}
	}
	return hit;
}

/**
 * @brief Casts a ray straight down in physics coordinates.
 *
 * @param[out] height Height of the closest hit.
 */
bool raycast_down(const float x, const float y, const float z, const float distance, const uint16_t kinds, float &height)
{
	const vec3_t origin = { x, y, z };
	const vec3_t direction = { 0.0f, -1.0f, 0.0f };
	raycast_hit_t hit;
	if (! raycast(origin, direction, distance, kinds, hit)) {
		return false;
	}
	height = hit.position.y;
	return true;
}

/**
 * @brief Converts a world X or Z coordinate to physics space with the origin at the given chunk.
 */
float to_physics(const double world, const int origin_chunk)
{
	return static_cast<float>(world - origin_chunk * CHUNK_SIZE);
}

/**
 * @brief Whether the truck is where it should be if the physics origin was the given chunk.
 *
 * Probes three points along the truck's centre line for anything movable; the truck
 * itself is the only movable thing certain to be there.
 */
bool truck_found_at(const int chunk_x, const int chunk_z, const double truck_x, const double truck_y, const double truck_z, const float yaw)
{
	const float forward_x = -sinf(yaw);
	const float forward_z = -cosf(yaw);

	int hits = 0;
	for (int probe = -1; probe <= 1; ++probe) {
		const float along = static_cast<float>(probe) * LOCATE_PROBE_SPACING;
		float height = 0.0f;
		const bool hit = raycast_down(
			to_physics(truck_x, chunk_x) + forward_x * along,
			static_cast<float>(truck_y) + LOCATE_ABOVE,
			to_physics(truck_z, chunk_z) + forward_z * along,
			LOCATE_ABOVE + LOCATE_BELOW,
			QUERY_DYNAMIC,
			height
		);
		if (hit) {
			++hits;
		}
	}
	return hits >= 2;
}

/**
 * @brief Logs what the physics world does contain around the candidate origins, to
 * work out why the truck was not found.
 */
void log_locate_failure(const int truck_chunk_x, const int truck_chunk_z, const double truck_x, const double truck_y, const double truck_z)
{
	const int MAX_LOGGED = 8;
	const int WIDE_RADIUS = 4;
	const float WIDE_HEIGHT = 3000.0f;

	// Anything at all, at any height, close to the truck's own chunk.

	int logged = 0;
	for (int dx = -WIDE_RADIUS; dx <= WIDE_RADIUS; ++dx) {
		for (int dz = -WIDE_RADIUS; dz <= WIDE_RADIUS; ++dz) {
			float height = 0.0f;
			const float x = to_physics(truck_x, truck_chunk_x + dx);
			const float z = to_physics(truck_z, truck_chunk_z + dz);
			if (raycast_down(x, WIDE_HEIGHT, z, WIDE_HEIGHT * 2.0f, QUERY_STATIC | QUERY_DYNAMIC, height) && (logged < MAX_LOGGED)) {
				++logged;
				log_message(SCS_LOG_TYPE_message, "  origin (%+d, %+d) chunks from the truck: something at height %.2f (truck is at %.2f)", dx, dz, height, truck_y);
			}
		}
	}

	// Static geometry at the truck's height, for every origin tried.

	int static_hits = 0;
	logged = 0;
	for (int dx = -LOCATE_RADIUS; dx <= LOCATE_RADIUS; ++dx) {
		for (int dz = -LOCATE_RADIUS; dz <= LOCATE_RADIUS; ++dz) {
			float height = 0.0f;
			const float x = to_physics(truck_x, truck_chunk_x + dx);
			const float z = to_physics(truck_z, truck_chunk_z + dz);
			if (! raycast_down(x, static_cast<float>(truck_y) + LOCATE_ABOVE, z, LOCATE_ABOVE + LOCATE_BELOW, QUERY_STATIC, height)) {
				continue;
			}
			++static_hits;
			if (logged < MAX_LOGGED) {
				++logged;
				log_message(SCS_LOG_TYPE_message, "  origin (%+d, %+d) chunks from the truck: static geometry at height %.2f", dx, dz, height);
			}
		}
	}
	log_message(SCS_LOG_TYPE_message, "  %d origins have static geometry under the truck", static_hits);
}

} // namespace

bool physics_attach(void)
{
	instance_global = NULL;
	raycast_function = NULL;
	faulted = false;
	failure_logged = false;
	origin_known = false;

	uint8_t *const instance_instruction = memory::find_signature(INSTANCE_SIGNATURE);
	uint8_t *const raycast = memory::find_signature(RAYCAST_SIGNATURE);
	if (! instance_instruction || ! raycast) {
		log_message(
			SCS_LOG_TYPE_warning,
			"ground following is not available on this game build (physics instance %s, raycast %s)",
			instance_instruction ? "found" : "missing",
			raycast ? "found" : "missing"
		);
		return false;
	}

	instance_global = reinterpret_cast<uint8_t **>(memory::resolve_relative(instance_instruction, 3, 7));
	raycast_function = reinterpret_cast<raycast_function_t>(raycast);

	world_global = NULL;
	make_activation_box = NULL;
	activate_collision = NULL;
	uint8_t *const activation = memory::find_signature(ACTIVATION_SIGNATURE);
	if (activation) {
		make_activation_box = reinterpret_cast<make_activation_box_t>(memory::resolve_relative(activation + ACTIVATION_MAKE_BOX_CALL, 1, 5));
		world_global = reinterpret_cast<uint8_t **>(memory::resolve_relative(activation + ACTIVATION_WORLD_LOAD, 3, 7));
		activate_collision = reinterpret_cast<activate_collision_t>(memory::resolve_relative(activation + ACTIVATION_ACTIVATE_CALL, 1, 5));
	}
	else {
		log_message(SCS_LOG_TYPE_warning, "unable to find how the game activates collision, only what is near the truck will be solid");
	}
	return true;
}

/**
 * @brief Calls into the game. False if the call crashed.
 */
static bool guarded_activate(void *const world, const void *const position, const float *const size)
{
	__try {
		// Larger than the 40 bytes the game uses.

		__declspec(align(16)) uint8_t box[64] = {};
		make_activation_box(NULL, box, position, size);
		activate_collision(world, box, 0);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void physics_activate(const double x, const double y, const double z, const float radius)
{
	if (! activate_collision || faulted) {
		return;
	}
	uint8_t *world = NULL;
	if (! memory::read(reinterpret_cast<const uint8_t *>(world_global), world) || ! world) {
		return;
	}

	placement_t position = {};
	set_world_position(position, x, y, z);
	const float size[3] = { radius * 2.0f, 2.0f, radius * 2.0f };
	if (! guarded_activate(world, &position, size)) {
		activate_collision = NULL;
		log_message(SCS_LOG_TYPE_error, "the game's collision activation crashed, it will not be used again");
	}
}

bool physics_locate(const double truck_x, const double truck_y, const double truck_z, const float yaw)
{
	if (! raycast_function || faulted) {
		return false;
	}
	if (origin_known && truck_found_at(origin_chunk_x, origin_chunk_z, truck_x, truck_y, truck_z, yaw)) {
		return true;
	}
	origin_known = false;

	void *scene_list[MAX_SCENE_COUNT];
	const size_t scene_count = scenes(scene_list);
	if (scene_count == 0) {
		log_message(SCS_LOG_TYPE_warning, "no physics scene found");
		return false;
	}

	// Physics might simply use world coordinates.

	if (truck_found_at(0, 0, truck_x, truck_y, truck_z, yaw)) {
		origin_known = true;
		origin_chunk_x = 0;
		origin_chunk_z = 0;
	}

	// Otherwise try origins in growing squares around the chunk the truck is in.

	const int truck_chunk_x = static_cast<int>(floor(truck_x / CHUNK_SIZE + 0.5));
	const int truck_chunk_z = static_cast<int>(floor(truck_z / CHUNK_SIZE + 0.5));
	for (int radius = 0; ! origin_known && ! faulted && (radius <= LOCATE_RADIUS); ++radius) {
		for (int dx = -radius; ! origin_known && (dx <= radius); ++dx) {
			for (int dz = -radius; dz <= radius; ++dz) {
				if ((abs(dx) != radius) && (abs(dz) != radius)) {
					continue;
				}
				if (truck_found_at(truck_chunk_x + dx, truck_chunk_z + dz, truck_x, truck_y, truck_z, yaw)) {
					origin_known = true;
					origin_chunk_x = truck_chunk_x + dx;
					origin_chunk_z = truck_chunk_z + dz;
					break;
				}
			}
		}
	}

	if (! origin_known) {
		log_message(SCS_LOG_TYPE_warning, "unable to find the truck in the physics world (%zu scenes), ground following is off", scene_count);
		if (! faulted && ! failure_logged) {
			failure_logged = true;
			log_locate_failure(truck_chunk_x, truck_chunk_z, truck_x, truck_y, truck_z);
		}
		return false;
	}

	float ground = 0.0f;
	const bool ground_found = physics_ground_height(truck_x, truck_z, truck_y + LOCATE_ABOVE, LOCATE_ABOVE + LOCATE_BELOW, ground);
	log_message(
		SCS_LOG_TYPE_message,
		"physics origin is chunk (%d, %d), truck chunk (%d, %d), %zu scenes, truck height %.2f, ground under it %s %.2f",
		origin_chunk_x, origin_chunk_z, truck_chunk_x, truck_chunk_z, scene_count,
		truck_y, ground_found ? "at" : "not found", ground
	);
	return true;
}

size_t physics_scenes(void **const result, const size_t capacity)
{
	void *scene_list[MAX_SCENE_COUNT];
	size_t count = scenes(scene_list);
	if (count > capacity) {
		count = capacity;
	}
	memcpy(result, scene_list, count * sizeof(void *));
	return count;
}

bool physics_origin(int &chunk_x, int &chunk_z)
{
	chunk_x = origin_chunk_x;
	chunk_z = origin_chunk_z;
	return origin_known;
}

bool physics_obstacle(const double x, const double y, const double z, const float direction_x, const float direction_z, const float distance, obstacle_t &obstacle)
{
	if (! origin_known) {
		return false;
	}
	const vec3_t origin = { to_physics(x, origin_chunk_x), static_cast<float>(y), to_physics(z, origin_chunk_z) };
	const vec3_t direction = { direction_x, 0.0f, direction_z };
	raycast_hit_t hit;
	if (! raycast(origin, direction, distance, QUERY_STATIC | QUERY_DYNAMIC, hit)) {
		return false;
	}
	obstacle.distance = hit.distance;
	obstacle.normal[0] = hit.normal.x;
	obstacle.normal[1] = hit.normal.y;
	obstacle.normal[2] = hit.normal.z;
	return true;
}

bool physics_ground_height(const double x, const double z, const double from_y, const float distance, float &height)
{
	if (! origin_known) {
		return false;
	}
	return raycast_down(
		to_physics(x, origin_chunk_x),
		static_cast<float>(from_y),
		to_physics(z, origin_chunk_z),
		distance,
		QUERY_STATIC,
		height
	);
}

} // namespace game
