/**************************************************************************/
/*  particle_effect_3d.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "core/math/face3.h"
#include "scene/resources/texture.h"

class GPUParticles3D;

class PackedScene;

// Root node of a reusable particle effect authored in the Particle workspace.
// It owns the timing of its emitters (start delay and emission duration,
// stored as metadata on each GPUParticles3D / CPUParticles3D descendant),
// plays the effect on demand, reports when it has finished and can free
// itself, which makes one-shot effects (impacts, explosions, muzzle flashes)
// easy to trigger from game code.
class ParticleEffect3D : public Node3D {
	GDCLASS(ParticleEffect3D, Node3D);

public:
	static constexpr const char *META_DELAY = "particle_effect_delay";
	static constexpr const char *META_DURATION = "particle_effect_duration";
	static constexpr const char *META_CURVE = "particle_effect_curve"; // Curve: light intensity over its duration.
	static constexpr const char *META_FLICKER = "particle_effect_flicker"; // 0..1 random light flicker.
	static constexpr const char *META_FADE = "particle_effect_fade"; // 0..1 part of a decal's duration used to fade out.
	static constexpr const char *META_PITCH_RANDOM = "particle_effect_pitch_random"; // 0..1 random sound pitch.

	enum EventKind {
		EVENT_NONE,
		EVENT_LIGHT,
		EVENT_SOUND,
		EVENT_CAMERA_SHAKE,
		EVENT_DECAL,
		EVENT_DAMAGE,
	};

private:
	struct EmitterState {
		ObjectID id;
		double delay = 0.0;
		double duration = 0.0;
		double lifetime = 1.0;
		bool one_shot = false;
		bool started = false;
		bool ended = false;
	};

	struct EventState {
		ObjectID id;
		EventKind kind = EVENT_NONE;
		double delay = 0.0;
		double duration = 0.0; // <= 0: continuous.
		bool started = false;
	};
	struct EventBase {
		real_t energy = 1.0;
		real_t alpha = 1.0;
		real_t pitch = 1.0;
	};

	bool autoplay = true;
	bool looping = false;
	bool auto_free = false;
	double speed_scale = 1.0;
	bool collide_with_level = true;
	uint32_t level_collision_mask = 1;
	ObjectID level_collision_id;
	AABB level_collision_region;
	bool level_collision_retry = false;
	bool level_collision_pending = false;
	AABB level_collision_pending_region;
	bool editor_preview = false;
	HashMap<ObjectID, int> editor_saved_fps;
	ObjectID pool_scene_id; // Set by spawn(): finished instances return to this scene's pool.

	bool playing = false;
	bool stopping = false;
	bool play_pending = false;
	double elapsed = 0.0;
	double stop_time = 0.0;
	LocalVector<EmitterState> emitters;
	LocalVector<EventState> events;
	HashMap<ObjectID, EventBase> event_bases;

	void _collect_emitters(Node *p_node);
	void _update_events();
	static void _set_emitter_emitting(Node *p_emitter, bool p_emitting, bool p_restart);
	double _get_max_lifetime() const;
	void _update(double p_delta);
	void _finish();
	void _restore_editor_preview();
	void _ensure_editor_level_collision(Node *p_root, uint32_t p_mask);
	static bool _prepare_level_collision_faces(Node *p_root, const Vector<Face3> &p_faces);
	bool _release_to_pool();
	static ParticleEffect3D *_take_from_pool(const Ref<PackedScene> &p_scene);
	static ParticleEffect3D *_instantiate_pooled(const Ref<PackedScene> &p_scene, Node *p_parent);
	bool _get_level_collision_region(AABB &r_region) const;
	void _update_level_collision();
	void _set_level_collision(const AABB &p_region, const Ref<Texture3D> &p_texture);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_autoplay(bool p_autoplay);
	bool is_autoplay() const;
	void set_looping(bool p_looping);
	bool is_looping() const;
	void set_auto_free(bool p_auto_free);
	bool is_auto_free() const;
	void set_speed_scale(double p_speed_scale);
	double get_speed_scale() const;
	void set_collide_with_level(bool p_enable);
	bool is_colliding_with_level() const;
	void set_level_collision_mask(uint32_t p_mask);
	uint32_t get_level_collision_mask() const;
	// Bakes the static physics shapes around the effect into a transient
	// GPUParticlesCollisionSDF3D, so emitters with collision bounce off walls
	// without hand-placed particle colliders. Called by play() at runtime.
	void update_level_collision();
	// Main thread completion of a background level collision bake.
	static void _level_collision_bake_done(int64_t p_bake);
	// Frees shared level collision bakes (call when the level geometry changed).
	static void clear_level_collision_cache();
	// Bakes the static collision of the whole level under p_root into shared
	// SDF tiles on worker threads (nodes under p_root, not saved). Effects use
	// them once ready instead of baking their own. Returns false when no
	// static collision was found. Done automatically by the first colliding
	// effect in a running game; call it at level load to have it ready sooner.
	static bool prepare_level_collision(Node *p_root, uint32_t p_mask = 0xFFFFFFFF);
	static bool is_level_collision_ready(const Node *p_in_world);
	static void _level_tile_baked(int64_t p_bake);
	static void _attach_level_manager(ObjectID p_root, ObjectID p_manager);

	void play();
	// Plays the effect inside the editor (Level viewport) with level collision,
	// restoring every animated value afterwards so the scene is not modified.
	void editor_preview_play();
	void editor_preview_stop();
	bool is_editor_previewing() const;
	void stop();
	void restart();
	// Restarts and fast-forwards the effect to p_time (used by editor scrubbing).
	void seek(double p_time);
	bool is_playing() const;
	double get_elapsed_time() const;
	// Seconds until every particle has died, or -1 if an emitter runs forever.
	double get_total_duration() const;

	PackedStringArray get_configuration_warnings() const override;

	static bool is_emitter(const Node *p_node);
	// True for visible GPU emitters whose process material collides
	// (collision_mode, or a shader that reads COLLIDED).
	static bool emitter_uses_collision(const Node *p_node);
	// Raises fixed_fps so one simulation step moves less than the collider
	// shell plus the particle size (stops fast particles tunnelling through walls).
	static void apply_collision_substeps(GPUParticles3D *p_gpu, real_t p_shell);
	// A collision_base_size (collision radius) that matches the drawn particle.
	static real_t get_suggested_collision_size(const GPUParticles3D *p_gpu);
	static EventKind get_event_kind(const Node *p_node);
	// Nodes shown as tracks on the effect timeline (emitters and timed events).
	static bool is_track(const Node *p_node);
	static void get_track_window(const Node *p_node, double &r_start, double &r_active_end, double &r_visual_end);
	static void set_emitter_delay(Node *p_emitter, double p_delay);
	static double get_emitter_delay(const Node *p_emitter);
	static void set_emitter_duration(Node *p_emitter, double p_duration);
	static double get_emitter_duration(const Node *p_emitter);
	// Start time and end of emission for one emitter; end is -1 when infinite.
	static void get_emitter_window(const Node *p_emitter, double &r_start, double &r_emission_end, double &r_visual_end);

	// Instantiates p_scene under p_parent at p_transform, plays it and frees
	// it when finished. Returns the new effect (nullptr if the scene root is
	// not a ParticleEffect3D).
	// Finished spawned effects are hidden and reused by later spawn() calls of
	// the same scene (up to 64 waiting per scene), so rapid fire does not
	// instantiate a scene per shot.
	static ParticleEffect3D *spawn(const Ref<PackedScene> &p_scene, Node *p_parent, const Transform3D &p_transform);
	// Creates p_count hidden instances in advance (e.g. at level load).
	static int prewarm(const Ref<PackedScene> &p_scene, Node *p_parent, int p_count);
	static int get_pooled_count(const Ref<PackedScene> &p_scene);
	static void clear_pool();

	ParticleEffect3D();
};
