/**************************************************************************/
/*  particle_impact_table.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_impact_table.h"

#include "core/object/class_db.h"
#include "core/variant/typed_array.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/physics_material.h"

void ParticleImpactTable::set_effects(const Dictionary &p_effects) {
	effects = p_effects;
	emit_changed();
}

Dictionary ParticleImpactTable::get_effects() const {
	return effects;
}

void ParticleImpactTable::set_default_effect(const Ref<PackedScene> &p_effect) {
	default_effect = p_effect;
	emit_changed();
}

Ref<PackedScene> ParticleImpactTable::get_default_effect() const {
	return default_effect;
}

void ParticleImpactTable::set_random_roll(bool p_enable) {
	random_roll = p_enable;
}

bool ParticleImpactTable::is_random_roll() const {
	return random_roll;
}

void ParticleImpactTable::set_surface_offset(real_t p_offset) {
	surface_offset = p_offset;
}

real_t ParticleImpactTable::get_surface_offset() const {
	return surface_offset;
}

void ParticleImpactTable::set_effect(const StringName &p_surface, const Ref<PackedScene> &p_effect) {
	// Keys typed in the inspector may be String or StringName; keep one entry.
	for (const Variant &key : effects.keys()) {
		if (String(key) == String(p_surface)) {
			effects.erase(key);
		}
	}
	if (p_effect.is_valid()) {
		effects[p_surface] = p_effect;
	}
	emit_changed();
}

Ref<PackedScene> ParticleImpactTable::_find_effect(const String &p_surface) const {
	if (p_surface.is_empty()) {
		return Ref<PackedScene>();
	}
	for (const KeyValue<Variant, Variant> &entry : effects) {
		if (String(entry.key).to_lower() == p_surface.to_lower()) {
			return entry.value;
		}
	}
	return Ref<PackedScene>();
}

Ref<PackedScene> ParticleImpactTable::get_effect(const StringName &p_surface) const {
	const Ref<PackedScene> effect = _find_effect(p_surface);
	return effect.is_valid() ? effect : default_effect;
}

StringName ParticleImpactTable::get_surface(Object *p_collider) const {
	Node *node = Object::cast_to<Node>(p_collider);
	if (!node) {
		return StringName();
	}
	// 1. "surface" metadata on the collider or a near ancestor (a body under a
	//    prop scene, a CSG child under a tagged CSG root...).
	Node *current = node;
	for (int i = 0; i < 4 && current; i++) {
		if (current->has_meta(SURFACE_META)) {
			return StringName(String(current->get_meta(SURFACE_META)));
		}
		current = current->get_parent();
	}
	// 2. The body's PhysicsMaterial: its "surface" metadata, else its name.
	const Ref<PhysicsMaterial> material = node->get("physics_material_override");
	if (material.is_valid()) {
		if (material->has_meta(SURFACE_META)) {
			return StringName(String(material->get_meta(SURFACE_META)));
		}
		if (!material->get_name().is_empty()) {
			return StringName(material->get_name());
		}
	}
	// 3. A group named like one of the table's surfaces.
	current = node;
	for (int i = 0; i < 4 && current; i++) {
		for (const Variant &key : effects.keys()) {
			const StringName surface = String(key);
			if (current->is_in_group(surface)) {
				return surface;
			}
		}
		current = current->get_parent();
	}
	return StringName();
}

ParticleEffect3D *ParticleImpactTable::spawn(Node *p_parent, const Vector3 &p_position, const Vector3 &p_normal, Object *p_collider) const {
	ERR_FAIL_NULL_V(p_parent, nullptr);
	const Ref<PackedScene> scene = get_effect(get_surface(p_collider));
	if (scene.is_null()) {
		return nullptr;
	}
	// +Y along the normal; a random roll around it varies decals and debris.
	const Vector3 up = p_normal.is_zero_approx() ? Vector3(0, 1, 0) : p_normal.normalized();
	const Vector3 helper = Math::abs(up.y) < 0.99 ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	Vector3 x_axis = helper.cross(up).normalized();
	if (random_roll) {
		x_axis = x_axis.rotated(up, Math::randf() * Math::TAU);
	}
	const Vector3 z_axis = x_axis.cross(up).normalized();
	const Basis basis(x_axis, up, z_axis);
	return ParticleEffect3D::spawn(scene, p_parent, Transform3D(basis, p_position + up * surface_offset));
}

ParticleEffect3D *ParticleImpactTable::spawn_from_hit(Node *p_parent, const Dictionary &p_hit) const {
	if (p_hit.is_empty() || !p_hit.has("position")) {
		return nullptr;
	}
	Object *collider = p_hit.get("collider", Variant());
	return spawn(p_parent, p_hit["position"], p_hit.get("normal", Vector3(0, 1, 0)), collider);
}

TypedArray<PackedScene> ParticleImpactTable::get_all_effects() const {
	TypedArray<PackedScene> result;
	for (const KeyValue<Variant, Variant> &entry : effects) {
		const Ref<PackedScene> scene = entry.value;
		if (scene.is_valid() && !result.has(scene)) {
			result.push_back(scene);
		}
	}
	if (default_effect.is_valid() && !result.has(default_effect)) {
		result.push_back(default_effect);
	}
	return result;
}

void ParticleImpactTable::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_effects", "effects"), &ParticleImpactTable::set_effects);
	ClassDB::bind_method(D_METHOD("get_effects"), &ParticleImpactTable::get_effects);
	ClassDB::bind_method(D_METHOD("set_default_effect", "effect"), &ParticleImpactTable::set_default_effect);
	ClassDB::bind_method(D_METHOD("get_default_effect"), &ParticleImpactTable::get_default_effect);
	ClassDB::bind_method(D_METHOD("set_random_roll", "enable"), &ParticleImpactTable::set_random_roll);
	ClassDB::bind_method(D_METHOD("is_random_roll"), &ParticleImpactTable::is_random_roll);
	ClassDB::bind_method(D_METHOD("set_surface_offset", "offset"), &ParticleImpactTable::set_surface_offset);
	ClassDB::bind_method(D_METHOD("get_surface_offset"), &ParticleImpactTable::get_surface_offset);
	ClassDB::bind_method(D_METHOD("set_effect", "surface", "effect"), &ParticleImpactTable::set_effect);
	ClassDB::bind_method(D_METHOD("get_effect", "surface"), &ParticleImpactTable::get_effect);
	ClassDB::bind_method(D_METHOD("get_surface", "collider"), &ParticleImpactTable::get_surface);
	ClassDB::bind_method(D_METHOD("spawn", "parent", "position", "normal", "collider"), &ParticleImpactTable::spawn, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("spawn_from_hit", "parent", "hit"), &ParticleImpactTable::spawn_from_hit);
	ClassDB::bind_method(D_METHOD("get_all_effects"), &ParticleImpactTable::get_all_effects);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "effects", PROPERTY_HINT_DICTIONARY_TYPE, "StringName;PackedScene"), "set_effects", "get_effects");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "default_effect", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_default_effect", "get_default_effect");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "random_roll"), "set_random_roll", "is_random_roll");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "surface_offset", PROPERTY_HINT_RANGE, "0,0.2,0.001,suffix:m"), "set_surface_offset", "get_surface_offset");
}
