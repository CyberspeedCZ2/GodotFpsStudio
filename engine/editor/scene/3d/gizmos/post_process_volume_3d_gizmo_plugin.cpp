/**************************************************************************/
/*  post_process_volume_3d_gizmo_plugin.cpp                               */
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
/* without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to   */
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

#include "post_process_volume_3d_gizmo_plugin.h"

#include "editor/editor_node.h"
#include "editor/scene/3d/gizmos/gizmo_3d_helper.h"
#include "scene/3d/post_process_volume_3d.h"

PostProcessVolume3DGizmoPlugin::PostProcessVolume3DGizmoPlugin() {
	helper.instantiate();
	Color volume_color(0.18, 0.78, 1.0, 1.0);
	create_material("volume", volume_color);
	volume_color.a = 0.35;
	create_material("blend", volume_color);
	create_handle_material("handles");
}

bool PostProcessVolume3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<PostProcessVolume3D>(p_spatial) != nullptr;
}

String PostProcessVolume3DGizmoPlugin::get_gizmo_name() const {
	return "PostProcessVolume3D";
}

int PostProcessVolume3DGizmoPlugin::get_priority() const {
	return -1;
}

String PostProcessVolume3DGizmoPlugin::get_handle_name(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	return helper->box_get_handle_name(p_id);
}

Variant PostProcessVolume3DGizmoPlugin::get_handle_value(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	PostProcessVolume3D *volume = Object::cast_to<PostProcessVolume3D>(p_gizmo->get_node_3d());
	return volume->get_box_size();
}

void PostProcessVolume3DGizmoPlugin::begin_handle_action(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) {
	helper->initialize_handle_action(get_handle_value(p_gizmo, p_id, p_secondary), p_gizmo->get_node_3d()->get_global_transform());
}

void PostProcessVolume3DGizmoPlugin::set_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, Camera3D *p_camera, const Point2 &p_point) {
	PostProcessVolume3D *volume = Object::cast_to<PostProcessVolume3D>(p_gizmo->get_node_3d());
	Vector3 size = volume->get_box_size();
	Vector3 segment[2];
	helper->get_segment(p_camera, p_point, segment);
	Vector3 position;
	helper->box_set_handle(segment, p_id, size, position);
	volume->set_box_size(size);
	volume->set_global_position(position);
}

void PostProcessVolume3DGizmoPlugin::commit_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, const Variant &p_restore, bool p_cancel) {
	helper->box_commit_handle(TTR("Change PostProcessVolume Size"), p_cancel, p_gizmo->get_node_3d());
}

void PostProcessVolume3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	PostProcessVolume3D *volume = Object::cast_to<PostProcessVolume3D>(p_gizmo->get_node_3d());
	p_gizmo->clear();

	Vector<Vector3> volume_lines;
	AABB volume_aabb;
	volume_aabb.size = volume->get_box_size();
	volume_aabb.position = volume_aabb.size / -2.0;
	for (int i = 0; i < 12; i++) {
		Vector3 a;
		Vector3 b;
		volume_aabb.get_edge(i, a, b);
		volume_lines.push_back(a);
		volume_lines.push_back(b);
	}
	p_gizmo->add_lines(volume_lines, get_material("volume", p_gizmo));
	p_gizmo->add_collision_segments(volume_lines);
	p_gizmo->add_handles(helper->box_get_handles(volume->get_box_size()), get_material("handles", p_gizmo));

	if (volume->get_blend_radius() > 0.0f) {
		Vector3 blend_size = volume->get_box_size() + Vector3(volume->get_blend_radius() * 2.0f, volume->get_blend_radius() * 2.0f, volume->get_blend_radius() * 2.0f);
		AABB blend_aabb;
		blend_aabb.size = blend_size;
		blend_aabb.position = blend_size / -2.0;
		Vector<Vector3> blend_lines;
		for (int i = 0; i < 12; i++) {
			Vector3 a;
			Vector3 b;
			blend_aabb.get_edge(i, a, b);
			blend_lines.push_back(a);
			blend_lines.push_back(b);
		}
		p_gizmo->add_lines(blend_lines, get_material("blend", p_gizmo));
	}
}
