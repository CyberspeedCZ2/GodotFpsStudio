/**************************************************************************/
/*  dynamic_navigation_agent_3d.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         GODOT FPS STUDIO                               */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "dynamic_navigation_agent_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/navigation_3d/navigation_server_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

void DynamicNavigationAgent3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_dynamic_target_position", "position"), &DynamicNavigationAgent3D::set_dynamic_target_position);
	ClassDB::bind_method(D_METHOD("clear_dynamic_target"), &DynamicNavigationAgent3D::clear_dynamic_target);
	ClassDB::bind_method(D_METHOD("request_dynamic_repath"), &DynamicNavigationAgent3D::request_dynamic_repath);
	ClassDB::bind_method(D_METHOD("get_next_dynamic_path_position"), &DynamicNavigationAgent3D::get_next_dynamic_path_position);
	ClassDB::bind_method(D_METHOD("get_current_dynamic_path"), &DynamicNavigationAgent3D::get_current_dynamic_path);
	ClassDB::bind_method(D_METHOD("get_dynamic_state"), &DynamicNavigationAgent3D::get_dynamic_state);
	ClassDB::bind_method(D_METHOD("is_dynamic_navigation_finished"), &DynamicNavigationAgent3D::is_dynamic_navigation_finished);
	ClassDB::bind_method(D_METHOD("is_dynamic_navigation_blocked"), &DynamicNavigationAgent3D::is_dynamic_navigation_blocked);
	ClassDB::bind_method(D_METHOD("get_dynamic_target_position"), &DynamicNavigationAgent3D::get_dynamic_target_position);

	ClassDB::bind_method(D_METHOD("set_dynamic_collision_mask", "mask"), &DynamicNavigationAgent3D::set_dynamic_collision_mask);
	ClassDB::bind_method(D_METHOD("get_dynamic_collision_mask"), &DynamicNavigationAgent3D::get_dynamic_collision_mask);
	ClassDB::bind_method(D_METHOD("set_dynamic_lookahead_distance", "distance"), &DynamicNavigationAgent3D::set_dynamic_lookahead_distance);
	ClassDB::bind_method(D_METHOD("get_dynamic_lookahead_distance"), &DynamicNavigationAgent3D::get_dynamic_lookahead_distance);
	ClassDB::bind_method(D_METHOD("set_dynamic_clearance", "clearance"), &DynamicNavigationAgent3D::set_dynamic_clearance);
	ClassDB::bind_method(D_METHOD("get_dynamic_clearance"), &DynamicNavigationAgent3D::get_dynamic_clearance);
	ClassDB::bind_method(D_METHOD("set_dynamic_check_interval", "interval"), &DynamicNavigationAgent3D::set_dynamic_check_interval);
	ClassDB::bind_method(D_METHOD("get_dynamic_check_interval"), &DynamicNavigationAgent3D::get_dynamic_check_interval);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "dynamic_collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_dynamic_collision_mask", "get_dynamic_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dynamic_lookahead_distance", PROPERTY_HINT_RANGE, "1,20,0.1,or_greater,suffix:m"), "set_dynamic_lookahead_distance", "get_dynamic_lookahead_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dynamic_clearance", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:m"), "set_dynamic_clearance", "get_dynamic_clearance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dynamic_check_interval", PROPERTY_HINT_RANGE, "0.05,1,0.05,or_greater,suffix:s"), "set_dynamic_check_interval", "get_dynamic_check_interval");

	BIND_ENUM_CONSTANT(DYNAMIC_STATE_IDLE);
	BIND_ENUM_CONSTANT(DYNAMIC_STATE_FOLLOWING);
	BIND_ENUM_CONSTANT(DYNAMIC_STATE_PLANNING);
	BIND_ENUM_CONSTANT(DYNAMIC_STATE_BLOCKED);
	BIND_ENUM_CONSTANT(DYNAMIC_STATE_ARRIVED);

	ADD_SIGNAL(MethodInfo("dynamic_navigation_state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("dynamic_path_changed", PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "path")));
	ADD_SIGNAL(MethodInfo("dynamic_target_reached", PropertyInfo(Variant::VECTOR3, "target")));
	ADD_SIGNAL(MethodInfo("dynamic_path_blocked", PropertyInfo(Variant::VECTOR3, "obstacle_position")));
}

DynamicNavigationAgent3D::DynamicNavigationAgent3D() {
	query_shape.instantiate();
	_update_query_shape();
}

void DynamicNavigationAgent3D::_notification(int p_what) {
	NavigationAgent3D::_notification(p_what);
	if (p_what == NOTIFICATION_INTERNAL_PHYSICS_PROCESS) {
		_update_dynamic_navigation();
	}
}

void DynamicNavigationAgent3D::_set_dynamic_state(DynamicState p_state) {
	if (dynamic_state == p_state) {
		return;
	}
	dynamic_state = p_state;
	emit_signal(SNAME("dynamic_navigation_state_changed"), (int)dynamic_state);
}

void DynamicNavigationAgent3D::_update_query_shape() {
	if (query_shape.is_null()) {
		return;
	}
	const real_t query_radius = MAX(get_radius() + dynamic_clearance, 0.05);
	query_shape->set_radius(query_radius);
	query_shape->set_height(MAX(get_height(), query_radius * 2.0));
}

void DynamicNavigationAgent3D::set_dynamic_collision_mask(uint32_t p_mask) {
	dynamic_collision_mask = p_mask;
}

void DynamicNavigationAgent3D::set_dynamic_lookahead_distance(real_t p_distance) {
	dynamic_lookahead_distance = MAX(p_distance, 0.5);
	check_timer = dynamic_check_interval;
}

void DynamicNavigationAgent3D::set_dynamic_clearance(real_t p_clearance) {
	dynamic_clearance = MAX(p_clearance, 0.0);
	_update_query_shape();
	check_timer = dynamic_check_interval;
}

void DynamicNavigationAgent3D::set_dynamic_check_interval(real_t p_interval) {
	dynamic_check_interval = CLAMP(p_interval, 0.05, 2.0);
}

void DynamicNavigationAgent3D::set_dynamic_target_position(const Vector3 &p_position) {
	if (dynamic_target_active && dynamic_target_position.is_equal_approx(p_position)) {
		return;
	}
	dynamic_target_position = p_position;
	dynamic_target_active = true;
	detour_path.clear();
	detour_index = 0;
	blocked_retry_timer = 0.0;
	check_timer = dynamic_check_interval;
	NavigationAgent3D::set_target_position(p_position);
	_set_dynamic_state(DYNAMIC_STATE_FOLLOWING);
}

void DynamicNavigationAgent3D::clear_dynamic_target() {
	dynamic_target_active = false;
	detour_path.clear();
	detour_index = 0;
	blocked_retry_timer = 0.0;
	check_timer = 0.0;
	_set_dynamic_state(DYNAMIC_STATE_IDLE);
}

void DynamicNavigationAgent3D::request_dynamic_repath() {
	if (!dynamic_target_active) {
		return;
	}
	detour_path.clear();
	detour_index = 0;
	blocked_retry_timer = 0.0;
	check_timer = dynamic_check_interval;
	NavigationAgent3D::set_target_position(dynamic_target_position);
	_set_dynamic_state(DYNAMIC_STATE_PLANNING);
}

bool DynamicNavigationAgent3D::_cast_segment(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector3 &p_from, const Vector3 &p_to, real_t &r_safe_fraction, PhysicsDirectSpaceState3D::ShapeRestInfo *r_rest_info) const {
	if (p_space_state == nullptr || query_shape.is_null()) {
		r_safe_fraction = 1.0;
		return true;
	}
	const Vector3 motion = p_to - p_from;
	if (motion.length_squared() <= CMP_EPSILON) {
		r_safe_fraction = 1.0;
		return true;
	}

	PhysicsDirectSpaceState3D::ShapeParameters query;
	query.shape_rid = query_shape->get_rid();
	query.transform = Transform3D(Basis(), p_from + Vector3(0.0, get_height() * 0.5, 0.0));
	query.motion = motion;
	query.margin = 0.01;
	query.collision_mask = dynamic_collision_mask;
	query.collide_with_bodies = true;
	query.collide_with_areas = false;
	if (Object::cast_to<CollisionObject3D>(p_actor) != nullptr) {
		query.exclude.insert(Object::cast_to<CollisionObject3D>(p_actor)->get_rid());
	}

	real_t unsafe_fraction = 1.0;
	const bool query_succeeded = p_space_state->cast_motion(query, r_safe_fraction, unsafe_fraction, nullptr);
	if (!query_succeeded) {
		r_safe_fraction = 0.0;
		return false;
	}
	if (r_safe_fraction >= 0.999) {
		return true;
	}

	// Jolt does not support ShapeRestInfo as an output parameter to
	// cast_motion(). Resolve the hit with a separate overlap query at the
	// unsafe fraction so the detour planner can still identify the obstacle.
	if (r_rest_info != nullptr) {
		PhysicsDirectSpaceState3D::ShapeParameters contact_query = query;
		contact_query.transform.origin += motion * CLAMP(unsafe_fraction + 0.002, 0.0, 1.0);
		contact_query.motion = Vector3();
		contact_query.margin = 0.05;
		if (!p_space_state->rest_info(contact_query, r_rest_info)) {
			PhysicsDirectSpaceState3D::ShapeResult overlaps[8];
			const int overlap_count = p_space_state->intersect_shape(contact_query, overlaps, 8);
			if (overlap_count > 0) {
				r_rest_info->rid = overlaps[0].rid;
				r_rest_info->collider_id = overlaps[0].collider_id;
				r_rest_info->shape = overlaps[0].shape;
			}
		}
	}
	return false;
}

bool DynamicNavigationAgent3D::_path_is_clear(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector<Vector3> &p_path) const {
	if (p_path.size() < 2) {
		return true;
	}
	for (int i = 1; i < p_path.size(); i++) {
		real_t safe_fraction = 1.0;
		if (!_cast_segment(p_actor, p_space_state, p_path[i - 1], p_path[i], safe_fraction)) {
			return false;
		}
	}
	return true;
}

Vector<Vector3> DynamicNavigationAgent3D::_get_clear_navigation_path(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector3 &p_from, const Vector3 &p_to, bool &r_valid) const {
	Vector<Vector3> result;
	r_valid = false;
	const RID map = get_navigation_map();
	if (!map.is_valid() || NavigationServer3D::get_singleton()->map_get_iteration_id(map) == 0) {
		return result;
	}
	const Vector<Vector3> query_path = NavigationServer3D::get_singleton()->map_get_path(map, p_from, p_to, true, get_navigation_layers());
	if (query_path.is_empty()) {
		return result;
	}
	result.push_back(p_from);
	for (int i = 0; i < query_path.size(); i++) {
		if (result[result.size() - 1].distance_to(query_path[i]) > 0.05) {
			result.push_back(query_path[i]);
		}
	}
	if (result[result.size() - 1].distance_to(p_to) > 0.05) {
		result.push_back(p_to);
	}
	r_valid = result.size() >= 2 && _path_is_clear(p_actor, p_space_state, result);
	return result;
}

bool DynamicNavigationAgent3D::_project_to_navigation_map(const Vector3 &p_position, Vector3 &r_projected) const {
	const RID map = get_navigation_map();
	if (!map.is_valid() || NavigationServer3D::get_singleton()->map_get_iteration_id(map) == 0) {
		return false;
	}
	r_projected = NavigationServer3D::get_singleton()->map_get_closest_point(map, p_position);
	const Vector2 horizontal_error(r_projected.x - p_position.x, r_projected.z - p_position.z);
	return r_projected.is_finite() && horizontal_error.length() <= 1.5 && Math::abs(r_projected.y - p_position.y) <= 1.5;
}

bool DynamicNavigationAgent3D::_find_blocked_segment(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector<Vector3> &p_path, Vector3 &r_block_position, Vector3 &r_block_direction, PhysicsDirectSpaceState3D::ShapeRestInfo &r_rest_info) const {
	if (p_path.is_empty()) {
		return false;
	}
	Vector3 from = p_actor->get_global_position();
	real_t checked_distance = 0.0;
	const int first_index = CLAMP(get_current_navigation_path_index(), 0, p_path.size() - 1);
	for (int i = first_index; i < p_path.size(); i++) {
		const Vector3 to = p_path[i];
		const real_t segment_length = from.distance_to(to);
		if (segment_length <= 0.05) {
			from = to;
			continue;
		}
		if (checked_distance >= dynamic_lookahead_distance) {
			break;
		}
		real_t safe_fraction = 1.0;
		PhysicsDirectSpaceState3D::ShapeRestInfo rest_info;
		const real_t checked_segment_length = MIN(segment_length, dynamic_lookahead_distance - checked_distance);
		const Vector3 checked_to = from + (to - from).normalized() * checked_segment_length;
		if (!_cast_segment(p_actor, p_space_state, from, checked_to, safe_fraction, &rest_info)) {
			r_block_position = from.lerp(checked_to, CLAMP(safe_fraction, 0.0, 1.0));
			r_block_direction = (to - from).normalized();
			r_rest_info = rest_info;
			return true;
		}
		checked_distance += checked_segment_length;
		if (checked_segment_length < segment_length) {
			break;
		}
		from = to;
	}
	return false;
}

bool DynamicNavigationAgent3D::_build_single_obstacle_detour(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector3 &p_block_position, const Vector3 &p_block_direction, const PhysicsDirectSpaceState3D::ShapeRestInfo &p_rest_info) {
	const RID map = get_navigation_map();
	if (!map.is_valid()) {
		return false;
	}
	Vector3 forward(p_block_direction.x, 0.0, p_block_direction.z);
	if (forward.length_squared() < 0.001) {
		return false;
	}
	forward.normalize();
	const Vector3 side(-forward.z, 0.0, forward.x);

	Vector3 obstacle_center = p_block_position - forward * (get_radius() + 0.2);
	real_t half_forward = 0.6;
	real_t half_side = 0.6;
	CollisionObject3D *collider = Object::cast_to<CollisionObject3D>(ObjectDB::get_instance(p_rest_info.collider_id));
	if (collider != nullptr && p_rest_info.shape >= 0) {
		const uint32_t owner_id = collider->shape_find_owner(p_rest_info.shape);
		int owner_shape_index = -1;
		for (int i = 0; i < collider->shape_owner_get_shape_count(owner_id); i++) {
			if (collider->shape_owner_get_shape_index(owner_id, i) == p_rest_info.shape) {
				owner_shape_index = i;
				break;
			}
		}
		Ref<Shape3D> shape = owner_shape_index >= 0 ? collider->shape_owner_get_shape(owner_id, owner_shape_index) : Ref<Shape3D>();
		if (shape.is_valid()) {
			const Transform3D shape_transform = collider->get_global_transform() * collider->shape_owner_get_transform(owner_id);
			obstacle_center = shape_transform.origin;
			Ref<BoxShape3D> box_shape = shape;
			if (box_shape.is_valid()) {
				const Vector3 size = box_shape->get_size();
				const Vector3 axis_x = shape_transform.basis.xform(Vector3(size.x * 0.5, 0.0, 0.0));
				const Vector3 axis_z = shape_transform.basis.xform(Vector3(0.0, 0.0, size.z * 0.5));
				half_forward = Math::abs(axis_x.dot(forward)) + Math::abs(axis_z.dot(forward));
				half_side = Math::abs(axis_x.dot(side)) + Math::abs(axis_z.dot(side));
			}
		}
	}
	obstacle_center.y = p_actor->get_global_position().y;

	Vector<Vector3> best_path;
	real_t best_length = INFINITY;
	const real_t base_clearance = get_radius() + dynamic_clearance + 0.12;
	const real_t forward_margin = half_forward + base_clearance;
	const Vector3 actor_position = p_actor->get_global_position();
	const real_t clearance_steps[] = { 0.0, 0.25, 0.5 };

	for (int side_sign : { -1, 1 }) {
		for (real_t extra_clearance : clearance_steps) {
			const real_t side_offset = half_side + base_clearance + extra_clearance;
			Vector3 before;
			Vector3 after;
			const Vector3 offset = side * (side_offset * side_sign);
			if (!_project_to_navigation_map(obstacle_center - forward * forward_margin + offset, before) ||
					!_project_to_navigation_map(obstacle_center + forward * forward_margin + offset, after)) {
				continue;
			}

			bool first_leg_valid = false;
			Vector<Vector3> candidate = _get_clear_navigation_path(p_actor, p_space_state, actor_position, before, first_leg_valid);
			if (!first_leg_valid) {
				continue;
			}
			if (candidate[candidate.size() - 1].distance_to(after) > 0.05) {
				candidate.push_back(after);
			}
			bool final_leg_valid = false;
			Vector<Vector3> final_leg = _get_clear_navigation_path(p_actor, p_space_state, after, dynamic_target_position, final_leg_valid);
			if (!final_leg_valid) {
				continue;
			}
			for (int i = 1; i < final_leg.size(); i++) {
				if (candidate[candidate.size() - 1].distance_to(final_leg[i]) > 0.05) {
					candidate.push_back(final_leg[i]);
				}
			}
			if (candidate.size() < 2 || !_path_is_clear(p_actor, p_space_state, candidate)) {
				continue;
			}
			real_t path_length = 0.0;
			for (int i = 1; i < candidate.size(); i++) {
				path_length += candidate[i - 1].distance_to(candidate[i]);
			}
			if (path_length < best_length) {
				best_length = path_length;
				best_path = candidate;
			}
		}
	}

	if (best_path.is_empty()) {
		return false;
	}
	detour_path = best_path;
	detour_index = 0;
	check_timer = dynamic_check_interval;
	emit_signal(SNAME("dynamic_path_changed"), get_current_dynamic_path());
	return true;
}

void DynamicNavigationAgent3D::_retry_blocked_path() {
	if (!dynamic_target_active) {
		_set_dynamic_state(DYNAMIC_STATE_IDLE);
		return;
	}
	NavigationAgent3D::set_target_position(dynamic_target_position);
	detour_path.clear();
	detour_index = 0;
	blocked_retry_timer = 0.0;
	check_timer = dynamic_check_interval;
	_set_dynamic_state(DYNAMIC_STATE_PLANNING);
}

void DynamicNavigationAgent3D::_update_dynamic_navigation() {
	if (!dynamic_target_active) {
		return;
	}
	const uint64_t physics_frame = Engine::get_singleton()->get_physics_frames();
	if (physics_frame == last_update_physics_frame) {
		return;
	}
	last_update_physics_frame = physics_frame;

	Node3D *actor = Object::cast_to<Node3D>(get_parent());
	if (actor == nullptr || !actor->is_inside_tree()) {
		_set_dynamic_state(DYNAMIC_STATE_BLOCKED);
		return;
	}
	const Vector3 actor_position = actor->get_global_position();
	if (Vector2(actor_position.x - dynamic_target_position.x, actor_position.z - dynamic_target_position.z).length() <= get_target_desired_distance()) {
		detour_path.clear();
		if (dynamic_state != DYNAMIC_STATE_ARRIVED) {
			_set_dynamic_state(DYNAMIC_STATE_ARRIVED);
			emit_signal(SNAME("dynamic_target_reached"), dynamic_target_position);
		}
		return;
	}
	// NavigationAgent3D refreshes its cached path lazily. Refresh before we
	// inspect it, including while blocked, so the first target is not reported
	// blocked simply because no caller has requested a path position yet.
	NavigationAgent3D::is_target_reachable();

	Ref<World3D> world = actor->get_world_3d();
	PhysicsDirectSpaceState3D *space_state = world.is_valid() ? world->get_direct_space_state() : nullptr;
	if (space_state == nullptr) {
		_set_dynamic_state(DYNAMIC_STATE_PLANNING);
		return;
	}

	check_timer += get_physics_process_delta_time();
	if (!detour_path.is_empty()) {
		if (check_timer < dynamic_check_interval) {
			_set_dynamic_state(DYNAMIC_STATE_FOLLOWING);
			return;
		}
		check_timer = 0.0;
		Vector<Vector3> remaining_path;
		remaining_path.push_back(actor_position);
		for (int i = detour_index; i < detour_path.size(); i++) {
			if (remaining_path[remaining_path.size() - 1].distance_to(detour_path[i]) > 0.05) {
				remaining_path.push_back(detour_path[i]);
			}
		}
		if (!_path_is_clear(actor, space_state, remaining_path)) {
			detour_path.clear();
			detour_index = 0;
			NavigationAgent3D::set_target_position(dynamic_target_position);
			_set_dynamic_state(DYNAMIC_STATE_PLANNING);
			return;
		}
		while (detour_index < detour_path.size() - 1 && actor_position.distance_to(detour_path[detour_index]) <= MAX(get_path_desired_distance(), 0.25)) {
			detour_index++;
		}
		_set_dynamic_state(DYNAMIC_STATE_FOLLOWING);
		return;
	}

	if (dynamic_state == DYNAMIC_STATE_BLOCKED) {
		blocked_retry_timer += get_physics_process_delta_time();
		if (blocked_retry_timer < 0.5) {
			return;
		}
		blocked_retry_timer = 0.0;
	}
	if (check_timer < dynamic_check_interval && dynamic_state != DYNAMIC_STATE_PLANNING) {
		return;
	}
	check_timer = 0.0;
	_set_dynamic_state(DYNAMIC_STATE_PLANNING);
	_update_query_shape();

	Vector<Vector3> path;
	const Vector<Vector3> &base_path = get_current_navigation_path();
	if (!base_path.is_empty()) {
		path = base_path;
	}
	if (path.is_empty()) {
		if (NavigationServer3D::get_singleton()->map_get_iteration_id(get_navigation_map()) == 0) {
			return;
		}
		_set_dynamic_state(DYNAMIC_STATE_BLOCKED);
		emit_signal(SNAME("dynamic_path_blocked"), actor_position);
		return;
	}

	Vector3 block_position;
	Vector3 block_direction;
	PhysicsDirectSpaceState3D::ShapeRestInfo rest_info;
	if (!_find_blocked_segment(actor, space_state, path, block_position, block_direction, rest_info)) {
		if (NavigationAgent3D::is_navigation_finished()) {
			if (dynamic_state != DYNAMIC_STATE_ARRIVED) {
				_set_dynamic_state(DYNAMIC_STATE_ARRIVED);
				emit_signal(SNAME("dynamic_target_reached"), dynamic_target_position);
			}
		} else {
			_set_dynamic_state(DYNAMIC_STATE_FOLLOWING);
		}
		return;
	}

	if (_build_single_obstacle_detour(actor, space_state, block_position, block_direction, rest_info)) {
		blocked_retry_timer = 0.0;
		_set_dynamic_state(DYNAMIC_STATE_FOLLOWING);
		return;
	}
	_set_dynamic_state(DYNAMIC_STATE_BLOCKED);
	blocked_retry_timer = 0.0;
	emit_signal(SNAME("dynamic_path_blocked"), block_position);
}

Vector3 DynamicNavigationAgent3D::get_next_dynamic_path_position() {
	_update_dynamic_navigation();
	const Vector3 base_next = NavigationAgent3D::get_next_path_position();
	if (detour_path.is_empty()) {
		return base_next;
	}
	Node3D *actor = Object::cast_to<Node3D>(get_parent());
	if (actor == nullptr) {
		return base_next;
	}
	while (detour_index < detour_path.size() - 1 && actor->get_global_position().distance_to(detour_path[detour_index]) <= MAX(get_path_desired_distance(), 0.25)) {
		detour_index++;
	}
	return detour_path[detour_index];
}

PackedVector3Array DynamicNavigationAgent3D::get_current_dynamic_path() const {
	PackedVector3Array result;
	if (!detour_path.is_empty()) {
		for (int i = detour_index; i < detour_path.size(); i++) {
			result.push_back(detour_path[i]);
		}
		return result;
	}
	const Vector<Vector3> &path = get_current_navigation_path();
	const int start = CLAMP(get_current_navigation_path_index(), 0, path.size());
	for (int i = start; i < path.size(); i++) {
		result.push_back(path[i]);
	}
	return result;
}

bool DynamicNavigationAgent3D::is_dynamic_navigation_finished() const {
	return dynamic_state == DYNAMIC_STATE_ARRIVED;
}
