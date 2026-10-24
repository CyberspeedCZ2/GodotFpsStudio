/**************************************************************************/
/*  path_3d_editor_plugin.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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

#include "path_3d_editor_plugin.h"

#include "npc_route_geometry.h"

#include "core/math/geometry_2d.h"
#include "core/math/geometry_3d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "core/os/time.h"
#include "core/templates/pair.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/debugger/view_3d_controller.h"
#include "scene/gui/check_button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/curve.h"
#include "servers/navigation_3d/navigation_server_3d.h"

static const StringName &npc_route_actions_meta() {
	return SNAME("_fps_npc_point_actions");
}

static const StringName &npc_route_width_meta() {
	return SNAME("_fps_npc_route_width");
}

static Ref<CapsuleShape3D> _get_npc_route_clearance_shape() {
	static Ref<CapsuleShape3D> shape;
	if (shape.is_null()) {
		shape.instantiate();
		shape->set_radius(0.65);
		shape->set_height(1.9);
	}
	return shape;
}

static String _get_npc_route_action_name(int p_action) {
	switch (p_action) {
		case Path3DEditorPlugin::NPC_POINT_ACTION_WAIT:
			return "wait";
		case Path3DEditorPlugin::NPC_POINT_ACTION_LOOK_AT:
			return "look_at";
		case Path3DEditorPlugin::NPC_POINT_ACTION_PLAY_ANIMATION:
			return "play_animation";
		case Path3DEditorPlugin::NPC_POINT_ACTION_INTERACT:
			return "interact";
		case Path3DEditorPlugin::NPC_POINT_ACTION_ATTACK:
			return "attack";
		case Path3DEditorPlugin::NPC_POINT_ACTION_TAKE_COVER:
			return "take_cover";
		default:
			return "move";
	}
}

String Path3DGizmo::get_handle_name(int p_id, bool p_secondary) const {
	Ref<Curve3D> c = path->get_curve();
	if (c.is_null()) {
		return "";
	}

	// Primary handles: position.
	if (!p_secondary) {
		return TTR("Curve Point #") + itos(p_id);
	}

	// Secondary handles: in, out, tilt.
	const HandleInfo info = _secondary_handles_info[p_id];
	switch (info.type) {
		case HandleType::HANDLE_TYPE_IN:
			return TTR("Handle In #") + itos(info.point_idx);
		case HandleType::HANDLE_TYPE_OUT:
			return TTR("Handle Out #") + itos(info.point_idx);
		case HandleType::HANDLE_TYPE_TILT:
			return TTR("Handle Tilt #") + itos(info.point_idx);
	}

	return "";
}

Variant Path3DGizmo::get_handle_value(int p_id, bool p_secondary) const {
	Ref<Curve3D> c = path->get_curve();
	if (c.is_null()) {
		return Variant();
	}

	// Primary handles: position.
	if (!p_secondary) {
		original = c->get_point_position(p_id);
		return original;
	}

	// Secondary handles: in, out, tilt.
	const HandleInfo info = _secondary_handles_info[p_id];
	Vector3 ofs;
	switch (info.type) {
		case HandleType::HANDLE_TYPE_TILT:
			return c->get_point_tilt(info.point_idx);
		case HandleType::HANDLE_TYPE_IN:
			ofs = c->get_point_in(info.point_idx);
			break;
		case HandleType::HANDLE_TYPE_OUT:
			ofs = c->get_point_out(info.point_idx);
			break;
	}

	original = ofs + c->get_point_position(info.point_idx);
	return ofs;
}

void Path3DGizmo::set_handle(int p_id, bool p_secondary, Camera3D *p_camera, const Point2 &p_point) {
	Ref<Curve3D> c = path->get_curve();
	if (c.is_null()) {
		return;
	}

	const Transform3D gt = path->get_global_transform();
	const Transform3D gi = gt.affine_inverse();
	const Vector3 ray_from = p_camera->project_ray_origin(p_point);
	const Vector3 ray_dir = p_camera->project_ray_normal(p_point);
	const Plane p = Plane(p_camera->get_transform().basis.get_column(2), gt.xform(original));

	// Primary handles: position.
	if (!p_secondary) {
		Vector3 inters;
		// Special case for primary handle, the handle id equals control point id.
		const int idx = p_id;
		auto set_route_point_position = [&](const Vector3 &p_local_position) {
			Path3DEditorPlugin *route_plugin = Path3DEditorPlugin::singleton;
			if (c->get_point_position(idx).is_equal_approx(p_local_position)) {
				return;
			}
			if (route_plugin && route_plugin->_is_fps_npc_route()) {
				PhysicsDirectSpaceState3D *space = p_camera->get_world_3d()->get_direct_space_state();
				const Transform3D path_transform = path->get_global_transform();
				const int target_collisions = route_plugin->_npc_route_adjacent_collision_count(c, idx, path_transform, space, idx, p_local_position);
				if (target_collisions > 0) {
					const int current_collisions = route_plugin->_npc_route_adjacent_collision_count(c, idx, path_transform, space);
					if (current_collisions == 0) {
						route_plugin->_set_route_status(TTR("Static obstacle detected; waypoint stopped with clearance"));
						return;
					}
				}
			}
			c->set_point_position(idx, p_local_position);
		};
		if (!Path3DEditorPlugin::singleton->_edit.waiting_handle_physics || !Path3DEditorPlugin::singleton->_edit.in_physics_frame) {
			Path3DEditorPlugin::singleton->_edit.waiting_handle_physics = true;
			Path3DEditorPlugin::singleton->_edit.gizmo_handle = p_id;
			Path3DEditorPlugin::singleton->_edit.gizmo_handle_secondary = p_secondary;
			Path3DEditorPlugin::singleton->_edit.gizmo_camera = p_camera;
			Path3DEditorPlugin::singleton->_edit.mouse_pos = p_point;
			return;
			// Only continue if inside physics frame and waiting for physics.
		}
		if (Path3DEditorPlugin::singleton->snap_to_collider) {
			PhysicsDirectSpaceState3D *ss = p_camera->get_world_3d()->get_direct_space_state();

			PhysicsDirectSpaceState3D::RayParameters ray_params;
			ray_params.from = ray_from;
			ray_params.to = ray_from + ray_dir * p_camera->get_far();
			PhysicsDirectSpaceState3D::RayResult result;
			if (ss->intersect_ray(ray_params, result)) {
				Vector3 world_position = result.position;
				Vector3 navmesh_position;
				if (Path3DEditorPlugin::singleton->_is_fps_npc_route() && Path3DEditorPlugin::singleton->npc_route_snap_to_navmesh) {
					if (!Path3DEditorPlugin::singleton->_snap_npc_route_point_to_navmesh(world_position, navmesh_position)) {
						Path3DEditorPlugin::singleton->_set_route_status(TTR("No nearby NavigationMesh; waypoint stayed in place"));
						return;
					}
					world_position = navmesh_position;
				}
				Vector3 local = gi.xform(world_position);
				set_route_point_position(local);
				return;
			}
			// Will continue and do the plane intersect_ray if doesn't hit anything.
		}
		if (p.intersects_ray(ray_from, ray_dir, &inters)) {
			if (Node3DEditor::get_singleton()->is_snap_enabled()) {
				float snap = Node3DEditor::get_singleton()->get_translate_snap();
				inters.snapf(snap);
			}

			Vector3 navmesh_position;
			if (Path3DEditorPlugin::singleton->_is_fps_npc_route() && Path3DEditorPlugin::singleton->npc_route_snap_to_navmesh) {
				if (!Path3DEditorPlugin::singleton->_snap_npc_route_point_to_navmesh(inters, navmesh_position)) {
					Path3DEditorPlugin::singleton->_set_route_status(TTR("No nearby NavigationMesh; waypoint stayed in place"));
					return;
				}
				inters = navmesh_position;
			}
			Vector3 local = gi.xform(inters);
			set_route_point_position(local);
		}

		return;
	}

	// Secondary handles: in, out, tilt.
	const HandleInfo info = _secondary_handles_info[p_id];
	switch (info.type) {
		case HandleType::HANDLE_TYPE_OUT:
		case HandleType::HANDLE_TYPE_IN: {
			const int idx = info.point_idx;
			const Vector3 base = c->get_point_position(idx);

			Vector3 inters;
			if (p.intersects_ray(ray_from, ray_dir, &inters)) {
				if (!Path3DEditorPlugin::singleton->is_handle_clicked()) {
					orig_in_length = c->get_point_in(idx).length();
					orig_out_length = c->get_point_out(idx).length();
					Path3DEditorPlugin::singleton->set_handle_clicked(true);
				}

				Vector3 local = gi.xform(inters) - base;
				if (Node3DEditor::get_singleton()->is_snap_enabled()) {
					float snap = Node3DEditor::get_singleton()->get_translate_snap();
					local.snapf(snap);
				}

				// Determine if control points should be swapped based on delta movement.
				// Only run on the next update after an overlap is detected, to get proper delta movement.
				if (control_points_overlapped) {
					control_points_overlapped = false;
					Vector3 delta = local - (info.type == HANDLE_TYPE_IN ? c->get_point_in(idx) : c->get_point_out(idx));
					Vector3 p0 = c->get_point_position(idx - 1) - base;
					Vector3 p1 = c->get_point_position(idx + 1) - base;
					HandleType new_type = Math::abs(delta.angle_to(p0)) < Math::abs(delta.angle_to(p1)) ? HANDLE_TYPE_IN : HANDLE_TYPE_OUT;
					if (info.type != new_type) {
						swapped_control_points_idx = idx;
					}
				}

				// Detect control points overlap.
				bool control_points_equal = c->get_point_in(idx).is_equal_approx(c->get_point_out(idx));
				if (idx > 0 && idx < (c->get_point_count() - 1) && control_points_equal) {
					control_points_overlapped = true;
				}

				HandleType control_type = info.type;
				if (swapped_control_points_idx == idx) {
					control_type = info.type == HANDLE_TYPE_IN ? HANDLE_TYPE_OUT : HANDLE_TYPE_IN;
				}

				if (control_type == HandleType::HANDLE_TYPE_IN) {
					c->set_point_in(idx, local);
					if (Path3DEditorPlugin::singleton->mirror_angle_enabled()) {
						c->set_point_out(idx, Path3DEditorPlugin::singleton->mirror_length_enabled() ? -local : (-local.normalized() * orig_out_length));
					}
				} else {
					c->set_point_out(idx, local);
					if (Path3DEditorPlugin::singleton->mirror_angle_enabled()) {
						c->set_point_in(idx, Path3DEditorPlugin::singleton->mirror_length_enabled() ? -local : (-local.normalized() * orig_in_length));
					}
				}
			}
			break;
		}
		case HandleType::HANDLE_TYPE_TILT: {
			const int idx = info.point_idx;
			const Vector3 position = c->get_point_position(idx);
			const Basis posture = c->get_point_baked_posture(idx);
			const Vector3 tangent = -posture.get_column(2);
			const Vector3 up = posture.get_column(1);
			const Plane tilt_plane_global = gt.xform(Plane(tangent, position));

			Vector3 intersection;

			if (tilt_plane_global.intersects_ray(ray_from, ray_dir, &intersection)) {
				Vector3 direction = gi.xform(intersection) - position;
				real_t tilt_angle = up.signed_angle_to(direction, tangent);

				if (Node3DEditor::get_singleton()->is_snap_enabled()) {
					real_t snap_degrees = Node3DEditor::get_singleton()->get_rotate_snap();
					tilt_angle = Math::deg_to_rad(Math::snapped(Math::rad_to_deg(tilt_angle), snap_degrees));
				}

				c->set_point_tilt(idx, tilt_angle);
			}
			break;
		}
	}
}

void Path3DGizmo::commit_handle(int p_id, bool p_secondary, const Variant &p_restore, bool p_cancel) {
	swapped_control_points_idx = -1;
	control_points_overlapped = false;

	Ref<Curve3D> c = path->get_curve();
	if (c.is_null()) {
		return;
	}

	if (!Path3DEditorPlugin::singleton) {
		return;
	}

	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();

	// Primary handles: position.
	if (!p_secondary && !Path3DEditorPlugin::singleton->curve_edit->is_pressed()) {
		// Special case for primary handle, the handle id equals control point id.
		const int idx = p_id;
		if (p_cancel) {
			c->set_point_position(idx, p_restore);
			return;
		}
		ur->create_action(TTR("Set Curve Point Position"));
		ur->add_do_method(c.ptr(), "set_point_position", idx, c->get_point_position(idx));
		ur->add_undo_method(c.ptr(), "set_point_position", idx, p_restore);
		ur->commit_action();

		return;
	}

	// Secondary handles: in, out, tilt.
	const HandleInfo info = _secondary_handles_info[p_id];
	const int idx = info.point_idx;
	switch (info.type) {
		case HandleType::HANDLE_TYPE_OUT: {
			if (p_cancel) {
				c->set_point_out(idx, p_restore);

				return;
			}

			ur->create_action(TTR("Set Curve Out Position"));
			ur->add_do_method(c.ptr(), "set_point_out", idx, c->get_point_out(idx));
			ur->add_undo_method(c.ptr(), "set_point_out", idx, p_restore);

			if (Path3DEditorPlugin::singleton->mirror_angle_enabled()) {
				ur->add_do_method(c.ptr(), "set_point_in", idx, Path3DEditorPlugin::singleton->mirror_length_enabled() ? -c->get_point_out(idx) : (-c->get_point_out(idx).normalized() * orig_in_length));
				ur->add_undo_method(c.ptr(), "set_point_in", idx, Path3DEditorPlugin::singleton->mirror_length_enabled() ? -static_cast<Vector3>(p_restore) : (-static_cast<Vector3>(p_restore).normalized() * orig_in_length));
			}

			ur->commit_action();
			break;
		}
		case HandleType::HANDLE_TYPE_IN: {
			if (p_cancel) {
				c->set_point_in(idx, p_restore);
				return;
			}

			ur->create_action(TTR("Set Curve In Position"));
			ur->add_do_method(c.ptr(), "set_point_in", idx, c->get_point_in(idx));
			ur->add_undo_method(c.ptr(), "set_point_in", idx, p_restore);

			if (Path3DEditorPlugin::singleton->mirror_angle_enabled()) {
				ur->add_do_method(c.ptr(), "set_point_out", idx, Path3DEditorPlugin::singleton->mirror_length_enabled() ? -c->get_point_in(idx) : (-c->get_point_in(idx).normalized() * orig_out_length));
				ur->add_undo_method(c.ptr(), "set_point_out", idx, Path3DEditorPlugin::singleton->mirror_length_enabled() ? -static_cast<Vector3>(p_restore) : (-static_cast<Vector3>(p_restore).normalized() * orig_out_length));
			}

			ur->commit_action();
			break;
		}
		case HandleType::HANDLE_TYPE_TILT: {
			if (p_cancel) {
				c->set_point_tilt(idx, p_restore);
				return;
			}
			ur->create_action(TTR("Set Curve Point Tilt"));
			ur->add_do_method(c.ptr(), "set_point_tilt", idx, c->get_point_tilt(idx));
			ur->add_undo_method(c.ptr(), "set_point_tilt", idx, p_restore);

			ur->commit_action();
			break;
		}
	}
}

void Path3DGizmo::redraw() {
	clear();

	Ref<StandardMaterial3D> path_thin_material = gizmo_plugin->get_material("path_thin_material", this);
	Ref<StandardMaterial3D> path_tilt_material = gizmo_plugin->get_material("path_tilt_material", this);
	Ref<StandardMaterial3D> path_tilt_muted_material = gizmo_plugin->get_material("path_tilt_muted_material", this);
	Ref<StandardMaterial3D> handles_material = gizmo_plugin->get_material("handles");
	Ref<StandardMaterial3D> first_pt_handle_material = gizmo_plugin->get_material("first_pt_handle");
	Ref<StandardMaterial3D> last_pt_handle_material = gizmo_plugin->get_material("last_pt_handle");
	Ref<StandardMaterial3D> closed_pt_handle_material = gizmo_plugin->get_material("closed_pt_handle");
	Ref<StandardMaterial3D> sec_handles_material = gizmo_plugin->get_material("sec_handles");

	first_pt_handle_material->set_albedo(Color(0.2, 1.0, 0.0));
	last_pt_handle_material->set_albedo(Color(1.0, 0.2, 0.0));
	closed_pt_handle_material->set_albedo(Color(1.0, 0.8, 0.0));

	Ref<Curve3D> c = path->get_curve();
	if (c.is_null()) {
		return;
	}

	debug_material = gizmo_plugin->get_material("path_material", this);

	Color path_color = path->get_debug_custom_color();
	if (path_color != Color(0.0, 0.0, 0.0)) {
		debug_material.instantiate();
		debug_material->set_albedo(path_color);
		debug_material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
		debug_material->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
		debug_material->set_flag(StandardMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
		debug_material->set_flag(StandardMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		debug_material->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
	}

	real_t interval = 0.1;
	const real_t length = c->get_baked_length();

	// 1. Draw curve and bones if it is visible (alpha > 0.0).
	if (length > CMP_EPSILON && path_color.a > 0.0) {
		const int sample_count = int(length / interval) + 2;
		interval = length / (sample_count - 1); // Recalculate real interval length.

		Vector<Transform3D> frames;
		frames.resize(sample_count);

		{
			Transform3D *w = frames.ptrw();

			for (int i = 0; i < sample_count; i++) {
				w[i] = c->sample_baked_with_rotation(i * interval, true, true);
			}
		}

		const Transform3D *r = frames.ptr();

		Vector<Vector3> _collision_segments;
		_collision_segments.resize((sample_count - 1) * 2);
		Vector3 *_collisions_ptr = _collision_segments.ptrw();

		Vector<Vector3> bones;
		bones.resize(sample_count * 4);
		Vector3 *bones_ptr = bones.ptrw();

		Vector<Vector3> ribbon;
		ribbon.resize(sample_count);
		Vector3 *ribbon_ptr = ribbon.ptrw();

		for (int i = 0; i < sample_count; i++) {
			const Vector3 p1 = r[i].origin;
			const Vector3 side = r[i].basis.get_column(0);
			const Vector3 up = r[i].basis.get_column(1);
			const Vector3 forward = r[i].basis.get_column(2);

			// Collision segments.
			if (i != sample_count - 1) {
				const Vector3 p2 = r[i + 1].origin;
				_collisions_ptr[(i * 2)] = p1;
				_collisions_ptr[(i * 2) + 1] = p2;
			}

			// Path3D as a ribbon.
			ribbon_ptr[i] = p1;

			if (i % 4 == 0) {
				// Draw fish bone every 4 points to reduce visual noise and performance impact
				// (compared to drawing it for every point).
				const Vector3 p_left = p1 + (side + forward - up * 0.3) * 0.06;
				const Vector3 p_right = p1 + (-side + forward - up * 0.3) * 0.06;

				const int bone_idx = i * 4;

				bones_ptr[bone_idx] = p1;
				bones_ptr[bone_idx + 1] = p_left;
				bones_ptr[bone_idx + 2] = p1;
				bones_ptr[bone_idx + 3] = p_right;
			}
		}

		add_collision_segments(_collision_segments);
		add_lines(bones, debug_material);
		add_vertices(ribbon, debug_material, Mesh::PRIMITIVE_LINE_STRIP);
	}

	// FPS Studio NPC routes keep their route body and waypoint markers visible even
	// when the Path3D itself is not selected. This is authoring visualization only;
	// the route still uses Godot's Curve3D and NavigationServer data.
	if (path->is_in_group("fps_npc_path")) {
		const real_t route_width = MAX(real_t(path->get_meta(npc_route_width_meta(), 0.8)), real_t(0.05)) * 1.25;
		Ref<Curve3D> route_curve = path->get_curve();
		if (length > CMP_EPSILON) {
			const int sample_count = CLAMP(int(length / 0.25) + 2, 2, 512);
			const real_t route_half_width = route_width * 0.5;
			const bool route_is_closed = route_curve->is_closed() && route_curve->get_point_count() > 2;
			const Vector3 route_up(0.0, 1.0, 0.0);

			// Sample positions only. Curve3D's transported frame can roll at sharp
			// or degenerate tangents, which makes a wide triangle strip fold over itself.
			// Build cross-sections from the horizontal path tangent to keep the width stable.
			Vector<Vector3> route_centers;
			route_centers.resize(sample_count);
			Vector3 *route_centers_ptr = route_centers.ptrw();
			for (int i = 0; i < sample_count; i++) {
				const real_t distance = length * real_t(i) / real_t(sample_count - 1);
				route_centers_ptr[i] = route_curve->sample_baked(distance, true) + route_up * 0.025;
			}

			Vector<Vector3> route_ribbon;
			route_ribbon.resize(sample_count * 2);
			Vector3 *route_ribbon_ptr = route_ribbon.ptrw();
			for (int i = 0; i < sample_count; i++) {
				const int previous_index = i > 0 ? i - 1 : (route_is_closed ? sample_count - 2 : 0);
				const int next_index = i + 1 < sample_count ? i + 1 : (route_is_closed ? 1 : sample_count - 1);
				Vector3 incoming = route_centers[i] - route_centers[previous_index];
				Vector3 outgoing = route_centers[next_index] - route_centers[i];
				incoming.y = 0.0;
				outgoing.y = 0.0;
				if (incoming.is_zero_approx()) {
					incoming = outgoing;
				}
				if (outgoing.is_zero_approx()) {
					outgoing = incoming;
				}
				incoming.normalize();
				outgoing.normalize();

				Vector3 side_in = incoming.cross(route_up).normalized();
				Vector3 side_out = outgoing.cross(route_up).normalized();
				if (side_in.is_zero_approx()) {
					side_in = side_out;
				}
				if (side_out.is_zero_approx()) {
					side_out = side_in;
				}
				if (side_in.is_zero_approx()) {
					side_in = Vector3(1, 0, 0);
					side_out = side_in;
				}

				Vector3 miter = side_in + side_out;
				if (miter.is_zero_approx()) {
					miter = side_out;
				} else {
					miter.normalize();
				}
				real_t miter_dot = miter.dot(side_out);
				if (miter_dot < 0.0) {
					miter = -miter;
					miter_dot = -miter_dot;
				}
				const real_t miter_length = MIN(route_width, route_half_width / MAX(miter_dot, real_t(0.5)));
				const Vector3 offset = miter * miter_length;
				route_ribbon_ptr[i * 2] = route_centers[i] - offset;
				route_ribbon_ptr[i * 2 + 1] = route_centers[i] + offset;
			}
			add_vertices(route_ribbon, gizmo_plugin->get_material("fps_route_corridor", this), Mesh::PRIMITIVE_TRIANGLE_STRIP);
			add_vertices(route_centers, gizmo_plugin->get_material("fps_route_centerline", this), Mesh::PRIMITIVE_LINE_STRIP);

			// Repeated arrowheads follow the baked curve tangent, showing the walking direction.
			Vector<Vector3> direction_arrows;
			const int arrow_count = MAX(1, int(length / 2.5));
			const real_t arrow_length = CLAMP(route_width * 0.65, real_t(0.22), real_t(0.55));
			const real_t arrow_half_width = route_width * 0.3;
			for (int i = 0; i < arrow_count; i++) {
				const real_t distance = length * real_t(i + 1) / real_t(arrow_count + 1);
				const real_t tangent_sample_distance = MIN(real_t(0.2), length * 0.1);
				const real_t before_distance = MAX(real_t(0.0), distance - tangent_sample_distance);
				const real_t after_distance = MIN(length, distance + tangent_sample_distance);
				Vector3 forward = route_curve->sample_baked(after_distance, true) - route_curve->sample_baked(before_distance, true);
				forward.y = 0.0;
				if (forward.is_zero_approx()) {
					continue;
				}
				forward.normalize();
				const Vector3 side = forward.cross(route_up).normalized();
				if (side.is_zero_approx()) {
					continue;
				}
				const Vector3 center = route_curve->sample_baked(distance, true) + route_up * 0.07;
				const Vector3 tip = center + forward * arrow_length * 0.65;
				const Vector3 base = center - forward * arrow_length * 0.35;
				direction_arrows.push_back(tip);
				direction_arrows.push_back(base + side * arrow_half_width);
				direction_arrows.push_back(base - side * arrow_half_width);
			}
			if (!direction_arrows.is_empty()) {
				add_vertices(direction_arrows, gizmo_plugin->get_material("fps_route_direction", this), Mesh::PRIMITIVE_TRIANGLES);
			}
		}

		Array point_actions = path->get_meta(npc_route_actions_meta(), Array());
		Vector<Vector3> points_by_action[7];
		for (int i = 0; i < route_curve->get_point_count(); i++) {
			int action = Path3DEditorPlugin::NPC_POINT_ACTION_MOVE;
			if (i < point_actions.size() && point_actions[i].get_type() == Variant::DICTIONARY) {
				const Dictionary point_action = point_actions[i];
				const String action_name = point_action.get("action", "move");
				if (action_name == "wait") {
					action = Path3DEditorPlugin::NPC_POINT_ACTION_WAIT;
				} else if (action_name == "look_at") {
					action = Path3DEditorPlugin::NPC_POINT_ACTION_LOOK_AT;
				} else if (action_name == "play_animation") {
					action = Path3DEditorPlugin::NPC_POINT_ACTION_PLAY_ANIMATION;
				} else if (action_name == "interact") {
					action = Path3DEditorPlugin::NPC_POINT_ACTION_INTERACT;
				} else if (action_name == "attack") {
					action = Path3DEditorPlugin::NPC_POINT_ACTION_ATTACK;
				} else if (action_name == "take_cover") {
					action = Path3DEditorPlugin::NPC_POINT_ACTION_TAKE_COVER;
				}
			}
			points_by_action[action].push_back(route_curve->get_point_position(i));
		}

		static const char *action_materials[] = {
			"fps_route_move",
			"fps_route_wait",
			"fps_route_look_at",
			"fps_route_animation",
			"fps_route_interact",
			"fps_route_attack",
			"fps_route_cover",
		};
		for (int action = 0; action < 7; action++) {
			if (!points_by_action[action].is_empty()) {
				add_vertices(points_by_action[action], gizmo_plugin->get_material(action_materials[action], this), Mesh::PRIMITIVE_POINTS, true);
			}
		}
		if (route_curve->get_point_count() > 0) {
			Vector<Vector3> start_point;
			start_point.push_back(route_curve->get_point_position(0));
			add_vertices(start_point, gizmo_plugin->get_material("fps_route_start", this), Mesh::PRIMITIVE_POINTS, true);
		}

		Path3DGizmoPlugin *route_gizmo_plugin = Object::cast_to<Path3DGizmoPlugin>(gizmo_plugin);
		const Vector<int> *connection_states = route_gizmo_plugin ? route_gizmo_plugin->get_route_reachability(path->get_instance_id()) : nullptr;
		if (connection_states) {
			Vector<Vector3> reachable_segments;
			Vector<Vector3> blocked_segments;
			const int route_point_count = route_curve->get_point_count();
			const bool has_loop_segment = route_curve->is_closed() && route_point_count > 2;
			const int route_segment_count = MAX(0, route_point_count - 1 + (has_loop_segment ? 1 : 0));
			for (int i = 0; i < route_segment_count && i < connection_states->size(); i++) {
				const int next_point = (i + 1) % route_point_count;
				if ((*connection_states)[i] == 1) {
					reachable_segments.push_back(route_curve->get_point_position(i));
					reachable_segments.push_back(route_curve->get_point_position(next_point));
				} else if ((*connection_states)[i] == 0 || (*connection_states)[i] == 2) {
					blocked_segments.push_back(route_curve->get_point_position(i));
					blocked_segments.push_back(route_curve->get_point_position(next_point));
				}
			}
			if (!reachable_segments.is_empty()) {
				add_lines(reachable_segments, gizmo_plugin->get_material("fps_route_reachable", this), true);
			}
			if (!blocked_segments.is_empty()) {
				add_lines(blocked_segments, gizmo_plugin->get_material("fps_route_blocked", this), true);
			}
		}
	}

	// 2. Draw handles when selected.
	if (Path3DEditorPlugin::singleton->get_edited_path() == path) {
		PackedVector3Array handle_lines;
		PackedVector3Array tilt_handle_lines;
		PackedVector3Array primary_handle_points;
		PackedVector3Array secondary_handle_points;
		PackedInt32Array collected_secondary_handle_ids; // Avoid shadowing member on Node3DEditorGizmo.

		_secondary_handles_info.resize(c->get_point_count() * 3);

		const float disk_size = EDITOR_GET("editors/3d_gizmos/gizmo_settings/path3d_tilt_disk_size");

		for (int idx = 0; idx < c->get_point_count(); idx++) {
			// Collect primary-handles.
			const Vector3 pos = c->get_point_position(idx);
			primary_handle_points.append(pos);

			HandleInfo info;
			info.point_idx = idx;

			// Collect in-handles except for the first point.
			if (idx > (c->is_closed() ? -1 : 0) && Path3DEditorPlugin::singleton->curve_edit_curve->is_pressed()) {
				const Vector3 in = c->get_point_in(idx);

				info.type = HandleType::HANDLE_TYPE_IN;
				const int handle_idx = idx * 3 + 0;
				collected_secondary_handle_ids.append(handle_idx);
				_secondary_handles_info.write[handle_idx] = info;

				secondary_handle_points.append(pos + in);
				handle_lines.append(pos);
				handle_lines.append(pos + in);
			}

			// Collect out-handles except for the last point.
			if (idx < (c->is_closed() ? c->get_point_count() : c->get_point_count() - 1) && Path3DEditorPlugin::singleton->curve_edit_curve->is_pressed()) {
				const Vector3 out = c->get_point_out(idx);

				info.type = HandleType::HANDLE_TYPE_OUT;
				const int handle_idx = idx * 3 + 1;
				collected_secondary_handle_ids.append(handle_idx);
				_secondary_handles_info.write[handle_idx] = info;

				secondary_handle_points.append(pos + out);
				handle_lines.append(pos);
				handle_lines.append(pos + out);
			}

			// Collect tilt-handles.
			if (Path3DEditorPlugin::singleton->curve_edit_tilt->is_pressed()) {
				// Tilt handle.
				{
					info.type = HandleType::HANDLE_TYPE_TILT;
					const int handle_idx = idx * 3 + 2;
					collected_secondary_handle_ids.append(handle_idx);
					_secondary_handles_info.write[handle_idx] = info;

					const Basis posture = c->get_point_baked_posture(idx, true);
					const Vector3 up = posture.get_column(1);
					secondary_handle_points.append(pos + up * disk_size);
					tilt_handle_lines.append(pos);
					tilt_handle_lines.append(pos + up * disk_size);
				}

				// Tilt disk.
				{
					const Basis posture = c->get_point_baked_posture(idx, false);
					const Vector3 up = posture.get_column(1);
					const Vector3 side = posture.get_column(0);

					PackedVector3Array disk;
					disk.append(pos);

					const int n = 36;
					for (int i = 0; i <= n; i++) {
						const float a = Math::TAU * i / n;
						const Vector3 edge = std::sin(a) * side + std::cos(a) * up;
						disk.append(pos + edge * disk_size);
					}
					add_vertices(disk, debug_material, Mesh::PRIMITIVE_LINE_STRIP);
				}
			}
		}

		if (handle_lines.size() > 1) {
			add_lines(handle_lines, path_thin_material);
		}

		if (tilt_handle_lines.size() > 1) {
			add_lines(tilt_handle_lines, path_tilt_material);
		}

		if (!Path3DEditorPlugin::singleton->curve_edit->is_pressed() && primary_handle_points.size()) {
			// Need to define indices separately.
			// Point count.
			const int pc = primary_handle_points.size();
			Vector<int> idx;
			idx.resize(pc);
			int *idx_ptr = idx.ptrw();
			for (int j = 0; j < pc; j++) {
				idx_ptr[j] = j;
			}

			// Initialize arrays for first point.
			PackedVector3Array first_pt_handle_point;
			Vector<int> first_pt_id;
			first_pt_handle_point.append(primary_handle_points[0]);
			first_pt_id.append(idx[0]);

			// Initialize arrays and add handle for last point if needed.
			if (pc > 1) {
				PackedVector3Array last_pt_handle_point;
				Vector<int> last_pt_id;
				last_pt_handle_point.append(primary_handle_points[pc - 1]);
				last_pt_id.append(idx[pc - 1]);
				primary_handle_points.remove_at(pc - 1);
				idx.remove_at(pc - 1);
				add_handles(last_pt_handle_point, c->is_closed() ? handles_material : last_pt_handle_material, last_pt_id);
			}

			// Add handle for first point.
			primary_handle_points.remove_at(0);
			idx.remove_at(0);
			add_handles(first_pt_handle_point, c->is_closed() ? closed_pt_handle_material : first_pt_handle_material, first_pt_id);

			// Add handles for remaining intermediate points.
			if (!primary_handle_points.is_empty()) {
				add_handles(primary_handle_points, handles_material, idx);
			}
		}
		if (secondary_handle_points.size()) {
			add_handles(secondary_handle_points, sec_handles_material, collected_secondary_handle_ids, false, true);
		}
		// Draw the gizmo plugin manually, because handles are registered. In which case, the caller code skips drawing the gizmo plugin.
		gizmo_plugin->redraw(this);
	}
}

void Path3DGizmo::_update_transform_gizmo() {
	Node3DEditor *node_3d_editor = Node3DEditor::get_singleton();
	if (!node_3d_editor) {
		return;
	}

	Ref<Curve3D> curve = path ? path->get_curve() : Ref<Curve3D>();
	const int current_point_count = curve.is_valid() ? curve->get_point_count() : 0;
	if (current_point_count != curve_point_count) {
		// Point indices can shift or become invalid when the Curve3D point array changes.
		if (path) {
			node_3d_editor->clear_subgizmo_selection(path);
		}
		curve_point_count = current_point_count;
	}

	node_3d_editor->update_transform_gizmo();
}

Path3DGizmo::Path3DGizmo(Path3D *p_path) {
	path = p_path;
	set_node_3d(p_path);
	orig_in_length = 0;
	orig_out_length = 0;
	Ref<Curve3D> curve = path->get_curve();
	curve_point_count = curve.is_valid() ? curve->get_point_count() : 0;

	// Connecting to a signal once, rather than plaguing the implementation with calls to `Node3DEditor::update_transform_gizmo`.
	path->connect("curve_changed", callable_mp(this, &Path3DGizmo::_update_transform_gizmo));
	path->connect("debug_color_changed", callable_mp(this, &Path3DGizmo::redraw));

	Path3DEditorPlugin::singleton->curve_edit->connect(SceneStringName(pressed), callable_mp(this, &Path3DGizmo::redraw));
	Path3DEditorPlugin::singleton->curve_edit_curve->connect(SceneStringName(pressed), callable_mp(this, &Path3DGizmo::redraw));
	Path3DEditorPlugin::singleton->curve_create->connect(SceneStringName(pressed), callable_mp(this, &Path3DGizmo::redraw));
	Path3DEditorPlugin::singleton->curve_del->connect(SceneStringName(pressed), callable_mp(this, &Path3DGizmo::redraw));
	Path3DEditorPlugin::singleton->curve_closed->connect(SceneStringName(pressed), callable_mp(this, &Path3DGizmo::redraw));
}

EditorPlugin::AfterGUIInput Path3DEditorPlugin::forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	if (!path) {
		return EditorPlugin::AFTER_GUI_INPUT_PASS;
	}
	Ref<Curve3D> c = path->get_curve();
	if (c.is_null()) {
		return EditorPlugin::AFTER_GUI_INPUT_PASS;
	}
	Transform3D gt = path->get_global_transform();
	Transform3D it = gt.affine_inverse();

	Ref<InputEventKey> key = p_event;
	if (_is_fps_npc_route() && key.is_valid() && key->is_pressed() && !key->is_echo() && (key->get_keycode() == Key::KEY_DELETE || key->get_keycode() == Key::BACKSPACE)) {
		const Vector<int> selected_points = _get_selected_route_point_ids();
		if (!selected_points.is_empty()) {
			// A selected waypoint is a Path3D subgizmo, so let the route editor
			// handle Delete instead of allowing the scene tree to delete the path node.
			get_tree()->get_root()->set_input_as_handled();

			const int point_count = c->get_point_count();
			if (point_count > 1) {
				Vector<int> sorted_selection = selected_points;
				sorted_selection.sort();
				Vector<int> points_to_remove;
				Array actions_before = _get_route_point_actions();
				Array actions_after = actions_before.duplicate(true);
				for (int i = sorted_selection.size() - 1; i >= 0 && points_to_remove.size() < point_count - 1; i--) {
					const int point_index = sorted_selection[i];
					if (point_index < 0 || point_index >= point_count) {
						continue;
					}
					points_to_remove.push_back(point_index);
					actions_after = _remove_route_point_action(actions_after, point_index);
				}

				if (!points_to_remove.is_empty()) {
					EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
					undo_redo->create_action(TTR("Remove NPC Waypoint(s)"));
					_add_route_point_action_undo(undo_redo, actions_before, actions_after);
					for (int point_index : points_to_remove) {
						undo_redo->add_do_method(c.ptr(), "remove_point", point_index);
						undo_redo->add_undo_method(c.ptr(), "add_point", c->get_point_position(point_index), c->get_point_in(point_index), c->get_point_out(point_index), point_index);
					}
					undo_redo->commit_action();
				}
			}
			return EditorPlugin::AFTER_GUI_INPUT_STOP;
		}
	}

	static const int click_dist = 10; //should make global

	Ref<InputEventMouseButton> mb = p_event;

	if (mb.is_valid()) {
		if (mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
			View3DController::NavigationScheme nav_scheme = (View3DController::NavigationScheme)EDITOR_GET("editors/3d/navigation/navigation_scheme").operator int();
			if ((nav_scheme == View3DController::NAV_SCHEME_MAYA || nav_scheme == View3DController::NAV_SCHEME_MODO) && mb->is_alt_pressed()) {
				return EditorPlugin::AFTER_GUI_INPUT_PASS;
			}
		}

		Point2 mbpos(mb->get_position().x, mb->get_position().y);

		Node3DEditorViewport *viewport = nullptr;
		for (uint32_t i = 0; i < Node3DEditor::VIEWPORTS_COUNT; i++) {
			Node3DEditorViewport *vp = Node3DEditor::get_singleton()->get_editor_viewport(i);
			if (vp->get_camera_3d() == p_camera) {
				viewport = vp;
				break;
			}
		}

		ERR_FAIL_NULL_V(viewport, EditorPlugin::AFTER_GUI_INPUT_PASS);

		if (!mb->is_pressed()) {
			set_handle_clicked(false);
		}

		if (mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT && (curve_create->is_pressed() || (curve_edit->is_pressed() && mb->is_command_or_control_pressed()))) {
			//click into curve, break it down
			Vector<Vector3> v3a = c->tessellate();
			int rc = v3a.size();
			int closest_seg = -1;
			Vector3 closest_seg_point;

			if (rc >= 2) {
				int idx = 0;
				const Vector3 *r = v3a.ptr();
				float closest_d = 1e20;

				if (viewport->point_to_screen(gt.xform(c->get_point_position(0))).distance_to(mbpos) < click_dist) {
					return EditorPlugin::AFTER_GUI_INPUT_PASS; //nope, existing
				}

				for (int i = 0; i < c->get_point_count() - 1; i++) {
					//find the offset and point index of the place to break up
					int j = idx;
					if (viewport->point_to_screen(gt.xform(c->get_point_position(i + 1))).distance_to(mbpos) < click_dist) {
						return EditorPlugin::AFTER_GUI_INPUT_PASS; //nope, existing
					}

					while (j < rc && c->get_point_position(i + 1) != r[j]) {
						Vector3 from = r[j];
						Vector3 to = r[j + 1];
						real_t cdist = from.distance_to(to);
						from = gt.xform(from);
						to = gt.xform(to);
						if (cdist > 0) {
							const Vector2 segment_a = viewport->point_to_screen(from);
							const Vector2 segment_b = viewport->point_to_screen(to);
							Vector2 inters = Geometry2D::get_closest_point_to_segment(mbpos, segment_a, segment_b);
							float d = inters.distance_to(mbpos);

							if (d < 10 && d < closest_d) {
								closest_d = d;
								closest_seg = i;
								Vector3 ray_from = viewport->get_ray_pos(mbpos);
								Vector3 ray_dir = viewport->get_ray(mbpos);

								Vector3 ra, rb;
								Geometry3D::get_closest_points_between_segments(ray_from, ray_from + ray_dir * 4096, from, to, ra, rb);

								closest_seg_point = it.xform(rb);
							}
						}
						j++;
					}
					if (idx == j) {
						idx++; //force next
					} else {
						idx = j; //swap
					}

					if (j == rc) {
						break;
					}
				}
			}

			EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
			if (closest_seg != -1) {
				//subdivide
				if (_is_fps_npc_route() && npc_route_snap_to_navmesh) {
					Vector3 navmesh_position;
					if (_snap_npc_route_point_to_navmesh(gt.xform(closest_seg_point), navmesh_position)) {
						closest_seg_point = it.xform(navmesh_position);
					} else {
						_set_route_status(TTR("No nearby NavigationMesh; waypoint was not inserted"));
						return EditorPlugin::AFTER_GUI_INPUT_STOP;
					}
				}

				ur->create_action(TTR("Split Path"));
				if (_is_fps_npc_route()) {
					const Array before = _get_route_point_actions();
					_add_route_point_action_undo(ur, before, _insert_route_point_action(before, closest_seg + 1));
				}
				ur->add_do_method(c.ptr(), "add_point", closest_seg_point, Vector3(), Vector3(), closest_seg + 1);
				ur->add_undo_method(c.ptr(), "remove_point", closest_seg + 1);
				ur->commit_action();
				return EditorPlugin::AFTER_GUI_INPUT_STOP;

			} else {
				Vector3 origin;
				if (c->get_point_count() == 0) {
					origin = path->get_transform().get_origin();
				} else {
					origin = gt.xform(c->get_point_position(c->get_point_count() - 1));
				}

				Vector3 ray_from = viewport->get_ray_pos(mbpos);
				Vector3 ray_dir = viewport->get_ray(mbpos);

				if (snap_to_collider) {
					_edit.click_ray_pos = ray_from;
					_edit.click_ray_dir = ray_dir * p_camera->get_far();
					_edit.gizmo_camera = p_camera;
					_edit.origin = origin;
					_edit.waiting_point_physics = true;
					return EditorPlugin::AFTER_GUI_INPUT_STOP;
				}

				Plane p(p_camera->get_transform().basis.get_column(2), origin);

				Vector3 inters;
				if (p.intersects_ray(ray_from, ray_dir, &inters)) {
					if (_is_fps_npc_route() && npc_route_snap_to_navmesh) {
						Vector3 navmesh_position;
						if (_snap_npc_route_point_to_navmesh(inters, navmesh_position)) {
							inters = navmesh_position;
						} else {
							_set_route_status(TTR("No nearby NavigationMesh; waypoint was not added"));
							return EditorPlugin::AFTER_GUI_INPUT_STOP;
						}
					}
					ur->create_action(TTR("Add Point to Curve"));
					if (_is_fps_npc_route()) {
						const Array before = _get_route_point_actions();
						_add_route_point_action_undo(ur, before, _insert_route_point_action(before, c->get_point_count()));
					}
					ur->add_do_method(c.ptr(), "add_point", it.xform(inters), Vector3(), Vector3(), -1);
					ur->add_undo_method(c.ptr(), "remove_point", c->get_point_count());
					ur->commit_action();
					return EditorPlugin::AFTER_GUI_INPUT_STOP;
				}

				//add new at pos
			}

		} else if (mb->is_pressed() && ((mb->get_button_index() == MouseButton::LEFT && curve_del->is_pressed()) || (mb->get_button_index() == MouseButton::RIGHT && curve_edit->is_pressed()))) {
			const float disk_size = EDITOR_GET("editors/3d_gizmos/gizmo_settings/path3d_tilt_disk_size");
			for (int i = 0; i < c->get_point_count(); i++) {
				real_t dist_to_p = viewport->point_to_screen(gt.xform(c->get_point_position(i))).distance_to(mbpos);
				real_t dist_to_p_out = viewport->point_to_screen(gt.xform(c->get_point_position(i) + c->get_point_out(i))).distance_to(mbpos);
				real_t dist_to_p_in = viewport->point_to_screen(gt.xform(c->get_point_position(i) + c->get_point_in(i))).distance_to(mbpos);
				real_t dist_to_p_up = viewport->point_to_screen(gt.xform(c->get_point_position(i) + c->get_point_baked_posture(i, true).get_column(1) * disk_size)).distance_to(mbpos);

				// Find the offset and point index of the place to break up.
				// Also check for the control points.
				if (dist_to_p < click_dist) {
					if (_is_fps_npc_route() && c->get_point_count() <= 1) {
						return EditorPlugin::AFTER_GUI_INPUT_STOP;
					}
					EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
					ur->create_action(TTR("Remove Path Point"));
					if (_is_fps_npc_route()) {
						const Array before = _get_route_point_actions();
						_add_route_point_action_undo(ur, before, _remove_route_point_action(before, i));
					}
					ur->add_do_method(c.ptr(), "remove_point", i);
					ur->add_undo_method(c.ptr(), "add_point", c->get_point_position(i), c->get_point_in(i), c->get_point_out(i), i);
					ur->commit_action();
					return EditorPlugin::AFTER_GUI_INPUT_STOP;
				} else if (dist_to_p_out < click_dist) {
					EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
					ur->create_action(TTR("Reset Out-Control Point"));
					ur->add_do_method(c.ptr(), "set_point_out", i, Vector3());
					ur->add_undo_method(c.ptr(), "set_point_out", i, c->get_point_out(i));
					ur->commit_action();
					return EditorPlugin::AFTER_GUI_INPUT_STOP;
				} else if (dist_to_p_in < click_dist) {
					EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
					ur->create_action(TTR("Reset In-Control Point"));
					ur->add_do_method(c.ptr(), "set_point_in", i, Vector3());
					ur->add_undo_method(c.ptr(), "set_point_in", i, c->get_point_in(i));
					ur->commit_action();
					return EditorPlugin::AFTER_GUI_INPUT_STOP;
				} else if (dist_to_p_up < click_dist) {
					EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
					ur->create_action(TTR("Reset Point Tilt"));
					ur->add_do_method(c.ptr(), "set_point_tilt", i, 0.0f);
					ur->add_undo_method(c.ptr(), "set_point_tilt", i, c->get_point_tilt(i));
					ur->commit_action();
					return EditorPlugin::AFTER_GUI_INPUT_STOP;
				}
			}
		}
	}

	return EditorPlugin::AFTER_GUI_INPUT_PASS;
}

void Path3DEditorPlugin::update_handles() {
	if (_edit.gizmo.is_valid()) {
		callable_mp(_edit.gizmo.ptr(), &Path3DGizmo::redraw).call_deferred();
	}
}

void Path3DEditorPlugin::edit(Object *p_object) {
	if (path) {
		path->disconnect("curve_changed", callable_mp(this, &Path3DEditorPlugin::_route_curve_changed));
		path_3d_gizmo_plugin->clear_route_reachability(path->get_instance_id());
	}
	pending_route_waypoint_drag = false;
	pending_route_waypoint_ids.clear();
	pending_route_waypoint_positions.clear();
	route_reachability_check_pending = false;

	path = Object::cast_to<Path3D>(p_object);
	route_current_selection.clear();

	if (path) {
		if (path->has_meta("fps_npc_route_width")) {
			if (!path->has_meta(npc_route_width_meta())) {
				path->set_meta(npc_route_width_meta(), path->get_meta("fps_npc_route_width"));
			}
			path->set_meta("fps_npc_route_width", Variant());
		}
		if (path->has_meta("fps_npc_point_actions")) {
			if (!path->has_meta(npc_route_actions_meta())) {
				path->set_meta(npc_route_actions_meta(), path->get_meta("fps_npc_point_actions"));
			}
			path->set_meta("fps_npc_point_actions", Variant());
		}
		if (_is_fps_npc_route() && path->get_curve().is_valid()) {
			Ref<Curve3D> route_curve = path->get_curve();
			if (route_curve->get_point_count() == 0) {
				// Repair NPC routes emptied by an older editor build before they can be selected.
				route_curve->add_point(Vector3());
			}
			path->set_meta(npc_route_actions_meta(), _get_route_point_actions());
		}
		path->connect("curve_changed", callable_mp(this, &Path3DEditorPlugin::_route_curve_changed));
	}

	_update_toolbar();
	if (_is_fps_npc_route() && npc_route_snap_to_navmesh) {
		_set_route_navmesh_snap_enabled(true);
	}
	if (_is_fps_npc_route()) {
		_request_route_reachability_check();
	}
	update_overlays();
}

bool Path3DEditorPlugin::handles(Object *p_object) const {
	return p_object->is_class("Path3D");
}

void Path3DEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		_update_theme();
		topmenu_bar->show();
	} else {
		topmenu_bar->hide();
		if (path) {
			path->disconnect("curve_changed", callable_mp(this, &Path3DEditorPlugin::_route_curve_changed));
			path_3d_gizmo_plugin->clear_route_reachability(path->get_instance_id());
		}
		pending_route_waypoint_drag = false;
		pending_route_waypoint_ids.clear();
		pending_route_waypoint_positions.clear();
		route_reachability_check_pending = false;
		path = nullptr;
		route_current_selection.clear();
	}
	set_process(p_visible && _is_fps_npc_route());
	set_physics_process(p_visible);
}

void Path3DEditorPlugin::_mode_changed(int p_mode) {
	curve_create->set_pressed_no_signal(p_mode == MODE_CREATE);
	curve_edit_curve->set_pressed_no_signal(p_mode == MODE_EDIT_CURVE);
	curve_edit_tilt->set_pressed_no_signal(p_mode == MODE_EDIT_TILT);
	curve_edit->set_pressed_no_signal(p_mode == MODE_EDIT);
	curve_del->set_pressed_no_signal(p_mode == MODE_DELETE);

	Node3DEditor::get_singleton()->clear_subgizmo_selection();
}

void Path3DEditorPlugin::_toggle_closed_curve() {
	Ref<Curve3D> c = path->get_curve();
	if (c.is_null()) {
		return;
	}
	if (c->get_point_count() < 2) {
		return;
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Toggle Open/Closed Curve"));
	ur->add_do_method(c.ptr(), "set_closed", !c.ptr()->is_closed());
	ur->add_undo_method(c.ptr(), "set_closed", c.ptr()->is_closed());
	ur->commit_action();
}

void Path3DEditorPlugin::_handle_option_pressed(int p_option) {
	PopupMenu *pm;
	pm = handle_menu->get_popup();

	switch (p_option) {
		case HANDLE_OPTION_ANGLE: {
			bool is_checked = pm->is_item_checked(HANDLE_OPTION_ANGLE);
			mirror_handle_angle = !is_checked;
			pm->set_item_checked(HANDLE_OPTION_ANGLE, mirror_handle_angle);
			pm->set_item_disabled(HANDLE_OPTION_LENGTH, !mirror_handle_angle);
		} break;
		case HANDLE_OPTION_LENGTH: {
			bool is_checked = pm->is_item_checked(HANDLE_OPTION_LENGTH);
			mirror_handle_length = !is_checked;
			pm->set_item_checked(HANDLE_OPTION_LENGTH, mirror_handle_length);
		} break;
		case HANDLE_OPTION_SNAP_COLLIDER: {
			bool is_checked = pm->is_item_checked(HANDLE_OPTION_SNAP_COLLIDER);
			snap_to_collider = !is_checked;
			pm->set_item_checked(HANDLE_OPTION_SNAP_COLLIDER, snap_to_collider);
		} break;
	}
}

void Path3DEditorPlugin::_create_curve() {
	ERR_FAIL_NULL(path);

	Ref<Curve3D> new_curve;
	new_curve.instantiate();
	const bool is_npc_route = _is_fps_npc_route();
	Variant previous_route_actions;
	if (is_npc_route) {
		new_curve->add_point(Vector3());
		previous_route_actions = path->get_meta(npc_route_actions_meta(), Variant());
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Create Curve in Path3D"));
	undo_redo->add_do_property(path, "curve", new_curve);
	undo_redo->add_undo_property(path, "curve", Ref<Curve3D>());
	if (is_npc_route) {
		Array initial_route_actions;
		Dictionary default_action;
		default_action["action"] = "move";
		default_action["duration"] = 0.0;
		default_action["target"] = "";
		initial_route_actions.push_back(default_action);
		undo_redo->add_do_method(path, "set_meta", npc_route_actions_meta(), initial_route_actions);
		undo_redo->add_undo_method(path, "set_meta", npc_route_actions_meta(), previous_route_actions);
	}
	undo_redo->add_do_method(this, "_update_toolbar");
	undo_redo->add_undo_method(this, "_update_toolbar");
	undo_redo->commit_action();
	if (is_npc_route && npc_route_snap_to_navmesh) {
		_set_route_navmesh_snap_enabled(true);
	}
}

void Path3DEditorPlugin::_confirm_clear_points() {
	if (!path || path->get_curve().is_null()) {
		return;
	}
	const int point_count = path->get_curve()->get_point_count();
	if (point_count == 0 || (_is_fps_npc_route() && point_count <= 1)) {
		return;
	}
	clear_points_dialog->reset_size();
	clear_points_dialog->popup_centered();
}

void Path3DEditorPlugin::_clear_points() {
	if (!path || path->get_curve().is_null()) {
		return;
	}
	const bool is_npc_route = _is_fps_npc_route();
	if (is_npc_route && path->get_curve()->get_point_count() <= 1) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	PackedVector3Array points = path->get_curve()->get_points().duplicate();
	const bool curve_was_closed = path->get_curve()->is_closed();

	undo_redo->create_action(TTR("Clear Curve Points"));
	if (is_npc_route) {
		Array actions_before = _get_route_point_actions();
		Array actions_after;
		if (!actions_before.is_empty()) {
			actions_after.push_back(actions_before[0]);
		} else {
			Dictionary default_action;
			default_action["action"] = "move";
			default_action["duration"] = 0.0;
			default_action["target"] = "";
			actions_after.push_back(default_action);
		}
		undo_redo->add_do_method(path, "set_meta", npc_route_actions_meta(), actions_after);
		undo_redo->add_undo_method(path, "set_meta", npc_route_actions_meta(), actions_before);
	}
	undo_redo->add_do_method(this, "_clear_curve_points");
	undo_redo->add_undo_method(this, "_restore_curve_points", points, curve_was_closed);
	undo_redo->commit_action();
}

void Path3DEditorPlugin::_clear_curve_points() {
	if (!path || path->get_curve().is_null()) {
		return;
	}
	Ref<Curve3D> curve = path->get_curve();
	if (_is_fps_npc_route()) {
		if (curve->get_point_count() == 0) {
			curve->add_point(Vector3());
			return;
		}
		if (curve->get_point_count() <= 1) {
			return;
		}
		curve->set_closed(false);
		while (curve->get_point_count() > 1) {
			curve->remove_point(curve->get_point_count() - 1);
		}
		return;
	}
	if (curve->get_point_count() == 0) {
		return;
	}
	curve->set_closed(false);
	curve->clear_points();
}

void Path3DEditorPlugin::_restore_curve_points(const PackedVector3Array &p_points, bool p_curve_was_closed) {
	if (!path || path->get_curve().is_null()) {
		return;
	}
	Ref<Curve3D> curve = path->get_curve();

	curve->set_closed(false);
	if (curve->get_point_count() > 0) {
		curve->clear_points();
	}

	for (int i = 0; i + 2 < p_points.size(); i += 3) {
		curve->add_point(p_points[i + 2], p_points[i], p_points[i + 1]);
	}
	if (_is_fps_npc_route() && curve->get_point_count() == 0) {
		curve->add_point(Vector3());
	}
	curve->set_closed(p_curve_was_closed);
}

void Path3DEditorPlugin::_update_theme() {
	// Editor icons and control metrics are only available after theme inheritance.
	if (!topmenu_bar->is_inside_tree()) {
		return;
	}

	curve_edit->set_button_icon(topmenu_bar->get_editor_theme_icon(SNAME("CurveEdit")));
	curve_edit_curve->set_button_icon(topmenu_bar->get_editor_theme_icon(SNAME("CurveCurve")));
	curve_edit_tilt->set_button_icon(topmenu_bar->get_editor_theme_icon(SNAME("CurveTilt")));
	curve_create->set_button_icon(topmenu_bar->get_editor_theme_icon(SNAME("CurveCreate")));
	curve_del->set_button_icon(topmenu_bar->get_editor_theme_icon(SNAME("CurveDelete")));
	curve_closed->set_button_icon(topmenu_bar->get_editor_theme_icon(SNAME("CurveClose")));
	curve_clear_points->set_button_icon(topmenu_bar->get_editor_theme_icon(SNAME("Clear")));
	create_curve_button->set_button_icon(topmenu_bar->get_editor_theme_icon(SNAME("Curve3D")));

	Vector<Control *> toolbar_controls;
	toolbar_controls.push_back(curve_edit);
	toolbar_controls.push_back(curve_edit_curve);
	toolbar_controls.push_back(curve_edit_tilt);
	toolbar_controls.push_back(curve_create);
	toolbar_controls.push_back(curve_del);
	toolbar_controls.push_back(curve_closed);
	toolbar_controls.push_back(curve_clear_points);
	toolbar_controls.push_back(handle_menu);
	toolbar_controls.push_back(create_curve_button);
	toolbar_controls.push_back(npc_point_action);
	toolbar_controls.push_back(npc_point_duration);
	toolbar_controls.push_back(npc_point_target);
	toolbar_controls.push_back(npc_apply_point_action);
	toolbar_controls.push_back(npc_route_navmesh_snap);
	toolbar_controls.push_back(npc_snap_route_to_navmesh);
	toolbar_controls.push_back(npc_corner_radius);
	toolbar_controls.push_back(npc_round_corner);
	toolbar_controls.push_back(npc_check_reachability);

	real_t toolbar_item_height = 28 * EDSCALE;
	for (Control *control : toolbar_controls) {
		control->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		Size2 minimum_size = control->get_custom_minimum_size();
		minimum_size.height = 0.0;
		control->set_custom_minimum_size(minimum_size);
		toolbar_item_height = MAX(toolbar_item_height, control->get_combined_minimum_size().height);
	}
	for (Control *control : toolbar_controls) {
		Size2 minimum_size = control->get_custom_minimum_size();
		minimum_size.height = toolbar_item_height;
		control->set_custom_minimum_size(minimum_size);
	}
	// Give icon-only tools the same footprint as the other toolbar buttons.
	for (Button *button : { curve_edit, curve_edit_curve, curve_edit_tilt, curve_create, curve_del, curve_closed, curve_clear_points }) {
		button->set_custom_minimum_size(Size2(toolbar_item_height, toolbar_item_height));
	}
	for (Label *label : { npc_route_selection_label, npc_route_reachability_label }) {
		Size2 minimum_size = label->get_custom_minimum_size();
		minimum_size.height = toolbar_item_height;
		label->set_custom_minimum_size(minimum_size);
		label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		label->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	}
}

void Path3DEditorPlugin::_update_toolbar() {
	if (!path) {
		return;
	}
	bool has_curve = path->get_curve().is_valid();
	toolbar->set_visible(has_curve);
	create_curve_button->set_visible(!has_curve);
	const bool is_route = _is_fps_npc_route();
	curve_clear_points->set_disabled(is_route && has_curve && path->get_curve()->get_point_count() <= 1);
	curve_clear_points->set_tooltip_text(is_route ? TTR("Clear all waypoints except the first") : TTR("Clear Points"));
	curve_del->set_tooltip_text(is_route ? TTR("Delete waypoint. An NPC route must keep at least one waypoint.") : TTR("Delete Point"));
	for (Control *control : npc_route_controls) {
		control->set_visible(is_route && has_curve);
	}
	curve_create->set_text(is_route ? TTR("Add Waypoint") : String());
	curve_create->set_accessibility_name(is_route ? TTRC("Add Waypoint") : TTRC("Add Point (in empty space)"));
	curve_create->set_tooltip_text(is_route ? TTR("Click to add a waypoint to the NPC route. Click a curve segment to insert a waypoint there.") : TTR("Add Point (in empty space)") + "\n" + TTR("Split Segment (in curve)"));
	set_process(is_route && topmenu_bar->is_visible_in_tree());
	if (is_route && has_curve) {
		_update_route_controls();
	}
}

bool Path3DEditorPlugin::_is_fps_npc_route() const {
	return path && path->is_in_group("fps_npc_path");
}

Array Path3DEditorPlugin::_get_route_point_actions() const {
	if (!_is_fps_npc_route() || path->get_curve().is_null()) {
		return Array();
	}

	Variant stored = path->get_meta(npc_route_actions_meta(), Array());
	Array actions;
	if (stored.get_type() == Variant::ARRAY) {
		actions = stored;
	}
	const int point_count = path->get_curve()->get_point_count();
	while (actions.size() < point_count) {
		Dictionary point_action;
		point_action["action"] = "move";
		point_action["duration"] = 0.0;
		point_action["target"] = "";
		actions.push_back(point_action);
	}
	while (actions.size() > point_count) {
		actions.remove_at(actions.size() - 1);
	}
	return actions;
}

Array Path3DEditorPlugin::_insert_route_point_action(const Array &p_actions, int p_index) const {
	Array updated = p_actions.duplicate(true);
	Dictionary point_action;
	point_action["action"] = "move";
	point_action["duration"] = 0.0;
	point_action["target"] = "";
	if (p_index < 0 || p_index > updated.size()) {
		p_index = updated.size();
	}
	updated.insert(p_index, point_action);
	return updated;
}

Array Path3DEditorPlugin::_remove_route_point_action(const Array &p_actions, int p_index) const {
	Array updated = p_actions.duplicate(true);
	if (p_index >= 0 && p_index < updated.size()) {
		updated.remove_at(p_index);
	}
	return updated;
}

void Path3DEditorPlugin::_add_route_point_action_undo(EditorUndoRedoManager *p_undo_redo, const Array &p_before, const Array &p_after) {
	if (!_is_fps_npc_route()) {
		return;
	}
	p_undo_redo->add_do_method(path, "set_meta", npc_route_actions_meta(), p_after);
	p_undo_redo->add_undo_method(path, "set_meta", npc_route_actions_meta(), p_before);
}

Vector<int> Path3DEditorPlugin::_get_selected_route_point_ids() const {
	if (!_is_fps_npc_route() || path->get_curve().is_null() || !Node3DEditor::get_singleton()) {
		return Vector<int>();
	}
	Node3DEditor *node_3d_editor = Node3DEditor::get_singleton();
	Vector<int> selected;
	for (const Ref<Node3DGizmo> &gizmo : path->get_gizmos()) {
		EditorNode3DGizmo *editor_gizmo = Object::cast_to<EditorNode3DGizmo>(gizmo.ptr());
		if (editor_gizmo && node_3d_editor->is_current_selected_gizmo(editor_gizmo)) {
			selected = editor_gizmo->get_subgizmo_selection();
			break;
		}
	}
	const int point_count = path->get_curve()->get_point_count();
	Vector<int> valid_selection;
	for (int point_id : selected) {
		if (point_id >= 0 && point_id < point_count) {
			valid_selection.push_back(point_id);
		}
	}
	return valid_selection;
}

void Path3DEditorPlugin::_update_route_controls() {
	if (!_is_fps_npc_route() || path->get_curve().is_null()) {
		return;
	}
	const Vector<int> selected = _get_selected_route_point_ids();
	bool selection_changed = selected.size() != route_current_selection.size();
	if (!selection_changed) {
		for (int i = 0; i < selected.size(); i++) {
			if (selected[i] != route_current_selection[i]) {
				selection_changed = true;
				break;
			}
		}
	}
	if (!selection_changed && !selected.is_empty()) {
		return;
	}
	route_current_selection = selected;

	if (selected.is_empty()) {
		npc_route_selection_label->set_text(TTR("Select waypoint"));
		npc_route_selection_label->set_tooltip_text(TTR("Select an NPC route waypoint to edit its action."));
		npc_apply_point_action->set_disabled(true);
		npc_round_corner->set_disabled(true);
		npc_corner_radius->set_editable(false);
		npc_point_duration->set_editable(false);
		npc_point_target->set_editable(false);
		return;
	}
	npc_apply_point_action->set_disabled(false);
	npc_round_corner->set_disabled(true);
	npc_corner_radius->set_editable(false);
	if (selected.size() == 1) {
		npc_route_selection_label->set_text(vformat(TTR("WP %d/%d"), selected[0] + 1, path->get_curve()->get_point_count()));
		npc_route_selection_label->set_tooltip_text(vformat(TTR("Waypoint %d of %d"), selected[0] + 1, path->get_curve()->get_point_count()));
	} else {
		npc_route_selection_label->set_text(vformat(TTR("%d selected"), selected.size()));
		npc_route_selection_label->set_tooltip_text(vformat(TTR("%d waypoints selected"), selected.size()));
	}

	if (selected.size() == 1) {
		const int point_index = selected[0];
		Array actions = _get_route_point_actions();
		if (point_index < 0 || point_index >= actions.size() || actions[point_index].get_type() != Variant::DICTIONARY) {
			return;
		}
		Dictionary point_action = actions[point_index];
		const String action_name = point_action.get("action", "move");
		const Ref<Curve3D> curve = path->get_curve();
		const bool can_round = action_name == "move" && curve->get_point_count() >= 3 && (curve->is_closed() || (point_index > 0 && point_index < curve->get_point_count() - 1));
		npc_round_corner->set_disabled(!can_round);
		npc_corner_radius->set_editable(can_round);
		int action_id = NPC_POINT_ACTION_MOVE;
		for (int i = NPC_POINT_ACTION_MOVE; i <= NPC_POINT_ACTION_TAKE_COVER; i++) {
			if (_get_npc_route_action_name(i) == action_name) {
				action_id = i;
				break;
			}
		}
		npc_point_action->select(action_id);
		npc_point_duration->set_value(point_action.get("duration", 0.0));
		npc_point_target->set_text(point_action.get("target", ""));
	}
	_update_route_action_fields(npc_point_action->get_selected_id());
	}

void Path3DEditorPlugin::_update_route_action_fields(int p_action_id) {
	const int action_id = p_action_id;
	npc_point_duration->set_editable(action_id == NPC_POINT_ACTION_WAIT || action_id == NPC_POINT_ACTION_LOOK_AT);
	npc_point_target->set_editable(action_id == NPC_POINT_ACTION_LOOK_AT || action_id == NPC_POINT_ACTION_PLAY_ANIMATION || action_id == NPC_POINT_ACTION_INTERACT || action_id == NPC_POINT_ACTION_ATTACK || action_id == NPC_POINT_ACTION_TAKE_COVER);
}

void Path3DEditorPlugin::_refresh_route_action_display() {
	if (!_is_fps_npc_route()) {
		return;
	}
	route_current_selection.clear();
	_update_route_controls();
	path->update_gizmos();
}

void Path3DEditorPlugin::_sync_route_point_actions() {
	if (!_is_fps_npc_route() || path->get_curve().is_null()) {
		return;
	}
	Array actions = path->get_meta(npc_route_actions_meta(), Array());
	const int point_count = path->get_curve()->get_point_count();
	if (actions.size() == point_count) {
		return;
	}
	while (actions.size() < point_count) {
		Dictionary point_action;
		point_action["action"] = "move";
		point_action["duration"] = 0.0;
		point_action["target"] = "";
		actions.push_back(point_action);
	}
	while (actions.size() > point_count) {
		actions.remove_at(actions.size() - 1);
	}
	path->set_meta(npc_route_actions_meta(), actions);
	_update_route_controls();
}

void Path3DEditorPlugin::_route_curve_changed() {
	if (!_is_fps_npc_route()) {
		_update_toolbar();
		return;
	}
	path_3d_gizmo_plugin->clear_route_reachability(path->get_instance_id());
	_set_route_status(TTR("Route changed; checking obstacles..."));
	_queue_route_reachability_check();
	path->update_gizmos();
	call_deferred("_sync_route_point_actions");
	_update_toolbar();
}

void Path3DEditorPlugin::_set_route_status(const String &p_text) {
	if (!npc_route_reachability_label) {
		return;
	}
	npc_route_reachability_label->set_text(p_text);
	npc_route_reachability_label->set_tooltip_text(p_text);
}

void Path3DEditorPlugin::_apply_route_point_action() {
	if (!_is_fps_npc_route() || path->get_curve().is_null()) {
		return;
	}
	const Vector<int> selected = _get_selected_route_point_ids();
	if (selected.is_empty()) {
		return;
	}
	Array before = _get_route_point_actions();
	Array after = before.duplicate(true);
	const String action_name = _get_npc_route_action_name(npc_point_action->get_selected_id());
	for (int point_index : selected) {
		if (point_index < 0 || point_index >= after.size() || after[point_index].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary point_action = after[point_index];
		point_action["action"] = action_name;
		point_action["duration"] = npc_point_duration->get_value();
		point_action["target"] = npc_point_target->get_text();
		after[point_index] = point_action;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Set NPC Waypoint Action"));
	undo_redo->add_do_method(path, "set_meta", npc_route_actions_meta(), after);
	undo_redo->add_undo_method(path, "set_meta", npc_route_actions_meta(), before);
	undo_redo->add_do_method(this, "_refresh_route_action_display");
	undo_redo->add_undo_method(this, "_refresh_route_action_display");
	undo_redo->commit_action();
}

bool Path3DEditorPlugin::_snap_npc_route_point_to_navmesh(const Vector3 &p_world_position, Vector3 &r_snapped_position) const {
	if (!_is_fps_npc_route() || !path->get_world_3d().is_valid()) {
		return false;
	}
	NavigationServer3D *navigation_server = NavigationServer3D::get_singleton();
	const RID map = path->get_world_3d()->get_navigation_map();
	if (!map.is_valid()) {
		return false;
	}
	const RID owner = navigation_server->map_get_closest_point_owner(map, p_world_position);
	if (!owner.is_valid()) {
		return false;
	}
	const Vector3 closest_point = navigation_server->map_get_closest_point(map, p_world_position);
	// Constrain horizontal movement but allow any vertical adjustment to follow hills and slopes.
	Vector3 horizontal_offset = closest_point - p_world_position;
	horizontal_offset.y = 0.0;
	if (horizontal_offset.length() > 1.5) {
		return false;
	}
	r_snapped_position = closest_point;
	return true;
}

bool Path3DEditorPlugin::_npc_route_segment_hits_static_collision(const Ref<Curve3D> &p_curve, int p_segment_index, const Transform3D &p_path_transform, PhysicsDirectSpaceState3D *p_space, int p_override_point, const Vector3 &p_override_position) const {
	if (p_curve.is_null() || !p_space) {
		return false;
	}
	const int point_count = p_curve->get_point_count();
	const bool has_loop_segment = p_curve->is_closed() && point_count > 2;
	const int segment_count = MAX(0, point_count - 1 + (has_loop_segment ? 1 : 0));
	if (p_segment_index < 0 || p_segment_index >= segment_count || point_count < 2) {
		return false;
	}

	const int next_point = (p_segment_index + 1) % point_count;
	Vector3 start_local = p_curve->get_point_position(p_segment_index);
	Vector3 finish_local = p_curve->get_point_position(next_point);
	if (p_segment_index == p_override_point) {
		start_local = p_override_position;
	}
	if (next_point == p_override_point) {
		finish_local = p_override_position;
	}
	const Vector3 start = p_path_transform.xform(start_local);
	const Vector3 control_start = p_path_transform.xform(start_local + p_curve->get_point_out(p_segment_index));
	const Vector3 control_finish = p_path_transform.xform(finish_local + p_curve->get_point_in(next_point));
	const Vector3 finish = p_path_transform.xform(finish_local);
	const real_t control_length = start.distance_to(control_start) + control_start.distance_to(control_finish) + control_finish.distance_to(finish);
	const int sample_count = CLAMP(int(Math::ceil(control_length / 0.2)), 1, 512);
	Ref<CapsuleShape3D> clearance_shape = _get_npc_route_clearance_shape();
	PhysicsDirectSpaceState3D::ShapeResult results[32];
	for (int sample = 0; sample <= sample_count; sample++) {
		const real_t t = real_t(sample) / real_t(sample_count);
		const real_t inverse_t = 1.0 - t;
		const Vector3 centerline = start * (inverse_t * inverse_t * inverse_t) + control_start * (3.0 * inverse_t * inverse_t * t) + control_finish * (3.0 * inverse_t * t * t) + finish * (t * t * t);
		PhysicsDirectSpaceState3D::ShapeParameters parameters;
		parameters.shape_rid = clearance_shape->get_rid();
		parameters.transform.origin = centerline + Vector3(0.0, 1.05, 0.0);
		parameters.margin = 0.05;
		parameters.collision_mask = UINT32_MAX;
		parameters.collide_with_bodies = true;
		parameters.collide_with_areas = false;
		const int result_count = p_space->intersect_shape(parameters, results, 32);
		for (int result_index = 0; result_index < result_count; result_index++) {
			Object *collider = results[result_index].collider;
			if (collider && (collider->is_class("StaticBody3D") || collider->is_class("CSGShape3D"))) {
				return true;
			}
		}
	}
	return false;
}

int Path3DEditorPlugin::_npc_route_adjacent_collision_count(const Ref<Curve3D> &p_curve, int p_point_index, const Transform3D &p_path_transform, PhysicsDirectSpaceState3D *p_space, int p_override_point, const Vector3 &p_override_position) const {
	if (p_curve.is_null() || p_point_index < 0 || p_point_index >= p_curve->get_point_count()) {
		return 0;
	}
	const int point_count = p_curve->get_point_count();
	const bool has_loop_segment = p_curve->is_closed() && point_count > 2;
	const int segment_count = MAX(0, point_count - 1 + (has_loop_segment ? 1 : 0));
	int collision_count = 0;
	int previous_segment = p_point_index - 1;
	if (previous_segment < 0 && has_loop_segment) {
		previous_segment = segment_count - 1;
	}
	if (previous_segment >= 0 && _npc_route_segment_hits_static_collision(p_curve, previous_segment, p_path_transform, p_space, p_override_point, p_override_position)) {
		collision_count++;
	}
	if (p_point_index < segment_count && p_point_index != previous_segment && _npc_route_segment_hits_static_collision(p_curve, p_point_index, p_path_transform, p_space, p_override_point, p_override_position)) {
		collision_count++;
	}
	return collision_count;
}

void Path3DEditorPlugin::_queue_route_reachability_check(bool p_immediate) {
	if (!_is_fps_npc_route()) {
		return;
	}
	route_reachability_check_pending = true;
	route_reachability_check_deadline_msec = Time::get_singleton()->get_ticks_msec() + (p_immediate ? 0 : 180);
}

void Path3DEditorPlugin::_request_route_reachability_check() {
	if (!_is_fps_npc_route()) {
		return;
	}
	_set_route_status(TTR("Checking NavigationMesh and static obstacles..."));
	_queue_route_reachability_check(true);
}

void Path3DEditorPlugin::_process_pending_route_waypoint_drag() {
	if (!pending_route_waypoint_drag) {
		return;
	}
	pending_route_waypoint_drag = false;
	Vector<int> point_ids = pending_route_waypoint_ids;
	Vector<Vector3> target_positions = pending_route_waypoint_positions;
	pending_route_waypoint_ids.clear();
	pending_route_waypoint_positions.clear();
	if (!_is_fps_npc_route() || path->get_curve().is_null() || path->get_world_3d().is_null()) {
		return;
	}
	Ref<Curve3D> curve = path->get_curve();
	PhysicsDirectSpaceState3D *space = path->get_world_3d()->get_direct_space_state();
	if (!space) {
		return;
	}
	const Transform3D path_transform = path->get_global_transform();
	for (int move_index = 0; move_index < point_ids.size() && move_index < target_positions.size(); move_index++) {
		const int point_index = point_ids[move_index];
		const Vector3 target_position = target_positions[move_index];
		if (point_index < 0 || point_index >= curve->get_point_count()) {
			continue;
		}
		const Vector3 current_position = curve->get_point_position(point_index);
		if (current_position.is_equal_approx(target_position)) {
			continue;
		}
		const int target_collision_count = _npc_route_adjacent_collision_count(curve, point_index, path_transform, space, point_index, target_position);
		if (target_collision_count > 0) {
			const int current_collision_count = _npc_route_adjacent_collision_count(curve, point_index, path_transform, space);
			if (current_collision_count == 0) {
				_set_route_status(TTR("Static obstacle detected; waypoint stopped with clearance"));
				continue;
			}
		}
		if (current_position.distance_squared_to(target_position) > 0.0000001) {
			curve->set_point_position(point_index, target_position);
		}
	}
}

void Path3DEditorPlugin::_set_route_navmesh_snap_enabled(bool p_enabled) {
	npc_route_snap_to_navmesh = p_enabled;
	npc_snap_route_to_navmesh->set_disabled(p_enabled);
	if (!p_enabled) {
		_set_route_status(TTR("Automatic NavigationMesh detection is disabled; waypoint height can be edited"));
		return;
	}
	const int aligned_count = _snap_route_to_navmesh();
	const int point_count = path && path->get_curve().is_valid() ? path->get_curve()->get_point_count() : 0;
	if (aligned_count != point_count) {
		_set_route_status(vformat(TTR("Detection on; NavMesh found for %d of %d waypoints. Unaligned points stay locked until placed on a walkable surface"), aligned_count, point_count));
		return;
	}
	_set_route_status(vformat(TTR("Detection on; all %d waypoints follow the NavigationMesh"), point_count));
}

void Path3DEditorPlugin::_set_route_navmesh_snap_state(bool p_enabled) {
	npc_route_snap_to_navmesh = p_enabled;
	npc_route_navmesh_snap->set_pressed_no_signal(p_enabled);
	npc_snap_route_to_navmesh->set_disabled(p_enabled);
	_set_route_status(p_enabled ? TTR("Detection on; waypoints stay on the NavigationMesh") : TTR("Automatic NavigationMesh detection is disabled; waypoint height can be edited"));
}

int Path3DEditorPlugin::_snap_route_to_navmesh() {
	if (!_is_fps_npc_route() || path->get_curve().is_null() || path->get_world_3d().is_null()) {
		return 0;
	}
	NavigationServer3D *navigation_server = NavigationServer3D::get_singleton();
	const RID map = path->get_world_3d()->get_navigation_map();
	if (!map.is_valid() || navigation_server->map_get_regions(map).is_empty()) {
		_set_route_status(TTR("No NavigationMesh is available"));
		return 0;
	}
	navigation_server->map_force_update(map);

	Ref<Curve3D> curve = path->get_curve();
	const Transform3D transform = path->get_global_transform();
	const Transform3D inverse = transform.affine_inverse();
	Vector<int> changed_points;
	Vector<Vector3> before_positions;
	Vector<Vector3> after_positions;
	int aligned_count = 0;
	for (int i = 0; i < curve->get_point_count(); i++) {
		const Vector3 before = curve->get_point_position(i);
		Vector3 snapped_world;
		if (!_snap_npc_route_point_to_navmesh(transform.xform(before), snapped_world)) {
			continue;
		}
		aligned_count++;
		const Vector3 after = inverse.xform(snapped_world);
		if (before.is_equal_approx(after)) {
			continue;
		}
		changed_points.push_back(i);
		before_positions.push_back(before);
		after_positions.push_back(after);
	}

	if (!changed_points.is_empty()) {
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Snap NPC Route to NavigationMesh"), UndoRedo::MERGE_DISABLE, path);
		const bool enables_detection = npc_route_snap_to_navmesh && aligned_count == curve->get_point_count();
		if (enables_detection) {
			undo_redo->add_do_method(this, "_set_route_navmesh_snap_state", true);
		}
		for (int i = 0; i < changed_points.size(); i++) {
			undo_redo->add_do_method(curve.ptr(), "set_point_position", changed_points[i], after_positions[i]);
			undo_redo->add_undo_method(curve.ptr(), "set_point_position", changed_points[i], before_positions[i]);
		}
		if (enables_detection) {
			undo_redo->add_undo_method(this, "_set_route_navmesh_snap_state", false);
		}
		undo_redo->commit_action();
	}
	_set_route_status(vformat(TTR("Aligned %d of %d waypoints to the NavigationMesh"), aligned_count, curve->get_point_count()));
	return aligned_count;
}

void Path3DEditorPlugin::_round_route_corner() {
	if (!_is_fps_npc_route() || path->get_curve().is_null()) {
		return;
	}
	const Vector<int> selected = _get_selected_route_point_ids();
	if (selected.size() != 1) {
		return;
	}
	const Ref<Curve3D> before_curve = path->get_curve();
	const int count = before_curve->get_point_count();
	const int index = selected[0];
	if (count < 3 || (!before_curve->is_closed() && (index == 0 || index == count - 1))) {
		return;
	}
	const Array before_actions = _get_route_point_actions();
	const Dictionary action = before_actions[index];
	if (String(action.get("action", "move")) != "move") {
		return;
	}
	const int previous = (index + count - 1) % count;
	const int next = (index + 1) % count;
	if (!before_curve->get_point_out(previous).is_zero_approx() || !before_curve->get_point_in(index).is_zero_approx() || !before_curve->get_point_out(index).is_zero_approx() || !before_curve->get_point_in(next).is_zero_approx()) {
		_set_route_status(TTR("Rounding needs straight adjoining segments"));
		return;
	}
	const Transform3D transform = path->get_global_transform();
	if (Math::is_zero_approx(transform.basis.determinant())) {
		return;
	}
	const NPCPathGeometry::CornerArc arc = NPCPathGeometry::create_corner_arc(
			transform.xform(before_curve->get_point_position(previous)),
			transform.xform(before_curve->get_point_position(index)),
			transform.xform(before_curve->get_point_position(next)), npc_corner_radius->get_value());
	if (arc.points.is_empty()) {
		_set_route_status(TTR("Select a corner, not a straight line or reversal"));
		return;
	}
	const Transform3D inverse = transform.affine_inverse();
	if (npc_route_snap_to_navmesh && path->get_world_3d().is_valid()) {
		const RID map = path->get_world_3d()->get_navigation_map();
		if (map.is_valid()) {
			NavigationServer3D::get_singleton()->map_force_update(map);
		}
	}
	Ref<Curve3D> after_curve = before_curve->duplicate();
	after_curve->clear_points();
	Array after_actions;
	for (int i = 0; i < count; i++) {
		if (i == index) {
			for (const Vector3 &point : arc.points) {
				Vector3 route_point = point;
				Vector3 navmesh_point;
				if (npc_route_snap_to_navmesh) {
					if (!_snap_npc_route_point_to_navmesh(route_point, navmesh_point)) {
						_set_route_status(TTR("Rounded corner leaves the NavigationMesh; no changes made"));
						return;
					}
					route_point = navmesh_point;
				}
				after_curve->add_point(inverse.xform(route_point));
				after_curve->set_point_tilt(after_curve->get_point_count() - 1, before_curve->get_point_tilt(index));
				after_actions.push_back(action.duplicate(true));
			}
		} else {
			after_curve->add_point(before_curve->get_point_position(i), before_curve->get_point_in(i), before_curve->get_point_out(i));
			after_curve->set_point_tilt(after_curve->get_point_count() - 1, before_curve->get_point_tilt(i));
			after_actions.push_back(before_actions[i]);
		}
	}
	after_curve->set_closed(before_curve->is_closed());
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Round NPC Route Corner"));
	undo_redo->add_do_method(path, "set_meta", npc_route_actions_meta(), after_actions);
	undo_redo->add_do_method(path, "set_curve", after_curve);
	undo_redo->add_undo_method(path, "set_meta", npc_route_actions_meta(), before_actions);
	undo_redo->add_undo_method(path, "set_curve", before_curve);
	undo_redo->add_do_method(this, "_refresh_route_action_display");
	undo_redo->add_undo_method(this, "_refresh_route_action_display");
	Node3DEditor::get_singleton()->clear_subgizmo_selection(path);
	undo_redo->commit_action();
	_set_route_status(vformat(TTR("Rounded radius: %.2f m"), arc.radius));
}

void Path3DEditorPlugin::_check_route_reachability() {
	if (!_is_fps_npc_route() || path->get_curve().is_null() || path->get_world_3d().is_null()) {
		return;
	}
	NavigationServer3D *navigation_server = NavigationServer3D::get_singleton();
	const RID map = path->get_world_3d()->get_navigation_map();
	Ref<Curve3D> curve = path->get_curve();
	const int point_count = curve->get_point_count();
	const bool has_loop_segment = curve->is_closed() && point_count > 2;
	const int segment_count = MAX(0, point_count - 1 + (has_loop_segment ? 1 : 0));
	Vector<int> states;
	states.resize(segment_count);
	for (int i = 0; i < segment_count; i++) {
		states.write[i] = -1;
	}
	const Transform3D path_transform = path->get_global_transform();
	PhysicsDirectSpaceState3D *space = path->get_world_3d()->get_direct_space_state();
	int static_collision_count = 0;
	if (space) {
		for (int i = 0; i < segment_count; i++) {
			if (_npc_route_segment_hits_static_collision(curve, i, path_transform, space)) {
				states.write[i] = 2;
				static_collision_count++;
			}
		}
	}

	if (!map.is_valid()) {
		_set_route_status(static_collision_count > 0 ? vformat(TTR("No active NavigationMesh; %d route links hit static obstacles"), static_collision_count) : TTR("No active NavigationMesh"));
		path_3d_gizmo_plugin->set_route_reachability(path->get_instance_id(), states);
		path->update_gizmos();
		return;
	}

	navigation_server->map_force_update(map);
	const int region_count = navigation_server->map_get_regions(map).size();
	if (region_count == 0) {
		_set_route_status(static_collision_count > 0 ? vformat(TTR("No NavigationRegion3D; %d route links hit static obstacles"), static_collision_count) : TTR("No NavigationRegion3D on this scene"));
		path_3d_gizmo_plugin->set_route_reachability(path->get_instance_id(), states);
		path->update_gizmos();
		return;
	}

	int reachable_count = 0;
	for (int i = 0; i < segment_count; i++) {
		const int next_point = (i + 1) % point_count;
		const Vector3 start = path_transform.xform(curve->get_point_position(i));
		const Vector3 finish = path_transform.xform(curve->get_point_position(next_point));
		const Vector3 nav_start = navigation_server->map_get_closest_point(map, start);
		const Vector3 nav_finish = navigation_server->map_get_closest_point(map, finish);
		const RID start_region = navigation_server->map_get_closest_point_owner(map, start);
		const RID finish_region = navigation_server->map_get_closest_point_owner(map, finish);
		if (!start_region.is_valid() || !finish_region.is_valid() || start.distance_to(nav_start) > 1.5 || finish.distance_to(nav_finish) > 1.5) {
			if (states[i] != 2) {
				states.write[i] = 0;
			}
			continue;
		}
		const Vector<Vector3> route = navigation_server->map_get_path(map, nav_start, nav_finish, true, 1);
		if (route.size() > 1 || nav_start.distance_to(nav_finish) < 0.01) {
			if (states[i] != 2) {
				states.write[i] = 1;
			}
			reachable_count++;
		} else {
			if (states[i] != 2) {
				states.write[i] = 0;
			}
		}
	}
	path_3d_gizmo_plugin->set_route_reachability(path->get_instance_id(), states);
	if (segment_count == 0) {
		_set_route_status(TTR("Add at least two waypoints"));
	} else {
		_set_route_status(vformat(TTR("%d/%d links reachable; %d intersect static geometry"), reachable_count, segment_count, static_collision_count));
	}
	path->update_gizmos();
}

void Path3DEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			_update_route_controls();
		} break;
		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (!path) {
				return;
			}

			if (!EditorSettings::get_singleton()->check_changed_settings_in_group("editors/3d_gizmos/gizmo_settings")) {
				return;
			}

			path->update_gizmos();
		} break;
		case NOTIFICATION_PHYSICS_PROCESS: {
			if (_edit.waiting_point_physics) {
				_edit.waiting_point_physics = false;
				const Transform3D gt = path->get_global_transform();
				const Transform3D it = gt.affine_inverse();
				Ref<Curve3D> c = path->get_curve();
				EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
				PhysicsDirectSpaceState3D *ss = get_tree()->get_root()->get_world_3d()->get_direct_space_state();
				if (ss) {
					PhysicsDirectSpaceState3D::RayParameters ray_params;
					PhysicsDirectSpaceState3D::RayResult result;
					ray_params.from = _edit.click_ray_pos;
					ray_params.to = ray_params.from + _edit.click_ray_dir;
					bool hit_something = false;
					Vector3 inters;
					if (ss->intersect_ray(ray_params, result)) {
						inters = result.position;
						hit_something = true;
					} else {
						Plane p(_edit.gizmo_camera->get_transform().basis.get_column(2), _edit.origin);
						if (p.intersects_ray(ray_params.from, _edit.click_ray_dir, &inters)) {
							hit_something = true;
						}
					}
					if (hit_something) {
						if (_is_fps_npc_route() && npc_route_snap_to_navmesh) {
							Vector3 navmesh_position;
							if (_snap_npc_route_point_to_navmesh(inters, navmesh_position)) {
								inters = navmesh_position;
							} else {
								_set_route_status(TTR("No nearby NavigationMesh; waypoint was not added"));
								hit_something = false;
							}
						}
					}
					if (hit_something) {
						ur->create_action(TTR("Add Point to Curve"));
						if (_is_fps_npc_route()) {
							const Array before = _get_route_point_actions();
							_add_route_point_action_undo(ur, before, _insert_route_point_action(before, c->get_point_count()));
						}
						ur->add_do_method(c.ptr(), "add_point", it.xform(inters), Vector3(), Vector3(), -1);
						ur->add_undo_method(c.ptr(), "remove_point", c->get_point_count());
						ur->commit_action();
					}
				}
			}
			if (_edit.waiting_handle_physics) {
				_edit.in_physics_frame = true;

				// Find gizmo reference.
				Vector<Ref<Node3DGizmo>> gizmos = path->get_gizmos();
				for (Ref<EditorNode3DGizmo> seg : gizmos) {
					if (seg.is_valid()) {
						_edit.gizmo = seg;
						break;
					}
				}

				_edit.gizmo->set_handle(_edit.gizmo_handle, _edit.gizmo_handle_secondary, _edit.gizmo_camera, _edit.mouse_pos);
				_edit.in_physics_frame = false;
				_edit.waiting_handle_physics = false;
			}
			_process_pending_route_waypoint_drag();
			if (route_reachability_check_pending && Time::get_singleton()->get_ticks_msec() >= route_reachability_check_deadline_msec) {
				route_reachability_check_pending = false;
				_check_route_reachability();
			}
		}
	}
}

void Path3DEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_update_toolbar"), &Path3DEditorPlugin::_update_toolbar);
	ClassDB::bind_method(D_METHOD("_refresh_route_action_display"), &Path3DEditorPlugin::_refresh_route_action_display);
	ClassDB::bind_method(D_METHOD("_sync_route_point_actions"), &Path3DEditorPlugin::_sync_route_point_actions);
	ClassDB::bind_method(D_METHOD("_set_route_navmesh_snap_state", "enabled"), &Path3DEditorPlugin::_set_route_navmesh_snap_state);
	ClassDB::bind_method(D_METHOD("_request_route_reachability_check"), &Path3DEditorPlugin::_request_route_reachability_check);
	ClassDB::bind_method(D_METHOD("_clear_curve_points"), &Path3DEditorPlugin::_clear_curve_points);
	ClassDB::bind_method(D_METHOD("_restore_curve_points", "points", "curve_was_closed"), &Path3DEditorPlugin::_restore_curve_points);
}

Path3DEditorPlugin::Path3DEditorPlugin() {
	singleton = this;
	mirror_handle_angle = true;
	mirror_handle_length = true;

	Ref<Path3DGizmoPlugin> gizmo_plugin;
	gizmo_plugin.instantiate();
	Node3DEditor::get_singleton()->add_gizmo_plugin(gizmo_plugin);
	path_3d_gizmo_plugin = gizmo_plugin;

	topmenu_bar = memnew(HBoxContainer);
	topmenu_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	topmenu_bar->hide();

	toolbar = memnew(HFlowContainer);
	toolbar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	toolbar->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	topmenu_bar->add_child(toolbar);

	curve_edit = memnew(Button);
	curve_edit->set_toggle_mode(true);
	curve_edit->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	curve_edit->set_tooltip_text(TTR("Select Points") + "\n" + TTR("Shift+Click: Select multiple Points") + "\n" + vformat(TTR("%s+Click: Add Point"), keycode_get_string((Key)KeyModifierMask::CMD_OR_CTRL)) + "\n" + TTR("Right Click: Delete Point"));
	curve_edit->set_accessibility_name(TTRC("Select Points"));
	toolbar->add_child(curve_edit);
	curve_edit->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_mode_changed).bind(MODE_EDIT));

	curve_edit_curve = memnew(Button);
	curve_edit_curve->set_toggle_mode(true);
	curve_edit_curve->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	curve_edit_curve->set_tooltip_text(TTR("Select Control Points") + "\n" + TTR("Shift+Click: Drag out Control Points"));
	curve_edit_curve->set_accessibility_name(TTRC("Select Control Points"));
	toolbar->add_child(curve_edit_curve);
	curve_edit_curve->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_mode_changed).bind(MODE_EDIT_CURVE));

	curve_edit_tilt = memnew(Button);
	curve_edit_tilt->set_toggle_mode(true);
	curve_edit_tilt->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	curve_edit_tilt->set_tooltip_text(TTR("Select Tilt Handles"));
	toolbar->add_child(curve_edit_tilt);
	curve_edit_tilt->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_mode_changed).bind(MODE_EDIT_TILT));

	curve_create = memnew(Button);
	curve_create->set_toggle_mode(true);
	curve_create->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	curve_create->set_tooltip_text(TTR("Add Point (in empty space)") + "\n" + TTR("Split Segment (in curve)"));
	curve_create->set_accessibility_name(TTRC("Add Point (in empty space)"));
	toolbar->add_child(curve_create);
	curve_create->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_mode_changed).bind(MODE_CREATE));

	curve_del = memnew(Button);
	curve_del->set_toggle_mode(true);
	curve_del->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	curve_del->set_tooltip_text(TTR("Delete Point"));
	toolbar->add_child(curve_del);
	curve_del->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_mode_changed).bind(MODE_DELETE));

	curve_closed = memnew(Button);
	curve_closed->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	curve_closed->set_tooltip_text(TTR("Close Curve"));
	toolbar->add_child(curve_closed);
	curve_closed->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_toggle_closed_curve));

	curve_clear_points = memnew(Button);
	curve_clear_points->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	curve_clear_points->set_tooltip_text(TTR("Clear Points"));
	curve_clear_points->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_confirm_clear_points));
	toolbar->add_child(curve_clear_points);

	clear_points_dialog = memnew(ConfirmationDialog);
	clear_points_dialog->set_title(TTR("Please Confirm..."));
	clear_points_dialog->set_text(TTR("Remove all curve points?"));
	clear_points_dialog->connect(SceneStringName(confirmed), callable_mp(this, &Path3DEditorPlugin::_clear_points));
	toolbar->add_child(clear_points_dialog);

	handle_menu = memnew(MenuButton);
	handle_menu->set_flat(false);
	handle_menu->set_text(TTR("Options"));
	toolbar->add_child(handle_menu);

	create_curve_button = memnew(Button);
	create_curve_button->set_text(TTR("Create Curve"));
	create_curve_button->hide();
	topmenu_bar->add_child(create_curve_button);
	create_curve_button->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_create_curve));

	PopupMenu *menu = handle_menu->get_popup();
	menu->add_check_item(TTR("Mirror Handle Angles"));
	menu->set_item_checked(HANDLE_OPTION_ANGLE, mirror_handle_angle);
	menu->add_check_item(TTR("Mirror Handle Lengths"));
	menu->set_item_checked(HANDLE_OPTION_LENGTH, mirror_handle_length);
	menu->add_check_item(TTR("Snap to Colliders"));
	menu->set_item_checked(HANDLE_OPTION_SNAP_COLLIDER, snap_to_collider);
	menu->connect(SceneStringName(id_pressed), callable_mp(this, &Path3DEditorPlugin::_handle_option_pressed));

	// All tools share one flow, so every row uses the full viewport width.
	// A nested flow would compete with its parent for width and row height.
	auto add_route_control = [this](Control *p_control) {
		toolbar->add_child(p_control);
		npc_route_controls.push_back(p_control);
		p_control->hide();
	};
	VSeparator *route_separator = memnew(VSeparator);
	add_route_control(route_separator);

	npc_route_selection_label = memnew(Label);
	npc_route_selection_label->set_text(TTR("Select waypoint"));
	npc_route_selection_label->set_custom_minimum_size(Size2(96, 0) * EDSCALE);
	add_route_control(npc_route_selection_label);

	npc_point_action = memnew(OptionButton);
	npc_point_action->set_custom_minimum_size(Size2(132, 0) * EDSCALE);
	npc_point_action->set_clip_text(true);
	npc_point_action->add_item(TTR("Move"), NPC_POINT_ACTION_MOVE);
	npc_point_action->add_item(TTR("Wait"), NPC_POINT_ACTION_WAIT);
	npc_point_action->add_item(TTR("Look at Target"), NPC_POINT_ACTION_LOOK_AT);
	npc_point_action->add_item(TTR("Play Animation"), NPC_POINT_ACTION_PLAY_ANIMATION);
	npc_point_action->add_item(TTR("Interact"), NPC_POINT_ACTION_INTERACT);
	npc_point_action->add_item(TTR("Attack"), NPC_POINT_ACTION_ATTACK);
	npc_point_action->add_item(TTR("Take Cover"), NPC_POINT_ACTION_TAKE_COVER);
	npc_point_action->set_tooltip_text(TTR("Action to store on the selected NPC route waypoint."));
	npc_point_action->connect(SceneStringName(item_selected), callable_mp(this, &Path3DEditorPlugin::_update_route_action_fields));
	add_route_control(npc_point_action);

	npc_point_duration = memnew(SpinBox);
	npc_point_duration->set_min(0.0);
	npc_point_duration->set_max(3600.0);
	npc_point_duration->set_step(0.1);
	npc_point_duration->set_suffix(" s");
	npc_point_duration->set_custom_minimum_size(Size2(70, 0) * EDSCALE);
	npc_point_duration->set_tooltip_text(TTR("Duration for wait or timed waypoint actions."));
	add_route_control(npc_point_duration);

	npc_point_target = memnew(LineEdit);
	npc_point_target->set_custom_minimum_size(Size2(108, 0) * EDSCALE);
	npc_point_target->set_placeholder(TTR("Target"));
	npc_point_target->set_tooltip_text(TTR("For Look at Target, enter a NodePath resolved relative to the NPC, then the active scene root. For Play Animation, enter an animation name."));
	add_route_control(npc_point_target);

	npc_apply_point_action = memnew(Button);
	npc_apply_point_action->set_text(TTR("Apply"));
	npc_apply_point_action->set_tooltip_text(TTR("Store the selected action on the selected waypoint(s)."));
	npc_apply_point_action->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_apply_route_point_action));
	add_route_control(npc_apply_point_action);

	npc_route_navmesh_snap = memnew(CheckButton);
	npc_route_navmesh_snap->set_text(TTR("NavMesh"));
	npc_route_navmesh_snap->set_custom_minimum_size(Size2(96, 0) * EDSCALE);
	npc_route_navmesh_snap->set_pressed(npc_route_snap_to_navmesh);
	npc_route_navmesh_snap->set_tooltip_text(TTR("When enabled, align every waypoint to the nearby NavigationMesh and keep points on it while editing. Turn off to edit waypoint heights. Horizontal shifts farther than 1.5 m are rejected."));
	npc_route_navmesh_snap->connect(SceneStringName(toggled), callable_mp(this, &Path3DEditorPlugin::_set_route_navmesh_snap_enabled));
	add_route_control(npc_route_navmesh_snap);

	npc_snap_route_to_navmesh = memnew(Button);
	npc_snap_route_to_navmesh->set_text(TTR("Snap"));
	npc_snap_route_to_navmesh->set_custom_minimum_size(Size2(64, 0) * EDSCALE);
	npc_snap_route_to_navmesh->set_disabled(npc_route_snap_to_navmesh);
	npc_snap_route_to_navmesh->set_tooltip_text(TTR("Project all existing NPC route waypoints onto the nearby NavigationMesh. Undo restores their previous positions."));
	npc_snap_route_to_navmesh->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_snap_route_to_navmesh));
	add_route_control(npc_snap_route_to_navmesh);

	npc_corner_radius = memnew(SpinBox);
	npc_corner_radius->set_min(0.25);
	npc_corner_radius->set_max(20.0);
	npc_corner_radius->set_step(0.05);
	npc_corner_radius->set_value(0.75);
	npc_corner_radius->set_suffix(" m");
	npc_corner_radius->set_custom_minimum_size(Size2(74, 0) * EDSCALE);
	npc_corner_radius->set_editable(false);
	npc_corner_radius->set_tooltip_text(TTR("World-space corner radius. Automatically reduced when adjoining segments are too short."));
	add_route_control(npc_corner_radius);
	npc_round_corner = memnew(Button);
	npc_round_corner->set_text(TTR("Round"));
	npc_round_corner->set_custom_minimum_size(Size2(72, 0) * EDSCALE);
	npc_round_corner->set_disabled(true);
	npc_round_corner->set_tooltip_text(TTR("Replace one selected Move waypoint with an arc of pass-through waypoints. Wait/action points and open route endpoints stay exact. Undo restores the original corner."));
	npc_round_corner->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_round_route_corner));
	add_route_control(npc_round_corner);

	npc_check_reachability = memnew(Button);
	npc_check_reachability->set_text(TTR("Check"));
	npc_check_reachability->set_custom_minimum_size(Size2(68, 0) * EDSCALE);
	npc_check_reachability->set_tooltip_text(TTR("Check NavigationMesh connectivity and collisions with static geometry along the whole route."));
	npc_check_reachability->connect(SceneStringName(pressed), callable_mp(this, &Path3DEditorPlugin::_request_route_reachability_check));
	add_route_control(npc_check_reachability);

	npc_route_reachability_label = memnew(Label);
	npc_route_reachability_label->set_custom_minimum_size(Size2(140, 0) * EDSCALE);
	npc_route_reachability_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	npc_route_reachability_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	npc_route_reachability_label->set_clip_text(false);
	npc_route_reachability_label->set_text_overrun_behavior(TextServer::OVERRUN_NO_TRIMMING);
	_set_route_status(TTR("Not checked"));
	add_route_control(npc_route_reachability_label);

	curve_edit->set_pressed_no_signal(true);

	topmenu_bar->connect(SceneStringName(theme_changed), callable_mp(this, &Path3DEditorPlugin::_update_theme), CONNECT_DEFERRED);
	Node3DEditor::get_singleton()->add_control_to_menu_panel(topmenu_bar);
	_update_theme();
}

Ref<EditorNode3DGizmo> Path3DGizmoPlugin::create_gizmo(Node3D *p_spatial) {
	Ref<Path3DGizmo> ref;

	Path3D *path = Object::cast_to<Path3D>(p_spatial);
	if (path) {
		ref.instantiate(path);
	}

	return ref;
}

bool Path3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<Path3D>(p_spatial) != nullptr;
}

String Path3DGizmoPlugin::get_gizmo_name() const {
	return "Path3D";
}

void Path3DGizmoPlugin::set_route_reachability(ObjectID p_path_id, const Vector<int> &p_states) {
	route_reachability[p_path_id] = p_states;
}

void Path3DGizmoPlugin::clear_route_reachability(ObjectID p_path_id) {
	route_reachability.erase(p_path_id);
}

const Vector<int> *Path3DGizmoPlugin::get_route_reachability(ObjectID p_path_id) const {
	return route_reachability.getptr(p_path_id);
}

void Path3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	Path3D *path = Object::cast_to<Path3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL(path);

	Ref<Curve3D> curve = path->get_curve();

	Ref<StandardMaterial3D> handle_material = get_material("handles", p_gizmo);
	Ref<StandardMaterial3D> first_pt_handle_material = get_material("first_pt_handle", p_gizmo);
	Ref<StandardMaterial3D> last_pt_handle_material = get_material("last_pt_handle", p_gizmo);
	Ref<StandardMaterial3D> closed_pt_handle_material = get_material("closed_pt_handle", p_gizmo);
	Ref<StandardMaterial3D> selected_pt_handle_material = get_material("selected_pt_handle", p_gizmo);

	first_pt_handle_material->set_albedo(Color(0.2, 1.0, 0.0));
	last_pt_handle_material->set_albedo(Color(1.0, 0.2, 0.0));
	closed_pt_handle_material->set_albedo(Color(1.0, 0.8, 0.0));
	selected_pt_handle_material->set_albedo(Color(0.2, 0.5, 1.0));

	PackedInt32Array selected_points = p_gizmo->get_subgizmo_selection();
	PackedVector3Array first_pt;
	PackedVector3Array last_pt;
	PackedVector3Array common_pt;
	PackedVector3Array selected_pt;
	int last = curve->get_point_count() - 1;
	for (int i = 0; i <= last; i++) {
		if (selected_points.has(i)) {
			selected_pt.push_back(curve->get_point_position(i));
		} else if (i == 0) {
			first_pt.push_back(curve->get_point_position(i));
		} else if (i == last) {
			last_pt.push_back(curve->get_point_position(i));
		} else {
			common_pt.push_back(curve->get_point_position(i));
		}
	}
	if (first_pt.size()) {
		p_gizmo->add_vertices(first_pt, curve->is_closed() ? closed_pt_handle_material : first_pt_handle_material, Mesh::PRIMITIVE_POINTS);
	}
	if (last_pt.size()) {
		p_gizmo->add_vertices(last_pt, curve->is_closed() ? handle_material : last_pt_handle_material, Mesh::PRIMITIVE_POINTS);
	}
	if (common_pt.size()) {
		p_gizmo->add_vertices(common_pt, handle_material, Mesh::PRIMITIVE_POINTS);
	}
	if (selected_pt.size()) {
		p_gizmo->add_vertices(selected_pt, selected_pt_handle_material, Mesh::PRIMITIVE_POINTS);
	}
}

int Path3DGizmoPlugin::subgizmos_intersect_ray(const EditorNode3DGizmo *p_gizmo, Camera3D *p_camera, const Vector2 &p_point) const {
	Path3D *path = Object::cast_to<Path3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(path, -1);
	Ref<Curve3D> curve = path->get_curve();
	ERR_FAIL_COND_V(curve.is_null(), -1);

	int ret = -1;
	if (Path3DEditorPlugin::singleton->curve_edit->is_pressed()) {
		for (int idx = 0; idx < curve->get_point_count(); ++idx) {
			Vector3 pos = path->get_global_transform().xform(curve->get_point_position(idx));
			if (p_camera->unproject_position(pos).distance_to(p_point) < 20) {
				ret = idx;
				break;
			}
		}
	}

	if (Path3DEditorPlugin::singleton) {
		Path3DEditorPlugin::singleton->update_handles();
	}

	return ret;
}

Vector<int> Path3DGizmoPlugin::subgizmos_intersect_frustum(const EditorNode3DGizmo *p_gizmo, const Camera3D *p_camera, const Vector<Plane> &p_frustum) const {
	Vector<int> contained_points;

	Path3D *path = Object::cast_to<Path3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(path, contained_points);
	Ref<Curve3D> curve = path->get_curve();
	ERR_FAIL_COND_V(curve.is_null(), contained_points);

	if (Path3DEditorPlugin::singleton->curve_edit->is_pressed()) {
		for (int idx = 0; idx < curve->get_point_count(); ++idx) {
			Vector3 pos = path->get_global_transform().xform(curve->get_point_position(idx));
			bool is_contained_in_frustum = true;
			for (int i = 0; i < p_frustum.size(); ++i) {
				if (p_frustum[i].distance_to(pos) > 0) {
					is_contained_in_frustum = false;
					break;
				}
			}

			if (is_contained_in_frustum) {
				contained_points.push_back(idx);
			}
		}
	}

	if (Path3DEditorPlugin::singleton) {
		Path3DEditorPlugin::singleton->update_handles();
	}

	return contained_points;
}

Transform3D Path3DGizmoPlugin::get_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id) const {
	Path3D *path = Object::cast_to<Path3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(path, Transform3D());
	Ref<Curve3D> curve = path->get_curve();
	ERR_FAIL_COND_V(curve.is_null(), Transform3D());
	if (p_id < 0 || p_id >= curve->get_point_count()) {
		return Transform3D();
	}

	Basis basis = transformation_locked_basis.has(p_id) ? transformation_locked_basis[p_id] : curve->get_point_baked_posture(p_id, true);
	if (!basis.is_orthonormal()) {
		basis.orthonormalize();
	}
	Vector3 pos = curve->get_point_position(p_id);

	Transform3D t = Transform3D(basis, pos);
	return t;
}

void Path3DGizmoPlugin::set_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id, Transform3D p_transform) {
	Path3D *path = Object::cast_to<Path3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL(path);
	Ref<Curve3D> curve = path->get_curve();
	ERR_FAIL_COND(curve.is_null());
	if (p_id < 0 || p_id >= curve->get_point_count()) {
		return;
	}

	if (!transformation_locked_basis.has(p_id)) {
		transformation_locked_basis[p_id] = Basis(curve->get_point_baked_posture(p_id, true)).orthonormalized();
	}
	Path3DEditorPlugin *route_plugin = Path3DEditorPlugin::singleton;
	const bool is_npc_route = route_plugin && route_plugin->_is_fps_npc_route();
	if (is_npc_route && route_plugin->npc_route_snap_to_navmesh) {
		const Transform3D path_transform = path->get_global_transform();
		Vector3 navmesh_position;
		if (!route_plugin->_snap_npc_route_point_to_navmesh(path_transform.xform(p_transform.origin), navmesh_position)) {
			route_plugin->_set_route_status(TTR("No nearby NavigationMesh; waypoint stayed in place"));
			return;
		}
		p_transform.origin = path_transform.affine_inverse().xform(navmesh_position);
	}
	if (curve->get_point_position(p_id).is_equal_approx(p_transform.origin)) {
		return;
	}
	if (is_npc_route) {
		// Run space queries and commit point movement from the editor's physics tick.
		// Rejected positions then leave the curve untouched, so the transform gizmo
		// does not repeatedly push against the same collision boundary.
		const int pending_index = route_plugin->pending_route_waypoint_ids.find(p_id);
		if (pending_index >= 0) {
			route_plugin->pending_route_waypoint_positions.write[pending_index] = p_transform.origin;
		} else {
			route_plugin->pending_route_waypoint_ids.push_back(p_id);
			route_plugin->pending_route_waypoint_positions.push_back(p_transform.origin);
		}
		route_plugin->pending_route_waypoint_drag = true;
		return;
	}
	curve->set_point_position(p_id, p_transform.origin);
}

void Path3DGizmoPlugin::commit_subgizmos(const EditorNode3DGizmo *p_gizmo, const Vector<int> &p_ids, const Vector<Transform3D> &p_restore, bool p_cancel) {
	Path3D *path = Object::cast_to<Path3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL(path);
	Ref<Curve3D> curve = path->get_curve();
	ERR_FAIL_COND(curve.is_null());
	if (Path3DEditorPlugin::singleton && Path3DEditorPlugin::singleton->path == path) {
		Path3DEditorPlugin::singleton->pending_route_waypoint_drag = false;
		Path3DEditorPlugin::singleton->pending_route_waypoint_ids.clear();
		Path3DEditorPlugin::singleton->pending_route_waypoint_positions.clear();
	}

	transformation_locked_basis.clear();

	if (p_cancel) {
		for (int i = 0; i < p_ids.size() && i < p_restore.size(); ++i) {
			const int idx = p_ids[i];
			if (idx >= 0 && idx < curve->get_point_count()) {
				curve->set_point_position(idx, p_restore[i].origin);
			}
		}
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();

	undo_redo->create_action(TTR("Set Curve Point Position"));

	for (int i = 0; i < p_ids.size() && i < p_restore.size(); ++i) {
		const int idx = p_ids[i];
		if (idx < 0 || idx >= curve->get_point_count()) {
			continue;
		}
		undo_redo->add_do_method(curve.ptr(), "set_point_position", idx, curve->get_point_position(idx));
		undo_redo->add_undo_method(curve.ptr(), "set_point_position", idx, p_restore[i].origin);
	}
	undo_redo->commit_action();
}

int Path3DGizmoPlugin::get_priority() const {
	return -1;
}

Path3DGizmoPlugin::Path3DGizmoPlugin() {
	Color path_color = SceneTree::get_singleton()->get_debug_paths_color();
	Color path_tilt_color = EDITOR_GET("editors/3d_gizmos/gizmo_colors/path_tilt");

	create_material("path_material", path_color);
	create_material("path_thin_material", Color(0.6, 0.6, 0.6));
	create_material("path_tilt_material", path_tilt_color);
	create_material("path_tilt_muted_material", path_tilt_color * 0.7);
	create_handle_material("handles", false, EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("EditorPathSmoothHandle"), EditorStringName(EditorIcons)));
	create_handle_material("first_pt_handle", false, EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("EditorPathSmoothHandle"), EditorStringName(EditorIcons)));
	create_handle_material("last_pt_handle", false, EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("EditorPathSmoothHandle"), EditorStringName(EditorIcons)));
	create_handle_material("closed_pt_handle", false, EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("EditorPathSmoothHandle"), EditorStringName(EditorIcons)));
	create_handle_material("selected_pt_handle", false, EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("EditorPathSmoothHandle"), EditorStringName(EditorIcons)));
	create_handle_material("sec_handles", false, EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("EditorCurveHandle"), EditorStringName(EditorIcons)));

	create_material("fps_route_corridor", Color(0.88, 0.12, 0.92, 0.36));
	Ref<StandardMaterial3D> corridor_material = get_material("fps_route_corridor");
	corridor_material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
	corridor_material->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
	corridor_material->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
	corridor_material->set_render_priority(StandardMaterial3D::RENDER_PRIORITY_MAX);

	const Pair<String, Color> route_materials[] = {
		Pair<String, Color>("fps_route_start", Color(0.46, 1.0, 0.16)),
		Pair<String, Color>("fps_route_move", Color(0.20, 0.88, 1.0)),
		Pair<String, Color>("fps_route_centerline", Color(0.88, 0.98, 1.0, 0.96)),
		Pair<String, Color>("fps_route_direction", Color(1.0, 0.94, 0.24, 0.98)),
		Pair<String, Color>("fps_route_wait", Color(1.0, 0.72, 0.12)),
		Pair<String, Color>("fps_route_look_at", Color(0.78, 0.42, 1.0)),
		Pair<String, Color>("fps_route_animation", Color(1.0, 0.55, 0.16)),
		Pair<String, Color>("fps_route_interact", Color(0.20, 1.0, 0.72)),
		Pair<String, Color>("fps_route_attack", Color(1.0, 0.24, 0.22)),
		Pair<String, Color>("fps_route_cover", Color(0.82, 0.94, 0.24)),
		Pair<String, Color>("fps_route_reachable", Color(0.18, 1.0, 0.28)),
		Pair<String, Color>("fps_route_blocked", Color(1.0, 0.18, 0.16)),
	};
	for (const Pair<String, Color> &route_material : route_materials) {
		create_material(route_material.first, route_material.second);
		const bool is_route_point_material = route_material.first == "fps_route_start" ||
				route_material.first == "fps_route_move" ||
				route_material.first == "fps_route_wait" ||
				route_material.first == "fps_route_look_at" ||
				route_material.first == "fps_route_animation" ||
				route_material.first == "fps_route_interact" ||
				route_material.first == "fps_route_attack" ||
				route_material.first == "fps_route_cover";
		for (Ref<StandardMaterial3D> material : materials[route_material.first]) {
			material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
			material->set_flag(StandardMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
			material->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
			material->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
			material->set_flag(StandardMaterial3D::FLAG_USE_POINT_SIZE, is_route_point_material);
			if (is_route_point_material) {
				material->set_point_size(route_material.first == "fps_route_start" ? 22.0 : 18.0);
			}
			material->set_render_priority(StandardMaterial3D::RENDER_PRIORITY_MAX);
		}
	}
}
