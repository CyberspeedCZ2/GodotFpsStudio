/**************************************************************************/
/*  dynamic_navigation_agent_3d.h                                         */
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

#pragma once

#include "scene/3d/navigation/navigation_agent_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

class CollisionObject3D;
class Node3D;
class DynamicNavigationAgent3D : public NavigationAgent3D {
	GDCLASS(DynamicNavigationAgent3D, NavigationAgent3D);

public:
	enum DynamicState {
		DYNAMIC_STATE_IDLE,
		DYNAMIC_STATE_FOLLOWING,
		DYNAMIC_STATE_PLANNING,
		DYNAMIC_STATE_BLOCKED,
		DYNAMIC_STATE_ARRIVED,
	};

private:
	uint32_t dynamic_collision_mask = 3;
	real_t dynamic_lookahead_distance = 8.0;
	real_t dynamic_clearance = 0.2;
	real_t dynamic_check_interval = 0.15;

	Ref<CapsuleShape3D> query_shape;
	Vector<Vector3> detour_path;
	int detour_index = 0;
	Vector3 dynamic_target_position;
	bool dynamic_target_active = false;
	DynamicState dynamic_state = DYNAMIC_STATE_IDLE;
	uint64_t last_update_physics_frame = UINT64_MAX;
	real_t check_timer = 0.0;
	real_t blocked_retry_timer = 0.0;

	void _notification(int p_what);
	static void _bind_methods();
	void _set_dynamic_state(DynamicState p_state);
	void _update_dynamic_navigation();
	void _update_query_shape();

	bool _cast_segment(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector3 &p_from, const Vector3 &p_to, real_t &r_safe_fraction, PhysicsDirectSpaceState3D::ShapeRestInfo *r_rest_info = nullptr) const;
	bool _path_is_clear(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector<Vector3> &p_path) const;
	Vector<Vector3> _get_clear_navigation_path(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector3 &p_from, const Vector3 &p_to, bool &r_valid) const;
	bool _find_blocked_segment(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector<Vector3> &p_path, Vector3 &r_block_position, Vector3 &r_block_direction, PhysicsDirectSpaceState3D::ShapeRestInfo &r_rest_info) const;
	bool _build_single_obstacle_detour(Node3D *p_actor, PhysicsDirectSpaceState3D *p_space_state, const Vector3 &p_block_position, const Vector3 &p_block_direction, const PhysicsDirectSpaceState3D::ShapeRestInfo &p_rest_info);
	bool _project_to_navigation_map(const Vector3 &p_position, Vector3 &r_projected) const;
	void _retry_blocked_path();

public:
	DynamicNavigationAgent3D();

	void set_dynamic_target_position(const Vector3 &p_position);
	void clear_dynamic_target();
	void request_dynamic_repath();
	Vector3 get_next_dynamic_path_position();
	PackedVector3Array get_current_dynamic_path() const;
	DynamicState get_dynamic_state() const { return dynamic_state; }
	bool is_dynamic_navigation_finished() const;
	bool is_dynamic_navigation_blocked() const { return dynamic_state == DYNAMIC_STATE_BLOCKED; }
	Vector3 get_dynamic_target_position() const { return dynamic_target_position; }

	void set_dynamic_collision_mask(uint32_t p_mask);
	uint32_t get_dynamic_collision_mask() const { return dynamic_collision_mask; }

	void set_dynamic_lookahead_distance(real_t p_distance);
	real_t get_dynamic_lookahead_distance() const { return dynamic_lookahead_distance; }

	void set_dynamic_clearance(real_t p_clearance);
	real_t get_dynamic_clearance() const { return dynamic_clearance; }

	void set_dynamic_check_interval(real_t p_interval);
	real_t get_dynamic_check_interval() const { return dynamic_check_interval; }
};

VARIANT_ENUM_CAST(DynamicNavigationAgent3D::DynamicState);
