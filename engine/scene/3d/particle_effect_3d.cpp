/**************************************************************************/
/*  particle_effect_3d.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_effect_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "servers/audio/audio_stream.h"
#include "scene/3d/camera_shake_3d.h"
#include "scene/3d/radial_damage_3d.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/gpu_particles_3d.h"
#include "core/math/convex_hull.h"
#include "core/object/callable_mp.h"
#include "core/object/worker_thread_pool.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/height_map_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/3d/world_boundary_shape_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/image_texture.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/resources/curve.h"
#include "scene/resources/packed_scene.h"

bool ParticleEffect3D::is_emitter(const Node *p_node) {
	return Object::cast_to<GPUParticles3D>(p_node) || Object::cast_to<CPUParticles3D>(p_node);
}

ParticleEffect3D::EventKind ParticleEffect3D::get_event_kind(const Node *p_node) {
	if (Object::cast_to<Light3D>(p_node)) {
		return EVENT_LIGHT;
	}
	if (Object::cast_to<AudioStreamPlayer3D>(p_node)) {
		return EVENT_SOUND;
	}
	if (Object::cast_to<CameraShake3D>(p_node)) {
		return EVENT_CAMERA_SHAKE;
	}
	if (Object::cast_to<Decal>(p_node)) {
		return EVENT_DECAL;
	}
	if (Object::cast_to<RadialDamage3D>(p_node)) {
		return EVENT_DAMAGE;
	}
	return EVENT_NONE;
}

bool ParticleEffect3D::is_track(const Node *p_node) {
	return is_emitter(p_node) || get_event_kind(p_node) != EVENT_NONE;
}

// Active length of an event; <= 0 means it stays active (continuous).
static double get_event_duration(const Node *p_node, ParticleEffect3D::EventKind p_kind) {
	switch (p_kind) {
		case ParticleEffect3D::EVENT_SOUND: {
			const AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(p_node);
			const double length = player && player->get_stream().is_valid() ? player->get_stream()->get_length() : 0.0;
			return length > 0.0 ? length / MAX(0.01, double(player->get_pitch_scale())) : 0.5;
		}
		case ParticleEffect3D::EVENT_CAMERA_SHAKE:
			return Object::cast_to<CameraShake3D>(p_node)->get_duration();
		case ParticleEffect3D::EVENT_DAMAGE:
			return 0.15; // Instant; a short bar keeps it visible in the timeline.
		default:
			return ParticleEffect3D::get_emitter_duration(p_node);
	}
}

void ParticleEffect3D::get_track_window(const Node *p_node, double &r_start, double &r_active_end, double &r_visual_end) {
	if (is_emitter(p_node)) {
		get_emitter_window(p_node, r_start, r_active_end, r_visual_end);
		return;
	}
	r_start = get_emitter_delay(p_node);
	const double duration = get_event_duration(p_node, get_event_kind(p_node));
	r_active_end = duration > 0.0 ? r_start + duration : -1.0;
	r_visual_end = r_active_end;
}

static bool is_track_enabled(const Node *p_node) {
	// Hidden emitters are muted: they neither play nor extend the effect.
	const Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	return !node_3d || node_3d->is_visible();
}

void ParticleEffect3D::set_emitter_delay(Node *p_emitter, double p_delay) {
	ERR_FAIL_NULL(p_emitter);
	if (p_delay <= 0.0) {
		p_emitter->remove_meta(META_DELAY);
	} else {
		p_emitter->set_meta(META_DELAY, p_delay);
	}
}

double ParticleEffect3D::get_emitter_delay(const Node *p_emitter) {
	ERR_FAIL_NULL_V(p_emitter, 0.0);
	return MAX(0.0, double(p_emitter->get_meta(META_DELAY, 0.0)));
}

void ParticleEffect3D::set_emitter_duration(Node *p_emitter, double p_duration) {
	ERR_FAIL_NULL(p_emitter);
	if (p_duration <= 0.0) {
		p_emitter->remove_meta(META_DURATION);
	} else {
		p_emitter->set_meta(META_DURATION, p_duration);
	}
}

double ParticleEffect3D::get_emitter_duration(const Node *p_emitter) {
	ERR_FAIL_NULL_V(p_emitter, 0.0);
	return MAX(0.0, double(p_emitter->get_meta(META_DURATION, 0.0)));
}

void ParticleEffect3D::get_emitter_window(const Node *p_emitter, double &r_start, double &r_emission_end, double &r_visual_end) {
	r_start = get_emitter_delay(p_emitter);
	const double duration = get_emitter_duration(p_emitter);
	const double lifetime = MAX(0.001, double(p_emitter->get("lifetime")));
	const bool one_shot = bool(p_emitter->get("one_shot"));
	if (duration > 0.0) {
		r_emission_end = r_start + duration;
	} else if (one_shot) {
		// A one-shot emitter releases its particles over one lifetime cycle.
		r_emission_end = r_start + lifetime * (1.0 - CLAMP(double(p_emitter->get("explosiveness")), 0.0, 1.0)) + 0.001;
	} else {
		r_emission_end = -1.0;
	}
	r_visual_end = r_emission_end < 0.0 ? -1.0 : r_emission_end + lifetime;
}

void ParticleEffect3D::_set_emitter_emitting(Node *p_emitter, bool p_emitting, bool p_restart) {
	if (p_restart) {
		p_emitter->call("restart", false);
	}
	p_emitter->set("emitting", p_emitting);
}

void ParticleEffect3D::_collect_emitters(Node *p_node) {
	if (is_emitter(p_node) && is_track_enabled(p_node)) {
		EmitterState state;
		state.id = p_node->get_instance_id();
		state.delay = get_emitter_delay(p_node);
		state.duration = get_emitter_duration(p_node);
		state.lifetime = MAX(0.001, double(p_node->get("lifetime")));
		state.one_shot = bool(p_node->get("one_shot"));
		emitters.push_back(state);
	} else if (get_event_kind(p_node) != EVENT_NONE && is_track_enabled(p_node)) {
		EventState event;
		event.id = p_node->get_instance_id();
		event.kind = get_event_kind(p_node);
		event.delay = get_emitter_delay(p_node);
		event.duration = get_event_duration(p_node, event.kind);
		events.push_back(event);
		// Remember the authored values once; the effect animates them.
		if (!event_bases.has(event.id)) {
			EventBase base;
			if (Light3D *light = Object::cast_to<Light3D>(p_node)) {
				base.energy = light->get_param(Light3D::PARAM_ENERGY);
			} else if (Decal *decal = Object::cast_to<Decal>(p_node)) {
				base.alpha = decal->get_modulate().a;
			} else if (AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(p_node)) {
				base.pitch = player->get_pitch_scale();
			}
			event_bases.insert(event.id, base);
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_emitters(p_node->get_child(i));
	}
}

double ParticleEffect3D::_get_max_lifetime() const {
	double result = 0.0;
	for (const EmitterState &state : emitters) {
		result = MAX(result, state.lifetime);
	}
	return result;
}

double ParticleEffect3D::get_total_duration() const {
	double total = 0.0;
	LocalVector<Node *> stack;
	stack.push_back(const_cast<ParticleEffect3D *>(this));
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		if (is_track(node) && is_track_enabled(node)) {
			double start, emission_end, visual_end;
			get_track_window(node, start, emission_end, visual_end);
			if (visual_end < 0.0) {
				return -1.0;
			}
			total = MAX(total, visual_end);
		}
		for (int i = 0; i < node->get_child_count(); i++) {
			stack.push_back(node->get_child(i));
		}
	}
	return total;
}

void ParticleEffect3D::play() {
	if (!is_inside_tree()) {
		play_pending = true;
		return;
	}
	play_pending = false;
	emitters.clear();
	events.clear();
	_collect_emitters(this);
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (emitter) {
			_set_emitter_emitting(emitter, false, true);
		}
	}
	if (!Engine::get_singleton()->is_editor_hint()) {
		_update_level_collision();
	}
	elapsed = 0.0;
	stop_time = 0.0;
	stopping = false;
	playing = true;
	set_process_internal(true);
	_update(0.0);
}

// --- Level collision --------------------------------------------------------
// GPU particles ignore physics bodies; they only collide with
// GPUParticlesCollision3D nodes. To make "collision" just work against walls
// that already have physics collision, the effect bakes the static shapes
// around it into a transient SDF collider when it plays.

static void push_face(Vector<Face3> &r_faces, const Transform3D &p_xform, const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c) {
	r_faces.push_back(Face3(p_xform.xform(p_a), p_xform.xform(p_b), p_xform.xform(p_c)));
}

static void push_oriented_face(Vector<Face3> &r_faces, const Transform3D &p_xform, const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Vector3 &p_normal) {
	if (Face3(p_a, p_b, p_c).get_plane().normal.dot(p_normal) < 0.0) {
		push_face(r_faces, p_xform, p_a, p_c, p_b);
	} else {
		push_face(r_faces, p_xform, p_a, p_b, p_c);
	}
}

static void push_mesh_faces(Vector<Face3> &r_faces, const Transform3D &p_xform, const Ref<Mesh> &p_mesh) {
	if (p_mesh.is_null()) {
		return;
	}
	const Vector<Face3> faces = p_mesh->get_faces();
	for (const Face3 &face : faces) {
		push_face(r_faces, p_xform, face.vertex[0], face.vertex[1], face.vertex[2]);
	}
}

static void push_shape_faces(Vector<Face3> &r_faces, const Transform3D &p_xform, const Ref<Shape3D> &p_shape) {
	if (const BoxShape3D *box = Object::cast_to<BoxShape3D>(p_shape.ptr())) {
		Ref<BoxMesh> mesh;
		mesh.instantiate();
		mesh->set_size(box->get_size());
		push_mesh_faces(r_faces, p_xform, mesh);
	} else if (const SphereShape3D *sphere = Object::cast_to<SphereShape3D>(p_shape.ptr())) {
		Ref<SphereMesh> mesh;
		mesh.instantiate();
		mesh->set_radius(sphere->get_radius());
		mesh->set_height(sphere->get_radius() * 2.0);
		mesh->set_radial_segments(16);
		mesh->set_rings(8);
		push_mesh_faces(r_faces, p_xform, mesh);
	} else if (const CapsuleShape3D *capsule = Object::cast_to<CapsuleShape3D>(p_shape.ptr())) {
		Ref<CapsuleMesh> mesh;
		mesh.instantiate();
		mesh->set_radius(capsule->get_radius());
		mesh->set_height(capsule->get_height());
		mesh->set_radial_segments(16);
		mesh->set_rings(4);
		push_mesh_faces(r_faces, p_xform, mesh);
	} else if (const CylinderShape3D *cylinder = Object::cast_to<CylinderShape3D>(p_shape.ptr())) {
		Ref<CylinderMesh> mesh;
		mesh.instantiate();
		mesh->set_top_radius(cylinder->get_radius());
		mesh->set_bottom_radius(cylinder->get_radius());
		mesh->set_height(cylinder->get_height());
		mesh->set_radial_segments(16);
		mesh->set_rings(1);
		push_mesh_faces(r_faces, p_xform, mesh);
	} else if (const ConvexPolygonShape3D *convex = Object::cast_to<ConvexPolygonShape3D>(p_shape.ptr())) {
		Geometry3D::MeshData hull;
		if (ConvexHullComputer::convex_hull(convex->get_points(), hull) != OK) {
			return;
		}
		for (const Geometry3D::MeshData::Face &face : hull.faces) {
			for (uint32_t i = 2; i < face.indices.size(); i++) {
				push_oriented_face(r_faces, p_xform, hull.vertices[face.indices[0]], hull.vertices[face.indices[i - 1]], hull.vertices[face.indices[i]], face.plane.normal);
			}
		}
	} else if (const ConcavePolygonShape3D *concave = Object::cast_to<ConcavePolygonShape3D>(p_shape.ptr())) {
		const Vector<Vector3> faces = concave->get_faces();
		for (int i = 0; i + 2 < faces.size(); i += 3) {
			push_face(r_faces, p_xform, faces[i], faces[i + 1], faces[i + 2]);
		}
	} else if (const HeightMapShape3D *height_map = Object::cast_to<HeightMapShape3D>(p_shape.ptr())) {
		const int width = height_map->get_map_width();
		const int depth = height_map->get_map_depth();
		const Vector<real_t> data = height_map->get_map_data();
		if (width < 2 || depth < 2 || data.size() < width * depth) {
			return;
		}
		const Vector3 offset((width - 1) * 0.5, 0.0, (depth - 1) * 0.5);
		for (int z = 0; z + 1 < depth; z++) {
			for (int x = 0; x + 1 < width; x++) {
				const Vector3 a = Vector3(x, data[z * width + x], z) - offset;
				const Vector3 b = Vector3(x + 1, data[z * width + x + 1], z) - offset;
				const Vector3 c = Vector3(x, data[(z + 1) * width + x], z + 1) - offset;
				const Vector3 d = Vector3(x + 1, data[(z + 1) * width + x + 1], z + 1) - offset;
				push_oriented_face(r_faces, p_xform, a, b, c, Vector3(0, 1, 0));
				push_oriented_face(r_faces, p_xform, b, d, c, Vector3(0, 1, 0));
			}
		}
	} else if (const WorldBoundaryShape3D *boundary = Object::cast_to<WorldBoundaryShape3D>(p_shape.ptr())) {
		const Plane plane = boundary->get_plane();
		const Vector3 center = plane.get_center();
		Vector3 tangent = plane.normal.cross(Math::abs(plane.normal.y) < 0.9 ? Vector3(0, 1, 0) : Vector3(1, 0, 0)).normalized() * 500.0;
		const Vector3 bitangent = plane.normal.cross(tangent);
		const Vector3 a = center - tangent - bitangent;
		const Vector3 b = center + tangent - bitangent;
		const Vector3 c = center + tangent + bitangent;
		const Vector3 d = center - tangent + bitangent;
		push_oriented_face(r_faces, p_xform, a, b, c, plane.normal);
		push_oriented_face(r_faces, p_xform, a, c, d, plane.normal);
	}
}

static bool emitter_uses_collision(const Node *p_node) {
	const GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_node);
	if (!gpu || !gpu->is_visible_in_tree()) {
		return false;
	}
	Ref<Material> material = gpu->get_process_material();
	if (material.is_null()) {
		return false;
	}
	bool valid = false;
	const Variant mode = material->get("collision_mode", &valid);
	return valid && int(mode) != 0;
}

bool ParticleEffect3D::_get_level_collision_region(AABB &r_region) const {
	bool found = false;
	for (const EmitterState &state : emitters) {
		const GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(ObjectDB::get_instance(state.id));
		if (!emitter_uses_collision(gpu)) {
			continue;
		}
		const AABB region = gpu->get_global_transform().xform(gpu->get_visibility_aabb());
		if (found) {
			r_region.merge_with(region);
		} else {
			r_region = region;
			found = true;
		}
	}
	if (!found) {
		return false;
	}
	r_region = r_region.grow(0.5);
	// Keep the SDF resolution useful: limit the baked area around the effect.
	const Vector3 center = r_region.get_center();
	r_region.size = r_region.size.min(Vector3(64, 64, 64));
	r_region.position = center - r_region.size * 0.5;
	return true;
}

void ParticleEffect3D::update_level_collision() {
	if (!playing) {
		emitters.clear();
		events.clear();
		_collect_emitters(this);
	}
	_update_level_collision();
}

namespace {
struct LevelCollisionBake {
	GPUParticlesCollisionSDF3D *baker = nullptr; // Not in the tree; owned by the bake.
	Vector<Face3> faces;
	Ref<Image> image;
	RID space;
	AABB region;
	ObjectID effect;
};

struct LevelCollisionCacheEntry {
	RID space;
	AABB region;
	Ref<Texture3D> texture;
	uint64_t used = 0;
};

// Effects replayed in the same place (a looping fire, repeated explosions)
// share one bake.
LocalVector<LevelCollisionCacheEntry> level_collision_cache;
uint64_t level_collision_clock = 0;
constexpr uint32_t LEVEL_COLLISION_CACHE_SIZE = 16;

Ref<Texture3D> find_cached_level_collision(const RID &p_space, const AABB &p_region) {
	for (LevelCollisionCacheEntry &entry : level_collision_cache) {
		if (entry.space == p_space && entry.region == p_region) {
			entry.used = ++level_collision_clock;
			return entry.texture;
		}
	}
	return Ref<Texture3D>();
}

void cache_level_collision(const RID &p_space, const AABB &p_region, const Ref<Texture3D> &p_texture) {
	if (level_collision_cache.size() >= LEVEL_COLLISION_CACHE_SIZE) {
		uint32_t oldest = 0;
		for (uint32_t i = 1; i < level_collision_cache.size(); i++) {
			if (level_collision_cache[i].used < level_collision_cache[oldest].used) {
				oldest = i;
			}
		}
		level_collision_cache.remove_at_unordered(oldest);
	}
	LevelCollisionCacheEntry entry;
	entry.space = p_space;
	entry.region = p_region;
	entry.texture = p_texture;
	entry.used = ++level_collision_clock;
	level_collision_cache.push_back(entry);
}

Ref<Texture3D> sdf_image_to_texture(const Ref<Image> &p_image) {
	if (p_image.is_null()) {
		return Ref<Texture3D>();
	}
	const int depth = MAX(1, int(p_image->get_meta("depth", 1)));
	const int width = p_image->get_width();
	const int height = p_image->get_height() / depth;
	Vector<Ref<Image>> slices;
	slices.resize(depth);
	for (int z = 0; z < depth; z++) {
		slices.write[z] = p_image->get_region(Rect2i(0, z * height, width, height));
	}
	Ref<ImageTexture3D> texture;
	texture.instantiate();
	if (texture->create(p_image->get_format(), width, height, depth, false, slices) != OK) {
		return Ref<Texture3D>();
	}
	return texture;
}

void level_collision_bake_task(void *p_userdata) {
	LevelCollisionBake *bake = static_cast<LevelCollisionBake *>(p_userdata);
	bake->image = bake->baker->bake_faces(bake->faces);
	callable_mp_static(&ParticleEffect3D::_level_collision_bake_done).call_deferred(int64_t(p_userdata));
}
} // namespace

void ParticleEffect3D::clear_level_collision_cache() {
	level_collision_cache.clear();
}

void ParticleEffect3D::_level_collision_bake_done(int64_t p_bake) {
	LevelCollisionBake *bake = reinterpret_cast<LevelCollisionBake *>(p_bake);
	const Ref<Texture3D> texture = sdf_image_to_texture(bake->image);
	memdelete(bake->baker);
	if (texture.is_valid()) {
		cache_level_collision(bake->space, bake->region, texture);
	}
	if (ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(bake->effect))) {
		if (effect->level_collision_pending && effect->level_collision_pending_region == bake->region) {
			effect->level_collision_pending = false;
			effect->_set_level_collision(bake->region, texture);
		}
	}
	memdelete(bake);
}

void ParticleEffect3D::_set_level_collision(const AABB &p_region, const Ref<Texture3D> &p_texture) {
	GPUParticlesCollisionSDF3D *sdf = Object::cast_to<GPUParticlesCollisionSDF3D>(ObjectDB::get_instance(level_collision_id));
	if (p_texture.is_null() || !collide_with_level || !is_inside_tree()) {
		if (sdf) {
			sdf->queue_free();
		}
		level_collision_id = ObjectID();
		level_collision_region = AABB();
		return;
	}
	if (!sdf) {
		sdf = memnew(GPUParticlesCollisionSDF3D);
		sdf->set_name("LevelParticleCollision");
		add_child(sdf, false, INTERNAL_MODE_BACK);
		sdf->set_as_top_level(true);
		level_collision_id = sdf->get_instance_id();
	}
	sdf->set_size(p_region.size);
	sdf->set_global_transform(Transform3D(Basis(), p_region.get_center()));
	sdf->set_texture(p_texture);
	level_collision_region = p_region;
}

void ParticleEffect3D::_update_level_collision() {
	level_collision_retry = false;
	AABB region;
	if (!collide_with_level || !is_inside_tree() || !_get_level_collision_region(region)) {
		level_collision_pending = false;
		_set_level_collision(AABB(), Ref<Texture3D>());
		return;
	}
	// Snap to a 1 m grid so nearby replays share a cached bake.
	const Vector3 center = region.get_center().snappedf(1.0);
	const Vector3 size = (region.size + Vector3(1, 1, 1)).ceil();
	region = AABB(center - size * 0.5, size);

	if (level_collision_id.is_valid() && level_collision_region == region) {
		return; // A looping or replayed effect that did not move keeps its bake.
	}
	if (level_collision_pending && level_collision_pending_region == region) {
		return;
	}
	Ref<World3D> world = get_world_3d();
	PhysicsDirectSpaceState3D *space = world.is_valid() ? world->get_direct_space_state() : nullptr;
	if (!space) {
		return;
	}
	const RID space_rid = world->get_space();
	const Ref<Texture3D> cached = find_cached_level_collision(space_rid, region);
	if (cached.is_valid()) {
		level_collision_pending = false;
		_set_level_collision(region, cached);
		return;
	}

	PhysicsServer3D *physics = PhysicsServer3D::get_singleton();
	const RID box = physics->box_shape_create();
	physics->shape_set_data(box, region.size * 0.5);
	PhysicsDirectSpaceState3D::ShapeParameters query;
	query.shape_rid = box;
	query.transform = Transform3D(Basis(), region.get_center());
	query.collision_mask = level_collision_mask;
	query.collide_with_areas = false;
	static const int MAX_RESULTS = 256;
	PhysicsDirectSpaceState3D::ShapeResult results[MAX_RESULTS];
	const int count = space->intersect_shape(query, results, MAX_RESULTS);
	physics->free_rid(box);

	// Faces in the SDF's local space (axis aligned, centred on the region).
	const Transform3D to_local = Transform3D(Basis(), -region.get_center());
	Vector<Face3> faces;
	HashSet<uint64_t> seen;
	for (int i = 0; i < count; i++) {
		Object *collider = results[i].collider;
		if (!collider) {
			continue;
		}
		const uint64_t key = (uint64_t(results[i].collider_id) * 31) ^ uint64_t(results[i].shape);
		if (seen.has(key)) {
			continue;
		}
		seen.insert(key);
		// Only static level geometry: characters, props and rigid bodies move.
		if (CollisionObject3D *body = Object::cast_to<CollisionObject3D>(collider)) {
			if (!Object::cast_to<StaticBody3D>(body)) {
				continue;
			}
			const uint32_t owner = body->shape_find_owner(results[i].shape);
			const Transform3D xform = to_local * body->get_global_transform() * body->shape_owner_get_transform(owner);
			for (int j = 0; j < body->shape_owner_get_shape_count(owner); j++) {
				if (body->shape_owner_get_shape_index(owner, j) == results[i].shape) {
					push_shape_faces(faces, xform, body->shape_owner_get_shape(owner, j));
				}
			}
		} else if (Node3D *node = Object::cast_to<Node3D>(collider)) {
			// CSG and similar nodes own their collision and expose meshes.
			if (node->has_method("get_meshes")) {
				const Array meshes = node->call("get_meshes");
				if (meshes.size() >= 2) {
					push_mesh_faces(faces, to_local * node->get_global_transform() * Transform3D(meshes[0]), meshes[1]);
				}
			}
		}
	}

	if (faces.size() < 2) {
		// Level bodies added this frame may not be queryable before the first
		// physics step; try once more then.
		if (Engine::get_singleton()->get_physics_frames() < 3) {
			level_collision_retry = true;
			set_physics_process_internal(true);
		}
		level_collision_pending = false;
		_set_level_collision(AABB(), Ref<Texture3D>());
		return;
	}
	print_verbose(vformat("ParticleEffect3D: baking level collision for %s from %d faces (%s).", get_name(), faces.size(), region));

	// Bake on a worker thread; the collider appears when it is ready.
	LevelCollisionBake *bake = memnew(LevelCollisionBake);
	bake->baker = memnew(GPUParticlesCollisionSDF3D);
	bake->baker->set_resolution(GPUParticlesCollisionSDF3D::RESOLUTION_64);
	bake->baker->set_size(region.size);
	bake->faces = faces;
	bake->space = space_rid;
	bake->region = region;
	bake->effect = get_instance_id();
	level_collision_pending = true;
	level_collision_pending_region = region;
	WorkerThreadPool::get_singleton()->add_native_task(&level_collision_bake_task, bake, false, "Particle level collision");
}

void ParticleEffect3D::set_collide_with_level(bool p_enable) {
	collide_with_level = p_enable;
	if (!p_enable) {
		level_collision_pending = false;
		_set_level_collision(AABB(), Ref<Texture3D>());
	}
}

bool ParticleEffect3D::is_colliding_with_level() const {
	return collide_with_level;
}

void ParticleEffect3D::set_level_collision_mask(uint32_t p_mask) {
	level_collision_mask = p_mask;
}

uint32_t ParticleEffect3D::get_level_collision_mask() const {
	return level_collision_mask;
}

void ParticleEffect3D::stop() {
	if (!playing) {
		return;
	}
	stopping = true;
	stop_time = elapsed;
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (emitter && !state.ended) {
			_set_emitter_emitting(emitter, false, false);
		}
		state.started = true;
		state.ended = true;
	}
}

void ParticleEffect3D::restart() {
	play();
}

void ParticleEffect3D::seek(double p_time) {
	play();
	if (p_time <= 0.0) {
		return;
	}
	elapsed = p_time;
	for (EventState &event : events) {
		// Don't fire sounds or shakes that were already due before the seek time.
		if (event.delay < p_time - 0.05) {
			event.started = true;
		}
	}
	_update_events();
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (!emitter || p_time < state.delay) {
			continue;
		}
		if (!state.started) {
			state.started = true;
			_set_emitter_emitting(emitter, true, true);
		}
		double run = p_time - state.delay;
		if (state.duration > 0.0 && run >= state.duration) {
			// Emission is over; simulate up to its end and let the tail age.
			state.ended = true;
			emitter->call("request_particles_process", run);
			_set_emitter_emitting(emitter, false, false);
		} else {
			emitter->call("request_particles_process", run);
		}
	}
}

bool ParticleEffect3D::is_playing() const {
	return playing;
}

double ParticleEffect3D::get_elapsed_time() const {
	return elapsed;
}

void ParticleEffect3D::_update(double p_delta) {
	if (!playing) {
		return;
	}
	elapsed += p_delta * speed_scale;
	double total = 0.0;
	bool infinite = false;
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (!emitter) {
			continue;
		}
		if (!state.started && elapsed >= state.delay) {
			state.started = true;
			_set_emitter_emitting(emitter, true, true);
		}
		if (state.started && !state.ended && state.duration > 0.0 && elapsed >= state.delay + state.duration) {
			state.ended = true;
			_set_emitter_emitting(emitter, false, false);
		}
		double start, emission_end, visual_end;
		get_emitter_window(emitter, start, emission_end, visual_end);
		if (visual_end < 0.0) {
			infinite = true;
		} else {
			total = MAX(total, visual_end);
		}
	}
	_update_events();
	for (const EventState &event : events) {
		if (event.duration <= 0.0) {
			infinite = true;
		} else {
			total = MAX(total, event.delay + event.duration);
		}
	}
	if (emitters.is_empty() && events.is_empty()) {
		// Nothing to play; never loop an empty effect (that would restart forever).
		_finish();
		return;
	}
	if (stopping) {
		if (elapsed >= stop_time + _get_max_lifetime()) {
			_finish();
		}
	} else if (!infinite && elapsed >= total) {
		// Loop only effects with a real duration, and never restart from within
		// play() itself (p_delta == 0), which would recurse.
		if (looping && total > 0.0 && p_delta > 0.0) {
			play();
		} else if (!looping || total <= 0.0) {
			_finish();
		}
	}
}

static real_t event_noise(uint64_t p_seed, double p_t) {
	const double i = Math::floor(p_t);
	const double f = p_t - i;
	auto hash = [](uint64_t p_x) -> real_t {
		p_x ^= p_x >> 33;
		p_x *= 0xff51afd7ed558ccdull;
		p_x ^= p_x >> 33;
		return real_t(p_x & 0xffff) / 65535.0;
	};
	const real_t a = hash(uint64_t(int64_t(i)) * 2654435761ull + p_seed);
	const real_t b = hash(uint64_t(int64_t(i) + 1) * 2654435761ull + p_seed);
	return a + (b - a) * real_t(f * f * (3.0 - 2.0 * f));
}

void ParticleEffect3D::_update_events() {
	for (EventState &event : events) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(event.id));
		if (!node) {
			continue;
		}
		const EventBase base = event_bases.has(event.id) ? event_bases[event.id] : EventBase();
		const double local = elapsed - event.delay;
		const bool continuous = event.duration <= 0.0;
		const bool inside = local >= 0.0 && (continuous || local <= event.duration);
		const double t = continuous ? 0.0 : CLAMP(local / event.duration, 0.0, 1.0);
		switch (event.kind) {
			case EVENT_LIGHT: {
				Light3D *light = Object::cast_to<Light3D>(node);
				real_t factor = 0.0;
				if (inside) {
					factor = 1.0;
					if (!continuous) {
						Ref<Curve> curve = node->get_meta(META_CURVE, Ref<Curve>());
						if (curve.is_valid()) {
							factor = curve->sample_baked(t);
						} else {
							// Default flash: quick attack, smooth decay.
							factor = t < 0.1 ? real_t(t / 0.1) : real_t(Math::pow(1.0 - (t - 0.1) / 0.9, 2.0));
						}
					}
					const real_t flicker = CLAMP(real_t(double(node->get_meta(META_FLICKER, 0.0))), 0.0, 1.0);
					if (flicker > 0.0) {
						factor *= 1.0 - flicker * event_noise(event.id, elapsed * 18.0);
					}
				}
				light->set_param(Light3D::PARAM_ENERGY, base.energy * MAX(real_t(0.0), factor));
			} break;
			case EVENT_DECAL: {
				Decal *decal = Object::cast_to<Decal>(node);
				real_t alpha = 0.0;
				if (inside) {
					alpha = 1.0;
					if (!continuous) {
						const double fade = CLAMP(double(node->get_meta(META_FADE, 0.5)), 0.01, 1.0);
						if (t > 1.0 - fade) {
							alpha = real_t((1.0 - t) / fade);
						}
					}
				}
				Color modulate = decal->get_modulate();
				modulate.a = base.alpha * alpha;
				decal->set_modulate(modulate);
			} break;
			case EVENT_SOUND: {
				if (!event.started && local >= 0.0) {
					event.started = true;
					AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(node);
					const real_t random = CLAMP(real_t(double(node->get_meta(META_PITCH_RANDOM, 0.0))), 0.0, 1.0);
					player->set_pitch_scale(MAX(real_t(0.01), base.pitch * (1.0 + random * real_t(Math::randf() * 2.0 - 1.0))));
					player->play();
				}
			} break;
			case EVENT_CAMERA_SHAKE: {
				if (!event.started && local >= 0.0) {
					event.started = true;
					Object::cast_to<CameraShake3D>(node)->shake();
				}
			} break;
			case EVENT_DAMAGE: {
				if (!event.started && local >= 0.0) {
					event.started = true;
					Object::cast_to<RadialDamage3D>(node)->apply_damage();
				}
			} break;
			default:
				break;
		}
	}
}

void ParticleEffect3D::_finish() {
	playing = false;
	stopping = false;
	set_process_internal(false);
	emit_signal(SceneStringName(finished));
	if (auto_free && !Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		queue_free();
	}
}

static bool has_emitter_descendant(const Node *p_node) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		const Node *child = p_node->get_child(i);
		if (ParticleEffect3D::is_track(child) || has_emitter_descendant(child)) {
			return true;
		}
	}
	return false;
}

PackedStringArray ParticleEffect3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!has_emitter_descendant(this)) {
		warnings.push_back(RTR("This effect has no emitters, so nothing will be visible. Add GPUParticles3D or CPUParticles3D children, or instance an effect scene saved from the Particle workspace."));
	}
	return warnings;
}

void ParticleEffect3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_CHILD_ORDER_CHANGED: {
			update_configuration_warnings();
		} break;
		case NOTIFICATION_READY: {
			if (play_pending || (autoplay && !Engine::get_singleton()->is_editor_hint())) {
				play();
			} else if (!Engine::get_singleton()->is_editor_hint()) {
				// Without autoplay the effect stays silent until play() is called.
				emitters.clear();
				events.clear();
				_collect_emitters(this);
				for (const EmitterState &state : emitters) {
					if (Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id))) {
						_set_emitter_emitting(emitter, false, false);
					}
				}
				// Lights and decals stay dark until the effect plays.
				elapsed = -1.0;
				_update_events();
				elapsed = 0.0;
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update(get_process_delta_time());
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			set_physics_process_internal(false);
			if (level_collision_retry) {
				_update_level_collision();
				level_collision_retry = false; // Only one retry.
			}
		} break;
	}
}

void ParticleEffect3D::set_autoplay(bool p_autoplay) {
	autoplay = p_autoplay;
}

bool ParticleEffect3D::is_autoplay() const {
	return autoplay;
}

void ParticleEffect3D::set_looping(bool p_looping) {
	looping = p_looping;
}

bool ParticleEffect3D::is_looping() const {
	return looping;
}

void ParticleEffect3D::set_auto_free(bool p_auto_free) {
	auto_free = p_auto_free;
}

bool ParticleEffect3D::is_auto_free() const {
	return auto_free;
}

void ParticleEffect3D::set_speed_scale(double p_speed_scale) {
	speed_scale = MAX(0.0, p_speed_scale);
}

double ParticleEffect3D::get_speed_scale() const {
	return speed_scale;
}

ParticleEffect3D *ParticleEffect3D::spawn(const Ref<PackedScene> &p_scene, Node *p_parent, const Transform3D &p_transform) {
	ERR_FAIL_COND_V(p_scene.is_null(), nullptr);
	ERR_FAIL_NULL_V(p_parent, nullptr);
	Node *instance = p_scene->instantiate();
	ERR_FAIL_NULL_V(instance, nullptr);
	ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(instance);
	if (!effect) {
		memdelete(instance);
		ERR_FAIL_V_MSG(nullptr, "The scene root is not a ParticleEffect3D. Save the effect from the Particle workspace.");
	}
	effect->set_auto_free(true);
	effect->set_autoplay(false);
	p_parent->add_child(effect);
	if (effect->is_inside_tree()) {
		effect->set_global_transform(p_transform);
	} else {
		effect->set_transform(p_transform);
	}
	effect->play();
	return effect;
}

void ParticleEffect3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_autoplay", "enable"), &ParticleEffect3D::set_autoplay);
	ClassDB::bind_method(D_METHOD("is_autoplay"), &ParticleEffect3D::is_autoplay);
	ClassDB::bind_method(D_METHOD("set_looping", "enable"), &ParticleEffect3D::set_looping);
	ClassDB::bind_method(D_METHOD("is_looping"), &ParticleEffect3D::is_looping);
	ClassDB::bind_method(D_METHOD("set_auto_free", "enable"), &ParticleEffect3D::set_auto_free);
	ClassDB::bind_method(D_METHOD("is_auto_free"), &ParticleEffect3D::is_auto_free);
	ClassDB::bind_method(D_METHOD("set_speed_scale", "scale"), &ParticleEffect3D::set_speed_scale);
	ClassDB::bind_method(D_METHOD("get_speed_scale"), &ParticleEffect3D::get_speed_scale);
	ClassDB::bind_method(D_METHOD("set_collide_with_level", "enable"), &ParticleEffect3D::set_collide_with_level);
	ClassDB::bind_method(D_METHOD("is_colliding_with_level"), &ParticleEffect3D::is_colliding_with_level);
	ClassDB::bind_method(D_METHOD("set_level_collision_mask", "mask"), &ParticleEffect3D::set_level_collision_mask);
	ClassDB::bind_method(D_METHOD("get_level_collision_mask"), &ParticleEffect3D::get_level_collision_mask);
	ClassDB::bind_method(D_METHOD("update_level_collision"), &ParticleEffect3D::update_level_collision);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("clear_level_collision_cache"), &ParticleEffect3D::clear_level_collision_cache);
	ClassDB::bind_method(D_METHOD("play"), &ParticleEffect3D::play);
	ClassDB::bind_method(D_METHOD("stop"), &ParticleEffect3D::stop);
	ClassDB::bind_method(D_METHOD("restart"), &ParticleEffect3D::restart);
	ClassDB::bind_method(D_METHOD("seek", "time"), &ParticleEffect3D::seek);
	ClassDB::bind_method(D_METHOD("is_playing"), &ParticleEffect3D::is_playing);
	ClassDB::bind_method(D_METHOD("get_elapsed_time"), &ParticleEffect3D::get_elapsed_time);
	ClassDB::bind_method(D_METHOD("get_total_duration"), &ParticleEffect3D::get_total_duration);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("set_emitter_delay", "emitter", "delay"), &ParticleEffect3D::set_emitter_delay);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("get_emitter_delay", "emitter"), &ParticleEffect3D::get_emitter_delay);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("set_emitter_duration", "emitter", "duration"), &ParticleEffect3D::set_emitter_duration);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("get_emitter_duration", "emitter"), &ParticleEffect3D::get_emitter_duration);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("spawn", "scene", "parent", "transform"), &ParticleEffect3D::spawn);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "is_autoplay");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "looping"), "set_looping", "is_looping");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_free"), "set_auto_free", "is_auto_free");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_scale", PROPERTY_HINT_RANGE, "0,8,0.01,or_greater"), "set_speed_scale", "get_speed_scale");
	ADD_GROUP("Level Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_level"), "set_collide_with_level", "is_colliding_with_level");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "level_collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_level_collision_mask", "get_level_collision_mask");

	ADD_SIGNAL(MethodInfo("finished"));
}

ParticleEffect3D::ParticleEffect3D() {
}
