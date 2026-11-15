/**************************************************************************/
/*  particle_impact_table.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/io/resource.h"
#include "core/variant/typed_array.h"
#include "scene/resources/packed_scene.h"

class Node;
class ParticleEffect3D;

// Maps surface types (concrete, metal, wood, glass, water, flesh...) to
// impact effects, so a weapon only reports where it hit and the table picks
// the matching ParticleEffect3D (with its decal and sound) and plays it.
class ParticleImpactTable : public Resource {
	GDCLASS(ParticleImpactTable, Resource);

	Dictionary effects; // StringName surface -> PackedScene.
	Ref<PackedScene> default_effect;
	bool random_roll = true;
	real_t surface_offset = 0.01;

	Ref<PackedScene> _find_effect(const String &p_surface) const;

protected:
	static void _bind_methods();

public:
	static constexpr const char *SURFACE_META = "surface";

	void set_effects(const Dictionary &p_effects);
	Dictionary get_effects() const;
	void set_default_effect(const Ref<PackedScene> &p_effect);
	Ref<PackedScene> get_default_effect() const;
	void set_random_roll(bool p_enable);
	bool is_random_roll() const;
	void set_surface_offset(real_t p_offset);
	real_t get_surface_offset() const;

	void set_effect(const StringName &p_surface, const Ref<PackedScene> &p_effect);
	// The effect for a surface, or the default effect.
	Ref<PackedScene> get_effect(const StringName &p_surface) const;
	// Surface of a hit collider: a "surface" metadata on the collider or one
	// of its 3 nearest ancestors, then the "surface" metadata or the name of
	// its PhysicsMaterial, then a node group named like a table entry.
	// Empty when nothing matches (the default effect is used).
	StringName get_surface(Object *p_collider) const;
	// Plays the surface's effect at p_position with its +Y axis along
	// p_normal (impact presets are authored with the surface normal as +Y).
	ParticleEffect3D *spawn(Node *p_parent, const Vector3 &p_position, const Vector3 &p_normal, Object *p_collider = nullptr) const;
	// Same for the result Dictionary of PhysicsDirectSpaceState3D.intersect_ray().
	ParticleEffect3D *spawn_from_hit(Node *p_parent, const Dictionary &p_hit) const;
	// Every effect scene in the table (for prewarming).
	TypedArray<PackedScene> get_all_effects() const;
};
