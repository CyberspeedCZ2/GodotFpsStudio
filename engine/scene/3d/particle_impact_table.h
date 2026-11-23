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
	Dictionary tints; // StringName surface -> Color (optional).
	Dictionary scales; // StringName surface -> float (optional).
	Dictionary variants; // StringName surface -> Array of PackedScene: more looks for the surface, picked at random with its effect.
	real_t reflect_amount = 0.5;
	Ref<PackedScene> default_effect;
	bool random_roll = true;
	real_t surface_offset = 0.01;

	Ref<PackedScene> _find_effect(const String &p_surface) const;
	Variant _find_entry(const Dictionary &p_table, const String &p_surface) const;

protected:
	static void _bind_methods();

public:
	static constexpr const char *SURFACE_META = "surface";

	void set_effects(const Dictionary &p_effects);
	Dictionary get_effects() const;
	void set_tints(const Dictionary &p_tints);
	Dictionary get_tints() const;
	void set_scales(const Dictionary &p_scales);
	Dictionary get_scales() const;
	// Tint / scale a surface's effect is played with (white / 1.0 when unset).
	Color get_surface_tint(const StringName &p_surface) const;
	real_t get_surface_scale(const StringName &p_surface) const;
	void set_variants(const Dictionary &p_variants);
	Dictionary get_variants() const;
	// How far debris leans from the surface normal towards the bounce of the
	// shot when spawn() gets its direction (0 = straight out, 1 = mirror).
	void set_reflect_amount(real_t p_amount);
	real_t get_reflect_amount() const;
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
	// p_tint / p_scale multiply the surface's own tint / scale (one effect for
	// red and yellow blood, small and big explosions).
	ParticleEffect3D *spawn(Node *p_parent, const Vector3 &p_position, const Vector3 &p_normal, Object *p_collider = nullptr, const Color &p_tint = Color(1, 1, 1, 1), real_t p_scale = 1.0, const Vector3 &p_direction = Vector3()) const;
	// Same for the result Dictionary of PhysicsDirectSpaceState3D.intersect_ray().
	ParticleEffect3D *spawn_from_hit(Node *p_parent, const Dictionary &p_hit, const Color &p_tint = Color(1, 1, 1, 1), real_t p_scale = 1.0, const Vector3 &p_direction = Vector3()) const;
	// Every effect scene in the table (for prewarming).
	TypedArray<PackedScene> get_all_effects() const;
};
