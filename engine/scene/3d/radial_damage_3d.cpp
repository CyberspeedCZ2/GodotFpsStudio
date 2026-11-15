/**************************************************************************/
/*  radial_damage_3d.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "radial_damage_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/resources/3d/shape_3d.h"
#include "scene/resources/mesh.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

static constexpr int MAX_SHAPE_RESULTS = 128;

AABB RadialDamage3D::_get_target_bounds(const Target &p_target) const {
	AABB bounds;
	bool has_bounds = false;
	for (CollisionObject3D *body : p_target.bodies) {
		for (int i = 0; i < body->get_child_count(); i++) {
			CollisionShape3D *shape_node = Object::cast_to<CollisionShape3D>(body->get_child(i));
			if (!shape_node || shape_node->is_disabled() || shape_node->get_shape().is_null()) {
				continue;
			}
			Ref<ArrayMesh> debug_mesh = shape_node->get_shape()->get_debug_mesh();
			if (debug_mesh.is_null()) {
				continue;
			}
			const AABB shape_bounds = shape_node->get_global_transform().xform(debug_mesh->get_aabb());
			if (has_bounds) {
				bounds.merge_with(shape_bounds);
			} else {
				bounds = shape_bounds;
				has_bounds = true;
			}
		}
		if (!has_bounds) {
			bounds = AABB(body->get_global_position() - Vector3(0.25, 0.25, 0.25), Vector3(0.5, 0.5, 0.5));
			has_bounds = true;
		}
	}
	return bounds;
}

real_t RadialDamage3D::get_damage_at_distance(real_t p_distance) const {
	const real_t inner = MIN(inner_radius, radius);
	if (p_distance <= inner) {
		return damage;
	}
	if (p_distance >= radius) {
		return 0.0;
	}
	return damage * (1.0 - (p_distance - inner) / MAX(real_t(0.001), radius - inner));
}

void RadialDamage3D::apply_damage() {
	// The editor preview and edited scenes never deal damage.
	if (!is_inside_tree() || Engine::get_singleton()->is_editor_hint() || radius <= 0.0) {
		return;
	}
	Ref<World3D> world = get_world_3d();
	PhysicsDirectSpaceState3D *space = world.is_valid() ? world->get_direct_space_state() : nullptr;
	if (!space) {
		return;
	}
	const Vector3 origin = get_global_position();

	// 1) Everything inside the sphere.
	PhysicsServer3D *physics = PhysicsServer3D::get_singleton();
	const RID sphere = physics->sphere_shape_create();
	physics->shape_set_data(sphere, radius);
	PhysicsDirectSpaceState3D::ShapeParameters shape_query;
	shape_query.shape_rid = sphere;
	shape_query.transform = Transform3D(Basis(), origin);
	shape_query.collision_mask = collision_mask;
	shape_query.collide_with_areas = hit_areas;
	PhysicsDirectSpaceState3D::ShapeResult results[MAX_SHAPE_RESULTS];
	const int count = space->intersect_shape(shape_query, results, MAX_SHAPE_RESULTS);
	physics->free_rid(sphere);

	// 2) Group colliders by the node that takes the damage (a character with
	// several hitboxes is hit once), plus rigid bodies to push.
	HashMap<ObjectID, Target> targets;
	Vector<ObjectID> order;
	for (int i = 0; i < count; i++) {
		CollisionObject3D *body = Object::cast_to<CollisionObject3D>(results[i].collider);
		if (!body) {
			continue;
		}
		Node *receiver = nullptr;
		for (Node *node = body; node; node = node->get_parent()) {
			if (node->has_method(damage_method)) {
				receiver = node;
				break;
			}
		}
		if (receiver && target_group != StringName() && !receiver->is_in_group(target_group)) {
			receiver = nullptr;
		}
		if (!receiver && !Object::cast_to<RigidBody3D>(body)) {
			continue;
		}
		const ObjectID key = receiver ? receiver->get_instance_id() : body->get_instance_id();
		if (!targets.has(key)) {
			Target target;
			target.node = receiver;
			targets.insert(key, target);
			order.push_back(key);
		}
		Target &target = targets[key];
		if (!target.bodies.has(body)) {
			target.bodies.push_back(body);
		}
	}

	for (const ObjectID &key : order) {
		const Target &target = targets[key];
		const AABB bounds = _get_target_bounds(target);
		// Distance to the nearest point of the target, so large props are hit
		// by their near side, not their center.
		const Vector3 nearest = origin.clamp(bounds.position, bounds.position + bounds.size);
		const real_t base = get_damage_at_distance(origin.distance_to(nearest));
		real_t falloff = radius > 0.0 ? CLAMP(1.0 - origin.distance_to(nearest) / radius, 0.0, 1.0) : 1.0;
		if (base <= 0.0 && falloff <= 0.0) {
			continue;
		}

		// 3) Cover: rays to the center and eight inset corners of the target.
		// Fully hidden takes nothing, partly hidden takes the visible share.
		real_t exposure = 1.0;
		if (use_cover) {
			PhysicsDirectSpaceState3D::RayParameters ray;
			ray.from = origin;
			ray.collision_mask = obstacle_mask;
			for (CollisionObject3D *body : target.bodies) {
				ray.exclude.insert(body->get_rid());
			}
			const Vector3 center = bounds.get_center();
			int visible = 0;
			const int samples = 9;
			for (int s = 0; s < samples; s++) {
				Vector3 point = center;
				if (s > 0) {
					point = center + (bounds.get_endpoint(s - 1) - center) * 0.8;
				}
				ray.to = point;
				PhysicsDirectSpaceState3D::RayResult hit;
				if (!space->intersect_ray(ray, hit)) {
					visible++;
				}
			}
			exposure = real_t(visible) / samples;
		}
		if (exposure <= 0.0) {
			continue;
		}

		const Vector3 hit_position = bounds.get_center();
		Vector3 direction = hit_position - origin;
		direction = direction.length_squared() > CMP_EPSILON ? direction.normalized() : Vector3(0, 1, 0);

		if (target.node && base > 0.0) {
			const real_t amount = base * exposure;
			// Call with as many arguments as the method takes:
			// (amount), (amount, hit_position) or (amount, hit_position, hit_direction).
			bool valid = false;
			const int arg_count = target.node->get_method_argument_count(damage_method, &valid);
			const int pass = valid ? CLAMP(arg_count, 1, 3) : 1;
			if (pass == 1) {
				target.node->call(damage_method, amount);
			} else if (pass == 2) {
				target.node->call(damage_method, amount, hit_position);
			} else {
				target.node->call(damage_method, amount, hit_position, direction);
			}
			emit_signal(SNAME("damaged"), target.node, amount, exposure);
		}
		if (impulse > 0.0) {
			for (CollisionObject3D *body : target.bodies) {
				if (RigidBody3D *rigid = Object::cast_to<RigidBody3D>(body)) {
					rigid->apply_impulse(direction * impulse * falloff * exposure, hit_position - rigid->get_global_position());
				}
			}
		}
	}
}

void RadialDamage3D::set_damage(real_t p_damage) {
	damage = MAX(0.0, p_damage);
}

real_t RadialDamage3D::get_damage() const {
	return damage;
}

void RadialDamage3D::set_inner_radius(real_t p_radius) {
	inner_radius = MAX(0.0, p_radius);
	update_gizmos();
}

real_t RadialDamage3D::get_inner_radius() const {
	return inner_radius;
}

void RadialDamage3D::set_radius(real_t p_radius) {
	radius = MAX(0.0, p_radius);
	update_gizmos();
}

real_t RadialDamage3D::get_radius() const {
	return radius;
}

void RadialDamage3D::set_impulse(real_t p_impulse) {
	impulse = MAX(0.0, p_impulse);
}

real_t RadialDamage3D::get_impulse() const {
	return impulse;
}

void RadialDamage3D::set_use_cover(bool p_enable) {
	use_cover = p_enable;
}

bool RadialDamage3D::is_using_cover() const {
	return use_cover;
}

void RadialDamage3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
}

uint32_t RadialDamage3D::get_collision_mask() const {
	return collision_mask;
}

void RadialDamage3D::set_obstacle_mask(uint32_t p_mask) {
	obstacle_mask = p_mask;
}

uint32_t RadialDamage3D::get_obstacle_mask() const {
	return obstacle_mask;
}

void RadialDamage3D::set_hit_areas(bool p_enable) {
	hit_areas = p_enable;
}

bool RadialDamage3D::is_hitting_areas() const {
	return hit_areas;
}

void RadialDamage3D::set_damage_method(const StringName &p_method) {
	damage_method = p_method;
}

StringName RadialDamage3D::get_damage_method() const {
	return damage_method;
}

void RadialDamage3D::set_target_group(const StringName &p_group) {
	target_group = p_group;
}

StringName RadialDamage3D::get_target_group() const {
	return target_group;
}

void RadialDamage3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_damage", "damage"), &RadialDamage3D::set_damage);
	ClassDB::bind_method(D_METHOD("get_damage"), &RadialDamage3D::get_damage);
	ClassDB::bind_method(D_METHOD("set_inner_radius", "radius"), &RadialDamage3D::set_inner_radius);
	ClassDB::bind_method(D_METHOD("get_inner_radius"), &RadialDamage3D::get_inner_radius);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &RadialDamage3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &RadialDamage3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_impulse", "impulse"), &RadialDamage3D::set_impulse);
	ClassDB::bind_method(D_METHOD("get_impulse"), &RadialDamage3D::get_impulse);
	ClassDB::bind_method(D_METHOD("set_use_cover", "enable"), &RadialDamage3D::set_use_cover);
	ClassDB::bind_method(D_METHOD("is_using_cover"), &RadialDamage3D::is_using_cover);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &RadialDamage3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &RadialDamage3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_obstacle_mask", "mask"), &RadialDamage3D::set_obstacle_mask);
	ClassDB::bind_method(D_METHOD("get_obstacle_mask"), &RadialDamage3D::get_obstacle_mask);
	ClassDB::bind_method(D_METHOD("set_hit_areas", "enable"), &RadialDamage3D::set_hit_areas);
	ClassDB::bind_method(D_METHOD("is_hitting_areas"), &RadialDamage3D::is_hitting_areas);
	ClassDB::bind_method(D_METHOD("set_damage_method", "method"), &RadialDamage3D::set_damage_method);
	ClassDB::bind_method(D_METHOD("get_damage_method"), &RadialDamage3D::get_damage_method);
	ClassDB::bind_method(D_METHOD("set_target_group", "group"), &RadialDamage3D::set_target_group);
	ClassDB::bind_method(D_METHOD("get_target_group"), &RadialDamage3D::get_target_group);
	ClassDB::bind_method(D_METHOD("get_damage_at_distance", "distance"), &RadialDamage3D::get_damage_at_distance);
	ClassDB::bind_method(D_METHOD("apply_damage"), &RadialDamage3D::apply_damage);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damage", PROPERTY_HINT_RANGE, "0,500,0.1,or_greater"), "set_damage", "get_damage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inner_radius", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m"), "set_inner_radius", "get_inner_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "impulse", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater"), "set_impulse", "get_impulse");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_cover"), "set_use_cover", "is_using_cover");
	ADD_GROUP("Targets", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "obstacle_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_obstacle_mask", "get_obstacle_mask");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "hit_areas"), "set_hit_areas", "is_hitting_areas");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "damage_method"), "set_damage_method", "get_damage_method");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "target_group"), "set_target_group", "get_target_group");

	ADD_SIGNAL(MethodInfo("damaged", PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node"), PropertyInfo(Variant::FLOAT, "amount"), PropertyInfo(Variant::FLOAT, "exposure")));
}
