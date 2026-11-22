/**************************************************************************/
/*  effect_spawner_3d.h                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"

class PackedScene;

// Plays another particle effect or instantiates a scene (gibs, debris props,
// a secondary explosion) at its place. Fires on spawn() or at its time inside
// a ParticleEffect3D, where it is a timed event like a light or a sound.
class EffectSpawner3D : public Node3D {
	GDCLASS(EffectSpawner3D, Node3D);

	Ref<PackedScene> scene;
	int count = 1;
	real_t scatter_radius = 0.0;
	real_t launch_speed = 0.0;
	real_t launch_spread = 30.0;
	bool inherit_look = true;

	// What an editor preview spawned; freed when the preview ends.
	LocalVector<ObjectID> editor_spawned;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_scene(const Ref<PackedScene> &p_scene);
	Ref<PackedScene> get_scene() const;
	void set_count(int p_count);
	int get_count() const;
	void set_scatter_radius(real_t p_radius);
	real_t get_scatter_radius() const;
	void set_launch_speed(real_t p_speed);
	real_t get_launch_speed() const;
	void set_launch_spread(real_t p_degrees);
	real_t get_launch_spread() const;
	// A spawned particle effect takes the tint and scale its effect plays with.
	void set_inherit_look(bool p_enable);
	bool is_inheriting_look() const;

	// Spawns under the parent of the effect this node belongs to (or under its
	// own parent when it is not part of an effect).
	void spawn();
	void spawn_from_effect(Node *p_parent, const Color &p_tint, real_t p_scale, bool p_editor_level_preview);
	void clear_editor_spawned();
};
