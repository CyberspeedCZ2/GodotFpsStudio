/**************************************************************************/
/*  radial_damage_3d.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "scene/3d/node_3d.h"

class CollisionObject3D;

// Area damage with distance falloff and cover (line of sight) checks, e.g. an
// explosion. Fires on apply_damage() or at its time inside a ParticleEffect3D.
class RadialDamage3D : public Node3D {
	GDCLASS(RadialDamage3D, Node3D);

	real_t damage = 50.0;
	real_t inner_radius = 1.5;
	real_t radius = 6.0;
	real_t impulse = 8.0;
	bool use_cover = true;
	uint32_t collision_mask = UINT32_MAX;
	uint32_t obstacle_mask = 1;
	bool hit_areas = false;
	StringName damage_method = "take_damage";
	StringName target_group;

	// ObjectIDs: take_damage is game script and may free or reparent targets.
	struct Target {
		ObjectID node; // Receives the damage (has damage_method), may be null.
		Vector<ObjectID> bodies;
	};

	static Vector<CollisionObject3D *> _get_live_bodies(const Target &p_target);
	AABB _get_target_bounds(const Vector<CollisionObject3D *> &p_bodies) const;

protected:
	static void _bind_methods();

public:
	void set_damage(real_t p_damage);
	real_t get_damage() const;
	void set_inner_radius(real_t p_radius);
	real_t get_inner_radius() const;
	void set_radius(real_t p_radius);
	real_t get_radius() const;
	void set_impulse(real_t p_impulse);
	real_t get_impulse() const;
	void set_use_cover(bool p_enable);
	bool is_using_cover() const;
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;
	void set_obstacle_mask(uint32_t p_mask);
	uint32_t get_obstacle_mask() const;
	void set_hit_areas(bool p_enable);
	bool is_hitting_areas() const;
	void set_damage_method(const StringName &p_method);
	StringName get_damage_method() const;
	void set_target_group(const StringName &p_group);
	StringName get_target_group() const;

	real_t get_damage_at_distance(real_t p_distance) const;
	void apply_damage();
};
