/**************************************************************************/
/*  scene_import_settings.cpp                                             */
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


// FPS Studio Model View: material editor (create, save, preview on model, isolate).
// Part of SceneImportSettingsScreen (Model View); split out of
// scene_import_settings.cpp, which keeps the Godot import dialog itself.

#include "scene_import_settings.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/file_access.h"
#include "core/io/config_file.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_saver.h"
#include "core/math/projection.h"
#include "core/object/callable_mp.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/3d/skeleton_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/3d/bone_attachment_3d.h"
#include "scene/3d/label_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/check_button.h"
#include "scene/gui/button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/panel.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/separator.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/sky.h"
#include "scene/resources/shader.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/surface_tool.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"
#include "servers/text/text_server.h"
#include "scene_import_settings_internal.h"

void SceneImportSettingsScreen::_update_material_editor() {
	if (!material_inspector || !material_create_button || !material_save_button || !material_preview_on_model_toggle || !material_isolate_toggle) {
		return;
	}
	const bool has_material = selected_type == "Material" && material_map.has(selected_id);
	material_preview_on_model_toggle->set_visible(has_material);
	material_isolate_toggle->set_visible(has_material);
	material_isolate_toggle->set_pressed_no_signal(material_isolation_enabled);
	material_isolate_toggle->set_disabled(!material_preview_on_model);
	material_create_button->hide();
	material_inspector->edit(nullptr);
	material_inspector->hide();
	material_save_button->hide();

	if (!has_material) {
		return;
	}

	MaterialData &md = material_map[selected_id];
	const bool is_external = md.settings.has("use_external/enabled") && (bool)md.settings["use_external/enabled"];
	if (!is_external) {
		material_create_button->show();
		return;
	}

	material_save_button->set_disabled(!md.modified);
	material_save_button->show();
	material_inspector->edit(md.material.ptr());
	material_inspector->show();
}

void SceneImportSettingsScreen::_material_create_button_pressed() {
	if (selected_type == "Material" && material_map.has(selected_id)) {
		_menu_callback(ACTION_EXTRACT_MATERIALS);
	}
}

void SceneImportSettingsScreen::_material_save_button_pressed() {
	if (selected_type != "Material" || !material_map.has(selected_id)) {
		return;
	}
	MaterialData &md = material_map[selected_id];
	if (!md.modified) {
		return;
	}
	if (_save_modified_external_materials() != OK) {
		return;
	}
	material_save_button->set_disabled(true);
}

void SceneImportSettingsScreen::_material_preview_on_model_toggled(bool p_enabled) {
	material_preview_on_model = p_enabled;
	if (selected_type != "Material" || !material_map.has(selected_id)) {
		_apply_material_isolation();
		return;
	}
	if (p_enabled) {
		if (Object::cast_to<Node3D>(scene)) {
			Object::cast_to<Node3D>(scene)->show();
		}
		mesh_preview->hide();
	} else {
		if (Object::cast_to<Node3D>(scene)) {
			Object::cast_to<Node3D>(scene)->hide();
		}
		material_preview->set_material(material_map[selected_id].material);
		mesh_preview->set_mesh(material_preview);
		_clear_mesh_preview_material_overrides();
		mesh_preview->show();
	}
	_update_material_editor();
	_apply_material_isolation();
	_update_camera();
}

void SceneImportSettingsScreen::_material_isolate_toggled(bool p_enabled) {
	material_isolation_enabled = p_enabled;
	_apply_material_isolation();
}

void SceneImportSettingsScreen::_clear_mesh_preview_material_overrides() {
	for (int surface = 0; surface < mesh_preview->get_surface_override_material_count(); surface++) {
		mesh_preview->set_surface_override_material(surface, Ref<Material>());
	}
}

void SceneImportSettingsScreen::_apply_import_materials_to_mesh_preview(const Ref<Mesh> &p_mesh) {
	_clear_mesh_preview_material_overrides();
	if (p_mesh.is_null()) {
		return;
	}

	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		const Ref<Material> source_material = p_mesh->surface_get_material(surface);
		if (source_material.is_null()) {
			continue;
		}

		const String material_id = _get_material_import_id(source_material);
		if (!material_map.has(material_id)) {
			continue;
		}

		const Ref<Material> preview_material = material_map[material_id].material;
		if (preview_material.is_valid()) {
			mesh_preview->set_surface_override_material(surface, preview_material);
		}
	}
}

void SceneImportSettingsScreen::_apply_material_isolation() {
	const bool isolate = material_isolation_enabled && material_preview_on_model && selected_type == "Material" && material_map.has(selected_id);
	for (const MaterialSurfaceData &surface_data : material_surface_usages) {
		MeshInstance3D *mesh_node = surface_data.mesh_node;
		if (mesh_node == nullptr || !mesh_node->get_mesh().is_valid() || surface_data.surface_index < 0 || surface_data.surface_index >= mesh_node->get_mesh()->get_surface_count()) {
			continue;
		}

		Ref<Material> preview_material = surface_data.preview_override;
		if (isolate && surface_data.material_id != selected_id) {
			preview_material = material_isolation_material;
		}
		mesh_node->set_surface_override_material(surface_data.surface_index, preview_material);
	}
}

void SceneImportSettingsScreen::_material_inspector_property_edited(const String &) {
	if (selected_type != "Material" || !material_map.has(selected_id)) {
		return;
	}
	MaterialData &md = material_map[selected_id];
	md.modified = true;
	material_save_button->set_disabled(false);
}
