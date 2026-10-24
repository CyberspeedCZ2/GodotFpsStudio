/**************************************************************************/
/*  marker_3d_gizmo_plugin.cpp                                            */
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

#include "marker_3d_gizmo_plugin.h"

#include "core/templates/pair.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "scene/3d/marker_3d.h"

Marker3DGizmoPlugin::Marker3DGizmoPlugin() {
	Ref<Theme> editor_theme = EditorNode::get_singleton()->get_editor_theme();
	const Pair<String, StringName> fps_marker_icons[] = {
		Pair<String, StringName>("fps_player_start", SNAME("FPSPlayerStart")),
		Pair<String, StringName>("fps_checkpoint", SNAME("FPSCheckpoint")),
		Pair<String, StringName>("fps_npc_spawn", SNAME("FPSNPCSpawn")),
		Pair<String, StringName>("fps_npc_waypoint", SNAME("FPSNPCWaypoint")),
		Pair<String, StringName>("fps_npc_action_point", SNAME("FPSNPCActionPoint")),
		Pair<String, StringName>("fps_npc_cover_point", SNAME("FPSNPCCoverPoint")),
		Pair<String, StringName>("fps_objective_marker", SNAME("FPSObjectiveMarker")),
	};
	for (const Pair<String, StringName> &marker_icon : fps_marker_icons) {
		const String material_name = marker_icon.first;
		create_icon_material(material_name, editor_theme->get_icon(marker_icon.second, EditorStringName(EditorIcons)), true);
		for (Ref<StandardMaterial3D> material : materials[material_name]) {
			// FPS markers are level-authoring aids. Keep them readable in dense scenes,
			// including when their position is inside a placed model.
			material->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
			material->set_flag(StandardMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
			material->set_render_priority(StandardMaterial3D::RENDER_PRIORITY_MAX);
		}
	}

	pos3d_mesh.instantiate();

	Vector<Vector3> cursor_points;
	Vector<Color> cursor_colors;
	const float cs = 1.0;
	// Add more points to create a "hard stop" in the color gradient.
	cursor_points.push_back(Vector3(+cs, 0, 0));
	cursor_points.push_back(Vector3());
	cursor_points.push_back(Vector3());
	cursor_points.push_back(Vector3(-cs, 0, 0));

	cursor_points.push_back(Vector3(0, +cs, 0));
	cursor_points.push_back(Vector3());
	cursor_points.push_back(Vector3());
	cursor_points.push_back(Vector3(0, -cs, 0));

	cursor_points.push_back(Vector3(0, 0, +cs));
	cursor_points.push_back(Vector3());
	cursor_points.push_back(Vector3());
	cursor_points.push_back(Vector3(0, 0, -cs));

	// Use the axis color which is brighter for the positive axis.
	// Use a darkened axis color for the negative axis.
	// This makes it possible to see in which direction the Marker3D node is rotated
	// (which can be important depending on how it's used).
	const Color color_x = EditorNode::get_singleton()->get_editor_theme()->get_color(SNAME("axis_x_color"), EditorStringName(Editor));
	cursor_colors.push_back(color_x);
	cursor_colors.push_back(color_x);
	cursor_colors.push_back(color_x.darkened(0.5));
	cursor_colors.push_back(color_x.darkened(0.5));

	const Color color_y = EditorNode::get_singleton()->get_editor_theme()->get_color(SNAME("axis_y_color"), EditorStringName(Editor));
	cursor_colors.push_back(color_y);
	cursor_colors.push_back(color_y);
	cursor_colors.push_back(color_y.darkened(0.5));
	cursor_colors.push_back(color_y.darkened(0.5));

	const Color color_z = EditorNode::get_singleton()->get_editor_theme()->get_color(SNAME("axis_z_color"), EditorStringName(Editor));
	cursor_colors.push_back(color_z);
	cursor_colors.push_back(color_z);
	cursor_colors.push_back(color_z.darkened(0.5));
	cursor_colors.push_back(color_z.darkened(0.5));

	Ref<StandardMaterial3D> mat = memnew(StandardMaterial3D);
	mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
	mat->set_flag(StandardMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	mat->set_flag(StandardMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
	mat->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
	mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);

	Array d;
	d.resize(RSE::ARRAY_MAX);
	d[Mesh::ARRAY_VERTEX] = cursor_points;
	d[Mesh::ARRAY_COLOR] = cursor_colors;
	pos3d_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, d);
	pos3d_mesh->surface_set_material(0, mat);
}

bool Marker3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<Marker3D>(p_spatial) != nullptr;
}

String Marker3DGizmoPlugin::get_gizmo_name() const {
	return "Marker3D";
}

int Marker3DGizmoPlugin::get_priority() const {
	return -1;
}

void Marker3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	const Marker3D *marker = Object::cast_to<Marker3D>(p_gizmo->get_node_3d());
	const Node3D *node = p_gizmo->get_node_3d();
	String fps_icon_material;
	if (node->is_in_group("fps_player_start")) {
		fps_icon_material = "fps_player_start";
	} else if (node->is_in_group("fps_checkpoint")) {
		fps_icon_material = "fps_checkpoint";
	} else if (node->is_in_group("fps_npc_spawn")) {
		fps_icon_material = "fps_npc_spawn";
	} else if (node->is_in_group("fps_npc_waypoint")) {
		fps_icon_material = "fps_npc_waypoint";
	} else if (node->is_in_group("fps_npc_action_point")) {
		fps_icon_material = "fps_npc_action_point";
	} else if (node->is_in_group("fps_npc_cover_point")) {
		fps_icon_material = "fps_npc_cover_point";
	} else if (node->is_in_group("fps_objective_marker")) {
		fps_icon_material = "fps_objective_marker";
	}

	if (!fps_icon_material.is_empty()) {
		p_gizmo->clear();
		p_gizmo->add_unscaled_billboard(get_material(fps_icon_material, p_gizmo), 0.07);

		// Keep a compact hit target at the marker origin for viewport selection.
		const real_t hit_extent = 0.12;
		const Vector<Vector3> hit_segments = {
			Vector3(-hit_extent, 0, 0), Vector3(hit_extent, 0, 0),
			Vector3(0, -hit_extent, 0), Vector3(0, hit_extent, 0),
			Vector3(0, 0, -hit_extent), Vector3(0, 0, hit_extent),
		};
		p_gizmo->add_collision_segments(hit_segments);
		return;
	}

	const real_t extents = marker->get_gizmo_extents();
	const Transform3D xform(Basis::from_scale(Vector3(extents, extents, extents)));

	p_gizmo->clear();
	p_gizmo->add_mesh(pos3d_mesh, Ref<Material>(), xform);

	const Vector<Vector3> points = {
		Vector3(-extents, 0, 0),
		Vector3(+extents, 0, 0),
		Vector3(0, -extents, 0),
		Vector3(0, +extents, 0),
		Vector3(0, 0, -extents),
		Vector3(0, 0, +extents),
	};
	p_gizmo->add_collision_segments(points);
}
