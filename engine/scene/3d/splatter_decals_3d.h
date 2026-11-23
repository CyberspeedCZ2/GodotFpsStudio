/**************************************************************************/
/*  splatter_decals_3d.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "scene/3d/node_3d.h"

class Texture2D;

// Leaves decals where flying drops would land (blood behind a hit, oil,
// paint): casts rays in a cone and puts a decal on what each one hits, after
// the time a drop needs to get there. Fires on splatter() or at its time
// inside a ParticleEffect3D. GPU particles cannot report where they land, so
// the rays stand in for them.
class SplatterDecals3D : public Node3D {
	GDCLASS(SplatterDecals3D, Node3D);

	Ref<Texture2D> texture;
	Ref<Texture2D> normal_texture;
	Color modulate = Color(1, 1, 1, 1);
	int count = 6;
	Vector3 direction = Vector3(0, 1, 0);
	real_t spread = 35.0;
	real_t max_distance = 3.0;
	Vector2 size = Vector2(0.15, 0.45);
	real_t drop_speed = 8.0;
	real_t lifetime = 15.0;
	real_t fade_time = 3.0;
	uint32_t collision_mask = 1;

protected:
	static void _bind_methods();

public:
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;
	void set_normal_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_normal_texture() const;
	void set_modulate(const Color &p_color);
	Color get_modulate() const;
	void set_count(int p_count);
	int get_count() const;
	void set_direction(const Vector3 &p_direction);
	Vector3 get_direction() const;
	void set_spread(real_t p_degrees);
	real_t get_spread() const;
	void set_max_distance(real_t p_distance);
	real_t get_max_distance() const;
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const;
	void set_drop_speed(real_t p_speed);
	real_t get_drop_speed() const;
	void set_lifetime(real_t p_lifetime);
	real_t get_lifetime() const;
	void set_fade_time(real_t p_time);
	real_t get_fade_time() const;
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;

	// Decals go under the parent of the effect this node belongs to.
	void splatter();
	void splatter_under(Node *p_parent, const Color &p_tint, real_t p_scale);
};
