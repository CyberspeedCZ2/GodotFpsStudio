/**************************************************************************/
/*  splatter_decals_3d.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "splatter_decals_3d.h"

#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/templates/local_vector.h"
#include "scene/3d/decal.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/animation/tween.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/texture.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace {
// Splatters of every effect together; the oldest make room for new ones.
constexpr int MAX_SPLATTER_DECALS = 64;
LocalVector<ObjectID> splatter_decals;

Vector3 random_in_cone(const Vector3 &p_axis, real_t p_degrees) {
	const Vector3 helper = Math::abs(p_axis.y) < 0.99 ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	const Vector3 side = p_axis.cross(helper).normalized();
	const real_t tilt = Math::deg_to_rad(p_degrees) * Math::sqrt(Math::randf());
	return p_axis.rotated(side, tilt).rotated(p_axis, Math::randf() * Math::TAU).normalized();
}
} // namespace

void SplatterDecals3D::splatter() {
	Node *parent = get_parent();
	for (Node *node = get_parent(); node; node = node->get_parent()) {
		if (Object::cast_to<ParticleEffect3D>(node)) {
			parent = node->get_parent();
			break;
		}
	}
	splatter_under(parent, Color(1, 1, 1, 1), 1.0);
}

void SplatterDecals3D::splatter_under(Node *p_parent, const Color &p_tint, real_t p_scale) {
	// The game only: the rays need the level's physics.
	if (!p_parent || !is_inside_tree() || texture.is_null() || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	const Ref<World3D> world = get_world_3d();
	PhysicsDirectSpaceState3D *space = world.is_valid() ? world->get_direct_space_state() : nullptr;
	if (!space) {
		return;
	}
	const Transform3D origin = get_global_transform().orthonormalized();
	const Vector3 axis = origin.basis.xform(direction.is_zero_approx() ? Vector3(0, 1, 0) : direction.normalized()).normalized();
	const Color color = Color(modulate.r * p_tint.r, modulate.g * p_tint.g, modulate.b * p_tint.b, modulate.a);
	for (int i = 0; i < count; i++) {
		PhysicsServer3DTypes::RayParameters ray;
		ray.from = origin.origin;
		ray.to = origin.origin + random_in_cone(axis, spread) * max_distance * p_scale;
		ray.collision_mask = collision_mask;
		PhysicsServer3DTypes::RayResult hit;
		if (!space->intersect_ray(ray, hit)) {
			continue;
		}
		// A decal projects along its -Y: its +Y is the surface normal.
		const Vector3 up = hit.normal.is_zero_approx() ? Vector3(0, 1, 0) : hit.normal.normalized();
		const Vector3 helper = Math::abs(up.y) < 0.99 ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
		const Vector3 x_axis = helper.cross(up).normalized().rotated(up, Math::randf() * Math::TAU);
		const real_t extent = Math::random(size.x, size.y) * p_scale;

		Decal *decal = memnew(Decal);
		decal->set_texture(Decal::TEXTURE_ALBEDO, texture);
		if (normal_texture.is_valid()) {
			decal->set_texture(Decal::TEXTURE_NORMAL, normal_texture);
		}
		decal->set_size(Vector3(extent, MAX(real_t(0.2), extent * 0.5), extent));
		decal->set_modulate(color);
		decal->set_visible(false);
		p_parent->add_child(decal);
		decal->set_global_transform(Transform3D(Basis(x_axis, up, x_axis.cross(up).normalized()), hit.position));
		decal->reset_physics_interpolation();

		// Appears when the drop arrives, stays, fades out and goes away.
		const double flight = drop_speed > 0.0 ? origin.origin.distance_to(hit.position) / drop_speed : 0.0;
		Ref<Tween> tween = decal->create_tween();
		tween->tween_interval(MAX(0.001, flight));
		tween->tween_callback(callable_mp(static_cast<Node3D *>(decal), &Node3D::set_visible).bind(true));
		tween->tween_interval(MAX(0.01, double(lifetime)));
		tween->tween_property(decal, NodePath("modulate:a"), 0.0, MAX(0.01, double(fade_time)));
		tween->tween_callback(callable_mp(static_cast<Node *>(decal), &Node::queue_free));

		splatter_decals.push_back(decal->get_instance_id());
	}
	// Drop freed ones, then the oldest above the limit.
	for (uint32_t i = 0; i < splatter_decals.size(); i++) {
		if (!ObjectDB::get_instance(splatter_decals[i])) {
			splatter_decals.remove_at(i);
			i--;
		}
	}
	while (int(splatter_decals.size()) > MAX_SPLATTER_DECALS) {
		if (Node *oldest = Object::cast_to<Node>(ObjectDB::get_instance(splatter_decals[0]))) {
			oldest->queue_free();
		}
		splatter_decals.remove_at(0);
	}
}

void SplatterDecals3D::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
}

Ref<Texture2D> SplatterDecals3D::get_texture() const {
	return texture;
}

void SplatterDecals3D::set_normal_texture(const Ref<Texture2D> &p_texture) {
	normal_texture = p_texture;
}

Ref<Texture2D> SplatterDecals3D::get_normal_texture() const {
	return normal_texture;
}

void SplatterDecals3D::set_modulate(const Color &p_color) {
	modulate = p_color;
}

Color SplatterDecals3D::get_modulate() const {
	return modulate;
}

void SplatterDecals3D::set_count(int p_count) {
	count = CLAMP(p_count, 1, 32);
}

int SplatterDecals3D::get_count() const {
	return count;
}

void SplatterDecals3D::set_direction(const Vector3 &p_direction) {
	direction = p_direction;
}

Vector3 SplatterDecals3D::get_direction() const {
	return direction;
}

void SplatterDecals3D::set_spread(real_t p_degrees) {
	spread = CLAMP(p_degrees, real_t(0.0), real_t(180.0));
}

real_t SplatterDecals3D::get_spread() const {
	return spread;
}

void SplatterDecals3D::set_max_distance(real_t p_distance) {
	max_distance = MAX(real_t(0.05), p_distance);
}

real_t SplatterDecals3D::get_max_distance() const {
	return max_distance;
}

void SplatterDecals3D::set_size(const Vector2 &p_size) {
	size = Vector2(MAX(real_t(0.01), MIN(p_size.x, p_size.y)), MAX(real_t(0.01), MAX(p_size.x, p_size.y)));
}

Vector2 SplatterDecals3D::get_size() const {
	return size;
}

void SplatterDecals3D::set_drop_speed(real_t p_speed) {
	drop_speed = MAX(real_t(0.0), p_speed);
}

real_t SplatterDecals3D::get_drop_speed() const {
	return drop_speed;
}

void SplatterDecals3D::set_lifetime(real_t p_lifetime) {
	lifetime = MAX(real_t(0.0), p_lifetime);
}

real_t SplatterDecals3D::get_lifetime() const {
	return lifetime;
}

void SplatterDecals3D::set_fade_time(real_t p_time) {
	fade_time = MAX(real_t(0.0), p_time);
}

real_t SplatterDecals3D::get_fade_time() const {
	return fade_time;
}

void SplatterDecals3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
}

uint32_t SplatterDecals3D::get_collision_mask() const {
	return collision_mask;
}

void SplatterDecals3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &SplatterDecals3D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &SplatterDecals3D::get_texture);
	ClassDB::bind_method(D_METHOD("set_normal_texture", "texture"), &SplatterDecals3D::set_normal_texture);
	ClassDB::bind_method(D_METHOD("get_normal_texture"), &SplatterDecals3D::get_normal_texture);
	ClassDB::bind_method(D_METHOD("set_modulate", "color"), &SplatterDecals3D::set_modulate);
	ClassDB::bind_method(D_METHOD("get_modulate"), &SplatterDecals3D::get_modulate);
	ClassDB::bind_method(D_METHOD("set_count", "count"), &SplatterDecals3D::set_count);
	ClassDB::bind_method(D_METHOD("get_count"), &SplatterDecals3D::get_count);
	ClassDB::bind_method(D_METHOD("set_direction", "direction"), &SplatterDecals3D::set_direction);
	ClassDB::bind_method(D_METHOD("get_direction"), &SplatterDecals3D::get_direction);
	ClassDB::bind_method(D_METHOD("set_spread", "degrees"), &SplatterDecals3D::set_spread);
	ClassDB::bind_method(D_METHOD("get_spread"), &SplatterDecals3D::get_spread);
	ClassDB::bind_method(D_METHOD("set_max_distance", "distance"), &SplatterDecals3D::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_max_distance"), &SplatterDecals3D::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &SplatterDecals3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &SplatterDecals3D::get_size);
	ClassDB::bind_method(D_METHOD("set_drop_speed", "speed"), &SplatterDecals3D::set_drop_speed);
	ClassDB::bind_method(D_METHOD("get_drop_speed"), &SplatterDecals3D::get_drop_speed);
	ClassDB::bind_method(D_METHOD("set_lifetime", "lifetime"), &SplatterDecals3D::set_lifetime);
	ClassDB::bind_method(D_METHOD("get_lifetime"), &SplatterDecals3D::get_lifetime);
	ClassDB::bind_method(D_METHOD("set_fade_time", "time"), &SplatterDecals3D::set_fade_time);
	ClassDB::bind_method(D_METHOD("get_fade_time"), &SplatterDecals3D::get_fade_time);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &SplatterDecals3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &SplatterDecals3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("splatter"), &SplatterDecals3D::splatter);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_normal_texture", "get_normal_texture");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate", "get_modulate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "count", PROPERTY_HINT_RANGE, "1,32,1"), "set_count", "get_count");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "direction"), "set_direction", "get_direction");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spread", PROPERTY_HINT_RANGE, "0,180,1,degrees"), "set_spread", "get_spread");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "0.05,20,0.01,or_greater,suffix:m"), "set_max_distance", "get_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drop_speed", PROPERTY_HINT_RANGE, "0,40,0.1,or_greater,suffix:m/s"), "set_drop_speed", "get_drop_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lifetime", PROPERTY_HINT_RANGE, "0,120,0.1,or_greater,suffix:s"), "set_lifetime", "get_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_time", PROPERTY_HINT_RANGE, "0,30,0.1,or_greater,suffix:s"), "set_fade_time", "get_fade_time");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
}
