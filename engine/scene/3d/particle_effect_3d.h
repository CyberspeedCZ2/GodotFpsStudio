/**************************************************************************/
/*  particle_effect_3d.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"

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

	bool autoplay = true;
	bool looping = false;
	bool auto_free = false;
	double speed_scale = 1.0;

	bool playing = false;
	bool stopping = false;
	bool play_pending = false;
	double elapsed = 0.0;
	double stop_time = 0.0;
	LocalVector<EmitterState> emitters;

	void _collect_emitters(Node *p_node);
	static void _set_emitter_emitting(Node *p_emitter, bool p_emitting, bool p_restart);
	double _get_max_lifetime() const;
	void _update(double p_delta);
	void _finish();

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

	void play();
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
	static ParticleEffect3D *spawn(const Ref<PackedScene> &p_scene, Node *p_parent, const Transform3D &p_transform);

	ParticleEffect3D();
};
