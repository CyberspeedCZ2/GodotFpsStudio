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

#include "scene_import_settings.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_saver.h"
#include "core/math/projection.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/fps_studio/fps_level_scene_template.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/3d/skeleton_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/check_button.h"
#include "scene/gui/button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/separator.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/surface_tool.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"
#include "servers/text/text_server.h"

// Camera framing margin applied around the fitted content so it never
// touches the viewport edges.
static constexpr real_t FRAME_MARGIN = 1.15;
static constexpr real_t PREVIEW_FOV_DEGREES = 75.0;
static constexpr real_t REFERENCE_VIEWPORT_WIDTH = 1920.0;
static constexpr real_t REFERENCE_ASPECT = 16.0 / 9.0;
static constexpr real_t MODEL_SETTINGS_PANEL_MIN_WIDTH = 320.0;
static constexpr real_t MODEL_SETTINGS_PANEL_DEFAULT_WIDTH = 400.0;
static const String MODEL_SETTINGS_PANEL_WIDTH_SETTING = "interface/fps_studio/model_view/inspector_width";
// Orthographic zoom limits (fraction of the fitted content size).
static constexpr real_t ZOOM_MIN = 0.05;
static constexpr real_t ZOOM_MAX = 20.0;

// Converts an index/vertex count to the number of rendered faces using the
// same rules as the renderer's _indices_to_primitives().
static uint64_t _mesh_faces_from_primitive(Mesh::PrimitiveType p_primitive, uint64_t p_index_count, uint64_t p_vertex_count) {
	uint64_t to_draw = p_index_count > 0 ? p_index_count : p_vertex_count;
	switch (p_primitive) {
		case Mesh::PRIMITIVE_TRIANGLES:
			return to_draw / 3;
		case Mesh::PRIMITIVE_TRIANGLE_STRIP:
			return to_draw >= 3 ? (to_draw - 2) : 0;
		default:
			return 0;
	}
}

// Compact number formatting for the mesh stats overlay (e.g. "12.3k", "4.5M").
static String _format_stats_count(uint64_t p_value) {
	if (p_value >= 1000000) {
		return String::num(p_value / 1000000.0, 2) + "M";
	} else if (p_value >= 10000) {
		return String::num(p_value / 1000.0, 1) + "k";
	}
	return itos(p_value);
}


class SceneImportSettingsData : public Object {
	GDCLASS(SceneImportSettingsData, Object)
	friend class SceneImportSettingsScreen;
	HashMap<StringName, Variant> *settings = nullptr;
	HashMap<StringName, Variant> current;
	HashMap<StringName, Variant> defaults;
	List<ResourceImporter::ImportOption> options;
	Vector<String> animation_list;

	bool hide_options = false;
	int section_filter = 4; // 1 mesh, 2 physics, 3 lighting, 4 all details.
	String path;

	ResourceImporterScene::InternalImportCategory category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MAX;

	bool _set(const StringName &p_name, const Variant &p_value) {
		if (settings) {
			if (defaults.has(p_name) && defaults[p_name] == p_value) {
				settings->erase(p_name);
			} else {
				(*settings)[p_name] = p_value;
			}

			current[p_name] = p_value;

			// SceneImportSettings must decide if a new collider should be generated or not.
			if (category == ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH_3D_NODE) {
				SceneImportSettingsScreen::get_singleton()->request_generate_collider();
			}

			Ref<ResourceImporterScene> resource_importer_scene = SceneImportSettingsScreen::get_singleton()->get_resource_importer_scene();
			if (category == ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MAX) {
				if (resource_importer_scene->get_option_visibility(path, p_name, current)) {
					SceneImportSettingsScreen::get_singleton()->update_view();
				}
			} else {
				if (resource_importer_scene->get_internal_option_update_view_required(category, p_name, current)) {
					SceneImportSettingsScreen::get_singleton()->update_view();
				}
			}

			return true;
		}
		return false;
	}

	bool _get(const StringName &p_name, Variant &r_ret) const {
		if (settings) {
			if (settings->has(p_name)) {
				r_ret = (*settings)[p_name];
				return true;
			}
		}
		if (defaults.has(p_name)) {
			r_ret = defaults[p_name];
			return true;
		}
		return false;
	}

	void handle_special_properties(PropertyInfo &r_option) const {
		ERR_FAIL_NULL(settings);
		if (r_option.name == "rest_pose/load_pose") {
			if (!settings->has("rest_pose/load_pose") || int((*settings)["rest_pose/load_pose"]) != 2) {
				if (settings->has("rest_pose/external_animation_library")) {
					(*settings)["rest_pose/external_animation_library"] = Variant();
				}
			}
		}
		if (r_option.name == "rest_pose/selected_animation") {
			if (!settings->has("rest_pose/load_pose")) {
				return;
			}
			String hint_string;

			switch (int((*settings)["rest_pose/load_pose"])) {
				case 1: {
					hint_string = String(",").join(animation_list);
					if (animation_list.size() == 1) {
						(*settings)["rest_pose/selected_animation"] = animation_list[0];
					}
				} break;
				case 2: {
					Object *res = nullptr;
					if (settings->has("rest_pose/external_animation_library")) {
						res = (*settings)["rest_pose/external_animation_library"];
					}
					Ref<Animation> anim(res);
					Ref<AnimationLibrary> library(res);
					if (anim.is_valid()) {
						hint_string = anim->get_name();
					}
					if (library.is_valid()) {
						LocalVector<StringName> anim_names;
						library->get_animation_list(&anim_names);
						if (anim_names.size() == 1) {
							(*settings)["rest_pose/selected_animation"] = String(anim_names[0]);
						}
						for (StringName anim_name : anim_names) {
							hint_string += "," + anim_name; // Include preceding, as a catch-all.
						}
					}
				} break;
				default:
					break;
			}
			r_option.hint = PROPERTY_HINT_ENUM;
			r_option.hint_string = hint_string;
		}
	}

	void _get_property_list(List<PropertyInfo> *r_list) const {
		if (hide_options) {
			return;
		}
		Ref<ResourceImporterScene> resource_importer_scene = SceneImportSettingsScreen::get_singleton()->get_resource_importer_scene();
		for (const ResourceImporter::ImportOption &E : options) {
			const String name = E.option.name;
			const bool lighting = name == "generate/shadow_meshes" || name == "generate/lightmap_uv" || name == "mesh_instance/cast_shadow";
			const bool physics = name == "generate/physics" || name.begins_with("physics/") || name.begins_with("decomposition/") || name.begins_with("primitive/");
			const bool mesh = category == ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH && !lighting && name != "generate/lods" && !name.begins_with("lods/");
			if ((section_filter == 1 && !mesh) || (section_filter == 2 && !physics) || (section_filter == 3 && !lighting)) {
				continue;
			}
			PropertyInfo option = E.option;
			if (category == ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MAX) {
				if (resource_importer_scene->get_option_visibility(path, E.option.name, current)) {
					handle_special_properties(option);
					r_list->push_back(option);
				}
			} else {
				if (resource_importer_scene->get_internal_option_visibility(category, E.option.name, current)) {
					handle_special_properties(option);
					r_list->push_back(option);
				}
			}
		}
	}
};

bool SceneImportSettingsScreen::_get_current(const StringName &p_name, Variant &r_ret) const {
	if (scene_import_settings_data->_get(p_name, r_ret)) {
		return true;
	}
	if (defaults.has(p_name)) {
		r_ret = defaults[p_name];
		return true;
	}
	return false;
}

void SceneImportSettingsScreen::_set_default(const StringName &p_name, const Variant &p_value) {
	defaults[p_name] = p_value;
	scene_import_settings_data->defaults[p_name] = p_value;
	scene_import_settings_data->_set(p_name, p_value);
}

String SceneImportSettingsScreen::_get_material_import_id(const Ref<Material> &p_material) {
	String import_id;

	if (p_material->has_meta("import_id")) {
		import_id = p_material->get_meta("import_id");
	} else if (!p_material->get_name().is_empty()) {
		import_id = p_material->get_name();
	} else if (unnamed_material_name_map.has(p_material)) {
		import_id = unnamed_material_name_map[p_material];
	} else {
		import_id = "@MATERIAL:" + itos(material_map.size());
		unnamed_material_name_map[p_material] = import_id;
	}
	return import_id;
}

static Ref<Material> load_external_material_for_preview(const HashMap<StringName, Variant> &p_settings) {
	if (!p_settings.has("use_external/enabled") || !(bool)p_settings["use_external/enabled"]) {
		return Ref<Material>();
	}

	Ref<Material> external_material;
	if (p_settings.has("use_external/path")) {
		const String path = p_settings["use_external/path"];
		external_material = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_REPLACE);
	}
	if (external_material.is_null() && p_settings.has("use_external/fallback_path")) {
		const String fallback_path = p_settings["use_external/fallback_path"];
		if (!fallback_path.is_empty()) {
			external_material = ResourceLoader::load(fallback_path, "", ResourceFormatLoader::CACHE_MODE_REPLACE);
		}
	}
	return external_material;
}

void SceneImportSettingsScreen::_fill_material(Tree *p_tree, const Ref<Material> &p_material, TreeItem *p_parent) {
	const String import_id = _get_material_import_id(p_material);
	const bool has_import_id = p_material->has_meta("import_id") || !p_material->get_name().is_empty();

	bool created = false;
	if (!material_map.has(import_id)) {
		MaterialData md;
		created = true;
		md.has_import_id = has_import_id;
		md.source_material = p_material;
		md.material = p_material;

		_load_default_subresource_settings(md.settings, "materials", import_id, ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MATERIAL);
		Ref<Material> external_material = load_external_material_for_preview(md.settings);
		if (external_material.is_valid()) {
			md.material = external_material;
		}

		material_map[import_id] = md;
	}

	MaterialData &material_data = material_map[import_id];
	ERR_FAIL_COND(p_material != material_data.source_material);

	Variant value;
	if (_get_current("materials/extract", value) && (int)value != 0) {
		String spath = base_path.get_base_dir();
		if (_get_current("materials/extract_path", value)) {
			String extpath = value;
			if (!extpath.is_empty()) {
				spath = extpath;
			}
		}

		String ext = ResourceImporterScene::material_extension[_get_current("materials/extract_format", value) ? (int)value : 0];
		String path = spath.path_join(import_id.validate_filename() + ext);
		String uid_path = ResourceUID::path_to_uid(path);
		material_data.settings["use_external/enabled"] = true;
		material_data.settings["use_external/path"] = uid_path;
		material_data.settings["use_external/fallback_path"] = path;
	}

	Ref<Texture2D> icon = get_editor_theme_icon(SNAME("StandardMaterial3D"));

	TreeItem *item = p_tree->create_item(p_parent);
	if (p_material->get_name().is_empty()) {
		item->set_text(0, TTR("<Unnamed Material>"));
	} else {
		item->set_text(0, p_material->get_name());
	}
	item->set_icon(0, icon);

	item->set_meta("type", "Material");
	item->set_meta("import_id", import_id);
	item->set_tooltip_text(0, vformat(TTR("Import ID: %s"), import_id));
	item->set_selectable(0, true);

	if (p_tree == scene_tree) {
		material_data.scene_node = item;
	} else if (p_tree == mesh_tree) {
		material_data.mesh_node = item;
	} else {
		material_data.material_node = item;
	}

	if (created) {
		_fill_material(material_tree, p_material, material_tree->get_root());
	}
}

void SceneImportSettingsScreen::_fill_mesh(Tree *p_tree, const Ref<Mesh> &p_mesh, TreeItem *p_parent) {
	String import_id;

	bool has_import_id = false;
	if (p_mesh->has_meta("import_id")) {
		import_id = p_mesh->get_meta("import_id");
		has_import_id = true;
	} else if (!p_mesh->get_name().is_empty()) {
		import_id = p_mesh->get_name();
		has_import_id = true;
	} else {
		import_id = "@MESH:" + itos(mesh_set.size());
	}

	if (!mesh_map.has(import_id)) {
		MeshData md;
		md.has_import_id = has_import_id;
		md.mesh = p_mesh;

		_load_default_subresource_settings(md.settings, "meshes", import_id, ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH);
		if (lod_source_meshes.has(import_id)) {
			md.lod_source_mesh = lod_source_meshes[import_id];
		}
		if (lod_source_skin_pose_transforms.has(import_id)) {
			md.lod_skin_pose_transforms = lod_source_skin_pose_transforms[import_id];
		}
		md.lod_pending = lod_config_pending_meshes.has(import_id);
		lod_settings_pending = lod_settings_pending || md.lod_pending;

		mesh_map[import_id] = md;
	}

	MeshData &mesh_data = mesh_map[import_id];

	Ref<Texture2D> icon = get_editor_theme_icon(SNAME("MeshItem"));

	TreeItem *item = p_tree->create_item(p_parent);
	item->set_text(0, p_mesh->get_name());
	item->set_icon(0, icon);

	bool created = false;
	if (!mesh_set.has(p_mesh)) {
		mesh_set.insert(p_mesh);
		created = true;
	}

	item->set_meta("type", "Mesh");
	item->set_meta("import_id", import_id);
	item->set_tooltip_text(0, vformat(TTR("Import ID: %s"), import_id));

	item->set_selectable(0, true);

	if (p_tree == scene_tree) {
		mesh_data.scene_node = item;
	} else {
		mesh_data.mesh_node = item;
	}

	item->set_collapsed(true);

	for (int i = 0; i < p_mesh->get_surface_count(); i++) {
		Ref<Material> mat = p_mesh->surface_get_material(i);
		if (mat.is_valid()) {
			_fill_material(p_tree, mat, item);
		}
	}

	if (created) {
		_fill_mesh(mesh_tree, p_mesh, mesh_tree->get_root());
	}
}

void SceneImportSettingsScreen::_fill_animation(Tree *p_tree, const Ref<Animation> &p_anim, const String &p_name, TreeItem *p_parent) {
	if (!animation_map.has(p_name)) {
		AnimationData ad;
		ad.animation = p_anim;

		_load_default_subresource_settings(ad.settings, "animations", p_name, ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_ANIMATION);
		Animation::LoopMode loop_mode = p_anim->get_loop_mode();
		if (!ad.settings.has("settings/loop_mode") && loop_mode != Animation::LoopMode::LOOP_NONE) {
			// Update the loop mode to match detected mode (from import hints).
			// This is necessary on the first import of a scene, otherwise the
			// default (0/NONE) is set when filling out defaults.
			ad.settings["settings/loop_mode"] = loop_mode;
		}

		animation_map[p_name] = ad;
	}

	AnimationData &animation_data = animation_map[p_name];

	Ref<Texture2D> icon = get_editor_theme_icon(SNAME("Animation"));

	TreeItem *item = p_tree->create_item(p_parent);
	item->set_text(0, p_name);
	item->set_icon(0, icon);

	item->set_meta("type", "Animation");
	item->set_meta("import_id", p_name);

	item->set_selectable(0, true);

	animation_data.scene_node = item;
}

void SceneImportSettingsScreen::_fill_scene(Node *p_node, TreeItem *p_parent_item) {
	String import_id;

	if (p_node->has_meta("import_id")) {
		import_id = p_node->get_meta("import_id");
	} else {
		import_id = "PATH:" + String(scene->get_path_to(p_node));
		p_node->set_meta("import_id", import_id);
	}

	ImporterMeshInstance3D *src_mesh_node = Object::cast_to<ImporterMeshInstance3D>(p_node);

	if (src_mesh_node) {
		MeshInstance3D *mesh_node = memnew(MeshInstance3D);
		mesh_node->set_name(src_mesh_node->get_name());
		mesh_node->set_transform(src_mesh_node->get_transform());
		mesh_node->set_skin(src_mesh_node->get_skin());
		mesh_node->set_skeleton_path(src_mesh_node->get_skeleton_path());
		mesh_node->set_visible(src_mesh_node->is_visible());
		if (src_mesh_node->get_mesh().is_valid()) {
			Ref<ImporterMesh> editor_mesh = src_mesh_node->get_mesh();
			mesh_node->set_mesh(editor_mesh->get_mesh());
		}
		// Replace the original mesh node in the scene tree with the new one.
		if (unlikely(p_node == scene)) {
			scene = mesh_node;
		}
		p_node->replace_by(mesh_node);
		memdelete(p_node);
		p_node = mesh_node;
	}

	String type = p_node->get_class();

	if (!has_theme_icon(type, EditorStringName(EditorIcons))) {
		type = "Node3D";
	}

	Ref<Texture2D> icon = get_editor_theme_icon(type);

	TreeItem *item = scene_tree->create_item(p_parent_item);
	item->set_text(0, p_node->get_name());

	if (p_node == scene) {
		icon = get_editor_theme_icon(SNAME("PackedScene"));
		item->set_text(0, TTR("Scene"));
	}

	item->set_icon(0, icon);

	item->set_meta("type", "Node");
	item->set_meta("class", type);
	item->set_meta("import_id", import_id);
	item->set_tooltip_text(0, vformat(TTR("Type: %s\nImport ID: %s"), type, import_id));

	item->set_selectable(0, true);

	if (!node_map.has(import_id)) {
		NodeData nd;

		if (p_node != scene) {
			ResourceImporterScene::InternalImportCategory category;
			if (src_mesh_node) {
				category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH_3D_NODE;
			} else if (Object::cast_to<AnimationPlayer>(p_node)) {
				category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_ANIMATION_NODE;

				animation_player = Object::cast_to<AnimationPlayer>(p_node);
				animation_player->connect(SceneStringName(animation_finished), callable_mp(this, &SceneImportSettingsScreen::_animation_finished));
			} else if (Object::cast_to<Skeleton3D>(p_node)) {
				category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_SKELETON_3D_NODE;
				Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node);
				skeleton->connect(SceneStringName(tree_entered), callable_mp(this, &SceneImportSettingsScreen::_skeleton_tree_entered).bind(skeleton));
				skeletons.push_back(skeleton);
			} else {
				category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_NODE;
			}

			_load_default_subresource_settings(nd.settings, "nodes", import_id, category);
		}

		node_map[import_id] = nd;
	}
	NodeData &node_data = node_map[import_id];

	node_data.node = p_node;
	node_data.scene_node = item;

	AnimationPlayer *anim_node = Object::cast_to<AnimationPlayer>(p_node);
	if (anim_node) {
		Vector<String> animation_list;
		for (const StringName &E : anim_node->get_sorted_animation_list()) {
			_fill_animation(scene_tree, anim_node->get_animation(E), E, item);
			animation_list.append(E);
		}
		if (scene_import_settings_data != nullptr) {
			scene_import_settings_data->animation_list = animation_list;
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_fill_scene(p_node->get_child(i), item);
	}
	Transform3D accum_xform;
	Node3D *base = Object::cast_to<Node3D>(p_node);
	while (base) {
		accum_xform = base->get_transform() * accum_xform;
		base = Object::cast_to<Node3D>(base->get_parent());
	}
	MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(p_node);
	if (mesh_node && mesh_node->get_mesh().is_valid()) {
		// This controls the display of mesh resources in the import settings dialog tree (the white mesh icon).
		// We want to show these icons for any import type that preserves meshes.
		if (_resource_importer_scene->get_scene_import_type() != "AnimationLibrary") {
			_fill_mesh(scene_tree, mesh_node->get_mesh(), item);
		}
		// pre_import() reads the source model without running the final importer
		// post-fix pass, so external materials from the .import settings would
		// otherwise be missing from Model View after opening or reimporting it.
		const Ref<Mesh> preview_mesh = mesh_node->get_mesh();
		for (int surface = 0; surface < preview_mesh->get_surface_count(); surface++) {
			Ref<Material> source_material = preview_mesh->surface_get_material(surface);
			String material_id;
			if (source_material.is_valid()) {
				material_id = _get_material_import_id(source_material);
				if (material_map.has(material_id)) {
					MaterialData &material_data = material_map[material_id];
					const Ref<Material> preview_material = material_data.material;
					if (preview_material.is_valid() && preview_material != source_material) {
						mesh_node->set_surface_override_material(surface, preview_material);
					}

					material_data.usage_count++;
					if (material_data.material_node != nullptr) {
						String display_name = source_material->get_name();
						if (display_name.is_empty()) {
							display_name = TTR("<Unnamed Material>");
						}
						material_data.material_node->set_text(0, vformat(TTR("%s (%d surfaces)"), display_name, material_data.usage_count));
						material_data.material_node->set_tooltip_text(0, vformat(TTR("Used on %d mesh surfaces.\nImport ID: %s"), material_data.usage_count, material_id));
					}
				}
			}

			MaterialSurfaceData surface_data;
			surface_data.mesh_node = mesh_node;
			surface_data.surface_index = surface;
			surface_data.material_id = material_id;
			surface_data.preview_override = mesh_node->get_surface_override_material(surface);
			material_surface_usages.push_back(surface_data);
		}

		// Add the collider view.
		MeshInstance3D *collider_view = memnew(MeshInstance3D);
		collider_view->set_name("collider_view");
		collider_view->set_visible(false);
		mesh_node->add_child(collider_view, true);
		collider_view->set_owner(mesh_node);

		AABB aabb = accum_xform.xform(mesh_node->get_mesh()->get_aabb());

		if (first_aabb) {
			contents_aabb = aabb;
			first_aabb = false;
		} else {
			contents_aabb.merge_with(aabb);
		}
	}
	MultiMeshInstance3D *multi_mesh_node = Object::cast_to<MultiMeshInstance3D>(p_node);
	if (multi_mesh_node && multi_mesh_node->get_multimesh().is_valid()) {
		const Ref<MultiMesh> multi_mesh = multi_mesh_node->get_multimesh();
		const Ref<Mesh> mm_mesh = multi_mesh->get_mesh();
		if (mm_mesh.is_valid()) {
			_fill_mesh(scene_tree, mm_mesh, item);
		}
		const AABB aabb = accum_xform.xform(multi_mesh->get_aabb());
		if (first_aabb) {
			contents_aabb = aabb;
			first_aabb = false;
		} else {
			contents_aabb.merge_with(aabb);
		}
	}

	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node);
	if (skeleton) {
		// Use the regular skeleton gizmo colors instead of the bright selection
		// highlight (which rendered all bones in glowing yellow), and let the
		// persistent toggle in the viewport decide the preview visibility.
		Ref<ArrayMesh> bones_mesh = Skeleton3DGizmoPlugin::get_bones_mesh(skeleton, -1, false);

		bones_mesh_preview->set_mesh(bones_mesh);
		bones_mesh_preview->set_transform(accum_xform * skeleton->get_transform());
		bones_mesh_preview->set_visible(skeleton_visibility_toggle->is_pressed());

		// Keep the skeleton gizmo extents out of the camera framing. Debug bone
		// lines (octahedrons, RGB joint axes) can span far beyond the actual
		// model and made it look tiny in the preview. They are only used as a
		// fallback when the file has no visible mesh content (animation-only
		// imports such as Mixamo clips).
		AABB aabb = accum_xform.xform(bones_mesh->get_aabb());

		if (has_skeleton_aabb) {
			skeleton_aabb.merge_with(aabb);
		} else {
			skeleton_aabb = aabb;
			has_skeleton_aabb = true;
		}
	}
}

void SceneImportSettingsScreen::_update_scene() {
	scene_tree->clear();
	material_tree->clear();
	mesh_tree->clear();

	// Hidden roots.
	material_tree->create_item();
	mesh_tree->create_item();

	_fill_scene(scene, nullptr);
}

void SceneImportSettingsScreen::_update_view_gizmos() {
	if (!is_visible()) {
		return;
	}
	const HashMap<StringName, Variant> &main_settings = scene_import_settings_data->current;
	bool reshow_settings = false;
	if (main_settings.has("nodes/import_as_skeleton_bones")) {
		bool new_import_as_skeleton = main_settings["nodes/import_as_skeleton_bones"];
		reshow_settings = reshow_settings || (new_import_as_skeleton != previous_import_as_skeleton);
		previous_import_as_skeleton = new_import_as_skeleton;
	}
	if (main_settings.has("animation/import_rest_as_RESET")) {
		bool new_rest_as_reset = main_settings["animation/import_rest_as_RESET"];
		reshow_settings = reshow_settings || (new_rest_as_reset != previous_rest_as_reset);
		previous_rest_as_reset = new_rest_as_reset;
	}
	if (reshow_settings) {
		if (_re_import() != OK) {
			return;
		}
		open_settings(base_path);
		return;
	}
	for (const KeyValue<String, NodeData> &e : node_map) {
		// Skip import nodes that aren't MeshInstance3D.
		const MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(e.value.node);
		if (mesh_node == nullptr || mesh_node->get_mesh().is_null()) {
			continue;
		}

		// Determine if the mesh collider should be visible.
		bool show_collider_view = false;
		if (e.value.settings.has(SNAME("generate/physics"))) {
			show_collider_view = e.value.settings[SNAME("generate/physics")];
		}

		// Get the collider_view MeshInstance3D.
		TypedArray<Node> descendants = mesh_node->find_children("collider_view", "MeshInstance3D", false);
		CRASH_COND_MSG(descendants.is_empty(), "This is unreachable, since the collider view is always created even when the collision is not used! If this is triggered there is a bug on the function `_fill_scene`.");
		MeshInstance3D *collider_view = Object::cast_to<MeshInstance3D>(descendants[0].operator Object *());

		// Regenerate the physics collider for this MeshInstance3D if either:
		// - A regeneration is requested for the selected import node.
		// - The collider is being made visible.
		if ((generate_collider && e.key == selected_id) || (show_collider_view && !collider_view->is_visible())) {
			// This collider_view doesn't have a mesh so we need to generate a new one.
			Ref<ImporterMesh> mesh;
			mesh.instantiate();
			// ResourceImporterScene::get_collision_shapes() expects ImporterMesh, not Mesh.
			// TODO: Duplicate code with EditorSceneFormatImporterESCN::import_scene()
			// Consider making a utility function to convert from Mesh to ImporterMesh.
			Ref<Mesh> mesh_3d_mesh = mesh_node->get_mesh();
			Ref<ArrayMesh> array_mesh_3d_mesh = mesh_3d_mesh;
			if (array_mesh_3d_mesh.is_valid()) {
				// For the MeshInstance3D nodes, we need to convert the ArrayMesh to an ImporterMesh specially.
				mesh->set_name(array_mesh_3d_mesh->get_name());
				for (int32_t blend_i = 0; blend_i < array_mesh_3d_mesh->get_blend_shape_count(); blend_i++) {
					mesh->add_blend_shape(array_mesh_3d_mesh->get_blend_shape_name(blend_i));
				}
				for (int32_t surface_i = 0; surface_i < array_mesh_3d_mesh->get_surface_count(); surface_i++) {
					mesh->add_surface(array_mesh_3d_mesh->surface_get_primitive_type(surface_i),
							array_mesh_3d_mesh->surface_get_arrays(surface_i),
							array_mesh_3d_mesh->surface_get_blend_shape_arrays(surface_i),
							array_mesh_3d_mesh->surface_get_lods(surface_i),
							array_mesh_3d_mesh->surface_get_material(surface_i),
							array_mesh_3d_mesh->surface_get_name(surface_i),
							array_mesh_3d_mesh->surface_get_format(surface_i));
				}
				mesh->set_blend_shape_mode(array_mesh_3d_mesh->get_blend_shape_mode());
			} else if (mesh_3d_mesh.is_valid()) {
				// For the MeshInstance3D nodes, we need to convert the Mesh to an ImporterMesh specially.
				mesh->set_name(mesh_3d_mesh->get_name());
				for (int32_t surface_i = 0; surface_i < mesh_3d_mesh->get_surface_count(); surface_i++) {
					mesh->add_surface(mesh_3d_mesh->surface_get_primitive_type(surface_i),
							mesh_3d_mesh->surface_get_arrays(surface_i),
							Array(),
							mesh_3d_mesh->surface_get_lods(surface_i),
							mesh_3d_mesh->surface_get_material(surface_i),
							mesh_3d_mesh->surface_get_material(surface_i).is_valid() ? mesh_3d_mesh->surface_get_material(surface_i)->get_name() : String(),
							mesh_3d_mesh->surface_get_format(surface_i));
				}
			}

			// Generate the mesh collider.
			Vector<Ref<Shape3D>> shapes = ResourceImporterScene::get_collision_shapes(mesh, e.value.settings, 1.0);
			const Transform3D transform = ResourceImporterScene::get_collision_shapes_transform(e.value.settings);

			Ref<ArrayMesh> collider_view_mesh;
			collider_view_mesh.instantiate();
			for (Ref<Shape3D> shape : shapes) {
				Ref<ArrayMesh> debug_shape_mesh;
				if (shape.is_valid()) {
					debug_shape_mesh = shape->get_debug_mesh();
				}
				if (debug_shape_mesh.is_valid()) {
					collider_view_mesh->add_surface_from_arrays(
							debug_shape_mesh->surface_get_primitive_type(0),
							debug_shape_mesh->surface_get_arrays(0));

					collider_view_mesh->surface_set_material(
							collider_view_mesh->get_surface_count() - 1,
							collider_mat);
				}
			}

			collider_view->set_mesh(collider_view_mesh);
			collider_view->set_transform(transform);
		}

		// Set the collider visibility.
		collider_view->set_visible(show_collider_view);
	}

	generate_collider = false;
}

void SceneImportSettingsScreen::_update_camera() {
	if (!camera || !base_viewport || !mesh_preview) {
		return;
	}
	AABB camera_aabb;

	float rot_x = cam_rot_x;
	float rot_y = cam_rot_y;
	float zoom = cam_zoom;
	Vector2 pan;
	if (selected_type == "Node" || selected_type == "Animation" || selected_type.is_empty() || (selected_type == "Material" && material_preview_on_model)) {
		camera_aabb = contents_aabb;
		pan = cam_pan;
		if (selected_type == "Material" && material_map.has(selected_id)) {
			const MaterialData &md = material_map[selected_id];
			rot_x = md.cam_rot_x;
			rot_y = md.cam_rot_y;
			zoom = md.cam_zoom;
			pan = md.cam_pan;
		}
	} else {
		if (mesh_preview->get_mesh().is_valid()) {
			camera_aabb = mesh_preview->get_transform().xform(mesh_preview->get_mesh()->get_aabb());
		} else {
			camera_aabb = AABB(Vector3(-1, -1, -1), Vector3(2, 2, 2));
		}
		if (selected_type == "Mesh" && mesh_map.has(selected_id)) {
			const MeshData &md = mesh_map[selected_id];
			rot_x = md.cam_rot_x;
			rot_y = md.cam_rot_y;
			zoom = md.cam_zoom;
			pan = md.cam_pan;
		} else if (selected_type == "Material" && material_map.has(selected_id)) {
			const MaterialData &md = material_map[selected_id];
			rot_x = md.cam_rot_x;
			rot_y = md.cam_rot_y;
			zoom = md.cam_zoom;
			pan = md.cam_pan;
		}
	}

	// The workspace can resize before a model is loaded. Never pass zero or
	// non-finite dimensions to the renderer's orthographic projection.
	if (!camera_aabb.is_finite() || camera_aabb.get_longest_axis_size() > 1e12) {
		camera_aabb = AABB(Vector3(-1, -1, -1), Vector3(2, 2, 2));
	}
	zoom = Math::is_finite(zoom) ? CLAMP(zoom, (float)ZOOM_MIN, (float)ZOOM_MAX) : 1.0f;
	rot_x = Math::is_finite(rot_x) ? rot_x : 0.0f;
	rot_y = Math::is_finite(rot_y) ? rot_y : 0.0f;
	if (!pan.is_finite()) {
		pan = Vector2();
	}
	const Vector3 center = camera_aabb.get_center();
	const Basis camera_basis = Basis(Vector3(0, 1, 0), rot_y) * Basis(Vector3(1, 0, 0), rot_x);
	const Size2 viewport_size = base_viewport->get_size();
	const float aspect = viewport_size.y > 0 ? MAX(viewport_size.x / viewport_size.y, 0.01f) : 1.0f;
	const real_t half_fov_tangent = Math::tan(Math::deg_to_rad(PREVIEW_FOV_DEGREES * 0.5));
	const float extent = MAX(camera_aabb.get_longest_axis_size(), 0.1f);
	// Fit a sphere around the bounds instead of re-projecting the AABB at
	// every orbit angle. The projected AABB width changes as the camera turns,
	// which otherwise changes the camera distance and makes the model appear
	// to zoom in and out during rotation.
	const real_t half_horizontal_fov_tangent = half_fov_tangent * aspect;
	const real_t narrow_half_fov = Math::atan(MIN(half_fov_tangent, half_horizontal_fov_tangent));
	const real_t bounding_radius = MAX(camera_aabb.size.length() * 0.5, (real_t)0.05);
	real_t fit_distance = bounding_radius / MAX(Math::sin(narrow_half_fov), (real_t)0.01);
	fit_distance = MAX(fit_distance * FRAME_MARGIN, (real_t)0.1);
	const real_t camera_distance = fit_distance * zoom;
	// Pan uses the visible height at the target plane, so dragging continues
	// to move the model by the same number of pixels at any zoom level.
	frame_size = (float)(2.0 * fit_distance * half_fov_tangent / FRAME_MARGIN);
	_update_preview_floor(camera_aabb);
	const real_t far_clip = MAX(camera_distance + extent * 4.0, camera_distance + preview_floor_size);
	camera->set_perspective(PREVIEW_FOV_DEGREES, 0.001, MAX(far_clip, (real_t)1.0));
	Transform3D xf;
	xf.basis = camera_basis;
	xf.origin = center;
	xf.translate_local(0, 0, camera_distance);
	xf.translate_local(pan.x, pan.y, 0);
	camera->set_transform(xf);
	_update_preview_lod_threshold();

	_update_mesh_stats();
}

void SceneImportSettingsScreen::_update_preview_floor(const AABB &p_bounds) {
	if (preview_floor == nullptr || preview_floor_mesh.is_null() || preview_floor_material.is_null() || scene == nullptr || !p_bounds.is_finite()) {
		if (preview_floor != nullptr) {
			preview_floor->hide();
		}
		preview_floor_size = 40.0;
		return;
	}

	const Vector3 center = p_bounds.get_center();
	const real_t extent = MAX(p_bounds.get_longest_axis_size(), (real_t)0.5);
	// Extend the simple plane well beyond the visible horizon. Depth fog fades
	// it into the sky before its finite edge can appear in the viewport.
	preview_floor_size = CLAMP(extent * 10.0, 1600.0, 4000.0);
	preview_floor_mesh->set_size(Size2(preview_floor_size, preview_floor_size));
	const real_t grid_step = extent < 8.0 ? 0.5 : (extent < 40.0 ? 1.0 : (extent < 160.0 ? 5.0 : 10.0));
	const real_t floor_y = p_bounds.position.y - MAX(extent * 0.001, (real_t)0.002);
	preview_floor->set_position(Vector3(center.x, floor_y, center.z));
	preview_floor_material->set_shader_parameter("grid_origin", Vector2(center.x, center.z));
	preview_floor_material->set_shader_parameter("grid_step", grid_step);
	preview_floor_material->set_shader_parameter("fade_distance", CLAMP(extent * 18.0, 45.0, 180.0));
	preview_floor->show();

	// Keep the fixed sun's shadow around the subject without clipping it on
	// large imported models.
	if (light1 != nullptr) {
		light1->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, CLAMP(extent * 2.0, 20.0, 250.0));
	}
}

void SceneImportSettingsScreen::_update_preview_lod_threshold() {
	if (!camera || !base_viewport) {
		return;
	}
	const real_t project_threshold = GLOBAL_GET("rendering/mesh_lod/lod_change/threshold_pixels");
	const Size2 size = base_viewport->get_size();
	if (size.x <= 0 || size.y <= 0 || !camera->is_inside_tree()) {
		base_viewport->set_mesh_lod_threshold(project_threshold * lod_bias);
		return;
	}
	const Projection reference = Projection::create_perspective(PREVIEW_FOV_DEGREES, REFERENCE_ASPECT, 0.05, 10000.0);
	const real_t actual_multiplier = camera->get_camera_projection().get_lod_multiplier();
	if (!Math::is_finite(actual_multiplier) || actual_multiplier <= 0.0) {
		base_viewport->set_mesh_lod_threshold(project_threshold * lod_bias);
		return;
	}
	// Match the reference camera even if this editor panel has a different
	// resolution or aspect ratio; this modifies only the preview viewport.
	const real_t threshold = project_threshold * lod_bias * size.x * reference.get_lod_multiplier() / (REFERENCE_VIEWPORT_WIDTH * actual_multiplier);
	base_viewport->set_mesh_lod_threshold(threshold);
}

Ref<ImporterMesh> SceneImportSettingsScreen::_duplicate_importer_mesh(const Ref<ImporterMesh> &p_source) const {
	if (p_source.is_null()) {
		return Ref<ImporterMesh>();
	}
	Ref<ImporterMesh> copy;
	copy.instantiate();
	copy->set_name(p_source->get_name());
	copy->set_blend_shape_mode(p_source->get_blend_shape_mode());
	for (int i = 0; i < p_source->get_blend_shape_count(); i++) {
		copy->add_blend_shape(p_source->get_blend_shape_name(i));
	}
	for (int i = 0; i < p_source->get_surface_count(); i++) {
	TypedArray<Array> blend_shape_arrays;
		for (int blend_shape = 0; blend_shape < p_source->get_blend_shape_count(); blend_shape++) {
			blend_shape_arrays.push_back(p_source->get_surface_blend_shape_arrays(i, blend_shape));
		}
		copy->add_surface(p_source->get_surface_primitive_type(i), p_source->get_surface_arrays(i), blend_shape_arrays, Dictionary(), p_source->get_surface_material(i), p_source->get_surface_name(i), p_source->get_surface_format(i));
	}
	return copy;
}

// Generates LOD levels for the preview meshes so the statistics overlay can
// report the LOD level the preview camera selects, mirroring the generation
// performed by ResourceImporterScene::_generate_meshes() on reimport. Runs on
// the pre-import scene (ImporterMeshInstance3D nodes, before conversion to
// MeshInstance3D), before _fill_scene() converts the meshes for the preview.
void SceneImportSettingsScreen::_generate_preview_lods(Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	ImporterMeshInstance3D *src_mesh_node = Object::cast_to<ImporterMeshInstance3D>(p_node);
	if (src_mesh_node) {
		Ref<ImporterMesh> importer_mesh = src_mesh_node->get_mesh();
		if (importer_mesh.is_valid() && importer_mesh->get_surface_count() > 0) {
			String mesh_id = importer_mesh->get_meta("import_id", importer_mesh->get_name());
			if (!mesh_id.is_empty() && !lod_source_meshes.has(mesh_id)) {
				lod_source_meshes[mesh_id] = _duplicate_importer_mesh(importer_mesh);
			}
			// Start from the main "Generate LODs" import setting; per-mesh
			// overrides ("generate/lods") stored in .import subresources may
			// force LOD generation on or off for this mesh, same as on
			// reimport (see ResourceImporterScene::_generate_meshes()).
			// "meshes/generate_lods" defaults to true on reimport when the
			// option is absent from the saved .import parameters.
			bool generate_lods = defaults.has("meshes/generate_lods") ? bool(defaults["meshes/generate_lods"]) : true;
			float merge_angle = 20.0f;
			// Fixed LOD chain configuration (0 = legacy automatic halving).
			int lod_count = defaults.has("meshes/lod_count") ? (int)defaults["meshes/lod_count"] : 0;
			float lod_reduction = defaults.has("meshes/lod_reduction") ? (float)defaults["meshes/lod_reduction"] : 0.5f;
			bool custom_lod_levels = false;
			Array lod_levels;

			if (!mesh_id.is_empty() && base_subresource_settings.has("meshes")) {
				Dictionary meshes_dict = base_subresource_settings["meshes"];
				if (meshes_dict.has(mesh_id)) {
					Dictionary mesh_settings = meshes_dict[mesh_id];
					custom_lod_levels = mesh_settings.get("lods/custom_levels", false);
					lod_levels = mesh_settings.get("lods/levels", Array());
					if (mesh_settings.has("lods/normal_merge_angle")) {
						merge_angle = (float)mesh_settings["lods/normal_merge_angle"];
					}
					if (mesh_settings.has("lods/count")) {
						lod_count = (int)mesh_settings["lods/count"];
					}
					if (mesh_settings.has("lods/reduction")) {
						lod_reduction = (float)mesh_settings["lods/reduction"];
					}
					if (mesh_settings.has("generate/lods")) {
						// Same values as ResourceImporterScene::MeshOverride
						// (Default=0, Enable=1, Disable=2), which is private.
						const int lods = (int)mesh_settings["generate/lods"];
						if (lods == 1) {
							generate_lods = true;
						} else if (lods == 2) {
							generate_lods = false;
						}
					}
				}
			}

			if (generate_lods && !importer_mesh->has_mesh()) {
				// Bone pose transforms are needed for correct LOD
				// simplification of skinned meshes, same as on reimport.
				Array skin_pose_transform_array;
				const Ref<Skin> skin = src_mesh_node->get_skin();
				if (skin.is_valid()) {
					NodePath skeleton_path = src_mesh_node->get_skeleton_path();
					Node *node = src_mesh_node->get_node_or_null(skeleton_path);
					Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(node);
					if (skeleton) {
						int bind_count = skin->get_bind_count();
						for (int i = 0; i < bind_count; i++) {
							Transform3D bind_pose = skin->get_bind_pose(i);
							String bind_name = skin->get_bind_name(i);
							int bone_idx = bind_name.is_empty() ? skin->get_bind_bone(i) : skeleton->find_bone(bind_name);
							if (bone_idx >= skeleton->get_bone_count()) {
								bone_idx = -1;
							}
							Transform3D bp_global_rest;
							if (bone_idx >= 0) {
								bp_global_rest = skeleton->get_bone_global_pose(bone_idx);
							} else {
								bp_global_rest = skeleton->get_bone_global_pose(i);
							}
							skin_pose_transform_array.push_back(bp_global_rest * bind_pose);
						}
					}
				}
				if (!mesh_id.is_empty()) {
					lod_source_skin_pose_transforms[mesh_id] = skin_pose_transform_array;
				}
				importer_mesh->generate_lods(merge_angle, skin_pose_transform_array, lod_count, lod_reduction, custom_lod_levels, lod_levels);
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_generate_preview_lods(p_node->get_child(i));
	}
}

// Gathers the cached surface geometry of a single mesh (see MeshStats) so
// the active LOD can be re-evaluated on every camera/viewport change without
// touching the RenderingServer. Returns false when the mesh has no usable
// surface geometry.
bool SceneImportSettingsScreen::_gather_mesh_stats_for_mesh(const Ref<Mesh> &p_mesh, MeshStats &r_stats) {
	r_stats = MeshStats();
	if (p_mesh.is_null()) {
		return false;
	}

	// Original polygon (quad/n-gon) count from the source file, stored as
	// "source_face_count" mesh metadata by importers that still have
	// access to it before triangulation (OBJ, COLLADA). Formats that
	// only store triangles (e.g. glTF/GLB) provide no such metadata.
	if (p_mesh->has_meta("source_face_count")) {
		r_stats.has_source_faces = true;
		r_stats.source_faces = (uint64_t)(int64_t)p_mesh->get_meta("source_face_count");
	}

	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		MeshSurfaceStats surface;
		surface.primitive = p_mesh->surface_get_primitive_type(s);

		Array arrays = p_mesh->surface_get_arrays(s);
		if (arrays.size() != Mesh::ARRAY_MAX) {
			continue;
		}
		PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		surface.index_count = indices.size();
		PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		surface.vertex_count = vertices.size();

		r_stats.base_indices += surface.index_count;
		r_stats.base_faces += _mesh_faces_from_primitive(surface.primitive, surface.index_count, surface.vertex_count);

		Dictionary lods = p_mesh->surface_get_lods(s);
		r_stats.max_lod_count = MAX(r_stats.max_lod_count, lods.size());
		if (lods.size() > 0) {
			r_stats.has_any_lods = true;
		}
		// LOD keys are edge-length thresholds (doubles, exactly as stored
		// in the mesh); sort ascending to mirror the renderer's LOD
		// selection order (fine -> coarse).
		Vector<double> edge_lengths;
		for (const KeyValue<Variant, Variant> &K : lods) {
			ERR_CONTINUE(!K.key.is_num());
			edge_lengths.push_back(K.key);
		}
		edge_lengths.sort();
		for (int i = 0; i < edge_lengths.size(); i++) {
			Vector<int> lod_indices = lods[edge_lengths[i]];
			// Unique source vertices referenced by this level (reported by
			// the statistics rows as the vertex cost of the level).
			HashSet<int> unique_lod_vertices;
			for (const int &idx : lod_indices) {
				unique_lod_vertices.insert(idx);
			}
			surface.lod_index_counts.push_back((uint64_t)lod_indices.size());
			surface.lod_edge_lengths.push_back(edge_lengths[i]);
			surface.lod_vertex_counts.push_back((uint64_t)unique_lod_vertices.size());
		}
		r_stats.surfaces.push_back(surface);
	}

	return r_stats.surfaces.size() > 0;
}

void SceneImportSettingsScreen::_gather_mesh_stats() {
	stats_base_faces = 0;
	stats_base_indices = 0;
	stats_source_faces = 0;
	stats_has_source_faces = true;
	stats_lod_count = 0;
	stats_has_any_lods = false;
	mesh_stats_map.clear();

	if (scene == nullptr) {
		return;
	}

	// Collect every unique mesh used by visible mesh nodes in the previewed
	// scene and cache its surface geometry, so the active LOD can be
	// re-evaluated on every camera change without touching the RenderingServer.
	// Debug-only children (collider_view, preview meshes) are never part of
	// node_map, so they are excluded automatically.
	for (const KeyValue<String, NodeData> &E : node_map) {
		Node *node = E.value.node;
		if (node == nullptr) {
			continue;
		}
		Ref<Mesh> mesh;
		MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(node);
		if (mesh_node != nullptr && mesh_node->is_visible_in_tree()) {
			mesh = mesh_node->get_mesh();
			// Manual LOD preview swaps the mesh resource; gather from the
			// original so the level statistics keep the real LOD chain.
			if (lod_swapped_meshes.has(mesh_node)) {
				mesh = lod_swapped_meshes[mesh_node];
			}
		}
		MultiMeshInstance3D *multimesh_node = Object::cast_to<MultiMeshInstance3D>(node);
		if (mesh.is_null() && multimesh_node != nullptr && multimesh_node->is_visible_in_tree() && multimesh_node->get_multimesh().is_valid()) {
			mesh = multimesh_node->get_multimesh()->get_mesh();
		}
		if (mesh.is_null() || mesh_stats_map.has(mesh)) {
			continue;
		}

		MeshStats ms;
		_gather_mesh_stats_for_mesh(mesh, ms);
		mesh_stats_map[mesh] = ms;

		// Aggregate the scene-wide totals from the per-mesh statistics.
		stats_base_faces += ms.base_faces;
		stats_base_indices += ms.base_indices;
		if (ms.has_source_faces) {
			stats_source_faces += ms.source_faces;
		} else {
			stats_has_source_faces = false;
		}
		stats_lod_count = MAX(stats_lod_count, ms.max_lod_count);
		if (ms.has_any_lods) {
			stats_has_any_lods = true;
		}
	}
}

void SceneImportSettingsScreen::_update_mesh_stats() {
	if (mesh_stats_panel == nullptr || mesh_stats_label == nullptr) {
		return;
	}

	// Statistics are shown for the content currently previewed: the whole
	// model while the scene is selected ("Node" / "Animation" selections and
	// the empty selection), or the single mesh selected in the Mesh tab.
	// Material selections show a sphere preview with no meaningful mesh
	// statistics, so the overlay stays hidden for them.
	Ref<Mesh> selected_mesh;
	if (selected_type == "Mesh" && mesh_map.has(selected_id)) {
		const MeshData &md = mesh_map[selected_id];
		selected_mesh = md.live_preview_mesh.is_valid() ? md.live_preview_mesh : md.mesh;
	}
	const bool scene_mode = selected_type == "Node" || selected_type == "Animation" || selected_type.is_empty();
	if (scene == nullptr || (!scene_mode && selected_mesh.is_null())) {
		mesh_stats_panel->hide();
		return;
	}

	if (mesh_stats_dirty) {
		_gather_mesh_stats();
		mesh_stats_dirty = false;
	}

	// Per-selected-mesh statistics are cached (gathering fetches the mesh
	// surface arrays) and re-gathered whenever the mesh selection changes.
	const MeshStats *selected_stats = nullptr;
	if (!scene_mode) {
		if (stats_selected_mesh != selected_mesh) {
			_gather_mesh_stats_for_mesh(selected_mesh, stats_selected);
			stats_selected_mesh = selected_mesh;
		}
		selected_stats = &stats_selected;
	}

	struct PreviewMeshInstance {
		const MeshStats *stats = nullptr;
		AABB bounds;
		real_t lod_scale = 1.0;
	};
	Vector<PreviewMeshInstance> evaluated_meshes;
	if (selected_stats != nullptr) {
		const Transform3D transform = mesh_preview->get_global_transform();
		const Vector3 scale = transform.basis.get_scale_abs();
		PreviewMeshInstance entry;
		entry.stats = selected_stats;
		entry.bounds = transform.xform(selected_mesh->get_aabb());
		entry.lod_scale = MAX(scale.x, MAX(scale.y, scale.z)) * mesh_preview->get_lod_bias();
		evaluated_meshes.push_back(entry);
	} else {
		for (const KeyValue<String, NodeData> &E : node_map) {
			GeometryInstance3D *instance = Object::cast_to<GeometryInstance3D>(E.value.node);
			if (!instance || !instance->is_visible_in_tree()) {
				continue;
			}
			Ref<Mesh> mesh;
			MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(instance);
			MultiMeshInstance3D *multi = Object::cast_to<MultiMeshInstance3D>(instance);
			if (mesh_node) {
				mesh = lod_swapped_meshes.has(mesh_node) ? lod_swapped_meshes[mesh_node] : mesh_node->get_mesh();
			} else if (multi && multi->get_multimesh().is_valid()) {
				mesh = multi->get_multimesh()->get_mesh();
			}
			if (mesh.is_null() || !mesh_stats_map.has(mesh)) {
				continue;
			}
			const Transform3D transform = instance->get_global_transform();
			const Vector3 scale = transform.basis.get_scale_abs();
			PreviewMeshInstance entry;
			entry.stats = &mesh_stats_map[mesh];
			entry.bounds = transform.xform(instance->get_aabb());
			entry.lod_scale = MAX(scale.x, MAX(scale.y, scale.z)) * instance->get_lod_bias();
			evaluated_meshes.push_back(entry);
		}
	}
	const Size2 viewport_size = base_viewport->get_size();
	const float lod_threshold_pixels = base_viewport->get_mesh_lod_threshold();
	const float lod_distance_multiplier = camera->get_camera_projection().get_lod_multiplier();
	const bool lods_disabled = base_viewport->get_debug_draw() == Viewport::DEBUG_DRAW_DISABLE_LOD;
	const real_t lod_threshold = (viewport_size.width > 0 && lod_threshold_pixels > 0.0) ? lod_threshold_pixels / viewport_size.width : 0.0;
	const Vector3 camera_position = camera->get_global_transform().origin;

	// Evaluate the LOD level the renderer currently picks for the preview
	// camera, and the triangle count it actually draws with, using the same
	// rules as RendererRD::MeshStorage::mesh_surface_get_lod (screen size vs
	// threshold, ascending edge lengths).
	// Highest LOD level available across the evaluated surfaces (LOD 0 is
	// the full-detail base level).
	int levels_count = 1;
	for (const PreviewMeshInstance &instance : evaluated_meshes) {
		for (const MeshSurfaceStats &surface : instance.stats->surfaces) {
			levels_count = MAX(levels_count, surface.lod_edge_lengths.size() + 1);
		}
	}
	const Vector<int> *preview_map = nullptr;
	if (selected_type == "Mesh" && mesh_map.has(selected_id) && !mesh_map[selected_id].preview_lod_map.is_empty()) {
		preview_map = &mesh_map[selected_id].preview_lod_map;
		levels_count = preview_map->size();
	}
	stats_level_faces.clear();
	stats_level_faces.resize(levels_count);
	stats_level_faces.fill(0);
	stats_level_vertices.resize(levels_count);
	stats_level_vertices.fill(0);

	// Evaluate the LOD level the renderer currently picks for the preview
	// camera, and the triangle count it actually draws with, using the same
	// rules as RendererRD::MeshStorage::mesh_surface_get_lod (screen size vs
	// threshold, ascending edge lengths). In manual mode every surface is
	// pinned to the selected level instead.
	stats_active_faces = 0;
	stats_active_indices = 0;
	stats_active_vertices = 0;

	int lod_min = -1;
	int lod_max = -1;
	const bool manual_lod = lod_mode == LOD_MODE_MANUAL;
	for (const PreviewMeshInstance &instance : evaluated_meshes) {
		const Vector3 aabb_min = instance.bounds.position;
		const Vector3 aabb_max = instance.bounds.position + instance.bounds.size;
		const Vector3 surface_distance = Vector3().max(aabb_min - camera_position).max(camera_position - aabb_max);
		const real_t distance = MAX(surface_distance.length(), (real_t)CMP_EPSILON);
		for (const MeshSurfaceStats &surface : instance.stats->surfaces) {
			// LOD level the renderer picks for this surface right now, and
			// the triangle count it draws with.
			int lod = 0;
			uint64_t index_count = surface.index_count;
			uint64_t vertex_count = surface.vertex_count;
			if (manual_lod) {
				const int mapped_level = preview_map && lod_manual_level < preview_map->size() ? (*preview_map)[lod_manual_level] : lod_manual_level;
				const int surface_level = MIN(mapped_level, surface.lod_edge_lengths.size());
				if (surface_level > 0) {
					lod = preview_map ? lod_manual_level : surface_level;
					index_count = surface.lod_index_counts[surface_level - 1];
					vertex_count = surface.lod_vertex_counts[surface_level - 1];
				}
			} else if (!lods_disabled && surface.lod_edge_lengths.size() > 0 && lod_threshold > 0.0) {
				for (int i = 0; i < surface.lod_edge_lengths.size(); i++) {
					const real_t screen_size = surface.lod_edge_lengths[i] * instance.lod_scale / (distance * lod_distance_multiplier);
					if (screen_size > lod_threshold) {
						break;
					}
					lod = i + 1;
					index_count = surface.lod_index_counts[i];
					vertex_count = surface.lod_vertex_counts[i];
				}
			}
			stats_active_faces += _mesh_faces_from_primitive(surface.primitive, index_count, vertex_count);
			stats_active_indices += index_count;
			stats_active_vertices += vertex_count;
			lod_min = lod_min < 0 ? lod : MIN(lod_min, lod);
			lod_max = lod_max < 0 ? lod : MAX(lod_max, lod);

			// Per-level totals for the level breakdown row (level 0 = full
			// detail).
			for (int i = 0; i < levels_count; i++) {
				// A surface with fewer levels keeps its last available level, as
				// in the manual preview. Include every surface in each total.
				const int mapped_level = preview_map ? (*preview_map)[i] : i;
				const int surface_level = MIN(mapped_level, surface.lod_index_counts.size());
				const uint64_t level_indices = surface_level > 0 ? surface.lod_index_counts[surface_level - 1] : surface.index_count;
				const uint64_t level_vertices = surface_level > 0 ? surface.lod_vertex_counts[surface_level - 1] : surface.vertex_count;
				stats_level_faces.write[i] += _mesh_faces_from_primitive(surface.primitive, level_indices, level_vertices);
				stats_level_vertices.write[i] += level_vertices;
			}
		}
	}
	if (lod_min < 0) {
		lod_min = 0;
	}
	if (lod_max < 0) {
		lod_max = 0;
	}

	stats_lod_min = lod_min;
	stats_lod_max = lod_max;
	stats_mixed_lod = lod_min != lod_max;
	if (lod_preview_slider && lod_mode == LOD_MODE_AUTO) {
		lod_preview_slider->set_value_no_signal(lod_max);
	}

	// Format the overlay text: each metric on its own line, plain white text.
	// The "Polygons/Faces" row shows the original polygon count of the
	// full-detail content currently previewed (the whole model in scene mode,
	// the selected mesh in per-mesh mode). The "LOD" row shows ONLY the level
	// currently being viewed: the manually pinned level (the LOD buttons in
	// the viewport toolbar) or the level the preview camera selects in Auto
	// mode. The full per-level breakdown is intentionally not shown here;
	// per-level details live in the LOD settings cards of the inspector.
	// Polygons/Faces is the number of polygons in the source file before
	// triangulation (a quad or an n-gon each count as a single polygon). The
	// polygon row is only shown when the source format preserves it; formats
	// that only store triangles (e.g. glTF/GLB) lose the original polygon
	// count, so no misleading substitute row is shown for them.
	const bool has_source_faces = scene_mode ? stats_has_source_faces : selected_stats->has_source_faces;
	const uint64_t source_faces = scene_mode ? stats_source_faces : selected_stats->source_faces;
	String text;
	if (has_source_faces) {
		text += vformat(TTR("Polygons/Faces: %s"),
				_format_stats_count(source_faces));
	}

	if (!text.is_empty()) {
		text += "\n";
	}
	if (stats_mixed_lod) {
		text += vformat(TTR("LOD %s-%s Triangles: %s"), itos(stats_lod_min), itos(stats_lod_max), _format_stats_count(stats_active_faces));
	} else {
		text += vformat(TTR("LOD %s Triangles: %s"), itos(stats_lod_max), _format_stats_count(stats_active_faces));
	}
	text += "\n" + vformat(TTR("LOD Vertices: %s"), _format_stats_count(stats_active_vertices));
	if (selected_stats != nullptr && !evaluated_meshes.is_empty()) {
		const AABB &bounds = evaluated_meshes[0].bounds;
		text += "\n" + vformat(TTR("Camera distance: %s m"), String::num(camera_position.distance_to(bounds.get_center()), 1));
	}

	// Keep the LOD level buttons and the inspector LOD cards in sync with
	// the available levels (the guards prevent rebuilds on every camera
	// move, so slider edits are never interrupted).
	if (lod_preview_slider != nullptr && lod_picker_levels != levels_count - 1) {
		_update_lod_toolbar_buttons(levels_count - 1);
	}
	if (lod_settings_panel != nullptr && lod_level_settings.size() != stats_level_faces.size()) {
		_update_lod_settings_panel();
	}

	mesh_stats_label->set_text(text);
	mesh_stats_panel->show();
}

void SceneImportSettingsScreen::_wireframe_toggled(bool p_enabled) {
	base_viewport->set_debug_draw(p_enabled ? Viewport::DEBUG_DRAW_WIREFRAME : Viewport::DEBUG_DRAW_DISABLED);
	_update_mesh_stats();
}

void SceneImportSettingsScreen::_lod_bias_changed(double p_value) {
	lod_bias = CLAMP((float)p_value, 0.25f, 4.0f);
	// Coarser levels switch in sooner as the screen-space threshold grows.
	_update_preview_lod_threshold();
	_update_mesh_stats();
}

// Builds (and caches) a preview mesh holding the actual decimated geometry
// of a single LOD level, so manual inspection can zoom into any level.
Ref<Mesh> SceneImportSettingsScreen::_get_lod_preview_mesh(const Ref<Mesh> &p_mesh, int p_level) {
	if (p_mesh.is_null() || p_level <= 0) {
		return p_mesh;
	}
	if (lod_preview_mesh_cache.has(p_mesh)) {
		const Ref<Mesh> &cached = lod_preview_mesh_cache[p_mesh];
		// The cache only ever holds builds for one pinned level.
		if (cached.is_valid() && cached->has_meta("lod_preview_level") && (int)cached->get_meta("lod_preview_level") == p_level) {
			return cached;
		}
	} else {
		// Drop stale builds whenever the pinned level changes.
		lod_preview_mesh_cache.clear();
	}

	Ref<ArrayMesh> preview;
	preview.instantiate();
	preview->set_name(vformat("%s (LOD %d preview)", p_mesh->get_name(), p_level));
	preview->set_meta("lod_preview_level", p_level);
	for (int b = 0; b < p_mesh->get_blend_shape_count(); b++) {
		preview->add_blend_shape(p_mesh->get_blend_shape_name(b));
	}
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		Array arrays = p_mesh->surface_get_arrays(s);
		if (arrays.size() != Mesh::ARRAY_MAX) {
			continue;
		}
		Dictionary lods = p_mesh->surface_get_lods(s);
		Vector<double> edge_lengths;
		for (const KeyValue<Variant, Variant> &K : lods) {
			if (K.key.is_num()) {
				edge_lengths.push_back(K.key);
			}
		}
		edge_lengths.sort();
		if (!edge_lengths.is_empty()) {
			Vector<int> lod_indices = lods[edge_lengths[MIN(p_level, edge_lengths.size()) - 1]];
			if (!lod_indices.is_empty()) {
				arrays[Mesh::ARRAY_INDEX] = lod_indices;
			}
		}
		preview->add_surface_from_arrays(p_mesh->surface_get_primitive_type(s), arrays, p_mesh->surface_get_blend_shape_arrays(s));
		preview->surface_set_material(preview->get_surface_count() - 1, p_mesh->surface_get_material(s));
	}

	Ref<Mesh> result = preview;
	lod_preview_mesh_cache[p_mesh] = result;
	return result;
}

// Applies the current LOD preview state to the previewed scene meshes: in
// Auto mode the real meshes stay put and the renderer switches levels with
// the camera (the bias scales the screen-space threshold); in manual mode
// every visible mesh is swapped for the actual decimated geometry of the
// pinned level (LOD 0 restores the originals).
void SceneImportSettingsScreen::_apply_lod_preview() {
	// Restore the originals of any previous manual swap first.
	for (const KeyValue<MeshInstance3D *, Ref<Mesh>> &E : lod_swapped_meshes) {
		if (E.key != nullptr && E.value.is_valid()) {
			E.key->set_mesh(E.value);
		}
	}
	lod_swapped_meshes.clear();

	if (lod_mode != LOD_MODE_MANUAL || scene == nullptr) {
		_update_preview_lod_threshold();
		_apply_material_isolation();
		return;
	}

	if (selected_type == "Mesh" && mesh_map.has(selected_id) && mesh_preview->is_visible()) {
		const MeshData &md = mesh_map[selected_id];
		Ref<Mesh> original = md.live_preview_mesh.is_valid() ? md.live_preview_mesh : md.mesh;
		const int source_level = !md.preview_lod_map.is_empty() && lod_manual_level < md.preview_lod_map.size() ? md.preview_lod_map[lod_manual_level] : lod_manual_level;
		Ref<Mesh> preview = _get_lod_preview_mesh(original, source_level);
		if (preview != original) {
			lod_swapped_meshes[mesh_preview] = original;
			mesh_preview->set_mesh(preview);
		}
	}
	for (const KeyValue<String, NodeData> &E : node_map) {
		MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(E.value.node);
		if (mesh_node == nullptr || !mesh_node->is_visible_in_tree()) {
			continue;
		}
		Ref<Mesh> mesh = mesh_node->get_mesh();
		if (mesh.is_null()) {
			continue;
		}
		Ref<Mesh> preview_mesh = _get_lod_preview_mesh(mesh, lod_manual_level);
		if (preview_mesh != mesh) {
			lod_swapped_meshes[mesh_node] = mesh;
			mesh_node->set_mesh(preview_mesh);
		}
	}
	_apply_material_isolation();
}

void SceneImportSettingsScreen::_load_default_subresource_settings(HashMap<StringName, Variant> &settings, const String &p_type, const String &p_import_id, ResourceImporterScene::InternalImportCategory p_category) {
	if (base_subresource_settings.has(p_type)) {
		Dictionary d = base_subresource_settings[p_type];
		if (d.has(p_import_id)) {
			d = d[p_import_id];
			List<ResourceImporterScene::ImportOption> options;
			_resource_importer_scene->get_internal_import_options(p_category, &options);
			for (const ResourceImporterScene::ImportOption &E : options) {
				String key = E.option.name;
				if (d.has(key)) {
					settings[key] = d[key];
				}
			}
		}
	}
}

void SceneImportSettingsScreen::request_generate_collider() {
	generate_collider = true;
}

void SceneImportSettingsScreen::update_view() {
	update_view_timer->start();
}

void SceneImportSettingsScreen::open_settings(const String &p_path, const String &p_scene_import_type) {
	if (lod_edit_timer) {
		lod_edit_timer->stop();
	}
	if (scene) {
		_save_lod_config();
		_cleanup();
		memdelete(scene);
		scene = nullptr;
	}
	if (material_inspector) {
		material_inspector->edit(nullptr);
	}

	scene_import_settings_data->settings = nullptr;
	scene_import_settings_data->path = p_path;

	// AnimationLibrary cannot make use of the mesh and material tabs.
	const bool disable_mesh_mat_tabs = p_scene_import_type == "AnimationLibrary";
	data_mode->set_tab_hidden(1, disable_mesh_mat_tabs);
	data_mode->set_tab_hidden(2, disable_mesh_mat_tabs);
	if (disable_mesh_mat_tabs) {
		data_mode->set_current_tab(0);
	}

	// Only show the save mesh options for PackedScene and MeshLibrary imports of scenes.
	const bool disable_save_mesh_mat = p_scene_import_type != "PackedScene" && p_scene_import_type != "MeshLibrary";
	action_menu->get_popup()->set_item_disabled(action_menu->get_popup()->get_item_id(ACTION_EXTRACT_MATERIALS), disable_save_mesh_mat);
	action_menu->get_popup()->set_item_disabled(action_menu->get_popup()->get_item_id(ACTION_CHOOSE_MESH_SAVE_PATHS), disable_save_mesh_mat);
	const bool disable_save_anim = p_scene_import_type != "PackedScene" && p_scene_import_type != "AnimationLibrary";
	action_menu->get_popup()->set_item_disabled(action_menu->get_popup()->get_item_id(ACTION_CHOOSE_ANIMATION_SAVE_PATHS), disable_save_anim);

	base_path = p_path;
	current_import_type = p_scene_import_type;
	lod_settings_pending = false;
	_setup_model_preview_lighting();

	mesh_set.clear();
	animation_map.clear();
	material_map.clear();
	material_surface_usages.clear();
	unnamed_material_name_map.clear();
	mesh_map.clear();
	node_map.clear();
	defaults.clear();

	mesh_preview->hide();
	if (preview_floor != nullptr) {
		preview_floor->hide();
	}

	selected_id = "";
	selected_type = "";

	cam_rot_x = -Math::PI / 4;
	cam_rot_y = -Math::PI / 4;
	cam_zoom = 1;
	cam_pan = Vector2();
	has_skeleton_aabb = false;
	skeleton_aabb = AABB();
	// Re-aggregate the mesh statistics for the freshly loaded scene and drop
	// the per-selected-mesh cache (the old scene was freed).
	mesh_stats_dirty = true;
	stats_selected_mesh = Ref<Mesh>();

	// Reset the LOD preview state for the fresh scene (Auto mode, neutral
	// bias); manual swaps and the preview mesh cache never survive a load.
	lod_mode = LOD_MODE_AUTO;
	lod_manual_level = 0;
	lod_bias = 1.0;
	lod_swapped_meshes.clear();
	lod_preview_mesh_cache.clear();
	lod_picker_levels = -1;
	lod_level_settings.clear();
	lod_config_pending_meshes.clear();
	lod_source_meshes.clear();
	lod_source_skin_pose_transforms.clear();
	lod_config_data.clear();
	lod_auto_toggle->set_disabled(false);
	base_viewport->set_mesh_lod_threshold((float)GLOBAL_GET("rendering/mesh_lod/lod_change/threshold_pixels"));
	if (lod_auto_toggle != nullptr) {
		lod_auto_toggle->set_pressed_no_signal(true);
	}
	if (lod_bias_slider != nullptr) {
		lod_bias_slider->set_value_no_signal(1.0);
		lod_bias_slider->show();
	}
	_update_lod_button_states();

	{
		base_subresource_settings.clear();

		Ref<ConfigFile> config;
		config.instantiate();
		Error err = config->load(p_path + ".import");
		if (err == OK) {
			Vector<String> keys = config->get_section_keys("params");
			for (const String &E : keys) {
				Variant value = config->get_value("params", E);
				if (E == "_subresources") {
					base_subresource_settings = value;
				} else {
					defaults[E] = value;
				}
			}
		}
	}

	// Keep in-progress LOD edits outside the import metadata. Updating the
	// .import file marks the source asset stale and makes the editor reimport
	// it on its next startup, so editable settings live in a sidecar until the
	// user applies them with Reimport.
	{
		Ref<ConfigFile> lod_config;
		lod_config.instantiate();
		if (lod_config->load(base_path + ".lodcfg") == OK) {
			lod_config_data = lod_config->get_value("lod", "meshes", Dictionary());
		}
		Dictionary imported_meshes = base_subresource_settings.get("meshes", Dictionary());
		for (const Variant &key : lod_config_data.keys()) {
			const String mesh_id = key;
			Dictionary pending_settings = lod_config_data[key];
			Dictionary import_settings = imported_meshes.get(mesh_id, Dictionary());
			for (const Variant &setting_key : pending_settings.keys()) {
				const String setting_name = setting_key;
				const Variant setting_value = pending_settings[setting_key];
				if (!import_settings.has(setting_name) || import_settings[setting_name] != setting_value) {
					lod_config_pending_meshes.insert(mesh_id);
				}
				import_settings[setting_name] = setting_value;
			}
			imported_meshes[mesh_id] = import_settings;
		}
		if (!lod_config_data.is_empty()) {
			base_subresource_settings["meshes"] = imported_meshes;
		}
	}

	// Regardless of p_scene_import_type, use PackedScene for pre_import because we want to see the full thing.
	_resource_importer_scene->set_scene_import_type("PackedScene");
	scene = _resource_importer_scene->pre_import(p_path, defaults);
	_resource_importer_scene->set_scene_import_type(p_scene_import_type);
	if (scene == nullptr) {
		mesh_stats_panel->hide();
		EditorNode::get_singleton()->show_warning(TTR("Error opening scene"));
		return;
	}

	// Generate LOD levels for the preview meshes (mirroring the reimport
	// generation) so the statistics overlay can report the LOD level the
	// preview camera selects. Must run before _update_scene() converts the
	// ImporterMeshInstance3D nodes to MeshInstance3D.
	_generate_preview_lods(scene);

	first_aabb = true;

	_update_scene();

	base_viewport->add_child(scene);

	inspector->edit(nullptr);

	if (first_aabb) {
		// The file has no visible mesh content (e.g. an animation-only import).
		// Fall back to the skeleton gizmo extents so the preview still frames
		// something; otherwise use the default preview cube.
		contents_aabb = has_skeleton_aabb ? skeleton_aabb : AABB(Vector3(-1, -1, -1), Vector3(2, 2, 2));
		first_aabb = false;
	}

	const HashMap<StringName, Variant> &main_settings = scene_import_settings_data->current;
	if (main_settings.has("nodes/import_as_skeleton_bones")) {
		previous_import_as_skeleton = main_settings["nodes/import_as_skeleton_bones"];
	}
	if (main_settings.has("animation/import_rest_as_RESET")) {
		previous_rest_as_reset = main_settings["animation/import_rest_as_RESET"];
	}
	_update_view_gizmos();
	_update_camera();

	// Start with the root item (Scene) selected.
	scene_tree->get_root()->select(0);
	_update_material_editor();

	file_label->set_text(base_path.get_file());
	file_label->set_tooltip_text(base_path);

	_update_empty_state();

	// Instead of popping up a modal dialog, open this as the "Model" workspace
	// screen so the preview gets the full editor area.
	EditorMainScreen *main_screen = EditorNode::get_singleton()->get_editor_main_screen();
	if (main_screen->get_selected_index() != EditorMainScreen::EDITOR_MODEL) {
		main_screen->select_by_name(TTRC("Model"));
	}
}

SceneImportSettingsScreen *SceneImportSettingsScreen::singleton = nullptr;

SceneImportSettingsScreen *SceneImportSettingsScreen::get_singleton() {
	return singleton;
}

Node *SceneImportSettingsScreen::get_selected_node() {
	if (selected_id == "") {
		return nullptr;
	}
	return node_map[selected_id].node;
}

void SceneImportSettingsScreen::_select(Tree *p_from, const String &p_type, const String &p_id) {
	// Restore the previous pinned preview before the selection changes its mesh.
	for (const KeyValue<MeshInstance3D *, Ref<Mesh>> &entry : lod_swapped_meshes) {
		if (entry.key && entry.value.is_valid()) {
			entry.key->set_mesh(entry.value);
		}
	}
	lod_swapped_meshes.clear();
	selecting = true;
	scene_import_settings_data->hide_options = false;
	// Only AnimationLibrary and actual scenes can make use of the animation and skeleton options.
	const bool hide_anim_and_skel_options = _resource_importer_scene->get_scene_import_type() != "PackedScene" && _resource_importer_scene->get_scene_import_type() != "AnimationLibrary";
	// Only actual scenes can make use of the node generation options such as generating physics colliders on meshes or setting a script.
	const bool hide_node_gen_options = _resource_importer_scene->get_scene_import_type() != "PackedScene";

	bones_mesh_preview->hide();
	if (p_type == "Node") {
		node_selected->hide(); // Always hide just in case.
		mesh_preview->hide();
		_reset_animation();

		if (Object::cast_to<Node3D>(scene)) {
			Object::cast_to<Node3D>(scene)->show();
		}
		material_tree->deselect_all();
		mesh_tree->deselect_all();
		NodeData &nd = node_map[p_id];

		MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(nd.node);
		if (mi) {
			Ref<Mesh> base_mesh = mi->get_mesh();
			if (base_mesh.is_valid()) {
				AABB aabb = base_mesh->get_aabb();
				Transform3D aabb_xf;
				aabb_xf.basis.scale(aabb.size);
				aabb_xf.origin = aabb.position;

				aabb_xf = mi->get_global_transform() * aabb_xf;
				node_selected->set_transform(aabb_xf);
				node_selected->show();
			}
		}

		if (nd.node == scene) {
			scene_import_settings_data->settings = &defaults;
			scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MAX;
		} else {
			scene_import_settings_data->settings = &nd.settings;
			if (mi) {
				scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH_3D_NODE;
				scene_import_settings_data->hide_options = hide_node_gen_options;
			} else if (Object::cast_to<AnimationPlayer>(nd.node)) {
				scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_ANIMATION_NODE;
				scene_import_settings_data->hide_options = hide_anim_and_skel_options;
			} else if (Object::cast_to<Skeleton3D>(nd.node)) {
				scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_SKELETON_3D_NODE;
				// Selecting a skeleton node implies the user wants to inspect the
				// rig; enable the persistent toggle so the state stays consistent.
				skeleton_visibility_toggle->set_pressed_no_signal(true);
				_update_skeleton_preview_visibility();
				scene_import_settings_data->hide_options = hide_anim_and_skel_options;
			} else {
				scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_NODE;
				scene_import_settings_data->hide_options = hide_node_gen_options;
			}
		}
	} else if (p_type == "Animation") {
		node_selected->hide(); // Always hide just in case.
		mesh_preview->hide();
		_reset_animation(p_id);

		if (Object::cast_to<Node3D>(scene)) {
			Object::cast_to<Node3D>(scene)->show();
		}
		material_tree->deselect_all();
		mesh_tree->deselect_all();
		AnimationData &ad = animation_map[p_id];

		scene_import_settings_data->settings = &ad.settings;
		scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_ANIMATION;
		scene_import_settings_data->hide_options = hide_anim_and_skel_options;

		_animation_update_skeleton_visibility();
	} else if (p_type == "Mesh") {
		node_selected->hide();
		if (Object::cast_to<Node3D>(scene)) {
			Object::cast_to<Node3D>(scene)->hide();
		}

		MeshData &md = mesh_map[p_id];
		if (md.mesh_node != nullptr) {
			if (p_from != mesh_tree) {
				md.mesh_node->uncollapse_tree();
				md.mesh_node->select(0);
				mesh_tree->ensure_cursor_is_visible();
			}
			if (p_from != scene_tree && md.scene_node != nullptr) {
				md.scene_node->uncollapse_tree();
				md.scene_node->select(0);
				scene_tree->ensure_cursor_is_visible();
			}
		}

		mesh_preview->set_mesh(md.live_preview_mesh.is_valid() ? md.live_preview_mesh : md.mesh);
		mesh_preview->show();
		_reset_animation();

		material_tree->deselect_all();

		scene_import_settings_data->settings = &md.settings;
		scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH;
	} else if (p_type == "Material") {
		node_selected->hide();
		_reset_animation();

		MaterialData &md = material_map[p_id];

		material_preview->set_material(md.material);
		if (material_preview_on_model) {
			if (Object::cast_to<Node3D>(scene)) {
				Object::cast_to<Node3D>(scene)->show();
			}
			mesh_preview->hide();
		} else {
			if (Object::cast_to<Node3D>(scene)) {
				Object::cast_to<Node3D>(scene)->hide();
			}
			mesh_preview->set_mesh(material_preview);
			mesh_preview->show();
		}

		if (p_from != mesh_tree) {
			md.mesh_node->uncollapse_tree();
			md.mesh_node->select(0);
			mesh_tree->ensure_cursor_is_visible();
		}
		if (p_from != scene_tree) {
			md.scene_node->uncollapse_tree();
			md.scene_node->select(0);
			scene_tree->ensure_cursor_is_visible();
		}
		if (p_from != material_tree) {
			md.material_node->uncollapse_tree();
			md.material_node->select(0);
			material_tree->ensure_cursor_is_visible();
		}

		scene_import_settings_data->settings = &md.settings;
		scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MATERIAL;
	}

	selected_type = p_type;
	selected_id = p_id;
	if (selected_type == "Material" && settings_tabs) {
		settings_tabs->set_current_tab(5);
	}
	_update_material_editor();
	lod_selected_level = 0;
	lod_auto_toggle->set_disabled(false);
	_apply_lod_preview();
	lod_level_settings.clear(); // Rebuild for the selected mesh, including when level counts match.

	selecting = false;

	_update_camera();

	List<ResourceImporter::ImportOption> options;

	if (scene_import_settings_data->category == ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MAX) {
		_resource_importer_scene->get_import_options(base_path, &options);
	} else {
		_resource_importer_scene->get_internal_import_options(scene_import_settings_data->category, &options);
	}

	scene_import_settings_data->defaults.clear();
	scene_import_settings_data->current.clear();

	if (scene_import_settings_data->settings) {
		for (const ResourceImporter::ImportOption &E : options) {
			scene_import_settings_data->defaults[E.option.name] = E.default_value;
			// Needed for visibility toggling (fails if something is missing).
			if (scene_import_settings_data->settings->has(E.option.name)) {
				scene_import_settings_data->current[E.option.name] = (*scene_import_settings_data->settings)[E.option.name];
			} else {
				scene_import_settings_data->current[E.option.name] = E.default_value;
			}
		}
	}

	scene_import_settings_data->options = options;
	inspector->edit(scene_import_settings_data);
	scene_import_settings_data->notify_property_list_changed();
	lod_settings_pending = false;
	for (const KeyValue<String, MeshData> &entry : mesh_map) {
		lod_settings_pending = lod_settings_pending || entry.value.lod_pending;
	}
	_update_lod_settings_panel();
	if (settings_tabs) {
		const int tab = p_type == "Mesh" ? 0 : (p_type == "Material" ? 5 : 4);
		settings_tabs->set_current_tab(tab);
		_settings_tab_changed(tab);
	}
}

void SceneImportSettingsScreen::_inspector_property_edited(const String &p_name) {
	if (p_name == "settings/loop_mode") {
		if (!animation_map.has(selected_id)) {
			return;
		}
		HashMap<StringName, Variant> settings(animation_map[selected_id].settings);
		if (settings.has(p_name)) {
			animation_loop_mode = static_cast<Animation::LoopMode>((int)settings[p_name]);
		} else {
			animation_loop_mode = Animation::LoopMode::LOOP_NONE;
		}
	}
	if ((p_name == "use_external/enabled") || (p_name == "use_external/path") || (p_name == "use_external/fallback_path")) {
		MaterialData &material_data = material_map[selected_id];
		String spath = base_path.get_base_dir();
		Variant value;
		if (_get_current("materials/extract_path", value)) {
			String extpath = value;
			if (!extpath.is_empty()) {
				spath = extpath;
			}
		}
		String opath = material_data.settings.has("use_external/path") ? (String)material_data.settings["use_external/path"] : String();
		if (opath.begins_with("uid://")) {
			opath = ResourceUID::uid_to_path(opath);
		}
		String ext = ResourceImporterScene::material_extension[_get_current("materials/extract_format", value) ? (int)value : 0];
		String npath = spath.path_join(selected_id.validate_filename() + ext);

		if (!material_data.settings.has("use_external/enabled") || (bool)material_data.settings["use_external/enabled"] == false || opath != npath) {
			if (_get_current("materials/extract", value) && (int)value != 0) {
				print_line("Material settings changed, automatic material extraction disabled.");
			}
			_set_default("materials/extract", 0);
		}
	}
}

void SceneImportSettingsScreen::_reset_bone_transforms() {
	for (Skeleton3D *skeleton : skeletons) {
		skeleton->reset_bone_poses();
	}
}

void SceneImportSettingsScreen::_play_animation() {
	if (animation_player == nullptr) {
		return;
	}
	StringName id = StringName(selected_id);
	if (animation_player->has_animation(id)) {
		if (animation_player->is_playing()) {
			animation_player->pause();
			animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("MainPlay")));
			set_process(false);
		} else {
			animation_player->play(id);
			animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("Pause")));
			set_process(true);
		}
	}
}

void SceneImportSettingsScreen::_stop_current_animation() {
	animation_pingpong = false;
	animation_player->stop();
	animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("MainPlay")));
	animation_slider->set_value_no_signal(0.0);
	set_process(false);
}

void SceneImportSettingsScreen::_reset_animation(const String &p_animation_name) {
	if (p_animation_name.is_empty()) {
		animation_preview->hide();

		if (animation_player != nullptr && animation_player->is_playing()) {
			animation_player->stop();
		}
		animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("MainPlay")));

		_reset_bone_transforms();
		set_process(false);
	} else {
		_reset_bone_transforms();
		animation_preview->show();

		animation_loop_mode = Animation::LoopMode::LOOP_NONE;
		animation_pingpong = false;

		if (animation_map.has(p_animation_name)) {
			HashMap<StringName, Variant> settings(animation_map[p_animation_name].settings);
			if (settings.has("settings/loop_mode")) {
				animation_loop_mode = static_cast<Animation::LoopMode>((int)settings["settings/loop_mode"]);
			}
		}

		if (animation_player->is_playing() && animation_loop_mode != Animation::LoopMode::LOOP_NONE) {
			animation_player->play(p_animation_name);
		} else {
			animation_player->stop(true);
			animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("MainPlay")));
			animation_player->set_assigned_animation(p_animation_name);
			animation_player->seek(0.0, true);
			animation_slider->set_value_no_signal(0.0);
			set_process(false);
		}
	}
}

void SceneImportSettingsScreen::_animation_slider_value_changed(double p_value) {
	if (animation_player == nullptr || !animation_map.has(selected_id) || animation_map[selected_id].animation.is_null()) {
		return;
	}
	if (animation_player->is_playing()) {
		animation_player->stop();
		animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("MainPlay")));
		set_process(false);
	}
	animation_player->seek(p_value * animation_map[selected_id].animation->get_length(), true);
}

void SceneImportSettingsScreen::_skeleton_tree_entered(Skeleton3D *p_skeleton) {
	bones_mesh_preview->set_skeleton_path(p_skeleton->get_path());
	Ref<Skin> skin = p_skeleton->create_skin_from_rest_transforms();
	p_skeleton->register_skin(skin);
	bones_mesh_preview->set_skin(skin);
}

void SceneImportSettingsScreen::_animation_finished(const StringName &p_name) {
	Animation::LoopMode loop_mode = animation_loop_mode;

	switch (loop_mode) {
		case Animation::LOOP_NONE: {
			animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("MainPlay")));
			animation_slider->set_value_no_signal(1.0);
			set_process(false);
		} break;
		case Animation::LOOP_LINEAR: {
			animation_player->play(p_name);
		} break;
		case Animation::LOOP_PINGPONG: {
			if (animation_pingpong) {
				animation_player->play(p_name);
			} else {
				animation_player->play_backwards(p_name);
			}
			animation_pingpong = !animation_pingpong;
		} break;
		default: {
		} break;
	}
}

void SceneImportSettingsScreen::_update_skeleton_preview_visibility() {
	if (skeleton_visibility_toggle->is_pressed()) {
		bones_mesh_preview->show();
	} else {
		bones_mesh_preview->hide();
	}
}

void SceneImportSettingsScreen::_animation_update_skeleton_visibility() {
	// Keep the persistent viewport toggle in sync with the animation bar button.
	skeleton_visibility_toggle->set_pressed_no_signal(animation_toggle_skeleton_visibility->is_pressed());
	_update_skeleton_preview_visibility();
}

void SceneImportSettingsScreen::_material_tree_selected() {
	if (selecting) {
		return;
	}
	TreeItem *item = material_tree->get_selected();
	String type = item->get_meta("type");
	String import_id = item->get_meta("import_id");

	_select(material_tree, type, import_id);
}

void SceneImportSettingsScreen::_mesh_tree_selected() {
	if (selecting) {
		return;
	}

	TreeItem *item = mesh_tree->get_selected();
	String type = item->get_meta("type");
	String import_id = item->get_meta("import_id");

	_select(mesh_tree, type, import_id);
}

void SceneImportSettingsScreen::_scene_tree_selected() {
	if (selecting) {
		return;
	}
	TreeItem *item = scene_tree->get_selected();
	String type = item->get_meta("type");
	String import_id = item->get_meta("import_id");

	_select(scene_tree, type, import_id);
}

void SceneImportSettingsScreen::_cleanup() {
	skeletons.clear();
	if (animation_player != nullptr) {
		animation_player->disconnect(SceneStringName(animation_finished), callable_mp(this, &SceneImportSettingsScreen::_animation_finished));
		animation_player = nullptr;
	}
	set_process(false);
}

void SceneImportSettingsScreen::_get_active_camera_ptrs(float **r_rot_x, float **r_rot_y, float **r_zoom, Vector2 **r_pan) {
	float *rot_x = &cam_rot_x;
	float *rot_y = &cam_rot_y;
	float *zoom = &cam_zoom;
	Vector2 *pan = &cam_pan;

	if (selected_type == "Mesh" && mesh_map.has(selected_id)) {
		MeshData &md = mesh_map[selected_id];
		rot_x = &md.cam_rot_x;
		rot_y = &md.cam_rot_y;
		zoom = &md.cam_zoom;
		pan = &md.cam_pan;
	} else if (selected_type == "Material" && material_map.has(selected_id)) {
		MaterialData &md = material_map[selected_id];
		rot_x = &md.cam_rot_x;
		rot_y = &md.cam_rot_y;
		zoom = &md.cam_zoom;
		pan = &md.cam_pan;
	}

	*r_rot_x = rot_x;
	*r_rot_y = rot_y;
	*r_zoom = zoom;
	*r_pan = pan;
}

void SceneImportSettingsScreen::_reset_camera() {
	float *rot_x = nullptr;
	float *rot_y = nullptr;
	float *zoom = nullptr;
	Vector2 *pan = nullptr;
	_get_active_camera_ptrs(&rot_x, &rot_y, &zoom, &pan);

	*rot_x = -Math::PI / 4;
	*rot_y = -Math::PI / 4;
	*zoom = 1;
	*pan = Vector2();
	_update_camera();
}

void SceneImportSettingsScreen::_viewport_input(const Ref<InputEvent> &p_input) {
	float *rot_x = nullptr;
	float *rot_y = nullptr;
	float *zoom = nullptr;
	Vector2 *pan = nullptr;
	_get_active_camera_ptrs(&rot_x, &rot_y, &zoom, &pan);

	Ref<InputEventMouseMotion> mm = p_input;
	if (mm.is_valid() && mm->get_button_mask().has_flag(MouseButtonMask::LEFT)) {
		// Orbit around the content.
		(*rot_x) -= mm->get_relative().y * 0.01 * EDSCALE;
		(*rot_y) -= mm->get_relative().x * 0.01 * EDSCALE;
		(*rot_x) = CLAMP((*rot_x), -Math::PI / 2, Math::PI / 2);
		_update_camera();
	} else if (mm.is_valid() && (mm->get_button_mask().has_flag(MouseButtonMask::MIDDLE) || mm->get_button_mask().has_flag(MouseButtonMask::RIGHT))) {
		// Pan the camera so the content follows the cursor 1:1.
		const Vector2 rel = mm->get_relative();
		const real_t view_height = MAX(base_viewport->get_size().y, 1.0);
		const real_t world_per_pixel = (frame_size * (*zoom) * FRAME_MARGIN) / view_height;
		pan->x -= rel.x * world_per_pixel;
		pan->y += rel.y * world_per_pixel;
		_update_camera();
	}
	if (mm.is_valid() && DisplayServer::get_singleton()->has_feature(DisplayServerEnums::FEATURE_CURSOR_SHAPE)) {
		DisplayServer::get_singleton()->cursor_set_shape(DisplayServerEnums::CursorShape::CURSOR_ARROW);
	}
	Ref<InputEventMouseButton> mb = p_input;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::WHEEL_DOWN) {
		// Honor the precise scroll factor (trackpads send fractional values).
		real_t factor = mb->get_factor();
		if (factor <= 0.0) {
			factor = 1.0;
		}
		(*zoom) *= (real_t)Math::pow(1.1, (double)factor);
		if ((*zoom) > ZOOM_MAX) {
			(*zoom) = ZOOM_MAX;
		}
		_update_camera();
	}
	if (mb.is_valid() && mb->get_button_index() == MouseButton::WHEEL_UP) {
		real_t factor = mb->get_factor();
		if (factor <= 0.0) {
			factor = 1.0;
		}
		(*zoom) /= (real_t)Math::pow(1.1, (double)factor);
		if ((*zoom) < ZOOM_MIN) {
			(*zoom) = ZOOM_MIN;
		}
		_update_camera();
	}
	Ref<InputEventMagnifyGesture> mg = p_input;
	if (mg.is_valid()) {
		real_t mg_factor = mg->get_factor();
		if (mg_factor == 0.0) {
			mg_factor = 1.0;
		}
		(*zoom) /= mg_factor;
		if ((*zoom) < ZOOM_MIN) {
			(*zoom) = ZOOM_MIN;
		} else if ((*zoom) > ZOOM_MAX) {
			(*zoom) = ZOOM_MAX;
		}
		_update_camera();
	}
}

Error SceneImportSettingsScreen::_save_modified_external_materials() {
	for (KeyValue<String, MaterialData> &entry : material_map) {
		MaterialData &md = entry.value;
		if (!md.modified) {
			continue;
		}
		if (md.material.is_null() || !md.settings.has("use_external/enabled") || !(bool)md.settings["use_external/enabled"]) {
			EditorNode::get_singleton()->add_io_error(TTR("Can't save material changes. Create an editable material file first."));
			return ERR_INVALID_PARAMETER;
		}

		String path;
		if (md.settings.has("use_external/fallback_path")) {
			path = md.settings["use_external/fallback_path"];
		}
		if (path.is_empty() && md.settings.has("use_external/path")) {
			path = md.settings["use_external/path"];
		}
		if (path.begins_with("uid://")) {
			path = ResourceUID::uid_to_path(path);
		}
		if (!path.is_resource_file()) {
			EditorNode::get_singleton()->add_io_error(TTR("Can't save material changes to an invalid resource path:") + "\n\t" + path);
			return ERR_INVALID_PARAMETER;
		}

		// The external material may already be present in ResourceCache as a
		// different Resource loaded by the scene importer. Saving it to the
		// configured path does not require taking over that cached resource path.
		Error err = ResourceSaver::save(md.material, path);
		if (err != OK) {
			EditorNode::get_singleton()->add_io_error(TTR("Can't save material changes:") + "\n\t" + path);
			return err;
		}
		md.modified = false;
	}
	return OK;
}

Error SceneImportSettingsScreen::_re_import() {
	Error material_save_error = _save_modified_external_materials();
	if (material_save_error != OK) {
		return material_save_error;
	}
	HashMap<StringName, Variant> main_settings;

	// The inspector's current map belongs to the selected subresource. Build
	// file-wide settings from the importer defaults and saved scene settings.
	List<ResourceImporter::ImportOption> scene_options;
	_resource_importer_scene->get_import_options(base_path, &scene_options);
	for (const ResourceImporter::ImportOption &option : scene_options) {
		main_settings[option.option.name] = option.default_value;
	}
	for (const KeyValue<StringName, Variant> &entry : defaults) {
		main_settings[entry.key] = entry.value;
	}
	main_settings.erase("_subresources");
	Dictionary nodes;
	Dictionary materials;
	Dictionary meshes;
	Dictionary animations;

	Dictionary subresources;

	for (KeyValue<String, NodeData> &E : node_map) {
		if (E.value.settings.size()) {
			Dictionary d;
			for (const KeyValue<StringName, Variant> &F : E.value.settings) {
				d[String(F.key)] = F.value;
			}
			nodes[E.key] = d;
		}
	}
	if (nodes.size()) {
		subresources["nodes"] = nodes;
	}

	for (KeyValue<String, MaterialData> &E : material_map) {
		if (E.value.settings.size()) {
			Dictionary d;
			for (const KeyValue<StringName, Variant> &F : E.value.settings) {
				d[String(F.key)] = F.value;
			}
			materials[E.key] = d;
		}
	}
	if (materials.size()) {
		subresources["materials"] = materials;
	}

	for (KeyValue<String, MeshData> &E : mesh_map) {
		if (E.value.settings.size()) {
			Dictionary d;
			for (const KeyValue<StringName, Variant> &F : E.value.settings) {
				d[String(F.key)] = F.value;
			}
			meshes[E.key] = d;
		}
	}
	if (meshes.size()) {
		subresources["meshes"] = meshes;
	}

	for (KeyValue<String, AnimationData> &E : animation_map) {
		if (E.value.settings.size()) {
			Dictionary d;
			for (const KeyValue<StringName, Variant> &F : E.value.settings) {
				d[String(F.key)] = F.value;
			}
			animations[E.key] = d;
		}
	}
	if (animations.size()) {
		subresources["animations"] = animations;
	}

	main_settings["_subresources"] = subresources;

	_cleanup(); // Prevent skeletons and other pointers from pointing to dangling references.
	EditorFileSystem::get_singleton()->reimport_file_with_custom_parameters(base_path, _resource_importer_scene->get_importer_name(), main_settings);
	return OK;
}

void SceneImportSettingsScreen::_update_empty_state() {
	// The empty-state hint is shown whenever the workspace has no model loaded
	// (e.g. when the user switches to the "Model" tab manually).
	empty_hint_panel->set_visible(scene == nullptr);
}

void SceneImportSettingsScreen::_close_screen() {
	_save_lod_config();
	if (lod_edit_timer) {
		lod_edit_timer->stop();
	}
	if (scene) {
		_cleanup();
		memdelete(scene);
		scene = nullptr;
	}

	selected_id = "";
	selected_type = "";
	base_path = "";
	current_import_type = "PackedScene";

	mesh_set.clear();
	animation_map.clear();
	material_map.clear();
	material_surface_usages.clear();
	unnamed_material_name_map.clear();
	mesh_map.clear();
	lod_source_meshes.clear();
	lod_source_skin_pose_transforms.clear();
	lod_config_pending_meshes.clear();
	lod_config_data.clear();
	node_map.clear();
	defaults.clear();
	base_subresource_settings.clear();
	if (material_inspector) {
		material_inspector->edit(nullptr);
	}

	scene_tree->clear();
	material_tree->clear();
	mesh_tree->clear();
	// Hidden roots (same as after _update_scene()).
	material_tree->create_item();
	mesh_tree->create_item();

	bones_mesh_preview->set_mesh(Ref<Mesh>());
	mesh_preview->set_mesh(Ref<Mesh>());
	mesh_preview->hide();
	if (preview_floor != nullptr) {
		preview_floor->hide();
	}
	animation_preview->hide();
	inspector->edit(nullptr);
	_update_material_editor();

	mesh_stats_map.clear();
	stats_selected_mesh = Ref<Mesh>();
	mesh_stats_dirty = true;
	stats_base_faces = 0;
	stats_base_indices = 0;
	stats_source_faces = 0;
	stats_has_source_faces = true;
	stats_has_any_lods = false;
	stats_lod_count = 0;
	stats_level_faces.clear();
	stats_active_vertices = 0;

	// Reset the LOD preview state (Auto mode, neutral bias); the manual
	// swaps and the preview mesh cache never survive a close.
	lod_mode = LOD_MODE_AUTO;
	lod_manual_level = 0;
	lod_bias = 1.0;
	lod_swapped_meshes.clear();
	lod_preview_mesh_cache.clear();
	lod_picker_levels = -1;
	lod_level_settings.clear();
	lod_auto_toggle->set_disabled(false);
	base_viewport->set_mesh_lod_threshold((float)GLOBAL_GET("rendering/mesh_lod/lod_change/threshold_pixels"));
	if (lod_auto_toggle != nullptr) {
		lod_auto_toggle->set_pressed_no_signal(true);
	}
	if (lod_bias_slider != nullptr) {
		lod_bias_slider->set_value_no_signal(1.0);
		lod_bias_slider->show();
	}
	_update_lod_button_states();
	mesh_stats_panel->hide();

	cam_rot_x = -Math::PI / 4;
	cam_rot_y = -Math::PI / 4;
	cam_zoom = 1;
	cam_pan = Vector2();
	first_aabb = true;
	contents_aabb = AABB(Vector3(-1, -1, -1), Vector3(2, 2, 2));
	has_skeleton_aabb = false;
	skeleton_aabb = AABB();

	file_label->set_text("");
	file_label->set_tooltip_text("");

	_update_empty_state();

	// Return to the Level workspace when closing from the Model screen.
	EditorMainScreen *main_screen = EditorNode::get_singleton()->get_editor_main_screen();
	if (main_screen->get_selected_index() == EditorMainScreen::EDITOR_MODEL) {
		main_screen->select(EditorMainScreen::EDITOR_3D);
	}
}

void SceneImportSettingsScreen::_reimport_button_pressed() {
	if (base_path.is_empty()) {
		return;
	}
	if (lod_edit_timer) {
		lod_edit_timer->stop();
	}
	_save_lod_config();
	const String path = base_path;
	const String import_type = current_import_type;
	const String old_type = selected_type;
	const String old_id = selected_id;
	const int old_data_tab = data_mode->get_current_tab();
	const int old_settings_tab = settings_tabs->get_current_tab();
	const int old_lod_mode = lod_mode;
	const int old_lod_level = lod_manual_level;
	const int old_selected_level = lod_selected_level;
	float *rot_x = nullptr, *rot_y = nullptr, *zoom = nullptr;
	Vector2 *pan = nullptr;
	_get_active_camera_ptrs(&rot_x, &rot_y, &zoom, &pan);
	const float saved_rot_x = *rot_x, saved_rot_y = *rot_y, saved_zoom = *zoom;
	const Vector2 saved_pan = *pan;
	if (_re_import() != OK) {
		return;
	}
	open_settings(path, import_type);
	if (!scene) {
		return;
	}
	if (old_type == "Mesh" && mesh_map.has(old_id)) {
		MeshData &md = mesh_map[old_id];
		md.cam_rot_x = saved_rot_x;
		md.cam_rot_y = saved_rot_y;
		md.cam_zoom = saved_zoom;
		md.cam_pan = saved_pan;
		if (md.mesh_node) {
			md.mesh_node->uncollapse_tree();
			md.mesh_node->select(0);
		}
		_select(mesh_tree, "Mesh", old_id);
		data_mode->set_current_tab(old_data_tab);
		if (old_lod_mode == LOD_MODE_MANUAL && old_lod_level < stats_level_faces.size()) {
			_on_lod_level_selected(old_lod_level);
		}
		lod_selected_level = MIN(old_selected_level, _get_custom_lod_levels().size());
		_update_lod_settings_panel();
		settings_tabs->set_current_tab(old_settings_tab);
		_settings_tab_changed(old_settings_tab);
	} else if (old_type == "Material" && material_map.has(old_id)) {
		MaterialData &md = material_map[old_id];
		md.cam_rot_x = saved_rot_x;
		md.cam_rot_y = saved_rot_y;
		md.cam_zoom = saved_zoom;
		md.cam_pan = saved_pan;
		if (md.material_node) {
			md.material_node->uncollapse_tree();
			md.material_node->select(0);
		}
		_select(material_tree, "Material", old_id);
		data_mode->set_current_tab(old_data_tab);
		settings_tabs->set_current_tab(old_settings_tab);
		_settings_tab_changed(old_settings_tab);
	} else if (old_type.is_empty() || old_type == "Node" || old_type == "Animation") {
		cam_rot_x = saved_rot_x;
		cam_rot_y = saved_rot_y;
		cam_zoom = saved_zoom;
		cam_pan = saved_pan;
		_update_camera();
	}
}

void SceneImportSettingsScreen::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			if (animation_player != nullptr && animation_player->is_playing()) {
				animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("Pause")));
			} else {
				animation_play_button->set_button_icon(get_editor_theme_icon(SNAME("MainPlay")));
			}
			animation_stop_button->set_button_icon(get_editor_theme_icon(SNAME("Stop")));

			frame_button->set_button_icon(get_editor_theme_icon(SNAME("CenterView")));
			skeleton_visibility_toggle->set_button_icon(get_editor_theme_icon(SNAME("SkeletonPreview")));

			animation_toggle_skeleton_visibility->set_button_icon(get_editor_theme_icon(SNAME("SkeletonPreview")));
		} break;

		case NOTIFICATION_PROCESS: {
			if (animation_player != nullptr && is_visible_in_tree()) {
				animation_slider->set_value_no_signal(animation_player->get_current_animation_position() / animation_player->get_current_animation_length());
			}
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			// Unlike the old modal dialog, switching to another workspace tab
			// must NOT free the previewed model; it stays alive so the user
			// can come back to it. Cleanup happens in open_settings() and
			// _close_screen().
			_update_empty_state();
		} break;
	}
}

void SceneImportSettingsScreen::_menu_callback(int p_id) {
	switch (p_id) {
		case ACTION_EXTRACT_MATERIALS: {
			save_path->set_title(TTR("Select folder to extract material resources"));
			external_extension_type->select(0);
		} break;
		case ACTION_CHOOSE_MESH_SAVE_PATHS: {
			save_path->set_title(TTR("Select folder where mesh resources will save on import"));
			external_extension_type->select(1);
		} break;
		case ACTION_CHOOSE_ANIMATION_SAVE_PATHS: {
			save_path->set_title(TTR("Select folder where animations will save on import"));
			external_extension_type->select(1);
		} break;
	}

	save_path->set_current_dir(base_path.get_base_dir());
	current_action = p_id;
	save_path->popup_centered_ratio();
}

void SceneImportSettingsScreen::_save_path_changed(const String &p_path) {
	save_path_item->set_text(1, p_path);

	if (FileAccess::exists(p_path)) {
		save_path_item->set_text(2, TTR("Warning: File exists"));
		save_path_item->set_tooltip_text(2, TTR("Existing file with the same name will be replaced."));
		save_path_item->set_icon(2, get_editor_theme_icon(SNAME("StatusWarning")));

	} else {
		save_path_item->set_text(2, TTR("Will create new file"));
		save_path_item->set_icon(2, get_editor_theme_icon(SNAME("StatusSuccess")));
	}
}

void SceneImportSettingsScreen::_browse_save_callback(Object *p_item, int p_column, int p_id, MouseButton p_button) {
	if (p_button != MouseButton::LEFT) {
		return;
	}

	TreeItem *item = Object::cast_to<TreeItem>(p_item);

	String path = item->get_text(1);

	item_save_path->set_current_file(path);
	save_path_item = item;

	item_save_path->popup_centered_ratio();
}

void SceneImportSettingsScreen::_save_dir_callback(const String &p_path) {
	external_path_tree->clear();
	TreeItem *root = external_path_tree->create_item();
	save_path_items.clear();

	switch (current_action) {
		case ACTION_EXTRACT_MATERIALS: {
			for (const KeyValue<String, MaterialData> &E : material_map) {
				MaterialData &md = material_map[E.key];

				TreeItem *item = external_path_tree->create_item(root);

				String name = md.material_node->get_text(0);

				item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
				item->set_icon(0, get_editor_theme_icon(SNAME("StandardMaterial3D")));
				item->set_text(0, name);

				if (md.has_import_id) {
					if (md.settings.has("use_external/enabled") && bool(md.settings["use_external/enabled"])) {
						item->set_text(2, TTR("Already External"));
						item->set_tooltip_text(2, TTR("This material already references an external file, no action will be taken.\nDisable the external property for it to be extracted again."));
					} else {
						item->set_metadata(0, E.key);
						item->set_editable(0, true);
						item->set_checked(0, true);
						name = name.validate_filename();
						String path = p_path.path_join(name);
						if (external_extension_type->get_selected() == 0) {
							path += ".tres";
						} else {
							path += ".res";
						}

						item->set_text(1, path);
						if (FileAccess::exists(path)) {
							item->set_text(2, TTR("Warning: File exists"));
							item->set_tooltip_text(2, TTR("Existing file with the same name will be replaced."));
							item->set_icon(2, get_editor_theme_icon(SNAME("StatusWarning")));

						} else {
							item->set_text(2, TTR("Will create new file"));
							item->set_icon(2, get_editor_theme_icon(SNAME("StatusSuccess")));
						}

						item->add_button(1, get_editor_theme_icon(SNAME("Folder")));
					}

				} else {
					item->set_text(2, TTR("No import ID"));
					item->set_tooltip_text(2, TTR("Material has no name nor any other way to identify on re-import.\nPlease name it or ensure it is exported with an unique ID."));
					item->set_icon(2, get_editor_theme_icon(SNAME("StatusError")));
				}

				save_path_items.push_back(item);
			}

			external_paths->set_title(TTR("Extract Materials to Resource Files"));
			external_paths->set_ok_button_text(TTR("Extract"));
		} break;
		case ACTION_CHOOSE_MESH_SAVE_PATHS: {
			for (const KeyValue<String, MeshData> &E : mesh_map) {
				MeshData &md = mesh_map[E.key];

				TreeItem *item = external_path_tree->create_item(root);

				String name = md.mesh_node->get_text(0);

				item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
				item->set_icon(0, get_editor_theme_icon(SNAME("MeshItem")));
				item->set_text(0, name);

				if (md.has_import_id) {
					if (md.settings.has("save_to_file/enabled") && bool(md.settings["save_to_file/enabled"])) {
						item->set_text(2, TTR("Already Saving"));
						item->set_tooltip_text(2, TTR("This mesh already saves to an external resource, no action will be taken."));
					} else {
						item->set_metadata(0, E.key);
						item->set_editable(0, true);
						item->set_checked(0, true);
						name = name.validate_filename();
						String path = p_path.path_join(name);
						if (external_extension_type->get_selected() == 0) {
							path += ".tres";
						} else {
							path += ".res";
						}

						item->set_text(1, path);
						if (FileAccess::exists(path)) {
							item->set_text(2, TTR("Warning: File exists"));
							item->set_tooltip_text(2, TTR("Existing file with the same name will be replaced on import."));
							item->set_icon(2, get_editor_theme_icon(SNAME("StatusWarning")));

						} else {
							item->set_text(2, TTR("Will save to new file"));
							item->set_icon(2, get_editor_theme_icon(SNAME("StatusSuccess")));
						}

						item->add_button(1, get_editor_theme_icon(SNAME("Folder")));
					}

				} else {
					item->set_text(2, TTR("No import ID"));
					item->set_tooltip_text(2, TTR("Mesh has no name nor any other way to identify on re-import.\nPlease name it or ensure it is exported with an unique ID."));
					item->set_icon(2, get_editor_theme_icon(SNAME("StatusError")));
				}

				save_path_items.push_back(item);
			}

			external_paths->set_title(TTR("Set paths to save meshes as resource files on Reimport"));
			external_paths->set_ok_button_text(TTR("Set Paths"));
		} break;
		case ACTION_CHOOSE_ANIMATION_SAVE_PATHS: {
			for (const KeyValue<String, AnimationData> &E : animation_map) {
				AnimationData &ad = animation_map[E.key];

				TreeItem *item = external_path_tree->create_item(root);

				String name = ad.scene_node->get_text(0);

				item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
				item->set_icon(0, get_editor_theme_icon(SNAME("Animation")));
				item->set_text(0, name);

				if (ad.settings.has("save_to_file/enabled") && bool(ad.settings["save_to_file/enabled"])) {
					item->set_text(2, TTR("Already Saving"));
					item->set_tooltip_text(2, TTR("This animation already saves to an external resource, no action will be taken."));
				} else {
					item->set_metadata(0, E.key);
					item->set_editable(0, true);
					item->set_checked(0, true);
					name = name.validate_filename();
					String path = p_path.path_join(name);
					if (external_extension_type->get_selected() == 0) {
						path += ".tres";
					} else {
						path += ".res";
					}

					item->set_text(1, path);
					if (FileAccess::exists(path)) {
						item->set_text(2, TTR("Warning: File exists"));
						item->set_tooltip_text(2, TTR("Existing file with the same name will be replaced on import."));
						item->set_icon(2, get_editor_theme_icon(SNAME("StatusWarning")));

					} else {
						item->set_text(2, TTR("Will save to new file"));
						item->set_icon(2, get_editor_theme_icon(SNAME("StatusSuccess")));
					}

					item->add_button(1, get_editor_theme_icon(SNAME("Folder")));
				}

				save_path_items.push_back(item);
			}

			external_paths->set_title(TTR("Set paths to save animations as resource files on Reimport"));
			external_paths->set_ok_button_text(TTR("Set Paths"));

		} break;
	}

	external_paths->popup_centered_ratio();
}

void SceneImportSettingsScreen::_save_dir_confirm() {
	for (int i = 0; i < save_path_items.size(); i++) {
		TreeItem *item = save_path_items[i];
		if (!item->is_checked(0)) {
			continue; //ignore
		}
		String path = item->get_text(1);
		String uid_path = path;
		if (path.begins_with("uid://")) {
			path = ResourceUID::uid_to_path(uid_path);
		}
		if (!path.is_resource_file()) {
			continue;
		}

		String id = item->get_metadata(0);

		switch (current_action) {
			case ACTION_EXTRACT_MATERIALS: {
				ERR_CONTINUE(!material_map.has(id));
				MaterialData &md = material_map[id];

				Error err = ResourceSaver::save(md.material, path);
				if (err != OK) {
					EditorNode::get_singleton()->add_io_error(TTR("Can't make material external to file, write error:") + "\n\t" + path);
					continue;
				}
				uid_path = ResourceUID::path_to_uid(path);

				md.settings["use_external/enabled"] = true;
				md.settings["use_external/path"] = uid_path;
				md.settings["use_external/fallback_path"] = path;

			} break;
			case ACTION_CHOOSE_MESH_SAVE_PATHS: {
				ERR_CONTINUE(!mesh_map.has(id));
				MeshData &md = mesh_map[id];

				md.settings["save_to_file/enabled"] = true;
				md.settings["save_to_file/path"] = uid_path;
				md.settings["save_to_file/fallback_path"] = path;
			} break;
			case ACTION_CHOOSE_ANIMATION_SAVE_PATHS: {
				ERR_CONTINUE(!animation_map.has(id));
				AnimationData &ad = animation_map[id];

				ad.settings["save_to_file/enabled"] = true;
				ad.settings["save_to_file/path"] = uid_path;
				ad.settings["save_to_file/fallback_path"] = path;

			} break;
		}
	}

	if (current_action == ACTION_EXTRACT_MATERIALS) {
		// Keep the selected material open in its editable resource view after extraction.
		const String selected_material_id = selected_type == "Material" ? selected_id : String();
		if (_re_import() != OK) {
			return;
		}
		open_settings(base_path);
		if (!selected_material_id.is_empty() && material_map.has(selected_material_id)) {
			_select(material_tree, "Material", selected_material_id);
			data_mode->set_current_tab(2);
		}
	} else {
		scene_import_settings_data->notify_property_list_changed();
	}
}

SceneImportSettingsScreen::SceneImportSettingsScreen() {
	singleton = this;
	_resource_importer_scene = memnew(ResourceImporterScene("PackedScene"));

	VBoxContainer *main_vb = memnew(VBoxContainer);
	add_child(main_vb);
	main_vb->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	HBoxContainer *menu_hb = memnew(HBoxContainer);
	main_vb->add_child(menu_hb);

	action_menu = memnew(MenuButton);
	action_menu->set_text(TTR("Actions..."));
	// Style the MenuButton like a regular Button to make it more noticeable.
	action_menu->set_flat(false);
	action_menu->set_focus_mode(Control::FOCUS_ALL);
	menu_hb->add_child(action_menu);

	action_menu->get_popup()->add_item(TTR("Create Editable Materials…"), ACTION_EXTRACT_MATERIALS);
	action_menu->get_popup()->add_separator();
	action_menu->get_popup()->add_item(TTR("Set Animation Save Paths"), ACTION_CHOOSE_ANIMATION_SAVE_PATHS);
	action_menu->get_popup()->add_item(TTR("Set Mesh Save Paths"), ACTION_CHOOSE_MESH_SAVE_PATHS);

	action_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &SceneImportSettingsScreen::_menu_callback));

	// Shows the file currently loaded in the workspace.
	file_label = memnew(Label);
	file_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	file_label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	menu_hb->add_child(file_label);

	tree_split = memnew(HSplitContainer);
	main_vb->add_child(tree_split);
	tree_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	data_mode = memnew(TabContainer);
	tree_split->add_child(data_mode);
	data_mode->set_custom_minimum_size(Size2(300 * EDSCALE, 0));
	data_mode->set_theme_type_variation("TabContainerOdd");

	property_split = memnew(HSplitContainer);
	tree_split->add_child(property_split);
	property_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);

	scene_tree = memnew(Tree);
	scene_tree->set_name(TTR("Scene"));
	scene_tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	data_mode->add_child(scene_tree);
	scene_tree->connect("cell_selected", callable_mp(this, &SceneImportSettingsScreen::_scene_tree_selected));

	mesh_tree = memnew(Tree);
	mesh_tree->set_name(TTR("Meshes"));
	mesh_tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	data_mode->add_child(mesh_tree);
	mesh_tree->set_hide_root(true);
	mesh_tree->connect("cell_selected", callable_mp(this, &SceneImportSettingsScreen::_mesh_tree_selected));

	material_tree = memnew(Tree);
	material_tree->set_name(TTR("Materials"));
	material_tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	data_mode->add_child(material_tree);
	material_tree->connect("cell_selected", callable_mp(this, &SceneImportSettingsScreen::_material_tree_selected));

	material_tree->set_hide_root(true);

	VBoxContainer *vp_vb = memnew(VBoxContainer);
	vp_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vp_vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	property_split->add_child(vp_vb);

	// Plain container so the empty-state hint can overlay the viewport.
	Control *vp_view = memnew(Control);
	vp_view->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vp_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	vp_vb->add_child(vp_view);

	SubViewportContainer *vp_container = memnew(SubViewportContainer);
	vp_container->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	vp_container->set_custom_minimum_size(Size2(10, 10));
	vp_container->set_stretch(true);
	vp_container->connect(SceneStringName(gui_input), callable_mp(this, &SceneImportSettingsScreen::_viewport_input));
	vp_view->add_child(vp_container);

	base_viewport = memnew(SubViewport);
	vp_container->add_child(base_viewport);

	// Shown when the Model workspace has no imported file loaded (e.g. when
	// the user switches to the tab manually).
	empty_hint_panel = memnew(PanelContainer);
	empty_hint_panel->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	empty_hint_panel->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	vp_view->add_child(empty_hint_panel);

	Label *empty_hint_label = memnew(Label);
	empty_hint_label->set_text(TTR("The Model workspace is empty.\n\nOpen an imported 3D model from the Content Browser to preview and configure it here:\ndouble-click the model file, or right-click it and choose \"Advanced Import Settings\"."));
	empty_hint_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_hint_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	empty_hint_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	empty_hint_label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	empty_hint_panel->add_child(empty_hint_label);

	animation_preview = memnew(PanelContainer);
	animation_preview->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vp_vb->add_child(animation_preview);
	animation_preview->hide();

	HBoxContainer *animation_hbox = memnew(HBoxContainer);
	animation_preview->add_child(animation_hbox);

	animation_play_button = memnew(Button);
	animation_hbox->add_child(animation_play_button);
	animation_play_button->set_flat(true);
	animation_play_button->set_accessibility_name(TTRC("Selected Animation Play/Pause"));
	animation_play_button->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	animation_play_button->set_shortcut(ED_SHORTCUT("scene_import_settings/play_selected_animation", TTRC("Selected Animation Play/Pause"), Key::SPACE));
	animation_play_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_play_animation));

	animation_stop_button = memnew(Button);
	animation_hbox->add_child(animation_stop_button);
	animation_stop_button->set_flat(true);
	animation_stop_button->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	animation_stop_button->set_tooltip_text(TTR("Selected Animation Stop"));
	animation_stop_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_stop_current_animation));

	animation_slider = memnew(HSlider);
	animation_hbox->add_child(animation_slider);
	animation_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	animation_slider->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	animation_slider->set_max(1.0);
	animation_slider->set_step(1.0 / 100.0);
	animation_slider->set_value_no_signal(0.0);
	animation_slider->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	animation_slider->set_accessibility_name(TTRC("Animation"));
	animation_slider->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_animation_slider_value_changed));

	animation_toggle_skeleton_visibility = memnew(Button);
	animation_hbox->add_child(animation_toggle_skeleton_visibility);
	animation_toggle_skeleton_visibility->set_toggle_mode(true);
	animation_toggle_skeleton_visibility->set_theme_type_variation("FlatButton");
	animation_toggle_skeleton_visibility->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	animation_toggle_skeleton_visibility->set_tooltip_text(TTR("Toggle Animation Skeleton Visibility"));

	animation_toggle_skeleton_visibility->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_animation_update_skeleton_visibility));

	base_viewport->set_use_own_world_3d(true);
	// Raise the LOD screen-space threshold well above the default 1.0 px so
	// the preview picks lower generated LOD levels within the usable zoom
	// range (the preview camera is orthogonal and static otherwise).
	base_viewport->set_mesh_lod_threshold((float)GLOBAL_GET("rendering/mesh_lod/lod_change/threshold_pixels"));

	HBoxContainer *viewport_hbox = memnew(HBoxContainer);
	vp_container->add_child(viewport_hbox);
	viewport_hbox->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT, Control::PRESET_MODE_MINSIZE, 2);

	viewport_hbox->add_spacer();

	// Compact horizontal toolbar in the top-right corner of the preview with
	// all viewport controls, instead of a tall vertical stack of buttons.
	// A single full-height overlay column keeps the toolbar glued to the
	// top-right corner (end-aligned so the buttons hug the true right edge
	// even when the statistics overlay below is wider); the mesh statistics
	// overlay lives in the same column (at the bottom) so both always share
	// the right edge of the viewport.
	VBoxContainer *vb_overlay = memnew(VBoxContainer);
	vb_overlay->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	viewport_hbox->add_child(vb_overlay);

	HBoxContainer *hb_tools = memnew(HBoxContainer);
	hb_tools->add_theme_constant_override("separation", 4.0 * EDSCALE);
	hb_tools->set_alignment(BoxContainer::ALIGNMENT_END);
	vb_overlay->add_child(hb_tools);

	frame_button = memnew(Button);
	frame_button->set_text(TTR("Frame"));
	frame_button->set_theme_type_variation("PreviewLightButton");
	frame_button->set_focus_mode(Control::FOCUS_NONE);
	frame_button->set_tooltip_text(TTR("Frame Model (reset camera zoom, rotation and pan)"));
	frame_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_reset_camera));
	hb_tools->add_child(frame_button);

	wireframe_toggle = memnew(Button);
	wireframe_toggle->set_text(TTR("Wireframe"));
	wireframe_toggle->set_theme_type_variation("PreviewLightButton");
	wireframe_toggle->set_toggle_mode(true);
	wireframe_toggle->set_focus_mode(Control::FOCUS_NONE);
	wireframe_toggle->set_tooltip_text(TTR("Show mesh edges in the preview."));
	wireframe_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_wireframe_toggled));
	hb_tools->add_child(wireframe_toggle);

	skeleton_visibility_toggle = memnew(Button);
	skeleton_visibility_toggle->set_theme_type_variation("PreviewLightButton");
	skeleton_visibility_toggle->set_toggle_mode(true);
	skeleton_visibility_toggle->set_pressed(true);
	skeleton_visibility_toggle->set_focus_mode(Control::FOCUS_NONE);
	skeleton_visibility_toggle->set_tooltip_text(TTR("Toggle Skeleton Preview"));
	skeleton_visibility_toggle->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_update_skeleton_preview_visibility));
	hb_tools->add_child(skeleton_visibility_toggle);

	// LOD selection is placed below the preview, leaving the toolbar for camera tools.
	HBoxContainer *lod_preview_bar = memnew(HBoxContainer);
	vp_vb->add_child(lod_preview_bar);
	Label *lod_label = memnew(Label);
	lod_label->set_text(TTR("LOD Preview"));
	lod_preview_bar->add_child(lod_label);
	lod_auto_toggle = memnew(Button);
	lod_auto_toggle->set_text(TTR("Auto"));
	lod_auto_toggle->set_toggle_mode(true);
	lod_auto_toggle->set_pressed(true);
	lod_auto_toggle->set_tooltip_text(TTR("Automatic selection simulates the 1080p, 75-degree reference camera. The camera distance is measured to the nearest point of the mesh bounds. Drag the slider to inspect a specific level."));
	lod_auto_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_on_lod_auto_toggled));
	lod_preview_bar->add_child(lod_auto_toggle);
	lod_preview_slider = memnew(HSlider);
	lod_preview_slider->set_min(0);
	lod_preview_slider->set_max(1);
	lod_preview_slider->set_step(1);
	lod_preview_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_preview_slider->set_tooltip_text(TTR("Inspect generated LOD levels. This does not change game transitions."));
	lod_preview_slider->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_on_lod_preview_changed));
	lod_preview_bar->add_child(lod_preview_slider);
	lod_preview_label = memnew(Label);
	lod_preview_label->set_text(TTR("Auto"));
	lod_preview_bar->add_child(lod_preview_label);

	// Mesh statistics overlay (bottom-right corner of the preview):
	// triangle/polygon totals of the model and of the LOD level the preview
	// camera currently selects. Lives in the same overlay column as the
	// toolbar so it also hugs the right edge of the viewport.
	Control *stats_spacer = memnew(Control);
	stats_spacer->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	stats_spacer->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	vb_overlay->add_child(stats_spacer);

	mesh_stats_panel = memnew(PanelContainer);
	mesh_stats_panel->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	viewport_hbox->add_child(mesh_stats_panel);
	viewport_hbox->move_child(mesh_stats_panel, 0);
	mesh_stats_panel->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);

	// A quiet backdrop keeps statistics legible over light meshes. The container only insets the text from the
	// viewport edges. Plain white label text as requested.
	Ref<StyleBoxFlat> stats_bg;
	stats_bg.instantiate();
	stats_bg->set_bg_color(Color(0, 0, 0, 0.55));
	stats_bg->set_content_margin_individual(8.0 * EDSCALE, 4.0 * EDSCALE, 8.0 * EDSCALE, 4.0 * EDSCALE);
	mesh_stats_panel->add_theme_style_override(SceneStringName(panel), stats_bg);

	mesh_stats_label = memnew(Label);
	mesh_stats_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	mesh_stats_label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	mesh_stats_label->add_theme_color_override(SceneStringName(font_color), Color(1, 1, 1));
	mesh_stats_label->set_tooltip_text(TTR("Triangle and polygon counts of the previewed content and of the LOD level currently selected by the preview camera.\nWhile the whole scene is selected this reports the imported model; while a mesh is selected in the Mesh tab this reports that mesh only.\nTriangles is the number of triangles actually stored in the mesh after import triangulation.\nPolygons/Faces is the number of polygons in the source file before triangulation (a quad or an n-gon each count as a single polygon). It is only shown when the source format preserves it; formats that only store triangles (e.g. glTF/GLB) lose the original polygon count.\nThe single \"LOD x Triangles\" row shows only the level the preview camera currently selects and the triangles it actually draws: LOD 0 is the full-detail model; LOD 1.. are the decimated levels generated for the preview like on reimport when \"Generate LODs\" is enabled. Zooming out switches to lower levels, same rules as in-game rendering (when meshes pick different levels, the row lists the range and the sum of what is drawn)."));
	mesh_stats_panel->add_child(mesh_stats_label);

	mesh_stats_panel->hide();

	camera = memnew(Camera3D);
	base_viewport->add_child(camera);
	camera->make_current();
	vp_container->connect(SceneStringName(resized), callable_mp(this, &SceneImportSettingsScreen::_update_camera));

	if (GLOBAL_GET("rendering/lights_and_shadows/use_physical_light_units")) {
		camera_attributes.instantiate();
		camera->set_attributes(camera_attributes);
	}

	// Share the same sky, ambient fill and sun used by newly created FPS Levels.
	environment = create_fps_level_environment();
	// Leave the sky FOV at 0 so it follows the perspective camera FOV below.
	camera->set_environment(environment);
	preview_world_environment_node = memnew(WorldEnvironment);
	preview_world_environment_node->set_environment(environment);
	base_viewport->add_child(preview_world_environment_node);

	light1 = create_fps_level_sun();
	// Keep the sun in preview-world space. Camera orbiting changes the view,
	// not the light direction or the model's shadow.
	base_viewport->add_child(light1);

	// A lit floor receives the model's soft directional shadow. The fine and
	// major grid lines are generated in the shader, so they remain stable at
	// every preview size without adding objects to the imported scene.
	Ref<Shader> preview_floor_shader;
	preview_floor_shader.instantiate();
	preview_floor_shader->set_code(R"shader(
shader_type spatial;

uniform vec3 floor_color : source_color = vec3(0.16, 0.18, 0.21);
uniform vec3 horizon_color : source_color = vec3(0.43, 0.51, 0.61);
uniform vec3 minor_grid_color : source_color = vec3(0.22, 0.32, 0.45);
uniform vec3 major_grid_color : source_color = vec3(0.32, 0.45, 0.62);
uniform vec2 grid_origin = vec2(0.0);
uniform float grid_step = 0.5;
uniform float fade_distance = 30.0;

varying vec3 preview_world_position;

void vertex() {
	preview_world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

float grid_line(vec2 coord) {
	vec2 derivative = max(fwidth(coord), vec2(0.0001));
	vec2 distance_to_line = abs(fract(coord - 0.5) - 0.5) / derivative;
	return 1.0 - min(min(distance_to_line.x, distance_to_line.y), 1.0);
}

void fragment() {
	float distance_from_center = distance(preview_world_position.xz, grid_origin);
	float grid_fade = 1.0 - smoothstep(fade_distance * 0.45, fade_distance, distance_from_center);
	float minor = grid_line((preview_world_position.xz - grid_origin) / grid_step);
	float major = grid_line((preview_world_position.xz - grid_origin) / (grid_step * 10.0));
	vec3 color = mix(floor_color, horizon_color, 1.0 - grid_fade);
	color = mix(color, minor_grid_color, minor * 0.24 * grid_fade);
	color = mix(color, major_grid_color, major * 0.4 * grid_fade);
	ALBEDO = color;
	ROUGHNESS = 0.95;
	METALLIC = 0.0;
}
)shader");
	preview_floor_material.instantiate();
	preview_floor_material->set_shader(preview_floor_shader);
	preview_floor_material->set_shader_parameter("floor_color", Color(0.16, 0.18, 0.21));
	preview_floor_material->set_shader_parameter("horizon_color", get_fps_level_horizon_color());
	preview_floor_mesh.instantiate();
	preview_floor_mesh->set_size(Size2(preview_floor_size, preview_floor_size));
	preview_floor_mesh->set_material(preview_floor_material);
	preview_floor = memnew(MeshInstance3D);
	preview_floor->set_name("ModelPreviewFloor");
	preview_floor->set_mesh(preview_floor_mesh);
	preview_floor->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	preview_floor->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
	base_viewport->add_child(preview_floor);
	preview_floor->hide();

	{
		Ref<StandardMaterial3D> selection_mat;
		selection_mat.instantiate();
		selection_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
		selection_mat->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
		selection_mat->set_albedo(Color(1, 0.8, 1.0));

		Ref<SurfaceTool> st;
		st.instantiate();
		st->begin(Mesh::PRIMITIVE_LINES);

		AABB base_aabb;
		base_aabb.size = Vector3(1, 1, 1);

		for (int i = 0; i < 12; i++) {
			Vector3 a, b;
			base_aabb.get_edge(i, a, b);

			st->add_vertex(a);
			st->add_vertex(a.lerp(b, 0.2));
			st->add_vertex(b);
			st->add_vertex(b.lerp(a, 0.2));
		}

		selection_mesh.instantiate();
		st->commit(selection_mesh);
		selection_mesh->surface_set_material(0, selection_mat);

		node_selected = memnew(MeshInstance3D);
		node_selected->set_mesh(selection_mesh);
		node_selected->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		base_viewport->add_child(node_selected);
		node_selected->hide();
	}

	{
		mesh_preview = memnew(MeshInstance3D);
		base_viewport->add_child(mesh_preview);
		mesh_preview->hide();

		material_preview.instantiate();
	}

	{
		collider_mat.instantiate();
		collider_mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
		collider_mat->set_flag(StandardMaterial3D::FLAG_DISABLE_FOG, true);
		collider_mat->set_albedo(Color(0.5, 0.5, 1.0));
	}

	{
		bones_mesh_preview = memnew(MeshInstance3D);
		bones_mesh_preview->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		bones_mesh_preview->set_skeleton_path(NodePath());
		base_viewport->add_child(bones_mesh_preview);
	}

	inspector = memnew(EditorInspector);
	inspector->set_custom_minimum_size(Size2(300 * EDSCALE, 0));
	inspector->connect(SNAME("property_edited"), callable_mp(this, &SceneImportSettingsScreen::_inspector_property_edited));
	// Display the same tooltips as in the Import dock.
	inspector->set_object_class(ResourceImporterScene::get_class_static());
	inspector->set_use_doc_hints(true);
	inspector->set_theme_type_variation(SNAME("EditorInspectorForeground"));

	settings_tabs = memnew(TabContainer);
	settings_tabs->set_custom_minimum_size(Size2(320 * EDSCALE, 0));
	settings_tabs->set_h_size_flags(Control::SIZE_FILL);
	settings_tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	property_split->add_child(settings_tabs);
	property_split->connect(SNAME("dragged"), callable_mp(this, &SceneImportSettingsScreen::_property_split_dragged));
	_build_lod_settings_panel();
	ScrollContainer *lod_panel_scroll = memnew(ScrollContainer);
	lod_panel_scroll->set_name(TTR("LOD"));
	lod_panel_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_panel_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	lod_panel_scroll->add_child(lod_settings_panel);
	settings_tabs->add_child(lod_panel_scroll);
	int tab_index = 1;
	for (const String &tab_name : { TTR("Mesh"), TTR("Physics"), TTR("Lighting"), TTR("Details") }) {
		VBoxContainer *tab = memnew(VBoxContainer);
		tab->set_name(tab_name);
		settings_tabs->add_child(tab);
		Label *hint = memnew(Label);
		hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		hint->hide();
		tab->add_child(hint);
		settings_tab_hints[tab_index++] = hint;
	}
	inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->get_tab_control(4)->add_child(inspector);

	VBoxContainer *material_tab = memnew(VBoxContainer);
	material_tab->set_name(TTR("Material"));
	material_tab->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	material_tab->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->add_child(material_tab);

	material_preview_on_model_toggle = memnew(CheckButton);
	material_preview_on_model_toggle->set_text(TTR("Preview on full model"));
	material_preview_on_model_toggle->set_pressed_no_signal(material_preview_on_model);
	material_preview_on_model_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_material_preview_on_model_toggled));
	material_tab->add_child(material_preview_on_model_toggle);

	material_isolation_material.instantiate();
	material_isolation_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material_isolation_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material_isolation_material->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
	material_isolation_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
	material_isolation_material->set_albedo(Color(0.0, 0.0, 0.0, 0.0));

	material_isolate_toggle = memnew(CheckButton);
	material_isolate_toggle->set_text(TTR("Isolate Material"));
	material_isolate_toggle->set_tooltip_text(TTR("Hide other material surfaces in the full-model preview."));
	material_isolate_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_material_isolate_toggled));
	material_tab->add_child(material_isolate_toggle);

	preview_day_night_label = memnew(Label);
	preview_day_night_label->set_text(TTR("DayNightCycle: 12:00"));
	material_tab->add_child(preview_day_night_label);

	preview_day_night_slider = memnew(HSlider);
	preview_day_night_slider->set_min(0.0);
	preview_day_night_slider->set_max(24.0);
	preview_day_night_slider->set_step(0.05);
	preview_day_night_slider->set_tooltip_text(TTR("Set the preview time of day. This changes only the Model View lighting."));
	preview_day_night_slider->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_preview_day_night_time_changed));
	material_tab->add_child(preview_day_night_slider);

	preview_sun_animation_toggle = memnew(CheckButton);
	preview_sun_animation_toggle->set_text(TTR("Animate Sun"));
	preview_sun_animation_toggle->set_tooltip_text(TTR("Animate the preview's DayNightCycle. This changes only the Model View lighting."));
	preview_sun_animation_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_animation_toggled));
	material_tab->add_child(preview_sun_animation_toggle);

	preview_sun_cycle_duration_label = memnew(Label);
	material_tab->add_child(preview_sun_cycle_duration_label);
	preview_sun_cycle_duration_slider = memnew(HSlider);
	preview_sun_cycle_duration_slider->set_min(10.0);
	preview_sun_cycle_duration_slider->set_max(600.0);
	preview_sun_cycle_duration_slider->set_step(5.0);
	preview_sun_cycle_duration_slider->set_tooltip_text(TTR("Set the duration of one full day and night in the preview."));
	preview_sun_cycle_duration_slider->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_cycle_duration_changed));
	material_tab->add_child(preview_sun_cycle_duration_slider);

	preview_sun_rotation_label = memnew(Label);
	material_tab->add_child(preview_sun_rotation_label);
	preview_sun_rotation_slider = memnew(HSlider);
	preview_sun_rotation_slider->set_min(-180.0);
	preview_sun_rotation_slider->set_max(180.0);
	preview_sun_rotation_slider->set_step(1.0);
	preview_sun_rotation_slider->set_tooltip_text(TTR("Rotate the sun around the model horizontally. This changes only the Model View lighting."));
	preview_sun_rotation_slider->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_rotation_changed));
	material_tab->add_child(preview_sun_rotation_slider);

	preview_day_night_label->hide();
	preview_day_night_slider->hide();
	preview_sun_animation_toggle->hide();
	preview_sun_cycle_duration_label->hide();
	preview_sun_cycle_duration_slider->hide();
	preview_sun_rotation_label->hide();
	preview_sun_rotation_slider->hide();

	material_create_button = memnew(Button);
	material_create_button->set_text(TTR("Create editable material files…"));
	material_create_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_material_create_button_pressed));
	material_tab->add_child(material_create_button);

	material_save_button = memnew(Button);
	material_save_button->set_text(TTR("Save Material"));
	material_save_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_material_save_button_pressed));
	material_tab->add_child(material_save_button);

	material_inspector = memnew(EditorInspector);
	material_inspector->set_custom_minimum_size(Size2(300 * EDSCALE, 0));
	material_inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	material_inspector->set_use_doc_hints(true);
	material_inspector->set_theme_type_variation(SNAME("EditorInspectorForeground"));
	material_inspector->connect(SNAME("property_edited"), callable_mp(this, &SceneImportSettingsScreen::_material_inspector_property_edited));
	material_tab->add_child(material_inspector);
	_update_material_editor();

	scene_import_settings_data = memnew(SceneImportSettingsData);
	settings_tabs->connect(SNAME("tab_changed"), callable_mp(this, &SceneImportSettingsScreen::_settings_tab_changed));

	// Bottom action bar replacing the modal dialog's OK/Cancel buttons.
	HBoxContainer *confirm_hb = memnew(HBoxContainer);
	confirm_hb->add_spacer();
	main_vb->add_child(confirm_hb);

	reimport_button = memnew(Button);
	reimport_button->set_text(TTR("Apply Changes"));
	reimport_button->set_tooltip_text(TTR("Generate the imported model once with all saved settings."));
	reimport_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_reimport_button_pressed));
	confirm_hb->add_child(reimport_button);

	close_button = memnew(Button);
	close_button->set_text(TTR("Close"));
	close_button->set_tooltip_text(TTR("Close the model preview and return to the Level workspace"));
	close_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_close_screen));
	confirm_hb->add_child(close_button);

	external_paths = memnew(ConfirmationDialog);
	add_child(external_paths);
	external_path_tree = memnew(Tree);
	external_paths->add_child(external_path_tree);
	external_path_tree->connect("button_clicked", callable_mp(this, &SceneImportSettingsScreen::_browse_save_callback));
	external_paths->connect(SceneStringName(confirmed), callable_mp(this, &SceneImportSettingsScreen::_save_dir_confirm));
	external_path_tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	external_path_tree->set_columns(3);
	external_path_tree->set_column_titles_visible(true);
	external_path_tree->set_column_expand(0, true);
	external_path_tree->set_column_custom_minimum_width(0, 100 * EDSCALE);
	external_path_tree->set_column_title(0, TTR("Resource"));
	external_path_tree->set_column_expand(1, true);
	external_path_tree->set_column_custom_minimum_width(1, 100 * EDSCALE);
	external_path_tree->set_column_title(1, TTR("Path"));
	external_path_tree->set_column_expand(2, false);
	external_path_tree->set_column_custom_minimum_width(2, 200 * EDSCALE);
	external_path_tree->set_column_title(2, TTR("Status"));
	save_path = memnew(EditorFileDialog);
	save_path->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	HBoxContainer *extension_hb = memnew(HBoxContainer);
	save_path->get_vbox()->add_child(extension_hb);
	extension_hb->add_spacer();
	extension_hb->add_child(memnew(Label(TTR("Save Extension:"))));
	external_extension_type = memnew(OptionButton);
	extension_hb->add_child(external_extension_type);
	external_extension_type->add_item(TTR("Text: *.tres"));
	external_extension_type->add_item(TTR("Binary: *.res"));
	external_path_tree->set_hide_root(true);
	add_child(save_path);

	item_save_path = memnew(EditorFileDialog);
	item_save_path->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	item_save_path->add_filter("*.tres", TTR("Text Resource"));
	item_save_path->add_filter("*.res", TTR("Binary Resource"));
	add_child(item_save_path);
	item_save_path->connect("file_selected", callable_mp(this, &SceneImportSettingsScreen::_save_path_changed));

	save_path->connect("dir_selected", callable_mp(this, &SceneImportSettingsScreen::_save_dir_callback));

	update_view_timer = memnew(Timer);
	update_view_timer->set_wait_time(0.2);
	update_view_timer->set_one_shot(true);
	update_view_timer->connect("timeout", callable_mp(this, &SceneImportSettingsScreen::_update_view_gizmos));
	add_child(update_view_timer);

	lod_edit_timer = memnew(Timer);
	lod_edit_timer->set_wait_time(0.35);
	lod_edit_timer->set_one_shot(true);
	lod_edit_timer->connect("timeout", callable_mp(this, &SceneImportSettingsScreen::_finish_lod_edit_update));
	add_child(lod_edit_timer);

	// Restore the user's panel width, or use a 400 px default while enforcing
	// the inspector's 320 px minimum. Widths are stored before editor scaling.
	const real_t panel_minimum_width = settings_tabs->get_combined_minimum_size().x;
	const real_t saved_panel_width = MAX((real_t)EDITOR_DEF(MODEL_SETTINGS_PANEL_WIDTH_SETTING, MODEL_SETTINGS_PANEL_DEFAULT_WIDTH), MODEL_SETTINGS_PANEL_MIN_WIDTH) * EDSCALE;
	const real_t desired_panel_width = MAX(panel_minimum_width, saved_panel_width);
	property_split->set_split_offset(Math::round(panel_minimum_width - desired_panel_width));

	EditorNode::get_singleton()->register_hdr_viewport(base_viewport);
	
}

// Rebuilds the flat per-level LOD buttons in the viewport toolbar. Called
// only when the number of available levels changes (p_max_level = highest
// available level index, LOD 0 = full detail).
void SceneImportSettingsScreen::_update_lod_toolbar_buttons(int p_max_level) {
	lod_picker_levels = MAX(p_max_level, 0);
	if (lod_preview_slider) {
		lod_preview_slider->set_value_no_signal(MIN(lod_manual_level, lod_picker_levels));
		lod_preview_slider->set_max(MAX(lod_picker_levels, 1));
		lod_preview_slider->set_editable(lod_picker_levels > 0);
	}
	_update_lod_button_states();
}

void SceneImportSettingsScreen::_on_lod_preview_changed(double p_value) {
	_on_lod_level_selected(CLAMP((int)p_value, 0, lod_picker_levels));
}

// Refreshes the pressed state of the per-level buttons (and the LOD card
// select buttons) from the current lod_mode / lod_manual_level.
void SceneImportSettingsScreen::_update_lod_button_states() {
	const bool manual = lod_mode == LOD_MODE_MANUAL;
	if (lod_preview_label) {
		lod_preview_label->set_text(manual ? vformat(TTR("LOD %d"), lod_manual_level) : TTR("Auto"));
	}
	if (lod_preview_slider && manual) {
		lod_preview_slider->set_value_no_signal(lod_manual_level);
	}
	for (int i = 0; i < lod_level_buttons.size(); i++) {
		if (lod_level_buttons[i]) {
			lod_level_buttons[i]->set_pressed(manual && lod_manual_level == i);
		}
	}
	for (int i = 0; i < lod_card_select_buttons.size(); i++) {
		Button *btn = lod_card_select_buttons[i];
		if (btn) {
			const bool active = manual && lod_manual_level == i;
			btn->set_text(active ? TTR("Viewing") : TTR("View"));
			btn->set_disabled(active);
		}
	}
}

void SceneImportSettingsScreen::_on_lod_level_selected(int p_level) {
	// Manual level pin: swap the preview meshes for this level's geometry.
	lod_mode = LOD_MODE_MANUAL;
	lod_manual_level = p_level;
	if (selected_type == "Mesh") {
		lod_selected_level = p_level;
	}
	if (lod_auto_toggle != nullptr) {
		lod_auto_toggle->set_pressed_no_signal(false);
	}
	if (lod_bias_slider != nullptr) {
		lod_bias_slider->hide();
	}
	_update_lod_button_states();
	_apply_lod_preview();
	_update_mesh_stats();
	if (selected_type == "Mesh") {
		_update_lod_settings_panel();
	}
}

void SceneImportSettingsScreen::_on_lod_auto_toggled(bool p_enabled) {
	if (p_enabled) {
		lod_mode = LOD_MODE_AUTO;
		lod_manual_level = 0;
		if (lod_bias_slider != nullptr) {
			lod_bias_slider->show();
		}
	} else {
		// Turning Auto off pins the current level (LOD 0 by default) so a
		// single level can be inspected with the LOD buttons.
		lod_mode = LOD_MODE_MANUAL;
		if (lod_bias_slider != nullptr) {
			lod_bias_slider->hide();
		}
	}
	_update_lod_button_states();
	_apply_lod_preview();
	_update_mesh_stats();
}

// Default level entries mirror the already generated chain. The settings
// become explicit only after the user edits them.
Array SceneImportSettingsScreen::_get_custom_lod_levels() const {
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return Array();
	}
	const MeshData &md = mesh_map[selected_id];
	if (md.settings.has("lods/custom_levels") && bool(md.settings["lods/custom_levels"])) {
		return Array(md.settings["lods/levels"]).duplicate(true);
	}
	Array levels;
	for (int i = 1; i < stats_level_faces.size(); i++) {
		Dictionary level;
		const float keep = stats_level_faces[i - 1] ? (float)stats_level_faces[i] / stats_level_faces[i - 1] : 0.5f;
		level["keep"] = CLAMP(keep, 0.05f, 0.95f);
		level["transition_bias"] = 1.0f;
		levels.push_back(level);
	}
	return levels;
}

void SceneImportSettingsScreen::_set_custom_lod_levels(const Array &p_levels) {
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return;
	}
	MeshData &md = mesh_map[selected_id];
	if (!md.has_import_id) {
		return;
	}
	md.settings["lods/custom_levels"] = true;
	md.settings["lods/levels"] = p_levels.duplicate(true);
	md.lod_pending = true;
	md.lod_preview_dirty = true;
	lod_settings_pending = true;
	if (lod_transition_estimate) {
		lod_transition_estimate->set_text(TTR("Updating LOD preview…"));
	}
	if (lod_status_label) {
		lod_status_label->set_text(TTR("Updating LOD preview… Changes are pending. Apply Changes generates the project asset."));
	}
	_schedule_lod_edit_update();
}

void SceneImportSettingsScreen::_schedule_lod_edit_update() {
	if (lod_edit_timer) {
		lod_edit_timer->start();
	} else {
		_finish_lod_edit_update();
	}
}

void SceneImportSettingsScreen::_save_lod_config() {
	if (base_path.is_empty() || mesh_map.is_empty()) {
		return;
	}
	for (const KeyValue<String, MeshData> &entry : mesh_map) {
		Dictionary saved_settings;
		for (const StringName &key : { StringName("generate/lods"), StringName("lods/normal_merge_angle"), StringName("lods/count"), StringName("lods/reduction"), StringName("lods/custom_levels"), StringName("lods/levels") }) {
			if (entry.value.settings.has(key)) {
				saved_settings[String(key)] = entry.value.settings[key];
			}
		}
		if (saved_settings.is_empty()) {
			lod_config_data.erase(entry.key);
		} else {
			lod_config_data[entry.key] = saved_settings;
		}
	}
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("lod", "version", 1);
	config->set_value("lod", "meshes", lod_config_data);
	const Error error = config->save(base_path + ".lodcfg");
	if (error != OK) {
		ERR_PRINT("Unable to save model LOD settings: " + base_path + ".lodcfg");
	}
}

void SceneImportSettingsScreen::_finish_lod_edit_update() {
	_save_lod_config();
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return;
	}
	MeshData &md = mesh_map[selected_id];
	if (!md.lod_pending || md.lod_source_mesh.is_null()) {
		return;
	}
	for (const KeyValue<MeshInstance3D *, Ref<Mesh>> &entry : lod_swapped_meshes) {
		if (entry.key != nullptr && entry.value.is_valid()) {
			entry.key->set_mesh(entry.value);
		}
	}
	lod_swapped_meshes.clear();

	const int mode = md.settings.has("generate/lods") ? (int)md.settings["generate/lods"] : 0;
	const bool generate_lods = mode == 1 || (mode == 0 && (!defaults.has("meshes/generate_lods") || bool(defaults["meshes/generate_lods"])));
	Ref<ImporterMesh> preview_source = _duplicate_importer_mesh(md.lod_source_mesh);
	if (preview_source.is_null()) {
		return;
	}
	if (generate_lods) {
		const float merge_angle = md.settings.has("lods/normal_merge_angle") ? (float)md.settings["lods/normal_merge_angle"] : 20.0f;
		const int lod_count = md.settings.has("lods/count") ? (int)md.settings["lods/count"] : (defaults.has("meshes/lod_count") ? (int)defaults["meshes/lod_count"] : 0);
		const float reduction = md.settings.has("lods/reduction") ? (float)md.settings["lods/reduction"] : (defaults.has("meshes/lod_reduction") ? (float)defaults["meshes/lod_reduction"] : 0.5f);
		const bool custom_levels = md.settings.has("lods/custom_levels") && bool(md.settings["lods/custom_levels"]);
		const Array levels = custom_levels ? Array(md.settings["lods/levels"]) : Array();
		preview_source->generate_lods(merge_angle, md.lod_skin_pose_transforms, lod_count, reduction, custom_levels, levels);
	}

	md.live_preview_mesh = preview_source->get_mesh();
	md.lod_preview_dirty = false;
	md.preview_lod_map.clear();
	lod_preview_mesh_cache.clear();
	stats_selected_mesh.unref();
	mesh_preview->set_mesh(md.live_preview_mesh);
	_update_mesh_stats();
	const int max_level = MAX(stats_level_faces.size() - 1, 0);
	lod_manual_level = MIN(lod_manual_level, max_level);
	lod_selected_level = MIN(lod_selected_level, max_level);
	lod_auto_toggle->set_disabled(false);
	if (lod_mode == LOD_MODE_AUTO) {
		lod_auto_toggle->set_pressed_no_signal(true);
	} else {
		_apply_lod_preview();
	}
	_update_mesh_stats();
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_lod_select_level(int p_level) {
	lod_selected_level = p_level;
	if (p_level < stats_level_faces.size()) {
		_on_lod_level_selected(p_level);
	} else {
		_update_lod_settings_panel();
	}
}

void SceneImportSettingsScreen::_lod_add_level() {
	Array levels = _get_custom_lod_levels();
	if (levels.size() >= 10) {
		return;
	}
	Dictionary level;
	level["keep"] = 0.5;
	level["transition_bias"] = 1.0;
	levels.push_back(level);
	_set_custom_lod_levels(levels);
	lod_selected_level = levels.size();
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_lod_remove_level(int p_index) {
	Array levels = _get_custom_lod_levels();
	if (p_index < 0 || p_index >= levels.size()) {
		return;
	}
	const int removed_level = p_index + 1; // LOD 0 is the retained source mesh.
	MeshData &md = mesh_map[selected_id];
	// Keep the target of the next surviving level relative to its new
	// predecessor when the removed level was in the middle of the chain.
	if (p_index + 1 < levels.size()) {
		Dictionary removed = levels[p_index];
		Dictionary next = levels[p_index + 1];
		next["keep"] = CLAMP((double)removed.get("keep", 0.5) * (double)next.get("keep", 0.5), 0.05, 0.95);
		levels[p_index + 1] = next;
	}
	levels.remove_at(p_index);
	md.preview_lod_map.clear();
	_set_custom_lod_levels(levels);
	// Keep the same surviving LOD selected when an earlier row is removed.
	// If the selected row itself was removed, keep its index to select the
	// next level that shifted into its place, or fall back to the previous
	// level when the removed row was the last one.
	if (lod_selected_level > removed_level) {
		lod_selected_level--;
	} else if (lod_selected_level == removed_level) {
		lod_selected_level = MIN(removed_level, levels.size());
	}
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_lod_level_value_changed(double p_value, int p_index) {
	Array levels = _get_custom_lod_levels();
	if (p_index < 0 || p_index >= levels.size()) {
		return;
	}
	Dictionary level = levels[p_index];
	level["keep"] = CLAMP(p_value / 100.0, 0.05, 0.95);
	levels[p_index] = level;
	_set_custom_lod_levels(levels);
	if (lod_preset_selector) {
		lod_preset_selector->select(4);
	}
}

void SceneImportSettingsScreen::_lod_target_distance_changed(double p_value) {
	const int level_index = lod_selected_level - 1;
	if (p_value <= 0.0 || lod_target_anchor <= 0.0) {
		return;
	}
	Array levels = _get_custom_lod_levels();
	if (level_index < 0 || level_index >= levels.size()) {
		return;
	}
	// Adjust the stored per-level transition value while keeping the target
	// distance control as the only user-facing control for LOD timing.
	const double transition_bias = CLAMP(lod_target_anchor / p_value, 0.1, 10.0);
	Dictionary level = levels[level_index];
	level["transition_bias"] = transition_bias;
	levels[level_index] = level;
	_set_custom_lod_levels(levels);
	if (lod_target_distance_input) {
		lod_target_distance_input->set_value_no_signal(lod_target_anchor / transition_bias);
	}
	if (lod_preset_selector) {
		lod_preset_selector->select(4);
	}
}

void SceneImportSettingsScreen::_lod_generate_toggled(bool p_enabled) {
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return;
	}
	if (!mesh_map[selected_id].has_import_id) {
		_update_lod_settings_panel();
		return;
	}
	mesh_map[selected_id].settings["generate/lods"] = p_enabled ? 1 : 2;
	mesh_map[selected_id].lod_pending = true;
	mesh_map[selected_id].lod_preview_dirty = true;
	lod_settings_pending = true;
	if (lod_transition_estimate) {
		lod_transition_estimate->set_text(TTR("Updating LOD preview…"));
	}
	if (lod_status_label) {
		lod_status_label->set_text(TTR("Updating LOD preview… Changes are pending. Apply Changes generates the project asset."));
	}
	scene_import_settings_data->current["generate/lods"] = p_enabled ? 1 : 2;
	scene_import_settings_data->notify_property_list_changed();
	_schedule_lod_edit_update();
}

void SceneImportSettingsScreen::_settings_tab_changed(int p_tab) {
	if (!settings_tabs || !scene_import_settings_data || p_tab < 0 || p_tab > 5) {
		return;
	}
	if (p_tab == 5) {
		_update_material_editor();
		return;
	}
	if (p_tab == 0) {
		return;
	}
	Control *container = settings_tabs->get_tab_control(p_tab);
	if (!container) {
		return;
	}
	if (inspector->get_parent() != container) {
		inspector->reparent(container);
	}
	scene_import_settings_data->section_filter = p_tab;
	Label *hint = settings_tab_hints[p_tab];
	if (hint) {
		String message;
		if (p_tab == 1 && scene_import_settings_data->category != ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH) {
			message = TTR("Select a mesh resource in the scene tree or Meshes tab.");
		} else if (p_tab == 2 && scene_import_settings_data->category != ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH_3D_NODE) {
			message = TTR("Select a mesh instance node in the Scene tree to configure collision and physics.");
		} else if (p_tab == 3 && scene_import_settings_data->category != ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH && scene_import_settings_data->category != ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH_3D_NODE) {
			message = TTR("Select a mesh or mesh instance to configure shadows and lightmap options.");
		}
		hint->set_text(message);
		hint->set_visible(!message.is_empty());
	}
	scene_import_settings_data->notify_property_list_changed();
}

void SceneImportSettingsScreen::_property_split_dragged(int p_offset) {
	if (!settings_tabs) {
		return;
	}
	const real_t minimum_width = settings_tabs->get_combined_minimum_size().x;
	const real_t panel_width = MAX(minimum_width, minimum_width - p_offset) / EDSCALE;
	EditorSettings::get_singleton()->set_setting(MODEL_SETTINGS_PANEL_WIDTH_SETTING, Math::round(panel_width));
}

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

void SceneImportSettingsScreen::_setup_model_preview_lighting() {
	if (!base_viewport || !camera || !light1 || !preview_world_environment_node) {
		return;
	}

	if (preview_day_night_cycle != nullptr) {
		base_viewport->remove_child(preview_day_night_cycle);
		memdelete(preview_day_night_cycle);
		preview_day_night_cycle = nullptr;
	}
	if (preview_moon_light != nullptr) {
		base_viewport->remove_child(preview_moon_light);
		memdelete(preview_moon_light);
		preview_moon_light = nullptr;
	}

	environment = create_fps_level_environment();
	DirectionalLight3D *default_sun = create_fps_level_sun();
	light1->set_transform(default_sun->get_transform());
	light1->set_color(default_sun->get_color());
	light1->set_param(Light3D::PARAM_ENERGY, default_sun->get_param(Light3D::PARAM_ENERGY));
	light1->set_param(Light3D::PARAM_INDIRECT_ENERGY, default_sun->get_param(Light3D::PARAM_INDIRECT_ENERGY));
	light1->set_shadow(default_sun->has_shadow());
	light1->set_shadow_mode(default_sun->get_shadow_mode());
	light1->set_sky_mode(default_sun->get_sky_mode());
	memdelete(default_sun);

	Node *source_cycle = nullptr;
	DirectionalLight3D *source_sun = nullptr;
	WorldEnvironment *source_world_environment = nullptr;
	float source_time_of_day = 12.0f;
	Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();
	if (edited_scene != nullptr) {
		TypedArray<Node> world_environments = edited_scene->find_children("*", "WorldEnvironment", true, false);
		if (!world_environments.is_empty()) {
			Object *world_environment_object = world_environments[0];
			source_world_environment = Object::cast_to<WorldEnvironment>(world_environment_object);
		}

		TypedArray<Node> directional_lights = edited_scene->find_children("*", "DirectionalLight3D", true, false);
		if (!directional_lights.is_empty()) {
			Object *directional_light_object = directional_lights[0];
			source_sun = Object::cast_to<DirectionalLight3D>(directional_light_object);
		}

		TypedArray<Node> scene_nodes = edited_scene->find_children("*", "Node3D", true, false);
		for (int i = 0; i < scene_nodes.size(); i++) {
			Object *candidate_object = scene_nodes[i];
			Node *candidate = Object::cast_to<Node>(candidate_object);
			bool has_time_of_day = false;
			Variant time_value = candidate->get("time_of_day", &has_time_of_day);
			if (has_time_of_day) {
				source_cycle = candidate;
				source_time_of_day = time_value;
				break;
			}
		}
		if (source_cycle != nullptr) {
			bool has_sun_reference = false;
			Object *sun_reference = source_cycle->get("sun_light", &has_sun_reference);
			if (has_sun_reference) {
				source_sun = Object::cast_to<DirectionalLight3D>(sun_reference);
			}
			bool has_environment_reference = false;
			Object *environment_reference = source_cycle->get("world_environment", &has_environment_reference);
			if (has_environment_reference) {
				source_world_environment = Object::cast_to<WorldEnvironment>(environment_reference);
			}
		}
	}

	if (source_world_environment != nullptr && source_world_environment->get_environment().is_valid()) {
		Ref<Resource> duplicated_environment = source_world_environment->get_environment()->duplicate(true);
		Ref<Environment> source_environment_copy = duplicated_environment;
		if (source_environment_copy.is_valid()) {
			environment = source_environment_copy;
		}
	}
	preview_world_environment_node->set_environment(environment);
	camera->set_environment(environment);

	if (source_sun != nullptr) {
		light1->set_transform(source_sun->get_global_transform());
		light1->set_color(source_sun->get_color());
		light1->set_param(Light3D::PARAM_ENERGY, source_sun->get_param(Light3D::PARAM_ENERGY));
		light1->set_param(Light3D::PARAM_INDIRECT_ENERGY, source_sun->get_param(Light3D::PARAM_INDIRECT_ENERGY));
		light1->set_param(Light3D::PARAM_SPECULAR, source_sun->get_param(Light3D::PARAM_SPECULAR));
		light1->set_param(Light3D::PARAM_SHADOW_BIAS, source_sun->get_param(Light3D::PARAM_SHADOW_BIAS));
		light1->set_param(Light3D::PARAM_SHADOW_BLUR, source_sun->get_param(Light3D::PARAM_SHADOW_BLUR));
		light1->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, source_sun->get_param(Light3D::PARAM_SHADOW_MAX_DISTANCE));
		light1->set_shadow(source_sun->has_shadow());
		light1->set_shadow_mode(source_sun->get_shadow_mode());
		light1->set_sky_mode(source_sun->get_sky_mode());
	}

	if (source_cycle != nullptr) {
		Ref<Script> cycle_script = source_cycle->get_script();
		if (cycle_script.is_valid()) {
			preview_moon_light = memnew(DirectionalLight3D);
			preview_moon_light->set_name("PreviewMoonLight");
			preview_moon_light->set_color(Color(0.68, 0.80, 0.98));
			preview_moon_light->set_param(Light3D::PARAM_ENERGY, 0.0);
			preview_moon_light->set_param(Light3D::PARAM_SHADOW_BIAS, 0.05);
			preview_moon_light->set_param(Light3D::PARAM_SHADOW_BLUR, 2.5);
			preview_moon_light->set_shadow(true);
			preview_moon_light->hide();
			base_viewport->add_child(preview_moon_light);

			preview_day_night_cycle = memnew(Node3D);
			preview_day_night_cycle->set_name("PreviewDayNightCycle");
			preview_day_night_cycle->set_script(cycle_script);
			preview_day_night_cycle->set("sun_light", light1);
			preview_day_night_cycle->set("moon_light", preview_moon_light);
			preview_day_night_cycle->set("world_environment", preview_world_environment_node);
			for (const StringName &property : { SNAME("latitude_tilt"), SNAME("azimuth_offset"), SNAME("sun_max_energy"), SNAME("moon_max_energy"), SNAME("moon_light_color") }) {
				bool has_property = false;
				Variant value = source_cycle->get(property, &has_property);
				if (has_property) {
					preview_day_night_cycle->set(property, value);
				}
			}
			bool has_azimuth_offset = false;
			preview_day_night_base_azimuth_offset = preview_day_night_cycle->get("azimuth_offset", &has_azimuth_offset);
			if (!has_azimuth_offset) {
				preview_day_night_base_azimuth_offset = 0.0f;
			}
			bool has_moon_color = false;
			Color moon_color = preview_day_night_cycle->get("moon_light_color", &has_moon_color);
			if (has_moon_color) {
				preview_moon_light->set_color(moon_color);
			}
			preview_day_night_cycle->set("cycle_active", false);
			preview_day_night_cycle->set("editor_preview_cycle", false);
			preview_day_night_cycle->set("time_scale", 1.0f);
			preview_day_night_cycle->connect(SNAME("time_changed"), callable_mp(this, &SceneImportSettingsScreen::_preview_day_night_cycle_time_changed));
			base_viewport->add_child(preview_day_night_cycle);

			const float preview_time = Math::fposmod((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "day_night_time", source_time_of_day), 24.0f);
			const float cycle_duration_seconds = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_cycle_duration_seconds", 60.0), 10.0f, 600.0f);
			const float sun_rotation_degrees = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_rotation_degrees", 0.0), -180.0f, 180.0f);
			const bool animate_sun = EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_animation_enabled", false);
			preview_day_night_cycle->set("azimuth_offset", preview_day_night_base_azimuth_offset + sun_rotation_degrees);
			preview_day_night_cycle->set("day_duration_minutes", cycle_duration_seconds / 60.0f);
			preview_day_night_cycle->set("time_of_day", preview_time);
			preview_day_night_cycle->set("editor_preview_cycle", animate_sun);
			preview_day_night_slider->set_value_no_signal(preview_time);
			_update_preview_day_night_label(preview_time);
			preview_sun_animation_toggle->set_pressed_no_signal(animate_sun);
			preview_sun_cycle_duration_slider->set_value_no_signal(cycle_duration_seconds);
			_update_preview_sun_cycle_duration_label(cycle_duration_seconds);
			preview_sun_rotation_slider->set_value_no_signal(sun_rotation_degrees);
			_update_preview_sun_rotation_label(sun_rotation_degrees);
			preview_day_night_label->show();
			preview_day_night_slider->show();
			preview_sun_animation_toggle->show();
			preview_sun_cycle_duration_label->show();
			preview_sun_cycle_duration_slider->show();
			preview_sun_rotation_label->show();
			preview_sun_rotation_slider->show();
			return;
		}
	}

	preview_day_night_label->hide();
	preview_day_night_slider->hide();
	preview_sun_animation_toggle->hide();
	preview_sun_cycle_duration_label->hide();
	preview_sun_cycle_duration_slider->hide();
	preview_sun_rotation_label->hide();
	preview_sun_rotation_slider->hide();
}

void SceneImportSettingsScreen::_preview_day_night_time_changed(double p_value) {
	if (preview_day_night_cycle == nullptr) {
		return;
	}
	const float time_of_day = Math::fposmod((float)p_value, 24.0f);
	preview_day_night_cycle->set("time_of_day", time_of_day);
	preview_day_night_slider->set_value_no_signal(time_of_day);
	_update_preview_day_night_label(time_of_day);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "day_night_time", time_of_day);
}

void SceneImportSettingsScreen::_preview_day_night_cycle_time_changed(double p_value, const String &) {
	preview_day_night_slider->set_value_no_signal(p_value);
	_update_preview_day_night_label(p_value);
}

void SceneImportSettingsScreen::_preview_sun_animation_toggled(bool p_enabled) {
	if (preview_day_night_cycle == nullptr) {
		return;
	}
	preview_day_night_cycle->set("editor_preview_cycle", p_enabled);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_animation_enabled", p_enabled);
}

void SceneImportSettingsScreen::_preview_sun_cycle_duration_changed(double p_value) {
	if (preview_day_night_cycle == nullptr) {
		return;
	}
	const float duration_seconds = CLAMP((float)p_value, 10.0f, 600.0f);
	preview_day_night_cycle->set("day_duration_minutes", duration_seconds / 60.0f);
	preview_sun_cycle_duration_slider->set_value_no_signal(duration_seconds);
	_update_preview_sun_cycle_duration_label(duration_seconds);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_cycle_duration_seconds", duration_seconds);
}

void SceneImportSettingsScreen::_preview_sun_rotation_changed(double p_value) {
	if (preview_day_night_cycle == nullptr) {
		return;
	}
	const float rotation_degrees = CLAMP((float)p_value, -180.0f, 180.0f);
	preview_day_night_cycle->set("azimuth_offset", preview_day_night_base_azimuth_offset + rotation_degrees);
	bool has_time_of_day = false;
	const float time_of_day = preview_day_night_cycle->get("time_of_day", &has_time_of_day);
	if (has_time_of_day) {
		preview_day_night_cycle->set("time_of_day", time_of_day);
	}
	preview_sun_rotation_slider->set_value_no_signal(rotation_degrees);
	_update_preview_sun_rotation_label(rotation_degrees);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_rotation_degrees", rotation_degrees);
}

void SceneImportSettingsScreen::_update_preview_day_night_label(double p_value) {
	if (!preview_day_night_label) {
		return;
	}
	const float time_of_day = Math::fposmod((float)p_value, 24.0f);
	const int hour = (int)time_of_day;
	int minute = Math::round((time_of_day - hour) * 60.0f);
	int display_hour = hour;
	if (minute >= 60) {
		minute = 0;
		display_hour = (display_hour + 1) % 24;
	}
	preview_day_night_label->set_text(vformat(TTR("DayNightCycle: %02d:%02d"), display_hour, minute));
}

void SceneImportSettingsScreen::_update_preview_sun_cycle_duration_label(double p_value) {
	if (!preview_sun_cycle_duration_label) {
		return;
	}
	preview_sun_cycle_duration_label->set_text(vformat(TTR("Cycle Duration: %d s"), (int64_t)Math::round(p_value)));
}

void SceneImportSettingsScreen::_update_preview_sun_rotation_label(double p_value) {
	if (!preview_sun_rotation_label) {
		return;
	}
	preview_sun_rotation_label->set_text(vformat(TTR("Sun Rotation: %d deg"), (int64_t)Math::round(p_value)));
}

void SceneImportSettingsScreen::_material_inspector_property_edited(const String &) {
	if (selected_type != "Material" || !material_map.has(selected_id)) {
		return;
	}
	MaterialData &md = material_map[selected_id];
	md.modified = true;
	material_save_button->set_disabled(false);
}

void SceneImportSettingsScreen::_lod_merge_angle_changed(double p_value) {
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return;
	}
	MeshData &md = mesh_map[selected_id];
	if (!md.has_import_id) {
		return;
	}
	md.settings["lods/normal_merge_angle"] = p_value;
	md.lod_pending = true;
	md.lod_preview_dirty = true;
	lod_settings_pending = true;
	if (lod_transition_estimate) {
		lod_transition_estimate->set_text(TTR("Updating LOD preview…"));
	}
	scene_import_settings_data->current["lods/normal_merge_angle"] = p_value;
	scene_import_settings_data->notify_property_list_changed();
	if (lod_status_label) {
		lod_status_label->set_text(TTR("Updating LOD preview… Changes are pending. Apply Changes generates the project asset."));
	}
	_schedule_lod_edit_update();
}

void SceneImportSettingsScreen::_lod_preset_selected(int p_index) {
	if (p_index == 0) {
		_lod_reset_levels();
		return;
	}
	// Each fraction is relative to the preceding level.
	const double keep = p_index == 1 ? 0.7 : p_index == 2 ? 0.5 : 0.35;
	Array levels;
	for (int i = 0; i < 3; i++) {
		Dictionary level;
		level["keep"] = keep;
		level["transition_bias"] = 1.0;
		levels.push_back(level);
	}
	_set_custom_lod_levels(levels);
	MeshData &md = mesh_map[selected_id];
	md.preview_lod_map.clear();
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_lod_reset_levels() {
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return;
	}
	mesh_map[selected_id].settings.erase("lods/custom_levels");
	mesh_map[selected_id].settings.erase("lods/levels");
	mesh_map[selected_id].preview_lod_map.clear();
	mesh_map[selected_id].live_preview_mesh.unref();
	stats_selected_mesh.unref();
	lod_auto_toggle->set_disabled(false);
	lod_picker_levels = -1;
	_apply_lod_preview();
	_update_mesh_stats();
	mesh_map[selected_id].lod_pending = true;
	mesh_map[selected_id].lod_preview_dirty = true;
	lod_settings_pending = true;
	_schedule_lod_edit_update();
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_build_lod_settings_panel() {
	lod_settings_panel = memnew(VBoxContainer);
	lod_settings_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_settings_panel->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	Label *header = memnew(Label);
	header->set_text(TTR("LOD Settings"));
	lod_settings_panel->add_child(header);
}

void SceneImportSettingsScreen::_gather_lod_level_settings() {
	lod_level_settings.clear();
	for (int i = 0; i < stats_level_faces.size(); i++) {
		LODLevelSettings level;
		level.level = i;
		level.triangle_count = stats_level_faces[i];
		lod_level_settings.push_back(level);
	}
}

void SceneImportSettingsScreen::_update_lod_settings_panel() {
	if (!lod_settings_panel) {
		return;
	}
	while (lod_settings_panel->get_child_count() > 1) {
		Node *child = lod_settings_panel->get_child(1);
		lod_settings_panel->remove_child(child);
		child->queue_free();
	}
	lod_card_select_buttons.clear();
	lod_status_label = nullptr;
	lod_transition_estimate = nullptr;
	lod_target_distance_input = nullptr;
	lod_target_anchor = 0.0;
	lod_preset_selector = nullptr;
	_gather_lod_level_settings();
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		Label *hint = memnew(Label);
		hint->set_text(TTR("Select a mesh to configure its LOD levels."));
		hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		lod_settings_panel->add_child(hint);
		return;
	}
	MeshData &md = mesh_map[selected_id];
	const int mode = md.settings.has("generate/lods") ? (int)md.settings["generate/lods"] : 0;
	const bool enabled = mode == 1 || (mode == 0 && (!defaults.has("meshes/generate_lods") || bool(defaults["meshes/generate_lods"])));
	CheckButton *generate = memnew(CheckButton);
	generate->set_text(TTR("Generate LODs"));
	generate->set_pressed_no_signal(enabled);
	generate->set_disabled(!md.has_import_id);
	generate->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_lod_generate_toggled));
	lod_settings_panel->add_child(generate);
	HBoxContainer *angle_row = memnew(HBoxContainer);
	lod_settings_panel->add_child(angle_row);
	Label *angle_label = memnew(Label);
	angle_label->set_text(TTR("Normal Merge Angle"));
	angle_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	angle_row->add_child(angle_label);
	SpinBox *angle = memnew(SpinBox);
	angle->set_min(0); angle->set_max(180); angle->set_step(1);
	angle->set_editable(md.has_import_id);
	angle->set_suffix(" deg");
	angle->set_value_no_signal(md.settings.has("lods/normal_merge_angle") ? (double)md.settings["lods/normal_merge_angle"] : 20.0);
	angle->set_tooltip_text(TTR("Lower values preserve more geometry edges during LOD simplification."));
	angle->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_lod_merge_angle_changed));
	angle_row->add_child(angle);
	HBoxContainer *preset_row = memnew(HBoxContainer);
	lod_settings_panel->add_child(preset_row);
	Label *preset_label = memnew(Label);
	preset_label->set_text(TTR("LOD Preset"));
	preset_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preset_row->add_child(preset_label);
	OptionButton *preset = memnew(OptionButton);
	lod_preset_selector = preset;
	preset->add_item(TTR("Automatic"));
	preset->add_item(TTR("Quality"));
	preset->add_item(TTR("Balanced"));
	preset->add_item(TTR("Performance"));
	preset->add_item(TTR("Custom"));
	preset->select(md.settings.has("lods/custom_levels") && bool(md.settings["lods/custom_levels"]) ? 4 : 0);
	preset->set_item_disabled(4, true);
	preset->set_disabled(!md.has_import_id);
	preset->connect(SceneStringName(item_selected), callable_mp(this, &SceneImportSettingsScreen::_lod_preset_selected));
	preset_row->add_child(preset);

	const Array levels = _get_custom_lod_levels();
	lod_selected_level = CLAMP(lod_selected_level, 0, levels.size());
	lod_status_label = memnew(Label);
	lod_status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	String lod_status;
	if (md.lod_preview_dirty) {
		lod_status = TTR("Updating LOD preview… Changes are pending. Apply Changes generates the project asset when you are ready.");
	} else if (lod_settings_pending) {
		lod_status = TTR("Preview is up to date. LOD settings are saved with this model; Apply Changes generates the project asset when you are ready.");
	} else {
		lod_status = TTR("Statistics from the current imported mesh.");
	}
	if (!md.has_import_id) {
		lod_status = TTR("This mesh has no stable import ID. Name it in the source file before saving per-mesh LOD settings.");
	} else if (!lod_settings_pending && enabled && md.settings.has("lods/custom_levels") && bool(md.settings["lods/custom_levels"]) && levels.size() > MAX(stats_level_faces.size() - 1, 0)) {
		lod_status = vformat(TTR("Generated %d of %d requested LODs. The mesh could not be simplified further."), MAX(stats_level_faces.size() - 1, 0), levels.size());
	}
	lod_status_label->set_text(lod_status);
	lod_settings_panel->add_child(lod_status_label);

	GridContainer *table = memnew(GridContainer);
	table->set_columns(4);
	table->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_settings_panel->add_child(table);
	for (const String &title : { TTR("Level"), TTR("Triangles"), TTR("Vertices"), String() }) {
		Label *heading = memnew(Label);
		heading->set_text(title);
		heading->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		table->add_child(heading);
	}
	for (int i = 0; i <= levels.size(); i++) {
		Button *select = memnew(Button);
		select->set_text(vformat(TTR("LOD %d"), i));
		select->set_toggle_mode(true);
		select->set_pressed_no_signal(i == lod_selected_level);
		select->set_tooltip_text(i == 0 ? TTR("Base mesh") : TTR("Select this level to edit its settings and preview it."));
		select->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_lod_select_level).bind(i));
		table->add_child(select);
		const bool known = !md.lod_preview_dirty && i < stats_level_faces.size();
		Label *triangles = memnew(Label);
		triangles->set_text(known ? _format_stats_count(stats_level_faces[i]) : (md.lod_preview_dirty ? TTR("Updating…") : TTR("Unavailable")));
		if (!known) {
			triangles->set_tooltip_text(TTR("This LOD level is being generated in the preview, or the mesh could not be simplified further."));
		}
		triangles->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		table->add_child(triangles);
		Label *vertices = memnew(Label);
		vertices->set_text(known && i < stats_level_vertices.size() ? _format_stats_count(stats_level_vertices[i]) : (md.lod_preview_dirty ? TTR("Updating…") : TTR("Unavailable")));
		if (!known) {
			vertices->set_tooltip_text(TTR("This LOD level is being generated in the preview, or the mesh could not be simplified further."));
		}
		vertices->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		table->add_child(vertices);
		Button *remove = memnew(Button);
		remove->set_text(TTR("Remove"));
		remove->set_disabled(i == 0 || !md.has_import_id);
		if (i > 0) {
			remove->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_lod_remove_level).bind(i - 1));
		}
		table->add_child(remove);
	}
	Button *add = memnew(Button);
	add->set_text(TTR("+ Add LOD"));
	add->set_disabled(levels.size() >= 10 || !md.has_import_id);
	add->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_lod_add_level));
	lod_settings_panel->add_child(add);

	PanelContainer *details = memnew(PanelContainer);
	lod_settings_panel->add_child(details);
	VBoxContainer *content = memnew(VBoxContainer);
	details->add_child(content);
	Label *selected = memnew(Label);
	selected->set_text(vformat(TTR("Selected LOD %d"), lod_selected_level));
	content->add_child(selected);
	if (lod_selected_level == 0) {
		Label *base_hint = memnew(Label);
		base_hint->set_text(md.lod_preview_dirty ? TTR("Updating LOD preview…") : TTR("The original mesh is always retained."));
		base_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		content->add_child(base_hint);
	} else {
		const int i = lod_selected_level - 1;
		Dictionary level = levels[i];
		HBoxContainer *row = memnew(HBoxContainer);
		content->add_child(row);
		Label *keep_label = memnew(Label);
		keep_label->set_text(TTR("Retain triangles (%)"));
		keep_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_child(keep_label);
		SpinBox *keep = memnew(SpinBox);
		keep->set_min(5); keep->set_max(95); keep->set_step(1);
		keep->set_editable(md.has_import_id);
		keep->set_value_no_signal((double)level.get("keep", 0.5) * 100.0);
		keep->set_suffix("%");
		keep->set_tooltip_text(TTR("Target percentage of triangles retained from the preceding LOD. The simplifier may stop before reaching the target."));
		keep->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_lod_level_value_changed).bind(i));
		HSlider *keep_slider = memnew(HSlider);
		keep_slider->set_min(5); keep_slider->set_max(95); keep_slider->set_step(1);
		keep_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		keep_slider->set_editable(md.has_import_id);
		keep_slider->set_tooltip_text(TTR("Adjust the percentage of triangles retained at this level."));
		keep->share(keep_slider);
		row->add_child(keep_slider);
		row->add_child(keep);
		lod_transition_estimate = memnew(Label);
		lod_transition_estimate->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		String estimate;
		double reference_distance = 0.0;
		const float pixel_threshold = GLOBAL_GET("rendering/mesh_lod/lod_change/threshold_pixels");
		if (md.lod_preview_dirty) {
			estimate = TTR("Updating LOD preview…");
		} else if (pixel_threshold <= 0.0f) {
			estimate = TTR("Automatic LOD selection is disabled by the project pixel threshold.");
		} else {
			// Renderer uses error * model_scale / (distance * projection
			// multiplier) <= pixel threshold / viewport width. Estimate
			// for a unit-scale instance with per-instance LOD bias 1.
			const Projection reference = Projection::create_perspective(75.0, 16.0 / 9.0, 0.05, 10000.0);
			const double divisor = pixel_threshold * reference.get_lod_multiplier();
			double near_distance = 0.0;
			double far_distance = 0.0;
			bool found = false;
			for (const MeshSurfaceStats &surface : stats_selected.surfaces) {
				if (i >= surface.lod_edge_lengths.size()) {
					continue;
				}
				const double distance = surface.lod_edge_lengths[i] * 1920.0 / divisor;
				if (!Math::is_finite(distance) || distance < 0.0) {
					continue;
				}
				if (!found) {
					near_distance = distance;
					far_distance = distance;
					found = true;
				} else {
					near_distance = MIN(near_distance, distance);
					far_distance = MAX(far_distance, distance);
				}
			}
			if (found) {
				reference_distance = near_distance;
			}
			if (!found) {
				estimate = TTR("No imported transition is available for this level.");
			} else if (Math::is_equal_approx(near_distance, far_distance)) {
				estimate = vformat(TTR("Reference switch distance: approximately %s m"), String::num(near_distance, 1));
			} else {
				estimate = vformat(TTR("Reference switch distance: approximately %s-%s m across surfaces"), String::num(near_distance, 1), String::num(far_distance, 1));
			}
		}
		lod_transition_estimate->set_text(estimate);
		lod_transition_estimate->set_tooltip_text(TTR("Estimate for a 1920 x 1080 perspective viewport, vertical FOV 75 degrees, unit mesh scale, instance LOD Bias 1, and the current project LOD pixel threshold. Distance is measured from the camera to the nearest point of the mesh bounding box. Different surfaces can switch at different distances. Actual results depend on camera, scale, viewport resolution and runtime settings."));
		content->add_child(lod_transition_estimate);
		if (reference_distance > 0.0 && md.has_import_id) {
			// For meshes with multiple surfaces, the target controls the
			// earliest transition across surfaces.
			lod_target_anchor = reference_distance * (double)level.get("transition_bias", 1.0);
			HBoxContainer *target_row = memnew(HBoxContainer);
			content->add_child(target_row);
			Label *target_label = memnew(Label);
			target_label->set_text(TTR("Reference target (m)"));
			target_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			target_row->add_child(target_label);
			lod_target_distance_input = memnew(SpinBox);
			lod_target_distance_input->set_min(MAX(lod_target_anchor / 10.0, 0.001));
			lod_target_distance_input->set_max(MAX(lod_target_anchor / 0.1, 0.01));
			lod_target_distance_input->set_step(0.01);
			lod_target_distance_input->set_value_no_signal(reference_distance);
			lod_target_distance_input->set_tooltip_text(TTR("Approximate target for the earliest surface switch under the reference camera. This updates LOD timing and the live preview; Apply Changes generates the project asset when ready."));
			lod_target_distance_input->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_lod_target_distance_changed));
			HSlider *target_slider = memnew(HSlider);
			target_slider->set_min(lod_target_distance_input->get_min());
			target_slider->set_max(lod_target_distance_input->get_max());
			target_slider->set_step(0.01);
			target_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			target_slider->set_editable(md.has_import_id);
			target_slider->set_tooltip_text(lod_target_distance_input->get_tooltip_text());
			lod_target_distance_input->share(target_slider);
			target_row->add_child(target_slider);
			target_row->add_child(lod_target_distance_input);
		}
	}

	Button *reset = memnew(Button);
	reset->set_text(TTR("Use automatic levels"));
	reset->set_disabled(!md.has_import_id);
	reset->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_lod_reset_levels));
	lod_settings_panel->add_child(reset);
}

SceneImportSettingsScreen::~SceneImportSettingsScreen() {
	memdelete(scene_import_settings_data);
}
