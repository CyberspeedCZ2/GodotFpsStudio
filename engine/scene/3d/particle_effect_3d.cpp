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
#include "scene/3d/effect_spawner_3d.h"
#include "scene/3d/physics/animatable_body_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "core/math/convex_hull.h"
#include "core/os/os.h"
#include "core/templates/hashfuncs.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/particle_process_material.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
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
#include "scene/3d/camera_3d.h"
#include "core/config/project_settings.h"

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
	if (Object::cast_to<EffectSpawner3D>(p_node)) {
		return EVENT_SPAWN;
	}
	return EVENT_NONE;
}

bool ParticleEffect3D::is_track(const Node *p_node) {
	return is_emitter(p_node) || get_event_kind(p_node) != EVENT_NONE;
}

static bool is_looping_stream(const Ref<AudioStream> &p_stream) {
	if (p_stream.is_null()) {
		return false;
	}
	bool valid = false;
	const Variant mode = p_stream->get("loop_mode", &valid); // AudioStreamWAV
	if (valid) {
		return int(mode) != 0;
	}
	const Variant loop = p_stream->get("loop", &valid); // Ogg Vorbis, MP3
	return valid && bool(loop);
}

// Active length of an event; <= 0 means it stays active (continuous).
static double get_event_duration(const Node *p_node, ParticleEffect3D::EventKind p_kind) {
	switch (p_kind) {
		case ParticleEffect3D::EVENT_SOUND: {
			const AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(p_node);
			if (player && is_looping_stream(player->get_stream())) {
				return 0.0; // A looping sound plays until the effect stops.
			}
			const double length = player && player->get_stream().is_valid() ? player->get_stream()->get_length() : 0.0;
			return length > 0.0 ? length / MAX(0.01, double(player->get_pitch_scale())) : 0.5;
		}
		case ParticleEffect3D::EVENT_CAMERA_SHAKE:
			return Object::cast_to<CameraShake3D>(p_node)->get_duration();
		case ParticleEffect3D::EVENT_DAMAGE:
		case ParticleEffect3D::EVENT_SPAWN:
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

double ParticleEffect3D::get_emitter_speed(const Node *p_emitter) {
	ERR_FAIL_NULL_V(p_emitter, 1.0);
	const double speed = p_emitter->has_meta(META_AUTHORED_SPEED) ? double(p_emitter->get_meta(META_AUTHORED_SPEED)) : double(p_emitter->get("speed_scale"));
	return speed > 0.001 ? speed : 1.0;
}

void ParticleEffect3D::get_emitter_window(const Node *p_emitter, double &r_start, double &r_emission_end, double &r_visual_end) {
	r_start = get_emitter_delay(p_emitter);
	const double duration = get_emitter_duration(p_emitter);
	// An emitter with its own speed_scale lives that much shorter or longer
	// on the effect's clock.
	const double lifetime = MAX(0.001, double(p_emitter->get("lifetime"))) / get_emitter_speed(p_emitter);
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
		state.lifetime = MAX(0.001, double(p_node->get("lifetime"))) / get_emitter_speed(p_node);
		state.one_shot = bool(p_node->get("one_shot"));
		double start, emission_end;
		get_emitter_window(p_node, start, emission_end, state.visual_end);
		emitters.push_back(state);
	} else if (get_event_kind(p_node) != EVENT_NONE && is_track_enabled(p_node)) {
		EventState event;
		event.id = p_node->get_instance_id();
		event.kind = get_event_kind(p_node);
		event.delay = get_emitter_delay(p_node);
		event.duration = get_event_duration(p_node, event.kind);
		if (event.kind == EVENT_LIGHT) {
			event.curve = p_node->get_meta(META_CURVE, Ref<Curve>());
			event.flicker = CLAMP(real_t(double(p_node->get_meta(META_FLICKER, 0.0))), 0.0, 1.0);
		} else if (event.kind == EVENT_DECAL) {
			event.fade = CLAMP(double(p_node->get_meta(META_FADE, 0.5)), 0.01, 1.0);
		} else if (event.kind == EVENT_SOUND) {
			event.pitch_random = CLAMP(real_t(double(p_node->get_meta(META_PITCH_RANDOM, 0.0))), 0.0, 1.0);
		} else if (event.kind == EVENT_SPAWN) {
			// What an earlier editor preview spawned ends with that preview.
			Object::cast_to<EffectSpawner3D>(p_node)->clear_editor_spawned();
		}
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
	if (p_node == this) {
		_drop_sub_emitter_targets();
	}
}

void ParticleEffect3D::_drop_sub_emitter_targets() {
	// A GPUParticles3D used as another emitter's sub_emitter is driven by that
	// emitter; the timeline must not turn its own emission on. Its particles
	// outlive the parent's by its lifetime, which extends the parent's end.
	for (uint32_t i = 0; i < emitters.size(); i++) {
		const GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(ObjectDB::get_instance(emitters[i].id));
		if (!gpu || gpu->get_sub_emitter().is_empty()) {
			continue;
		}
		const GPUParticles3D *target = Object::cast_to<GPUParticles3D>(gpu->get_node_or_null(gpu->get_sub_emitter()));
		if (!target || target == gpu) {
			continue;
		}
		const ObjectID target_id = target->get_instance_id();
		for (uint32_t j = 0; j < emitters.size(); j++) {
			if (emitters[j].id != target_id) {
				continue;
			}
			if (emitters[i].visual_end >= 0.0) {
				emitters[i].visual_end += emitters[j].lifetime;
			}
			const_cast<GPUParticles3D *>(target)->set_emitting(false);
			emitters.remove_at(j);
			if (j < i) {
				i--;
			}
			break;
		}
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

void ParticleEffect3D::_play_pending() {
	if (play_pending && is_inside_tree()) {
		play();
	}
}

namespace {
constexpr int EFFECT_POOL_MAX_FREE = 64;
HashMap<ObjectID, LocalVector<ObjectID>> effect_pools; // PackedScene -> free effects.

// Effects playing in a running game, counted against
// rendering/particle_effects/max_active_effects.
LocalVector<ObjectID> budget_effects;
uint64_t effect_play_serial = 0;
constexpr double CULLED_LOOP_RECHECK = 0.5; // Seconds between distance checks of a culled looping effect.
} // namespace

void ParticleEffect3D::play() {
	if (!is_inside_tree()) {
		play_pending = true;
		return;
	}
	play_pending = false;
	culled = false;
	emitters.clear();
	events.clear();
	_collect_emitters(this);
	// Fire, steam, a looping alarm light: these wait when they cannot play
	// (distance, budget) and start again later instead of ending for good.
	resumable = looping;
	for (const EmitterState &state : emitters) {
		resumable = resumable || state.visual_end < 0.0;
	}
	for (const EventState &event : events) {
		resumable = resumable || (event.duration <= 0.0 && (event.kind == EVENT_LIGHT || event.kind == EVENT_SOUND));
	}
	if (_should_skip_play()) {
		_skip_play();
		return;
	}
	_apply_overrides();
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (emitter) {
			// Local space overrides the authored local_coords; the authored value
			// comes back when the (pooled) effect is played without it.
			static const StringName authored_meta = "_authored_local_coords";
			if (local_space && !bool(emitter->get_meta(META_KEEP_WORLD, false))) {
				if (!emitter->has_meta(authored_meta)) {
					emitter->set_meta(authored_meta, emitter->get("local_coords"));
				}
				emitter->set("local_coords", true);
			} else if (emitter->has_meta(authored_meta)) {
				emitter->set("local_coords", emitter->get_meta(authored_meta));
				emitter->remove_meta(authored_meta);
			}
			_set_emitter_emitting(emitter, false, true);
		}
	}
	if (!Engine::get_singleton()->is_editor_hint() || editor_preview) {
		_update_level_collision();
	}
	if (!Engine::get_singleton()->is_editor_hint()) {
		play_serial = ++effect_play_serial;
		if (!budget_active) {
			budget_effects.push_back(get_instance_id());
			budget_active = true;
		}
		_enforce_max_instances();
	}
	paused = false;
	_apply_speed_scale();
	lod_elapsed = 0.0;
	_apply_amount_scale();
	elapsed = 0.0;
	stop_time = 0.0;
	stop_end = 0.0;
	stopping = false;
	playing = true;
	set_process_internal(true);
	_update(0.0);
}

// --- Tint and scale -----------------------------------------------------------
// The authored values a play overrides are kept in a metadata Dictionary on
// each node (with the node's original process material); every play derives
// the values from them, and a neutral play restores them and drops the copies.

namespace {
// A plain C string: a namespace-scope StringName would be built before the
// StringName table exists.
constexpr const char *FX_AUTHORED_META = "_fx_authored";

Variant fx_scaled(const Variant &p_value, real_t p_scale) {
	switch (p_value.get_type()) {
		case Variant::FLOAT:
			return double(p_value) * p_scale;
		case Variant::VECTOR3:
			return Vector3(p_value) * p_scale;
		case Variant::AABB: {
			const AABB box = p_value;
			return AABB(box.position * p_scale, box.size * p_scale);
		}
		default:
			return p_value;
	}
}

// Applies one override on p_target, remembering the authored value under p_key.
void fx_override(Object *p_target, Dictionary &r_authored, const String &p_key, const StringName &p_property, real_t p_scale, const Color *p_tint) {
	bool valid = false;
	const Variant current = p_target->get(p_property, &valid);
	if (!valid) {
		return;
	}
	if (!r_authored.has(p_key)) {
		r_authored[p_key] = current;
	}
	const Variant authored = r_authored[p_key];
	if (p_tint) {
		if (authored.get_type() == Variant::COLOR) {
			const Color base = authored;
			// Keep alpha: lights/decals animate it, and tints are about hue.
			p_target->set(p_property, Color(base.r * p_tint->r, base.g * p_tint->g, base.b * p_tint->b, base.a * p_tint->a));
		}
	} else {
		p_target->set(p_property, fx_scaled(authored, p_scale));
	}
}

const char *FX_PROCESS_SCALE_PROPERTIES[] = {
	"scale_min", "scale_max", "initial_velocity_min", "initial_velocity_max", "gravity",
	"linear_accel_min", "linear_accel_max", "radial_accel_min", "radial_accel_max",
	"tangential_accel_min", "tangential_accel_max", "damping_min", "damping_max",
	"emission_sphere_radius", "emission_box_extents", "emission_ring_radius",
	"emission_ring_inner_radius", "emission_ring_height", "emission_shape_offset",
};
const char *FX_CPU_SCALE_PROPERTIES[] = {
	"scale_amount_min", "scale_amount_max", "initial_velocity_min", "initial_velocity_max", "gravity",
	"linear_accel_min", "linear_accel_max", "radial_accel_min", "radial_accel_max",
	"tangential_accel_min", "tangential_accel_max", "damping_min", "damping_max",
	"emission_sphere_radius", "emission_box_extents", "emission_ring_radius",
	"emission_ring_inner_radius", "emission_ring_height",
};
} // namespace

void ParticleEffect3D::_apply_overrides() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return; // The editor shows the authored effect (and must not save copies).
	}
	const Color play_tint = tint * spawn_tint;
	const real_t play_scale = effect_scale * spawn_scale;
	const bool neutral = play_tint.is_equal_approx(Color(1, 1, 1, 1)) && Math::is_equal_approx(play_scale, real_t(1.0));
	const bool tinted = !play_tint.is_equal_approx(Color(1, 1, 1, 1));
	LocalVector<Node *> stack;
	for (int i = 0; i < get_child_count(); i++) {
		stack.push_back(get_child(i));
	}
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		for (int i = 0; i < node->get_child_count(); i++) {
			stack.push_back(node->get_child(i));
		}
		const bool is_target = Object::cast_to<GPUParticles3D>(node) || Object::cast_to<CPUParticles3D>(node) || Object::cast_to<Light3D>(node) || Object::cast_to<Decal>(node) || Object::cast_to<CameraShake3D>(node) || Object::cast_to<RadialDamage3D>(node);
		if (!is_target) {
			continue;
		}
		if (neutral) {
			if (!node->has_meta(FX_AUTHORED_META)) {
				continue;
			}
			// Restore the authored look of an instance played with overrides before.
			const Dictionary authored = node->get_meta(FX_AUTHORED_META);
			if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(node)) {
				if (authored.has("@process_material")) {
					gpu->set_process_material(authored["@process_material"]);
				}
			}
			for (const KeyValue<Variant, Variant> &kv : authored) {
				const String key = kv.key;
				if (!key.begins_with("@") && !key.begins_with("pm/")) {
					node->set(StringName(key), kv.value);
				}
			}
			node->remove_meta(FX_AUTHORED_META);
			continue;
		}
		Dictionary authored = node->get_meta(FX_AUTHORED_META, Dictionary());
		if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(node)) {
			// A local copy of the process material carries this play's values.
			if (!authored.has("@process_material")) {
				authored["@process_material"] = gpu->get_process_material();
				Ref<ParticleProcessMaterial> original = gpu->get_process_material();
				if (original.is_valid()) {
					gpu->set_process_material(original->duplicate());
				}
			}
			Ref<ParticleProcessMaterial> material = gpu->get_process_material();
			if (material.is_valid()) {
				fx_override(material.ptr(), authored, "pm/color", "color", 1.0, &play_tint);
				for (const char *property : FX_PROCESS_SCALE_PROPERTIES) {
					fx_override(material.ptr(), authored, String("pm/") + property, property, play_scale, nullptr);
				}
			}
			fx_override(gpu, authored, "visibility_aabb", "visibility_aabb", play_scale, nullptr);
			fx_override(gpu, authored, "collision_base_size", "collision_base_size", play_scale, nullptr);
		} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(node)) {
			fx_override(cpu, authored, "color", "color", 1.0, &play_tint);
			for (const char *property : FX_CPU_SCALE_PROPERTIES) {
				fx_override(cpu, authored, property, property, play_scale, nullptr);
			}
		} else if (Light3D *light = Object::cast_to<Light3D>(node)) {
			fx_override(light, authored, "light_color", "light_color", 1.0, &play_tint);
			fx_override(light, authored, "omni_range", "omni_range", play_scale, nullptr);
			fx_override(light, authored, "spot_range", "spot_range", play_scale, nullptr);
		} else if (Decal *decal = Object::cast_to<Decal>(node)) {
			if (tinted || authored.has("modulate")) {
				// Only the color channels; the decal event animates alpha.
				if (!authored.has("modulate")) {
					authored["modulate"] = decal->get_modulate();
				}
				const Color base = authored["modulate"];
				const Color current = decal->get_modulate();
				decal->set_modulate(Color(base.r * play_tint.r, base.g * play_tint.g, base.b * play_tint.b, current.a));
			}
			fx_override(decal, authored, "size", "size", play_scale, nullptr);
		} else if (Object::cast_to<CameraShake3D>(node) || Object::cast_to<RadialDamage3D>(node)) {
			fx_override(node, authored, "inner_radius", "inner_radius", play_scale, nullptr);
			fx_override(node, authored, "radius", "radius", play_scale, nullptr);
		}
		node->set_meta(FX_AUTHORED_META, authored);
	}
}

void ParticleEffect3D::set_tint(const Color &p_tint) {
	tint = p_tint;
}

Color ParticleEffect3D::get_tint() const {
	return tint;
}

void ParticleEffect3D::set_effect_scale(real_t p_scale) {
	effect_scale = MAX(real_t(0.001), p_scale);
}

real_t ParticleEffect3D::get_effect_scale() const {
	return effect_scale;
}

// --- Budget and distance ----------------------------------------------------
// Effects with a damage event are gameplay and always play. Others are
// skipped when the camera is beyond cull_distance, and when the budget is
// full the oldest effect of the lowest priority (not above this one) is cut
// off to make room; if every active effect has a higher priority, this one
// is skipped. A skipped effect emits finished on the next frame (and returns
// to its pool); a looping one keeps checking and starts once it qualifies.

bool ParticleEffect3D::_should_skip_play() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}
	for (const EventState &event : events) {
		if (event.kind == EVENT_DAMAGE) {
			return false;
		}
	}
	if (cull_distance > 0.0) {
		Viewport *viewport = get_viewport();
		const Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
		if (camera && camera->get_global_position().distance_squared_to(get_global_position()) > cull_distance * cull_distance) {
			return true;
		}
	}
	if (budget_active) {
		return false; // Already counted (a replay or the next loop).
	}
	const int max_active = GLOBAL_GET_CACHED(int, "rendering/particle_effects/max_active_effects");
	if (max_active <= 0) {
		return false;
	}
	for (uint32_t i = 0; i < budget_effects.size(); i++) {
		ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(budget_effects[i]));
		if (!effect || !effect->is_inside_tree() || !effect->playing) {
			if (effect) {
				effect->budget_active = false;
			}
			budget_effects.remove_at_unordered(i);
			i--;
		}
	}
	if (int(budget_effects.size()) < max_active) {
		return false;
	}
	if (resuming) {
		return true; // Still full: keep waiting rather than push another effect out.
	}
	ParticleEffect3D *victim = nullptr;
	for (const ObjectID &id : budget_effects) {
		ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(id));
		if (!effect || effect->priority > priority || _has_pending_damage(effect->events)) {
			continue;
		}
		if (!victim || effect->priority < victim->priority || (effect->priority == victim->priority && effect->play_serial < victim->play_serial)) {
			victim = effect;
		}
	}
	if (!victim) {
		return true;
	}
	if (victim->resumable) {
		victim->_suspend(); // Fire or steam comes back once there is room.
	} else {
		victim->_force_finish();
	}
	return false;
}

bool ParticleEffect3D::_has_pending_damage(const LocalVector<EventState> &p_events) {
	for (const EventState &event : p_events) {
		if (event.kind == EVENT_DAMAGE && !event.started) {
			return true;
		}
	}
	return false;
}

void ParticleEffect3D::_silence() {
	// Emitters off, lights and decals dark (their window has not started).
	for (const EmitterState &state : emitters) {
		if (Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id))) {
			_set_emitter_emitting(emitter, false, false);
		}
	}
	elapsed = -1.0;
	_update_events();
	elapsed = 0.0;
}

void ParticleEffect3D::_skip_play() {
	_silence();
	emitters.clear();
	events.clear();
	culled = true;
	playing = true;
	stopping = false;
	elapsed = 0.0;
	set_process_internal(true);
}

void ParticleEffect3D::_enforce_max_instances() {
	if (max_instances <= 0) {
		return;
	}
	const String path = get_scene_file_path();
	if (path.is_empty()) {
		return;
	}
	// Every playing effect is in budget_effects (also with no global budget).
	while (true) {
		ParticleEffect3D *oldest = nullptr;
		int count = 0;
		for (const ObjectID &id : budget_effects) {
			ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(id));
			if (!effect || effect == this || !effect->playing || effect->culled || effect->get_scene_file_path() != path || _has_pending_damage(effect->events)) {
				continue;
			}
			count++;
			if (!oldest || effect->play_serial < oldest->play_serial) {
				oldest = effect;
			}
		}
		if (count < max_instances || !oldest) {
			return;
		}
		oldest->_force_finish();
	}
}

void ParticleEffect3D::_force_finish() {
	// Cut off at once (budget): clear live particles, darken lights/decals.
	for (const EmitterState &state : emitters) {
		if (Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id))) {
			emitter->call("restart", false);
			emitter->set("emitting", false);
		}
	}
	elapsed = -1.0;
	_update_events();
	_finish();
}

void ParticleEffect3D::_suspend() {
	// Recycled by the budget, but continuous: clear it like _force_finish()
	// and wait as a culled effect, which starts again when a slot is free.
	for (const EmitterState &state : emitters) {
		if (Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id))) {
			emitter->call("restart", false);
			emitter->set("emitting", false);
		}
	}
	_stop_looping_sounds();
	elapsed = -1.0;
	_update_events();
	_budget_remove();
	emitters.clear();
	events.clear();
	culled = true;
	playing = true;
	stopping = false;
	elapsed = 0.0;
	set_process_internal(true);
}

// The effect's speed_scale and pause reach the emitters too (their particles
// would otherwise keep their own pace while the timeline runs faster or
// stands still). Game only: the Particle preview drives emitter speeds itself.
void ParticleEffect3D::_apply_speed_scale() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	const double scale = paused ? 0.0 : speed_scale;
	const bool neutral = Math::is_equal_approx(scale, 1.0);
	for (const EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (!emitter) {
			continue;
		}
		if (neutral) {
			if (emitter->has_meta(META_AUTHORED_SPEED)) {
				emitter->set("speed_scale", emitter->get_meta(META_AUTHORED_SPEED));
				emitter->remove_meta(META_AUTHORED_SPEED);
			}
			continue;
		}
		if (!emitter->has_meta(META_AUTHORED_SPEED)) {
			emitter->set_meta(META_AUTHORED_SPEED, emitter->get("speed_scale"));
		}
		emitter->set("speed_scale", double(emitter->get_meta(META_AUTHORED_SPEED)) * scale);
	}
}

// Fewer particles for the quality setting and for effects far from the
// camera. Tiny emitters (a flash, a single ring) always keep all of theirs.
void ParticleEffect3D::_apply_amount_scale() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	static const StringName authored_meta = "_authored_amount_ratio";
	real_t ratio = CLAMP(real_t(GLOBAL_GET_CACHED(double, "rendering/particle_effects/amount_scale")), real_t(0.05), real_t(1.0));
	if (lod_distance > 0.0) {
		Viewport *viewport = get_viewport();
		const Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
		if (camera) {
			const real_t distance = camera->get_global_position().distance_to(get_global_position());
			const real_t far = CLAMP((distance - lod_distance) / lod_distance, real_t(0.0), real_t(1.0));
			ratio *= Math::lerp(real_t(1.0), lod_amount, far);
		}
	}
	const bool neutral = ratio > 0.999;
	for (const EmitterState &state : emitters) {
		GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(ObjectDB::get_instance(state.id));
		if (!gpu || gpu->get_amount() < 8) {
			continue;
		}
		if (neutral) {
			if (gpu->has_meta(authored_meta)) {
				gpu->set_amount_ratio(float(double(gpu->get_meta(authored_meta))));
				gpu->remove_meta(authored_meta);
			}
			continue;
		}
		if (!gpu->has_meta(authored_meta)) {
			gpu->set_meta(authored_meta, gpu->get_amount_ratio());
		}
		const float target = float(double(gpu->get_meta(authored_meta))) * ratio;
		if (!Math::is_equal_approx(gpu->get_amount_ratio(), target)) {
			gpu->set_amount_ratio(target);
		}
	}
}

void ParticleEffect3D::_budget_remove() {
	if (!budget_active) {
		return;
	}
	budget_active = false;
	const int64_t index = budget_effects.find(get_instance_id());
	if (index >= 0) {
		budget_effects.remove_at_unordered(index);
	}
}

int ParticleEffect3D::get_active_effect_count() {
	int count = 0;
	for (const ObjectID &id : budget_effects) {
		const ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(id));
		if (effect && effect->playing && effect->is_inside_tree()) {
			count++;
		}
	}
	return count;
}

namespace {

// Drops effects freed with an earlier level, so size checks count live ones.
void prune_pool(LocalVector<ObjectID> &r_pool) {
	for (uint32_t i = 0; i < r_pool.size(); i++) {
		const Node *effect = Object::cast_to<Node>(ObjectDB::get_instance(r_pool[i]));
		if (!effect || effect->is_queued_for_deletion()) {
			r_pool.remove_at_unordered(i);
			i--;
		}
	}
}
} // namespace

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

real_t ParticleEffect3D::get_suggested_collision_size(const GPUParticles3D *p_gpu) {
	ERR_FAIL_NULL_V(p_gpu, 0.05);
	// collision_base_size is the collision sphere radius: particles stop this
	// far from a surface. Use a quarter of the drawn particle (its visual
	// centre sits a bit above the surface, soft edges may overlap it).
	real_t visual = 0.2;
	const Ref<Mesh> mesh = p_gpu->get_draw_passes() > 0 ? p_gpu->get_draw_pass_mesh(0) : Ref<Mesh>();
	if (const QuadMesh *quad = Object::cast_to<QuadMesh>(mesh.ptr())) {
		visual = MIN(quad->get_size().x, quad->get_size().y);
	} else if (mesh.is_valid()) {
		const Vector3 size = mesh->get_aabb().size;
		visual = MAX(MAX(size.x, size.y), size.z);
		const real_t smallest_nonzero = MIN(size.x > 0.0 ? size.x : visual, MIN(size.y > 0.0 ? size.y : visual, size.z > 0.0 ? size.z : visual));
		visual = smallest_nonzero;
	}
	real_t scale = 1.0;
	const Ref<ParticleProcessMaterial> material = p_gpu->get_process_material();
	if (material.is_valid()) {
		scale = (material->get_param_min(ParticleProcessMaterial::PARAM_SCALE) + material->get_param_max(ParticleProcessMaterial::PARAM_SCALE)) * 0.5;
	}
	return CLAMP(visual * scale * 0.25, real_t(0.01), real_t(2.0));
}

bool ParticleEffect3D::emitter_uses_collision(const Node *p_node) {
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
	if (valid) {
		return int(mode) != 0;
	}
	// ShaderMaterial / VisualShader process shaders (e.g. Convert to Shader
	// Code) respond to collisions themselves through COLLIDED.
	if (const ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(material.ptr())) {
		const Ref<Shader> shader = shader_material->get_shader();
		return shader.is_valid() && shader->get_code().contains("COLLIDED");
	}
	return false;
}

// Exact SDF distances are only needed close to the walls; farther cells keep
// this value so the bake skips far geometry.
static constexpr float LEVEL_COLLISION_BAND = 1.5;
// Solid shell behind each collision face, in SDF cells.
static constexpr float LEVEL_COLLISION_THICKNESS = 1.5;
static constexpr int LEVEL_COLLISION_MAX_FPS = 240;

// Cells along the longest axis: about 15 cm cells for small effects (an
// impact bakes 32^3 instead of 64^3), at most 64 for big ones.
static int get_level_collision_subdiv(const AABB &p_region) {
	const real_t longest = p_region.get_longest_axis_size();
	if (longest <= 16 * 0.15) {
		return 16;
	}
	if (longest <= 32 * 0.15) {
		return 32;
	}
	return 64;
}

static float get_level_collision_band(const AABB &p_region) {
	const float cell = p_region.get_longest_axis_size() / get_level_collision_subdiv(p_region);
	return CLAMP(cell * 6.0f, 0.6f, LEVEL_COLLISION_BAND);
}

static void configure_level_collision_baker(GPUParticlesCollisionSDF3D *p_baker, const AABB &p_region) {
	const int subdiv = get_level_collision_subdiv(p_region);
	p_baker->set_resolution(subdiv == 16 ? GPUParticlesCollisionSDF3D::RESOLUTION_16 : (subdiv == 32 ? GPUParticlesCollisionSDF3D::RESOLUTION_32 : GPUParticlesCollisionSDF3D::RESOLUTION_64));
	p_baker->set_size(p_region.size);
	p_baker->set_thickness(LEVEL_COLLISION_THICKNESS);
}

// GPU particles only collide where they are at each simulation step, so a
// fast particle can jump over a thin wall between two steps. Raise the
// emitter's fixed FPS until one step moves less than the collision shell.
void ParticleEffect3D::apply_collision_substeps(GPUParticles3D *p_gpu, real_t p_shell) {
	real_t speed = 20.0;
	const Ref<ParticleProcessMaterial> material = p_gpu->get_process_material();
	if (material.is_valid()) {
		speed = material->get_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY) +
				material->get_gravity().length() * MIN(real_t(p_gpu->get_lifetime()), real_t(1.0)) +
				Math::abs(material->get_param_max(ParticleProcessMaterial::PARAM_RADIAL_ACCEL)) * 0.5 +
				Math::abs(material->get_param_max(ParticleProcessMaterial::PARAM_LINEAR_ACCEL)) * 0.5;
	}
	speed *= MAX(real_t(p_gpu->get_speed_scale()), real_t(1.0));
	const real_t safe_step = MAX(real_t(0.05), (p_shell + p_gpu->get_collision_base_size()) * 0.5);
	const int fps = CLAMP(int(Math::ceil(speed / safe_step)), 1, LEVEL_COLLISION_MAX_FPS);
	if (fps > p_gpu->get_fixed_fps()) {
		p_gpu->set_fixed_fps(fps);
	}
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
	// Keep the SDF fine (cells <= ~0.3 m): bake at most 20 m around the
	// effect, where particles hit walls while they are still visible.
	const AABB around_effect(get_global_position() - Vector3(10, 10, 10), Vector3(20, 20, 20));
	r_region = r_region.intersection(around_effect);
	return r_region.has_volume();
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
	ObjectID level_root;
	AABB region;
	ObjectID effect;
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
};

struct LevelCollisionCacheEntry {
	RID space;
	ObjectID level_root; // The root viewport's space survives scene changes; the level does not.
	AABB region;
	Ref<Texture3D> texture;
	uint64_t used = 0;
};

// Effects replayed in the same place (a looping fire, repeated explosions)
// share one bake.
LocalVector<LevelCollisionCacheEntry> level_collision_cache;
uint64_t level_collision_clock = 0;
constexpr uint32_t LEVEL_COLLISION_CACHE_SIZE = 16;
constexpr int LEVEL_COLLISION_SYNC_FACES = 20000;

Ref<Texture3D> find_cached_level_collision(const RID &p_space, ObjectID p_level_root, const AABB &p_region) {
	for (LevelCollisionCacheEntry &entry : level_collision_cache) {
		if (entry.space == p_space && entry.level_root == p_level_root && entry.region == p_region) {
			entry.used = ++level_collision_clock;
			return entry.texture;
		}
	}
	return Ref<Texture3D>();
}

void cache_level_collision(const RID &p_space, ObjectID p_level_root, const AABB &p_region, const Ref<Texture3D> &p_texture) {
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
	entry.level_root = p_level_root;
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
	bake->image = bake->baker->bake_faces(bake->faces, get_level_collision_band(bake->region), false);
	callable_mp_static(&ParticleEffect3D::_level_collision_bake_done).call_deferred(int64_t(p_userdata));
}
} // namespace

void ParticleEffect3D::_level_collision_bake_done(int64_t p_bake) {
	LevelCollisionBake *bake = reinterpret_cast<LevelCollisionBake *>(p_bake);
	// The task body has queued this call; waiting frees its pool entry.
	WorkerThreadPool::get_singleton()->wait_for_task_completion(bake->task);
	const Ref<Texture3D> texture = sdf_image_to_texture(bake->image);
	memdelete(bake->baker);
	if (texture.is_valid()) {
		cache_level_collision(bake->space, bake->level_root, bake->region, texture);
	}
	if (ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(bake->effect))) {
		if (effect->level_collision_pending && effect->level_collision_pending_region == bake->region) {
			effect->level_collision_pending = false;
			effect->_set_level_collision(bake->region, texture);
		}
	}
	memdelete(bake);
}

void ParticleEffect3D::_attach_level_manager(ObjectID p_root, ObjectID p_manager) {
	Node3D *manager = Object::cast_to<Node3D>(ObjectDB::get_instance(p_manager));
	if (!manager || manager->get_parent()) {
		return;
	}
	Node *root = Object::cast_to<Node>(ObjectDB::get_instance(p_root));
	if (!root || !root->is_inside_tree()) {
		memdelete(manager); // The level went away before the manager was attached.
		return;
	}
	root->add_child(manager, false, INTERNAL_MODE_BACK);
	manager->set_global_transform(Transform3D());
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


// --- Shared level collision -------------------------------------------------
// The whole level is baked once into overlapping SDF tiles (16 m cores with
// 2 m overlap, 128 cells = ~0.16 m, so a cell centre always lands inside a
// 0.2 m floor or wall and particles neither float above it nor fall through)
// on worker threads. The tiles are collision
// nodes in the level itself, so every effect (impacts, spawned effects, the
// Level editor viewport) collides without a per-effect bake.

namespace {
constexpr real_t LEVEL_TILE_CORE = 16.0;
constexpr real_t LEVEL_TILE_OVERLAP = 2.0;
constexpr GPUParticlesCollisionSDF3D::Resolution LEVEL_TILE_RESOLUTION = GPUParticlesCollisionSDF3D::RESOLUTION_128;
constexpr real_t LEVEL_MAX_EXTENT = 1024.0;

// Shared with the tile tasks of one bake generation; set when the bake is
// superseded so queued tiles skip their work.
struct LevelBakeCancel {
	SafeFlag cancelled;
	SafeRefCount refs;
};

LevelBakeCancel *level_bake_cancel_create() {
	LevelBakeCancel *cancel = memnew(LevelBakeCancel);
	cancel->refs.init();
	return cancel;
}

void level_bake_cancel_unref(LevelBakeCancel *p_cancel) {
	if (p_cancel && p_cancel->refs.unref()) {
		memdelete(p_cancel);
	}
}

struct LevelCollisionState {
	ObjectID manager;
	ObjectID root; // The level (edited scene / current scene) the tiles belong to.
	RID space;
	LevelBakeCancel *cancel = nullptr;
	uint64_t generation = 0;
	int pending = 0;
	int tiles = 0;
	bool ready = false;
	uint64_t start_usec = 0;
	uint32_t faces_hash = 0;
};

LocalVector<LevelCollisionState> level_states;
uint64_t level_generation = 0;

struct LevelTileBake {
	ObjectID manager;
	uint64_t generation = 0;
	LevelBakeCancel *cancel = nullptr; // Referenced.
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	Vector<Face3> faces; // World space faces that may touch this tile.
	AABB tile;
	Ref<Image> image;
};

void release_level_state(LevelCollisionState &r_state) {
	if (r_state.cancel) {
		r_state.cancel->cancelled.set();
		level_bake_cancel_unref(r_state.cancel);
		r_state.cancel = nullptr;
	}
}

void prune_level_states() {
	for (uint32_t i = 0; i < level_states.size(); i++) {
		if (!ObjectDB::get_instance(level_states[i].manager) || !ObjectDB::get_instance(level_states[i].root)) {
			release_level_state(level_states[i]);
			level_states.remove_at_unordered(i);
			i--;
		}
	}
}

// Several levels can share one world (editor scene tabs), so a state belongs
// to a space and a level root.
LevelCollisionState *find_level_state_for_root(const RID &p_space, ObjectID p_root) {
	prune_level_states();
	for (LevelCollisionState &state : level_states) {
		if (state.space == p_space && state.root == p_root) {
			return &state;
		}
	}
	return nullptr;
}

LevelCollisionState *find_level_state_for_node(const RID &p_space, const Node *p_node) {
	prune_level_states();
	for (LevelCollisionState &state : level_states) {
		if (state.space != p_space) {
			continue;
		}
		const Node *root = Object::cast_to<Node>(ObjectDB::get_instance(state.root));
		if (root && (root == p_node || root->is_ancestor_of(p_node))) {
			return &state;
		}
	}
	return nullptr;
}

RID get_level_root_space(const Node *p_root) {
	Viewport *viewport = p_root ? p_root->get_viewport() : nullptr;
	const Ref<World3D> world = viewport ? viewport->find_world_3d() : Ref<World3D>();
	return world.is_valid() ? world->get_space() : RID();
}

void collect_level_faces(Node *p_node, uint32_t p_mask, Vector<Face3> &r_faces) {
	if (Object::cast_to<ParticleEffect3D>(p_node) || !p_node->is_inside_tree()) {
		return;
	}
	// Lifts, doors and moving platforms (AnimatableBody3D is a StaticBody3D)
	// would stay baked where they stood when the level loaded.
	if (StaticBody3D *body = Object::cast_to<AnimatableBody3D>(p_node) ? nullptr : Object::cast_to<StaticBody3D>(p_node)) {
		if (body->get_collision_layer() & p_mask) {
			List<uint32_t> owners;
			body->get_shape_owners(&owners);
			for (const uint32_t owner : owners) {
				if (body->is_shape_owner_disabled(owner)) {
					continue;
				}
				const Transform3D xform = body->get_global_transform() * body->shape_owner_get_transform(owner);
				for (int i = 0; i < body->shape_owner_get_shape_count(owner); i++) {
					const Ref<Shape3D> shape = body->shape_owner_get_shape(owner, i);
					// An infinite plane would blow up the level bounds.
					if (shape.is_valid() && !Object::cast_to<WorldBoundaryShape3D>(shape.ptr())) {
						push_shape_faces(r_faces, xform, shape);
					}
				}
			}
		}
	} else if (Node3D *node = Object::cast_to<Node3D>(p_node)) {
		// CSG roots with collision expose their meshes.
		if (node->has_method("is_root_shape") && bool(node->call("is_root_shape")) && bool(node->get("use_collision")) && (uint32_t(int64_t(node->get("collision_layer"))) & p_mask)) {
			const Array meshes = node->call("get_meshes");
			if (meshes.size() >= 2) {
				push_mesh_faces(r_faces, node->get_global_transform() * Transform3D(meshes[0]), meshes[1]);
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		collect_level_faces(p_node->get_child(i), p_mask, r_faces);
	}
}

void level_tile_bake_task(void *p_userdata) {
	LevelTileBake *bake = static_cast<LevelTileBake *>(p_userdata);
	if (bake->cancel && bake->cancel->cancelled.is_set()) {
		// A newer bake replaced this one; its result would be discarded.
		bake->faces = Vector<Face3>();
		callable_mp_static(&ParticleEffect3D::_level_tile_baked).call_deferred(int64_t(p_userdata));
		return;
	}
	const Vector3 center = bake->tile.get_center();
	const Vector3 half = bake->tile.size * 0.5;
	Vector<Face3> local;
	for (const Face3 &face : bake->faces) {
		if (!face.get_aabb().intersects(bake->tile) || !Geometry3D::triangle_box_overlap(center, half, face.vertex)) {
			continue;
		}
		local.push_back(Face3(face.vertex[0] - center, face.vertex[1] - center, face.vertex[2] - center));
	}
	bake->faces = Vector<Face3>(); // Release the shared copy early.
	if (local.size() >= 2) {
		GPUParticlesCollisionSDF3D *baker = memnew(GPUParticlesCollisionSDF3D);
		baker->set_resolution(LEVEL_TILE_RESOLUTION);
		baker->set_size(bake->tile.size);
		baker->set_thickness(LEVEL_COLLISION_THICKNESS);
		bake->image = baker->bake_faces(local, LEVEL_COLLISION_BAND, false);
		memdelete(baker);
	}
	callable_mp_static(&ParticleEffect3D::_level_tile_baked).call_deferred(int64_t(p_userdata));
}
} // namespace

bool ParticleEffect3D::prepare_level_collision(Node *p_root, uint32_t p_mask) {
	ERR_FAIL_NULL_V(p_root, false);
	ERR_FAIL_COND_V(!p_root->is_inside_tree(), false);
	Vector<Face3> faces;
	collect_level_faces(p_root, p_mask, faces);
	return _prepare_level_collision_faces(p_root, faces);
}

static uint32_t hash_level_faces(const Vector<Face3> &p_faces) {
	return p_faces.is_empty() ? 0u : hash_murmur3_buffer(p_faces.ptr(), p_faces.size() * int(sizeof(Face3)));
}

void ParticleEffect3D::_ensure_editor_level_collision(Node *p_root, uint32_t p_mask) {
	// The Level editor re-bakes only when the static geometry changed.
	Vector<Face3> faces;
	collect_level_faces(p_root, p_mask, faces);
	const uint32_t hash = hash_level_faces(faces);
	const LevelCollisionState *state = find_level_state_for_root(get_level_root_space(p_root), p_root->get_instance_id());
	if (state && state->faces_hash == hash) {
		return;
	}
	_prepare_level_collision_faces(p_root, faces);
}

bool ParticleEffect3D::_prepare_level_collision_faces(Node *p_root, const Vector<Face3> &p_faces) {
	const Vector<Face3> &faces = p_faces;
	if (faces.size() < 2) {
		return false;
	}
	AABB bounds = faces[0].get_aabb();
	for (const Face3 &face : faces) {
		bounds.merge_with(face.get_aabb());
	}
	bounds = bounds.grow(1.0).intersection(AABB(bounds.get_center() - Vector3(1, 1, 1) * LEVEL_MAX_EXTENT * 0.5, Vector3(1, 1, 1) * LEVEL_MAX_EXTENT));

	// The root may be busy adding children (an effect dropped into the level
	// starts its preview while the editor adds it), so the world comes from
	// the viewport and a new manager is attached deferred.
	const RID space = get_level_root_space(p_root);
	const ObjectID root_id = p_root->get_instance_id();

	// Replace an earlier bake of the same level (never another level's tiles).
	Node3D *manager = nullptr;
	if (const LevelCollisionState *existing = find_level_state_for_root(space, root_id)) {
		manager = Object::cast_to<Node3D>(ObjectDB::get_instance(existing->manager));
	}
	for (int i = 0; !manager && i < p_root->get_child_count(true); i++) {
		if (p_root->get_child(i, true)->get_name() == StringName("LevelParticleCollision")) {
			manager = Object::cast_to<Node3D>(p_root->get_child(i, true));
		}
	}
	if (manager) {
		for (int i = manager->get_child_count() - 1; i >= 0; i--) {
			Node *tile = manager->get_child(i);
			manager->remove_child(tile);
			tile->queue_free();
		}
	} else {
		manager = memnew(Node3D);
		manager->set_name("LevelParticleCollision");
		manager->set_as_top_level(true);
		callable_mp_static(&ParticleEffect3D::_attach_level_manager).call_deferred(p_root->get_instance_id(), manager->get_instance_id());
	}
	LevelCollisionState *state = find_level_state_for_root(space, root_id);
	if (!state) {
		level_states.push_back(LevelCollisionState());
		state = &level_states[level_states.size() - 1];
	}
	release_level_state(*state); // Cancels tiles of the previous generation.
	state->cancel = level_bake_cancel_create();
	state->manager = manager->get_instance_id();
	state->root = root_id;
	state->space = space;
	state->generation = ++level_generation;
	state->pending = 0;
	state->tiles = 0;
	state->ready = false;
	state->start_usec = OS::get_singleton()->get_ticks_usec();
	state->faces_hash = hash_level_faces(faces);

	const Vector3i count = Vector3i(((bounds.size - Vector3(1, 1, 1) * 0.01) / LEVEL_TILE_CORE).floor()) + Vector3i(1, 1, 1);
	// Bucket the faces into the tiles they may touch (main thread, one pass),
	// so each tile task scans only its own faces and empty tiles are skipped.
	// Tile x spans [x * core - overlap, (x + 1) * core + overlap] from bounds.
	const int tile_count = count.x * count.y * count.z;
	LocalVector<Vector<Face3>> buckets;
	buckets.resize(tile_count);
	for (const Face3 &face : faces) {
		const AABB face_aabb = face.get_aabb();
		const Vector3 from = (face_aabb.position - bounds.position - Vector3(1, 1, 1) * LEVEL_TILE_OVERLAP) / LEVEL_TILE_CORE - Vector3(1, 1, 1);
		const Vector3 to = (face_aabb.get_end() - bounds.position + Vector3(1, 1, 1) * LEVEL_TILE_OVERLAP) / LEVEL_TILE_CORE;
		const Vector3i lo = Vector3i(from.ceil()).maxi(0);
		const Vector3i hi = Vector3i(to.floor()).min(count - Vector3i(1, 1, 1));
		for (int z = lo.z; z <= hi.z; z++) {
			for (int y = lo.y; y <= hi.y; y++) {
				for (int x = lo.x; x <= hi.x; x++) {
					buckets[(z * count.y + y) * count.x + x].push_back(face);
				}
			}
		}
	}
	for (int z = 0; z < count.z; z++) {
		for (int y = 0; y < count.y; y++) {
			for (int x = 0; x < count.x; x++) {
				Vector<Face3> &tile_faces = buckets[(z * count.y + y) * count.x + x];
				if (tile_faces.size() < 2) {
					continue; // Nothing to collide with here.
				}
				LevelTileBake *bake = memnew(LevelTileBake);
				bake->manager = state->manager;
				bake->generation = state->generation;
				bake->cancel = state->cancel;
				bake->cancel->refs.ref();
				bake->faces = tile_faces;
				tile_faces = Vector<Face3>();
				const Vector3 core_position = bounds.position + Vector3(x, y, z) * LEVEL_TILE_CORE;
				bake->tile = AABB(core_position - Vector3(1, 1, 1) * LEVEL_TILE_OVERLAP, Vector3(1, 1, 1) * (LEVEL_TILE_CORE + LEVEL_TILE_OVERLAP * 2.0));
				state->pending++;
				// The completion is deferred to this (main) thread, so the ID is
				// stored before _level_tile_baked can read it.
				bake->task = WorkerThreadPool::get_singleton()->add_native_task(&level_tile_bake_task, bake, false, "Particle level collision tile");
			}
		}
	}
	if (state->pending == 0) {
		state->ready = true; // No tile had geometry to bake.
	}
	print_verbose(vformat("ParticleEffect3D: baking level particle collision for %s: %d faces, %d tiles in %s.", p_root->get_name(), faces.size(), state->pending, bounds));
	return true;
}

void ParticleEffect3D::_level_tile_baked(int64_t p_bake) {
	LevelTileBake *bake = reinterpret_cast<LevelTileBake *>(p_bake);
	// The task body has queued this call; waiting frees its pool entry.
	WorkerThreadPool::get_singleton()->wait_for_task_completion(bake->task);
	level_bake_cancel_unref(bake->cancel);
	bake->cancel = nullptr;
	Node3D *manager = Object::cast_to<Node3D>(ObjectDB::get_instance(bake->manager));
	LevelCollisionState *state = nullptr;
	for (LevelCollisionState &candidate : level_states) {
		if (candidate.manager == bake->manager && candidate.generation == bake->generation) {
			state = &candidate;
		}
	}
	if (manager && state) {
		const Ref<Texture3D> texture = sdf_image_to_texture(bake->image);
		if (texture.is_valid() && OS::get_singleton()->is_stdout_verbose()) {
			texture->set_meta("_debug_sdf_image", bake->image); // Lets tests sample the field.
		}
		if (texture.is_valid()) {
			GPUParticlesCollisionSDF3D *tile = memnew(GPUParticlesCollisionSDF3D);
			tile->set_name("Tile");
			tile->set_size(bake->tile.size);
			tile->set_resolution(LEVEL_TILE_RESOLUTION);
			tile->set_texture(texture);
			tile->set_position(bake->tile.get_center());
			manager->add_child(tile, true);
			state->tiles++;
		}
		state->pending--;
		if (state->pending <= 0) {
			state->ready = true;
			print_verbose(vformat("ParticleEffect3D: level particle collision ready: %d tiles in %.1f ms.", state->tiles, (OS::get_singleton()->get_ticks_usec() - state->start_usec) / 1000.0));
		}
	}
	memdelete(bake);
}

void ParticleEffect3D::clear_level_collision_cache() {
	level_collision_cache.clear();
	// Pools are left alone: forgetting them would leave pooled effects in the
	// tree for good, and freed ones are pruned on the next use.
	// Forget the shared level bakes too; the next colliding effect re-bakes.
	for (LevelCollisionState &state : level_states) {
		release_level_state(state);
		if (Node *manager = Object::cast_to<Node>(ObjectDB::get_instance(state.manager))) {
			manager->queue_free();
		}
	}
	level_states.clear();
}

bool ParticleEffect3D::is_level_collision_ready(const Node *p_in_world) {
	const Node3D *node = Object::cast_to<Node3D>(p_in_world);
	if (!node || !node->is_inside_tree() || node->get_world_3d().is_null()) {
		return false;
	}
	const LevelCollisionState *state = find_level_state_for_node(node->get_world_3d()->get_space(), node);
	return state && state->ready;
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

	const real_t shell = region.get_longest_axis_size() / get_level_collision_subdiv(region) * LEVEL_COLLISION_THICKNESS;
	for (const EmitterState &state : emitters) {
		GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(ObjectDB::get_instance(state.id));
		if (emitter_uses_collision(gpu)) {
			if (editor_preview && !editor_saved_fps.has(state.id)) {
				editor_saved_fps[state.id] = gpu->get_fixed_fps(); // Restored after the preview.
			}
			apply_collision_substeps(gpu, shell);
		}
	}

	if (Engine::get_singleton()->is_editor_hint()) {
		// Level editor preview: only the shared level tiles (no per-effect bake).
		Node *edited_root = get_tree()->get_edited_scene_root();
		if (edited_root && (edited_root == this || edited_root->is_ancestor_of(this))) {
			_ensure_editor_level_collision(edited_root, level_collision_mask);
		}
		return;
	}

	Ref<World3D> world = get_world_3d();
	PhysicsDirectSpaceState3D *space = world.is_valid() ? world->get_direct_space_state() : nullptr;
	if (!space) {
		return;
	}
	const RID space_rid = world->get_space();

	// Shared level bake: start it with the first colliding effect, and once
	// it is ready no effect needs its own bake.
	Node *level_root = get_tree()->get_current_scene() ? get_tree()->get_current_scene() : get_tree()->get_root();
	LevelCollisionState *level = find_level_state_for_node(space_rid, this);
	if (!level) {
		// Effects spawned outside the level (e.g. under the root) use its tiles too.
		level = find_level_state_for_root(space_rid, level_root->get_instance_id());
	}
	// Only bake the level for effects in its world (not a SubViewport with
	// its own world, which would otherwise re-bake the level on every play).
	if (!level && get_level_root_space(level_root) == space_rid) {
		if (prepare_level_collision(level_root, level_collision_mask)) {
			level = find_level_state_for_root(space_rid, level_root->get_instance_id());
		}
	}
	if (level && level->ready) {
		level_collision_pending = false;
		_set_level_collision(AABB(), Ref<Texture3D>());
		return;
	}

	if (level_collision_id.is_valid() && level_collision_region == region) {
		return; // A looping or replayed effect that did not move keeps its bake.
	}
	if (level_collision_pending && level_collision_pending_region == region) {
		return;
	}
	const ObjectID level_root_id = level_root->get_instance_id();
	const Ref<Texture3D> cached = find_cached_level_collision(space_rid, level_root_id, region);
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
			if (!Object::cast_to<StaticBody3D>(body) || Object::cast_to<AnimatableBody3D>(body)) {
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
	// The narrow band bake is a few milliseconds, so the collision exists
	// before the first particles reach a wall (fast sparks at t = 0).
	const uint64_t bake_start = OS::get_singleton()->get_ticks_usec();
	if (faces.size() <= LEVEL_COLLISION_SYNC_FACES) {
		GPUParticlesCollisionSDF3D *baker = memnew(GPUParticlesCollisionSDF3D);
		configure_level_collision_baker(baker, region);
		const Ref<Image> image = baker->bake_faces(faces, get_level_collision_band(region));
		const Ref<Texture3D> texture = sdf_image_to_texture(image);
		memdelete(baker);
		if (OS::get_singleton()->is_stdout_verbose() && texture.is_valid()) {
			texture->set_meta("_debug_sdf_image", image); // Lets tests sample the field.
		}
		print_verbose(vformat("ParticleEffect3D: baked level collision for %s from %d faces in %.1f ms (%s).", get_name(), faces.size(), (OS::get_singleton()->get_ticks_usec() - bake_start) / 1000.0, region));
		if (texture.is_valid()) {
			cache_level_collision(space_rid, level_root_id, region, texture);
		}
		level_collision_pending = false;
		_set_level_collision(region, texture);
		return;
	}
	print_verbose(vformat("ParticleEffect3D: baking level collision for %s from %d faces in the background (%s).", get_name(), faces.size(), region));

	// Large levels bake on a worker thread; the collider appears when ready.
	LevelCollisionBake *bake = memnew(LevelCollisionBake);
	bake->baker = memnew(GPUParticlesCollisionSDF3D);
	configure_level_collision_baker(bake->baker, region);
	bake->faces = faces;
	bake->space = space_rid;
	bake->level_root = level_root_id;
	bake->region = region;
	bake->effect = get_instance_id();
	level_collision_pending = true;
	level_collision_pending_region = region;
	// High priority: this stand-in must not wait behind the whole level's tiles.
	bake->task = WorkerThreadPool::get_singleton()->add_native_task(&level_collision_bake_task, bake, true, "Particle level collision");
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

void ParticleEffect3D::_stop_looping_sounds() {
	for (const EventState &event : events) {
		if (event.kind != EVENT_SOUND) {
			continue;
		}
		AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(ObjectDB::get_instance(event.id));
		if (player && player->is_playing() && is_looping_stream(player->get_stream())) {
			player->stop();
		}
	}
}

void ParticleEffect3D::stop() {
	if (!playing) {
		return;
	}
	if (culled) {
		// Waiting for the camera or a free slot: nothing is playing, so it is
		// over now (it would otherwise start when the player comes closer).
		_finish();
		return;
	}
	_stop_looping_sounds(); // One-shot sounds play out with the particles.
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
	// Events that have not fired yet never do (no damage or sound from an
	// effect that was stopped). A light or decal that is on fades out with
	// the last particles; one in its timed window plays that window out.
	const double tail = _get_max_lifetime();
	stop_end = elapsed + tail;
	for (EventState &event : events) {
		const double local = elapsed - event.delay;
		if (event.kind != EVENT_LIGHT && event.kind != EVENT_DECAL) {
			event.started = true;
		} else if (local < 0.0) {
			event.cancelled = true;
		} else if (event.duration <= 0.0) {
			event.fade_from = elapsed;
			event.fade_length = MAX(0.2, tail);
			stop_end = MAX(stop_end, elapsed + event.fade_length);
		} else {
			stop_end = MAX(stop_end, event.delay + event.duration);
		}
	}
}

void ParticleEffect3D::restart() {
	play();
}

void ParticleEffect3D::seek(double p_time) {
	// play() fires the events due at time 0; when seeking past them they must
	// stay silent (a seek in a game would deal the explosion's damage again).
	suppress_instant_events = p_time > 0.0;
	play();
	suppress_instant_events = false;
	if (p_time <= 0.0 || !playing || culled) {
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
			// Emission is over: simulate its length with emitting on, then the
			// tail with emitting off (the residual part of the request does
			// that and leaves the emitter off). request_particles_process()
			// turns emitting back on, and setting it off afterwards made the
			// whole request run without emission: continuous emitters (Column)
			// showed nothing when seeking past their emission end.
			state.ended = true;
			emitter->call("request_particles_process", state.duration, MAX(run - state.duration, 0.0001));
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
	if (culled) {
		if (!resumable) {
			_finish();
			return;
		}
		elapsed += p_delta;
		if (elapsed >= CULLED_LOOP_RECHECK) {
			resuming = true;
			play(); // Starts once the camera is close enough / there is room.
			resuming = false;
		}
		return;
	}
	if (paused) {
		return;
	}
	elapsed += p_delta * speed_scale;
	if (lod_distance > 0.0 && p_delta > 0.0) {
		lod_elapsed += p_delta;
		if (lod_elapsed >= CULLED_LOOP_RECHECK) {
			lod_elapsed = 0.0;
			_apply_amount_scale(); // The camera moves; long effects follow it.
		}
	}
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
		if (state.visual_end < 0.0) {
			infinite = true;
		} else {
			total = MAX(total, state.visual_end);
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
		if (elapsed >= stop_end) {
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
		const EventBase *base_ptr = event_bases.getptr(event.id);
		const EventBase base = base_ptr ? *base_ptr : EventBase();
		const double local = elapsed - event.delay;
		const bool continuous = event.duration <= 0.0;
		const bool inside = !event.cancelled && local >= 0.0 && (continuous || local <= event.duration);
		// 1 while playing; a stopped continuous light / decal fades to 0.
		const real_t stop_fade = event.fade_from < 0.0 ? real_t(1.0) : real_t(CLAMP(1.0 - (elapsed - event.fade_from) / MAX(0.001, event.fade_length), 0.0, 1.0));
		const bool fire = !event.started && local >= 0.0 && !suppress_instant_events;
		const double t = continuous ? 0.0 : CLAMP(local / event.duration, 0.0, 1.0);
		switch (event.kind) {
			case EVENT_LIGHT: {
				Light3D *light = Object::cast_to<Light3D>(node);
				real_t factor = 0.0;
				if (inside) {
					factor = 1.0;
					if (!continuous) {
						if (event.curve.is_valid()) {
							factor = event.curve->sample_baked(t);
						} else {
							// Default flash: quick attack, smooth decay.
							factor = t < 0.1 ? real_t(t / 0.1) : real_t(Math::pow(1.0 - (t - 0.1) / 0.9, 2.0));
						}
					}
					if (event.flicker > 0.0) {
						factor *= 1.0 - event.flicker * event_noise(event.id, elapsed * 18.0);
					}
				}
				const real_t energy = base.energy * MAX(real_t(0.0), factor) * stop_fade;
				if (energy != event.last_value) {
					// Lights outside their window stay dark without touching the server each frame.
					event.last_value = energy;
					light->set_param(Light3D::PARAM_ENERGY, energy);
				}
			} break;
			case EVENT_DECAL: {
				Decal *decal = Object::cast_to<Decal>(node);
				real_t alpha = 0.0;
				if (inside) {
					alpha = 1.0;
					if (!continuous) {
						const double fade = event.fade;
						if (t > 1.0 - fade) {
							alpha = real_t((1.0 - t) / fade);
						}
					}
				}
				const real_t value = base.alpha * alpha * stop_fade;
				if (value != event.last_value) {
					event.last_value = value;
					Color modulate = decal->get_modulate();
					modulate.a = value;
					decal->set_modulate(modulate);
				}
			} break;
			case EVENT_SOUND: {
				if (fire) {
					event.started = true;
					AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(node);
					const real_t random = event.pitch_random;
					player->set_pitch_scale(MAX(real_t(0.01), base.pitch * (1.0 + random * real_t(Math::randf() * 2.0 - 1.0))));
					player->play();
				}
			} break;
			case EVENT_CAMERA_SHAKE: {
				if (fire) {
					event.started = true;
					Object::cast_to<CameraShake3D>(node)->shake();
				}
			} break;
			case EVENT_DAMAGE: {
				if (fire) {
					event.started = true;
					Object::cast_to<RadialDamage3D>(node)->apply_damage();
				}
			} break;
			case EVENT_SPAWN: {
				if (fire) {
					event.started = true;
					// Next to this effect (in the level), with this play's look.
					Object::cast_to<EffectSpawner3D>(node)->spawn_from_effect(get_parent(), tint * spawn_tint, effect_scale * spawn_scale, editor_preview);
				}
			} break;
			default:
				break;
		}
	}
}

void ParticleEffect3D::_restore_editor_preview() {
	// A Level editor preview animates authored values; put them back so the
	// scene never saves a preview state.
	for (const KeyValue<ObjectID, int> &saved : editor_saved_fps) {
		if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(ObjectDB::get_instance(saved.key))) {
			gpu->set_fixed_fps(saved.value);
		}
	}
	editor_saved_fps.clear();
	for (const KeyValue<ObjectID, EventBase> &base : event_bases) {
		Object *node = ObjectDB::get_instance(base.key);
		if (Light3D *light = Object::cast_to<Light3D>(node)) {
			light->set_param(Light3D::PARAM_ENERGY, base.value.energy);
		} else if (Decal *decal = Object::cast_to<Decal>(node)) {
			Color modulate = decal->get_modulate();
			modulate.a = base.value.alpha;
			decal->set_modulate(modulate);
		} else if (AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(node)) {
			player->set_pitch_scale(base.value.pitch);
			player->stop(); // Stop in the Level editor also silences the effect.
		}
	}
	for (const EventState &event : events) {
		if (EffectSpawner3D *spawner = Object::cast_to<EffectSpawner3D>(ObjectDB::get_instance(event.id))) {
			spawner->clear_editor_spawned();
		}
	}
	for (const EmitterState &state : emitters) {
		if (Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id))) {
			emitter->set("emitting", false);
		}
	}
}

void ParticleEffect3D::editor_preview_play() {
	if (playing && editor_preview) {
		_restore_editor_preview();
	}
	editor_preview = true;
	play();
}

void ParticleEffect3D::editor_preview_stop() {
	if (!editor_preview) {
		return;
	}
	playing = false;
	stopping = false;
	set_process_internal(false);
	_restore_editor_preview();
	editor_preview = false;
}

bool ParticleEffect3D::is_editor_previewing() const {
	return editor_preview && playing;
}

void ParticleEffect3D::_finish() {
	_stop_looping_sounds();
	if (!events.is_empty()) {
		// Lights and decals end dark: one that is always on (stopped with
		// stop()) would stay lit on an effect waiting in its pool.
		const double finished_at = elapsed;
		elapsed = -1.0;
		_update_events();
		elapsed = finished_at;
	}
	playing = false;
	paused = false;
	stopping = false;
	culled = false;
	_budget_remove();
	set_process_internal(false);
	if (editor_preview) {
		_restore_editor_preview();
		editor_preview = false;
	}
	emit_signal(SceneStringName(finished));
	if (playing || play_pending) {
		return; // A finished handler replayed the effect; keep it.
	}
	if (auto_free && !Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		if (!_release_to_pool()) {
			queue_free();
		}
	}
}

// --- Pooling ----------------------------------------------------------------
// spawn() reuses finished instances of the same scene instead of
// instantiating a new one per shot. Pooled effects stay in the tree, hidden.

bool ParticleEffect3D::_release_to_pool() {
	if (pool_scene_id.is_null() || is_queued_for_deletion()) {
		return false;
	}
	LocalVector<ObjectID> &pool = effect_pools[pool_scene_id];
	prune_pool(pool);
	if (int(pool.size()) >= EFFECT_POOL_MAX_FREE || pool.has(get_instance_id())) {
		return false;
	}
	// finished connections belong to the use that just ended.
	List<Connection> connections;
	get_signal_connection_list(SceneStringName(finished), &connections);
	for (const Connection &connection : connections) {
		if (!(connection.flags & CONNECT_PERSIST)) {
			disconnect(SceneStringName(finished), connection.callable);
		}
	}
	// No need to hide it: a finished effect has no live particles, its lights
	// and decals are faded out and its sounds are done. Hiding would only
	// propagate visibility changes through the whole effect twice per use.
	pool.push_back(get_instance_id());
	return true;
}

ParticleEffect3D *ParticleEffect3D::_take_from_pool(const Ref<PackedScene> &p_scene) {
	HashMap<ObjectID, LocalVector<ObjectID>>::Iterator pool = effect_pools.find(p_scene->get_instance_id());
	if (!pool) {
		return nullptr;
	}
	while (!pool->value.is_empty()) {
		const ObjectID id = pool->value[pool->value.size() - 1];
		pool->value.remove_at(pool->value.size() - 1);
		ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(id));
		if (effect && effect->is_inside_tree() && !effect->is_queued_for_deletion() && !effect->playing) {
			return effect;
		}
	}
	return nullptr;
}

ParticleEffect3D *ParticleEffect3D::_instantiate_pooled(const Ref<PackedScene> &p_scene, Node *p_parent) {
	Node *instance = p_scene->instantiate();
	ERR_FAIL_NULL_V(instance, nullptr);
	ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(instance);
	if (!effect) {
		memdelete(instance);
		ERR_FAIL_V_MSG(nullptr, "The scene root is not a ParticleEffect3D. Save the effect from the Particle workspace.");
	}
	effect->set_auto_free(true);
	effect->set_autoplay(false);
	effect->pool_scene_id = p_scene->get_instance_id();
	p_parent->add_child(effect);
	return effect;
}

int ParticleEffect3D::prewarm(const Ref<PackedScene> &p_scene, Node *p_parent, int p_count) {
	ERR_FAIL_COND_V(p_scene.is_null(), 0);
	ERR_FAIL_NULL_V(p_parent, 0);
	int created = 0;
	LocalVector<ObjectID> &pool = effect_pools[p_scene->get_instance_id()];
	prune_pool(pool);
	for (int i = 0; i < p_count && int(pool.size()) < EFFECT_POOL_MAX_FREE; i++) {
		ParticleEffect3D *effect = _instantiate_pooled(p_scene, p_parent);
		if (!effect) {
			break;
		}
		// Without autoplay it stays silent (lights and decals dark) until used.
		pool.push_back(effect->get_instance_id());
		created++;
	}
	return created;
}

int ParticleEffect3D::get_pooled_count(const Ref<PackedScene> &p_scene) {
	ERR_FAIL_COND_V(p_scene.is_null(), 0);
	HashMap<ObjectID, LocalVector<ObjectID>>::Iterator pool = effect_pools.find(p_scene->get_instance_id());
	if (!pool) {
		return 0;
	}
	prune_pool(pool->value);
	return int(pool->value.size());
}

void ParticleEffect3D::clear_pool() {
	for (KeyValue<ObjectID, LocalVector<ObjectID>> &pool : effect_pools) {
		for (const ObjectID &id : pool.value) {
			if (Node *effect = Object::cast_to<Node>(ObjectDB::get_instance(id))) {
				effect->queue_free();
			}
		}
	}
	effect_pools.clear();
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
		case NOTIFICATION_PREDELETE: {
			_budget_remove();
		} break;
		case NOTIFICATION_ENTER_TREE: {
			// READY fires only once; a reused (pooled) effect that was played
			// outside the tree starts when it enters it again.
			if (play_pending && is_ready()) {
				callable_mp(this, &ParticleEffect3D::_play_pending).call_deferred();
			}
		} break;
		case NOTIFICATION_READY: {
			if (play_pending || (autoplay && !Engine::get_singleton()->is_editor_hint())) {
				play();
			} else if (!Engine::get_singleton()->is_editor_hint()) {
				// Without autoplay the effect stays silent (emitters off, lights and
				// decals dark) until play() is called.
				emitters.clear();
				events.clear();
				_collect_emitters(this);
				_silence();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update(get_process_delta_time());
		} break;
		case NOTIFICATION_EDITOR_PRE_SAVE: {
			if (editor_preview) {
				editor_preview_stop();
			}
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
	if (playing && !culled) {
		_apply_speed_scale();
	}
}

void ParticleEffect3D::set_paused(bool p_paused) {
	if (paused == p_paused || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	paused = p_paused;
	if (!playing || culled) {
		return;
	}
	_apply_speed_scale();
	for (const EventState &event : events) {
		if (AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(ObjectDB::get_instance(event.id))) {
			player->set_stream_paused(paused);
		}
	}
}

bool ParticleEffect3D::is_paused() const {
	return paused;
}

void ParticleEffect3D::set_lod_distance(real_t p_distance) {
	lod_distance = MAX(real_t(0.0), p_distance);
}

real_t ParticleEffect3D::get_lod_distance() const {
	return lod_distance;
}

void ParticleEffect3D::set_lod_amount(real_t p_amount) {
	lod_amount = CLAMP(p_amount, real_t(0.05), real_t(1.0));
}

real_t ParticleEffect3D::get_lod_amount() const {
	return lod_amount;
}

double ParticleEffect3D::get_speed_scale() const {
	return speed_scale;
}

void ParticleEffect3D::set_priority(int p_priority) {
	priority = p_priority;
}

int ParticleEffect3D::get_priority() const {
	return priority;
}

void ParticleEffect3D::set_max_instances(int p_max) {
	max_instances = MAX(0, p_max);
}

int ParticleEffect3D::get_max_instances() const {
	return max_instances;
}

void ParticleEffect3D::set_cull_distance(real_t p_distance) {
	cull_distance = MAX(real_t(0.0), p_distance);
}

real_t ParticleEffect3D::get_cull_distance() const {
	return cull_distance;
}

void ParticleEffect3D::set_local_space(bool p_enable) {
	local_space = p_enable;
}

bool ParticleEffect3D::is_local_space() const {
	return local_space;
}

ParticleEffect3D *ParticleEffect3D::spawn_attached(const Ref<PackedScene> &p_scene, Node3D *p_parent, const Transform3D &p_local_transform, const Color &p_tint, real_t p_scale) {
	ERR_FAIL_COND_V(p_scene.is_null(), nullptr);
	ERR_FAIL_NULL_V(p_parent, nullptr);
	const Transform3D global = p_parent->is_inside_tree() ? p_parent->get_global_transform() * p_local_transform : p_local_transform;
	ParticleEffect3D *effect = _take_from_pool(p_scene);
	if (effect) {
		if (effect->get_parent() != p_parent) {
			effect->get_parent()->remove_child(effect);
			p_parent->add_child(effect);
		}
	} else {
		effect = _instantiate_pooled(p_scene, p_parent);
		if (!effect) {
			return nullptr;
		}
	}
	if (!effect->local_space) {
		effect->local_space_from_attach = true; // A later plain spawn() turns it off again.
	}
	effect->set_local_space(true);
	effect->spawn_tint = p_tint;
	effect->spawn_scale = MAX(real_t(0.001), p_scale);
	if (effect->is_inside_tree()) {
		effect->set_global_transform(global);
	} else {
		effect->set_transform(p_local_transform);
	}
	effect->reset_physics_interpolation(); // Teleported from the pool (see spawn()).
	effect->play();
	return effect;
}

ParticleEffect3D *ParticleEffect3D::spawn(const Ref<PackedScene> &p_scene, Node *p_parent, const Transform3D &p_transform, const Color &p_tint, real_t p_scale) {
	ERR_FAIL_COND_V(p_scene.is_null(), nullptr);
	ERR_FAIL_NULL_V(p_parent, nullptr);
	ParticleEffect3D *effect = _take_from_pool(p_scene);
	if (effect) {
		if (effect->get_parent() != p_parent) {
			effect->get_parent()->remove_child(effect);
			p_parent->add_child(effect);
		}
	} else {
		effect = _instantiate_pooled(p_scene, p_parent);
		if (!effect) {
			return nullptr;
		}
	}
	if (effect->local_space_from_attach) {
		effect->local_space_from_attach = false;
		effect->set_local_space(false); // Pooled after spawn_attached(); now free again.
	}
	effect->spawn_tint = p_tint; // Always set: a reused instance drops the previous look.
	effect->spawn_scale = MAX(real_t(0.001), p_scale);
	if (effect->is_inside_tree()) {
		effect->set_global_transform(p_transform);
	} else {
		effect->set_transform(p_transform);
	}
	// A pooled effect is teleported from where it waited: with physics
	// interpolation it would be drawn (and emit its first particles) on the
	// way between the two places, off the spot it was spawned at.
	effect->reset_physics_interpolation();
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
	ClassDB::bind_method(D_METHOD("editor_preview_play"), &ParticleEffect3D::editor_preview_play);
	ClassDB::bind_method(D_METHOD("editor_preview_stop"), &ParticleEffect3D::editor_preview_stop);
	ClassDB::bind_method(D_METHOD("is_editor_previewing"), &ParticleEffect3D::is_editor_previewing);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("clear_level_collision_cache"), &ParticleEffect3D::clear_level_collision_cache);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("prewarm", "scene", "parent", "count"), &ParticleEffect3D::prewarm);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("get_pooled_count", "scene"), &ParticleEffect3D::get_pooled_count);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("clear_pool"), &ParticleEffect3D::clear_pool);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("prepare_level_collision", "level_root", "mask"), &ParticleEffect3D::prepare_level_collision, DEFVAL(0xFFFFFFFF));
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("is_level_collision_ready", "node_in_world"), &ParticleEffect3D::is_level_collision_ready);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("emitter_uses_collision", "emitter"), &ParticleEffect3D::emitter_uses_collision);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("get_suggested_collision_size", "emitter"), &ParticleEffect3D::get_suggested_collision_size);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("apply_collision_substeps", "emitter", "shell"), &ParticleEffect3D::apply_collision_substeps);
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
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("spawn", "scene", "parent", "transform", "tint", "scale"), &ParticleEffect3D::spawn, DEFVAL(Color(1, 1, 1, 1)), DEFVAL(1.0));
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("spawn_attached", "scene", "parent", "local_transform", "tint", "scale"), &ParticleEffect3D::spawn_attached, DEFVAL(Transform3D()), DEFVAL(Color(1, 1, 1, 1)), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("set_tint", "tint"), &ParticleEffect3D::set_tint);
	ClassDB::bind_method(D_METHOD("get_tint"), &ParticleEffect3D::get_tint);
	ClassDB::bind_method(D_METHOD("set_effect_scale", "scale"), &ParticleEffect3D::set_effect_scale);
	ClassDB::bind_method(D_METHOD("get_effect_scale"), &ParticleEffect3D::get_effect_scale);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("get_active_effect_count"), &ParticleEffect3D::get_active_effect_count);
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &ParticleEffect3D::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &ParticleEffect3D::get_priority);
	ClassDB::bind_method(D_METHOD("set_max_instances", "max"), &ParticleEffect3D::set_max_instances);
	ClassDB::bind_method(D_METHOD("get_max_instances"), &ParticleEffect3D::get_max_instances);
	ClassDB::bind_method(D_METHOD("set_cull_distance", "distance"), &ParticleEffect3D::set_cull_distance);
	ClassDB::bind_method(D_METHOD("get_cull_distance"), &ParticleEffect3D::get_cull_distance);
	ClassDB::bind_method(D_METHOD("set_local_space", "enable"), &ParticleEffect3D::set_local_space);
	ClassDB::bind_method(D_METHOD("is_local_space"), &ParticleEffect3D::is_local_space);
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &ParticleEffect3D::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &ParticleEffect3D::is_paused);
	ClassDB::bind_method(D_METHOD("set_lod_distance", "distance"), &ParticleEffect3D::set_lod_distance);
	ClassDB::bind_method(D_METHOD("get_lod_distance"), &ParticleEffect3D::get_lod_distance);
	ClassDB::bind_method(D_METHOD("set_lod_amount", "amount"), &ParticleEffect3D::set_lod_amount);
	ClassDB::bind_method(D_METHOD("get_lod_amount"), &ParticleEffect3D::get_lod_amount);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("get_emitter_speed", "emitter"), &ParticleEffect3D::get_emitter_speed);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "is_autoplay");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "looping"), "set_looping", "is_looping");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_free"), "set_auto_free", "is_auto_free");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_scale", PROPERTY_HINT_RANGE, "0,8,0.01,or_greater"), "set_speed_scale", "get_speed_scale");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "local_space"), "set_local_space", "is_local_space");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_paused", "is_paused");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint"), "set_tint", "get_tint");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "effect_scale", PROPERTY_HINT_RANGE, "0.05,10,0.01,or_greater"), "set_effect_scale", "get_effect_scale");
	ADD_GROUP("Budget", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "-10,10,1,or_less,or_greater"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_instances", PROPERTY_HINT_RANGE, "0,256,1,or_greater"), "set_max_instances", "get_max_instances");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cull_distance", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m"), "set_cull_distance", "get_cull_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_distance", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m"), "set_lod_distance", "get_lod_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_amount", PROPERTY_HINT_RANGE, "0.05,1,0.01"), "set_lod_amount", "get_lod_amount");
	ADD_GROUP("Level Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_level"), "set_collide_with_level", "is_colliding_with_level");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "level_collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_level_collision_mask", "get_level_collision_mask");

	ADD_SIGNAL(MethodInfo("finished"));
}

ParticleEffect3D::ParticleEffect3D() {
}
