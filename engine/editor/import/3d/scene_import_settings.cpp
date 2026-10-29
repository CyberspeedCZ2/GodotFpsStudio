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
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/sky.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/surface_tool.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"
#include "servers/text/text_server.h"

// Camera framing margin applied around the fitted content so it never
// touches the viewport edges.
static constexpr real_t FRAME_MARGIN = 1.15;
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

void SceneImportSettingsScreen::_fill_material(Tree *p_tree, const Ref<Material> &p_material, TreeItem *p_parent) {
	String import_id;
	bool has_import_id = false;

	if (p_material->has_meta("import_id")) {
		import_id = p_material->get_meta("import_id");
		has_import_id = true;
	} else if (!p_material->get_name().is_empty()) {
		import_id = p_material->get_name();
		has_import_id = true;
	} else if (unnamed_material_name_map.has(p_material)) {
		import_id = unnamed_material_name_map[p_material];
	} else {
		import_id = "@MATERIAL:" + itos(material_map.size());
		unnamed_material_name_map[p_material] = import_id;
	}

	bool created = false;
	if (!material_map.has(import_id)) {
		MaterialData md;
		created = true;
		md.has_import_id = has_import_id;
		md.material = p_material;

		_load_default_subresource_settings(md.settings, "materials", import_id, ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MATERIAL);

		material_map[import_id] = md;
	}

	MaterialData &material_data = material_map[import_id];
	ERR_FAIL_COND(p_material != material_data.material);

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
		_re_import();
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
	AABB camera_aabb;

	float rot_x = cam_rot_x;
	float rot_y = cam_rot_y;
	float zoom = cam_zoom;
	Vector2 pan;
if (selected_type == "Node" || selected_type == "Animation" || selected_type.is_empty()) {
	camera_aabb = contents_aabb;
	pan = cam_pan;
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

Vector3 center = camera_aabb.get_center();
float camera_size = camera_aabb.get_longest_axis_size();
if (camera_size <= 0.0) {
	// Degenerate AABB; fall back to the default preview cube size.
	camera_size = 2.0;
}

frame_size = camera_size;

// Small margin so the fitted content never touches the viewport edges.
camera->set_orthogonal(camera_size * zoom * FRAME_MARGIN, 0.0001, camera_size * 2);

Transform3D xf;
xf.basis = Basis(Vector3(0, 1, 0), rot_y) * Basis(Vector3(1, 0, 0), rot_x);
xf.origin = center;
xf.translate_local(0, 0, camera_size);
// Pan the camera in its own local plane (world units) so the content
// follows the cursor.
xf.translate_local(pan.x, pan.y, 0);

	camera->set_transform(xf);

	_update_mesh_stats();
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

			String mesh_id = importer_mesh->get_meta("import_id", importer_mesh->get_name());
			if (!mesh_id.is_empty() && base_subresource_settings.has("meshes")) {
				Dictionary meshes_dict = base_subresource_settings["meshes"];
				if (meshes_dict.has(mesh_id)) {
					Dictionary mesh_settings = meshes_dict[mesh_id];
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
				importer_mesh->generate_lods(merge_angle, skin_pose_transform_array, lod_count, lod_reduction);
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
		selected_mesh = mesh_map[selected_id].mesh;
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

	// The meshes to evaluate: every previewed mesh (scene mode) or just the
	// selected one (per-mesh mode, matching what the preview shows).
	Vector<const MeshStats *> evaluated_meshes;
	if (selected_stats != nullptr) {
		evaluated_meshes.push_back(selected_stats);
	} else {
		for (const KeyValue<Ref<Mesh>, MeshStats> &E : mesh_stats_map) {
			evaluated_meshes.push_back(&E.value);
		}
	}

	// Screen-space LOD threshold, mirroring the rules used by the forward
	// renderers (RendererViewport::draw_viewport divides the pixel threshold
	// by the viewport width): screen_size = edge_length / lod_multiplier is
	// compared against threshold / viewport_width. Orthogonal cameras use a
	// fixed distance of 1, so zooming out lowers the projected edge size.
	const Size2 viewport_size = base_viewport->get_size();
	const float lod_threshold_pixels = base_viewport->get_mesh_lod_threshold();
	const float lod_distance_multiplier = camera->get_camera_projection().get_lod_multiplier();
	const bool lods_disabled = base_viewport->get_debug_draw() == Viewport::DEBUG_DRAW_DISABLE_LOD;
	const real_t lod_threshold = (viewport_size.width > 0 && lod_threshold_pixels > 0.0) ? lod_threshold_pixels / viewport_size.width : 0.0;

	// Evaluate the LOD level the renderer currently picks for the preview
	// camera, and the triangle count it actually draws with, using the same
	// rules as RendererRD::MeshStorage::mesh_surface_get_lod (screen size vs
	// threshold, ascending edge lengths).
	// Highest LOD level available across the evaluated surfaces (LOD 0 is
	// the full-detail base level).
	int levels_count = 1;
	for (const MeshStats *ms : evaluated_meshes) {
		for (const MeshSurfaceStats &surface : ms->surfaces) {
			levels_count = MAX(levels_count, surface.lod_edge_lengths.size() + 1);
		}
	}
	stats_level_faces.clear();
	stats_level_faces.resize(levels_count);
	stats_level_faces.fill(0);

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
	for (const MeshStats *ms : evaluated_meshes) {
		for (const MeshSurfaceStats &surface : ms->surfaces) {
			// LOD level the renderer picks for this surface right now, and
			// the triangle count it draws with.
			int lod = 0;
			uint64_t index_count = surface.index_count;
			uint64_t vertex_count = surface.vertex_count;
			if (manual_lod) {
				if (lod_manual_level > 0 && lod_manual_level <= surface.lod_edge_lengths.size()) {
					lod = lod_manual_level;
					index_count = surface.lod_index_counts[lod - 1];
					vertex_count = surface.lod_vertex_counts[lod - 1];
				}
			} else if (!lods_disabled && surface.lod_edge_lengths.size() > 0 && lod_threshold > 0.0) {
				for (int i = 0; i < surface.lod_edge_lengths.size(); i++) {
					const real_t screen_size = surface.lod_edge_lengths[i] / lod_distance_multiplier;
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
			stats_level_faces.write[0] += _mesh_faces_from_primitive(surface.primitive, surface.index_count, surface.vertex_count);
			for (int i = 0; i < surface.lod_edge_lengths.size(); i++) {
				stats_level_faces.write[i + 1] += _mesh_faces_from_primitive(surface.primitive, surface.lod_index_counts[i], surface.lod_vertex_counts[i]);
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

	// Keep the LOD level buttons and the inspector LOD cards in sync with
	// the available levels (the guards prevent rebuilds on every camera
	// move, so slider edits are never interrupted).
	if (lod_buttons_container != nullptr && lod_picker_levels != levels_count - 1) {
		_update_lod_toolbar_buttons(levels_count - 1);
	}
	if (lod_settings_panel != nullptr && lod_level_settings.size() != stats_level_faces.size()) {
		_update_lod_settings_panel();
	}

	mesh_stats_label->set_text(text);
	mesh_stats_panel->show();
}

void SceneImportSettingsScreen::_lod_bias_changed(double p_value) {
	lod_bias = CLAMP((float)p_value, 0.25f, 4.0f);
	// Coarser levels switch in sooner as the screen-space threshold grows.
	base_viewport->set_mesh_lod_threshold(16.0f * lod_bias);
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
		if (p_level > edge_lengths.size()) {
			continue;
		}
		Vector<int> lod_indices = lods[edge_lengths[p_level - 1]];
		if (lod_indices.is_empty()) {
			continue;
		}
		arrays[Mesh::ARRAY_INDEX] = lod_indices;
		preview->add_surface_from_arrays(p_mesh->surface_get_primitive_type(s), arrays);
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
		base_viewport->set_mesh_lod_threshold(16.0f * lod_bias);
		return;
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
	if (scene) {
		_cleanup();
		memdelete(scene);
		scene = nullptr;
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

	mesh_set.clear();
	animation_map.clear();
	material_map.clear();
	unnamed_material_name_map.clear();
	mesh_map.clear();
	node_map.clear();
	defaults.clear();

	mesh_preview->hide();

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
	base_viewport->set_mesh_lod_threshold(16.0);
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
			if (p_from != scene_tree) {
				md.scene_node->uncollapse_tree();
				md.scene_node->select(0);
				scene_tree->ensure_cursor_is_visible();
			}
		}

		mesh_preview->set_mesh(md.mesh);
		mesh_preview->show();
		_reset_animation();

		material_tree->deselect_all();

		scene_import_settings_data->settings = &md.settings;
		scene_import_settings_data->category = ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH;
	} else if (p_type == "Material") {
		node_selected->hide();
		if (Object::cast_to<Node3D>(scene)) {
			Object::cast_to<Node3D>(scene)->hide();
		}

		mesh_preview->show();
		_reset_animation();

		MaterialData &md = material_map[p_id];

		material_preview->set_material(md.material);
		mesh_preview->set_mesh(material_preview);

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

void SceneImportSettingsScreen::_on_light_1_switch_pressed() {
	light1->set_visible(light_1_switch->is_pressed());
}

void SceneImportSettingsScreen::_on_light_2_switch_pressed() {
	light2->set_visible(light_2_switch->is_pressed());
}

void SceneImportSettingsScreen::_on_light_rotate_switch_pressed() {
	bool light_top_level = !light_rotate_switch->is_pressed();
	light1->set_as_top_level_keep_local(light_top_level);
	light2->set_as_top_level_keep_local(light_top_level);
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

void SceneImportSettingsScreen::_re_import() {
	HashMap<StringName, Variant> main_settings;

	main_settings = scene_import_settings_data->current;
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
}

void SceneImportSettingsScreen::_update_empty_state() {
	// The empty-state hint is shown whenever the workspace has no model loaded
	// (e.g. when the user switches to the "Model" tab manually).
	empty_hint_panel->set_visible(scene == nullptr);
}

void SceneImportSettingsScreen::_close_screen() {
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
	unnamed_material_name_map.clear();
	mesh_map.clear();
	node_map.clear();
	defaults.clear();
	base_subresource_settings.clear();

	scene_tree->clear();
	material_tree->clear();
	mesh_tree->clear();
	// Hidden roots (same as after _update_scene()).
	material_tree->create_item();
	mesh_tree->create_item();

	bones_mesh_preview->set_mesh(Ref<Mesh>());
	mesh_preview->set_mesh(Ref<Mesh>());
	mesh_preview->hide();
	animation_preview->hide();
	inspector->edit(nullptr);

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
	base_viewport->set_mesh_lod_threshold(16.0);
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
	_re_import();
	// The workspace screen stays open; reload the preview with the fresh
	// import result so the user can continue tweaking the settings.
	open_settings(base_path, current_import_type);
}

void SceneImportSettingsScreen::_update_theme_item_cache() {
	Control::_update_theme_item_cache();
	theme_cache.light_1_icon = get_editor_theme_icon(SNAME("MaterialPreviewLight1"));
	theme_cache.light_2_icon = get_editor_theme_icon(SNAME("MaterialPreviewLight2"));
	theme_cache.rotate_icon = get_editor_theme_icon(SNAME("PreviewRotate"));
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

			light_1_switch->set_button_icon(theme_cache.light_1_icon);
			light_2_switch->set_button_icon(theme_cache.light_2_icon);
			light_rotate_switch->set_button_icon(theme_cache.rotate_icon);

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
		//as this happens right now, the scene needs to be saved and reimported.
		_re_import();
		open_settings(base_path);
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

	action_menu->get_popup()->add_item(TTR("Extract Materials"), ACTION_EXTRACT_MATERIALS);
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
	base_viewport->set_mesh_lod_threshold(16.0);

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
	frame_button->set_theme_type_variation("PreviewLightButton");
	frame_button->set_focus_mode(Control::FOCUS_NONE);
	frame_button->set_tooltip_text(TTR("Frame Model (reset camera zoom, rotation and pan)"));
	frame_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_reset_camera));
	hb_tools->add_child(frame_button);

	skeleton_visibility_toggle = memnew(Button);
	skeleton_visibility_toggle->set_theme_type_variation("PreviewLightButton");
	skeleton_visibility_toggle->set_toggle_mode(true);
	skeleton_visibility_toggle->set_pressed(true);
	skeleton_visibility_toggle->set_focus_mode(Control::FOCUS_NONE);
	skeleton_visibility_toggle->set_tooltip_text(TTR("Toggle Skeleton Preview"));
	skeleton_visibility_toggle->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_update_skeleton_preview_visibility));
	hb_tools->add_child(skeleton_visibility_toggle);

	light_rotate_switch = memnew(Button);
	light_rotate_switch->set_theme_type_variation("PreviewLightButton");
	light_rotate_switch->set_toggle_mode(true);
	light_rotate_switch->set_pressed(true);
	light_rotate_switch->set_focus_mode(Control::FOCUS_NONE);
	light_rotate_switch->set_tooltip_text(TTR("Rotate Lights With Model"));
	light_rotate_switch->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_on_light_rotate_switch_pressed));
	hb_tools->add_child(light_rotate_switch);

	light_1_switch = memnew(Button);
	light_1_switch->set_theme_type_variation("PreviewLightButton");
	light_1_switch->set_toggle_mode(true);
	light_1_switch->set_pressed(true);
	light_1_switch->set_focus_mode(Control::FOCUS_NONE);
	light_1_switch->set_tooltip_text(TTR("Primary Light"));
	light_1_switch->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_on_light_1_switch_pressed));
	hb_tools->add_child(light_1_switch);

	light_2_switch = memnew(Button);
	light_2_switch->set_theme_type_variation("PreviewLightButton");
	light_2_switch->set_toggle_mode(true);
	light_2_switch->set_pressed(true);
	light_2_switch->set_focus_mode(Control::FOCUS_NONE);
	light_2_switch->set_tooltip_text(TTR("Secondary Light"));
	light_2_switch->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_on_light_2_switch_pressed));
	hb_tools->add_child(light_2_switch);

	// LOD preview controls (UE5-style): an "Auto" toggle with the switching
	// bias, then one flat button per LOD level that swaps the preview meshes
	// for the actual decimated geometry of that level.
	lod_auto_toggle = memnew(Button);
	lod_auto_toggle->set_text(TTR("Auto"));
	lod_auto_toggle->set_flat(true);
	lod_auto_toggle->set_toggle_mode(true);
	lod_auto_toggle->set_pressed(true);
	lod_auto_toggle->set_focus_mode(Control::FOCUS_NONE);
	lod_auto_toggle->set_tooltip_text(TTR("Automatic LOD switching: the renderer picks the level from the preview camera (the bias scales the switching threshold).\nTurn Auto off and use the LOD 0..N buttons to inspect a single level up close."));
	lod_auto_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_on_lod_auto_toggled));
	hb_tools->add_child(lod_auto_toggle);

	lod_bias_slider = memnew(HSlider);
	lod_bias_slider->set_min(0.25);
	lod_bias_slider->set_max(4.0);
	lod_bias_slider->set_step(0.05);
	lod_bias_slider->set_value_no_signal(1.0);
	lod_bias_slider->set_custom_minimum_size(Size2(80.0 * EDSCALE, 0));
	lod_bias_slider->set_focus_mode(Control::FOCUS_NONE);
	lod_bias_slider->set_tooltip_text(TTR("LOD bias for Auto mode: scales the screen-space threshold used to switch LOD levels.\nAbove 1.0 switches to coarser levels sooner; below 1.0 keeps finer levels longer."));
	lod_bias_slider->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_lod_bias_changed));
	hb_tools->add_child(lod_bias_slider);

	// One flat button per LOD level, rebuilt whenever the available levels
	// change (see _update_lod_toolbar_buttons).
	lod_buttons_container = memnew(HBoxContainer);
	lod_buttons_container->add_theme_constant_override("separation", 2.0 * EDSCALE);
	hb_tools->add_child(lod_buttons_container);
	_update_lod_toolbar_buttons(0);

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
	vb_overlay->add_child(mesh_stats_panel);

	// No visible background; the container only insets the text from the
	// viewport edges. Plain white label text as requested.
	Ref<StyleBoxFlat> stats_bg;
	stats_bg.instantiate();
	stats_bg->set_bg_color(Color(0, 0, 0, 0));
	stats_bg->set_content_margin_individual(0, 0, 8.0 * EDSCALE, 8.0 * EDSCALE);
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

	if (GLOBAL_GET("rendering/lights_and_shadows/use_physical_light_units")) {
		camera_attributes.instantiate();
		camera->set_attributes(camera_attributes);
	}

	// Use a grayscale gradient sky to avoid skewing the preview towards a specific color,
	// but still allow shaded areas to be easily distinguished (using the ambient and reflected light).
	// This also helps the user orient themselves in the preview, since the bottom of the sky is black
	// and the top of the sky is white.
	procedural_sky_material.instantiate();
	procedural_sky_material->set_sky_top_color(Color(1, 1, 1));
	procedural_sky_material->set_sky_horizon_color(Color(0.5, 0.5, 0.5));
	procedural_sky_material->set_ground_horizon_color(Color(0.5, 0.5, 0.5));
	procedural_sky_material->set_ground_bottom_color(Color(0, 0, 0));
	procedural_sky_material->set_sky_curve(2.0);
	procedural_sky_material->set_ground_curve(0.5);
	// Hide the sun from the sky.
	procedural_sky_material->set_sun_angle_max(0.0);
	sky.instantiate();
	sky->set_material(procedural_sky_material);
	environment.instantiate();
	environment->set_background(Environment::BG_SKY);
	environment->set_sky(sky);
	// A custom FOV must be specified, as an orthogonal camera is used for the preview.
	environment->set_sky_custom_fov(50.0);
	camera->set_environment(environment);

	light1 = memnew(DirectionalLight3D);
	light1->set_transform(Transform3D(Basis::looking_at(Vector3(-1, -1, -1))));
	light1->set_shadow(true);
	camera->add_child(light1);

	light2 = memnew(DirectionalLight3D);
	light2->set_transform(Transform3D(Basis::looking_at(Vector3(0, 1, 0), Vector3(0, 0, 1))));
	light2->set_color(Color(0.5f, 0.5f, 0.5f));
	camera->add_child(light2);

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

	// Inspector column: the generic import options on top and the per-LOD
	// level configuration cards below (screen-size thresholds with the
	// estimated switch distances).
	VBoxContainer *inspector_column = memnew(VBoxContainer);
	inspector_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	inspector_column->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	inspector_column->add_child(inspector);

	_build_lod_settings_panel();

	ScrollContainer *lod_panel_scroll = memnew(ScrollContainer);
	lod_panel_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_panel_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	lod_panel_scroll->set_custom_minimum_size(Size2(0, 200 * EDSCALE));
	lod_panel_scroll->add_child(lod_settings_panel);
	inspector_column->add_child(lod_panel_scroll);

	property_split->add_child(inspector_column);

	scene_import_settings_data = memnew(SceneImportSettingsData);

	// Bottom action bar replacing the modal dialog's OK/Cancel buttons.
	HBoxContainer *confirm_hb = memnew(HBoxContainer);
	confirm_hb->add_spacer();
	main_vb->add_child(confirm_hb);

	reimport_button = memnew(Button);
	reimport_button->set_text(TTR("Reimport"));
	reimport_button->set_tooltip_text(TTR("Reimport the model with the current settings"));
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

	EditorNode::get_singleton()->register_hdr_viewport(base_viewport);
	
}

// Rebuilds the flat per-level LOD buttons in the viewport toolbar. Called
// only when the number of available levels changes (p_max_level = highest
// available level index, LOD 0 = full detail).
void SceneImportSettingsScreen::_update_lod_toolbar_buttons(int p_max_level) {
	if (lod_buttons_container == nullptr) {
		return;
	}

	for (Button *btn : lod_level_buttons) {
		if (btn) {
			btn->queue_free();
		}
	}
	lod_level_buttons.clear();
	lod_picker_levels = p_max_level;

	for (int i = 0; i <= p_max_level; i++) {
		Button *lod_btn = memnew(Button);
		lod_btn->set_flat(true);
		lod_btn->set_text(vformat(TTR("LOD %d"), i));
		lod_btn->set_toggle_mode(true);
		lod_btn->set_focus_mode(Control::FOCUS_NONE);
		lod_btn->set_tooltip_text(vformat(TTR("Preview only LOD %d: swap the preview meshes for the decimated geometry of this level."), i));
		lod_btn->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_on_lod_level_selected).bind(i));
		lod_buttons_container->add_child(lod_btn);
		lod_level_buttons.push_back(lod_btn);
	}

	_update_lod_button_states();
}

// Refreshes the pressed state of the per-level buttons (and the LOD card
// select buttons) from the current lod_mode / lod_manual_level.
void SceneImportSettingsScreen::_update_lod_button_states() {
	const bool manual = lod_mode == LOD_MODE_MANUAL;
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
	if (lod_auto_toggle != nullptr) {
		lod_auto_toggle->set_pressed_no_signal(false);
	}
	if (lod_bias_slider != nullptr) {
		lod_bias_slider->hide();
	}
	_update_lod_button_states();
	_apply_lod_preview();
	_update_mesh_stats();
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

// Gather LOD level statistics from the mesh stats. User-edited screen-size
// thresholds are preserved across rebuilds while the level count is stable.
void SceneImportSettingsScreen::_gather_lod_level_settings() {
	Vector<LODLevelSettings> previous = lod_level_settings;
	lod_level_settings.clear();

	if (stats_level_faces.size() == 0) {
		return;
	}

	// Default screen sizes halve per level: LOD 0 = 1.0, LOD 1 = 0.5,
	// LOD 2 = 0.25, LOD 3 = 0.125, etc.
	for (int i = 0; i < stats_level_faces.size(); i++) {
		LODLevelSettings settings;
		settings.level = i;
		settings.triangle_count = stats_level_faces[i];
		settings.vertex_count = 0;
		settings.screen_size = Math::pow(0.5f, (float)i);
		if (i < previous.size()) {
			// Preserve user-edited thresholds across rebuilds.
			settings.screen_size = previous[i].screen_size;
		}
		settings.distance = _calculate_distance_from_screen_size(settings.screen_size);
		settings.enabled = true;

		if (i > 0 && stats_level_faces[0] > 0) {
			settings.reduction_percent = 100.0f * (1.0f - (float)settings.triangle_count / (float)stats_level_faces[0]);
		} else {
			settings.reduction_percent = 0.0f;
		}

		lod_level_settings.push_back(settings);
	}
}

// Calculate approximate distance from screen size threshold
float SceneImportSettingsScreen::_calculate_distance_from_screen_size(float p_screen_size) {
	// FOV-based distance calculation
	// Assuming 75° FOV and 1m tall object
	constexpr float DEFAULT_FOV = 75.0f;
	constexpr float OBJECT_HEIGHT = 1.0f;
	
	if (p_screen_size <= 0.001f) {
		return 1000.0f; // Far plane
	}
	
	float fov_radians = Math::deg_to_rad(DEFAULT_FOV);
	float distance = (OBJECT_HEIGHT / (2.0f * p_screen_size)) / Math::tan(fov_radians / 2.0f);
	
	return distance;
}

// Build LOD Settings Panel for the inspector
void SceneImportSettingsScreen::_build_lod_settings_panel() {
	if (lod_settings_panel != nullptr) {
		return; // Already built
	}

	lod_settings_panel = memnew(VBoxContainer);
	lod_settings_panel->set_name(TTR("LOD Settings"));
	lod_settings_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_settings_panel->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);

	Label *header = memnew(Label);
	header->set_text(TTR("LOD Levels"));
	header->add_theme_font_size_override("font_size", 15);
	lod_settings_panel->add_child(header);

	// The actual per-LOD cards are created dynamically in
	// _update_lod_settings_panel() whenever the available levels change.
	_update_lod_settings_panel();
}

// Update LOD Settings Panel with current mesh data
void SceneImportSettingsScreen::_update_lod_settings_panel() {
	if (lod_settings_panel == nullptr) {
		return;
	}

	// Clear existing LOD cards (keep header)
	while (lod_settings_panel->get_child_count() > 1) {
		Node *child = lod_settings_panel->get_child(1);
		lod_settings_panel->remove_child(child);
		child->queue_free();
	}
	lod_distance_labels.clear();
	lod_card_select_buttons.clear();

	// Gather current LOD statistics
	_gather_lod_level_settings();

	if (lod_level_settings.size() == 0) {
		Label *no_lods = memnew(Label);
		no_lods->set_text(TTR("No LOD levels. Enable \"Generate LODs\" in the import options and reimport to create them."));
		lod_settings_panel->add_child(no_lods);
		return;
	}
	
	// Create a card for each LOD level
	for (int i = 0; i < lod_level_settings.size(); i++) {
		const LODLevelSettings &settings = lod_level_settings[i];
		
		// Card container
		PanelContainer *card = memnew(PanelContainer);
		card->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
		lod_settings_panel->add_child(card);
		
		VBoxContainer *card_vb = memnew(VBoxContainer);
		card->add_child(card_vb);
		
		// Header with level name and select button
		HBoxContainer *header_hb = memnew(HBoxContainer);
		card_vb->add_child(header_hb);
		
		Label *level_label = memnew(Label);
		level_label->set_text(vformat(TTR("LOD %d"), i));
		level_label->add_theme_font_size_override("font_size", 14);
		level_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		header_hb->add_child(level_label);
		
		Button *select_btn = memnew(Button);
		select_btn->set_focus_mode(Control::FOCUS_NONE);
		select_btn->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_on_lod_level_selected).bind(i));
		header_hb->add_child(select_btn);
		lod_card_select_buttons.push_back(select_btn);
		
		// Geometry stats
		Label *geo_stats = memnew(Label);
		String stats_text = vformat(TTR("Triangles: %s"), _format_stats_count(settings.triangle_count));
		if (i > 0) {
			stats_text += vformat(" (↓%.1f%%)", settings.reduction_percent);
		}
		geo_stats->set_text(stats_text);
		card_vb->add_child(geo_stats);
		
		// Screen Size control
		Label *screen_label = memnew(Label);
		screen_label->set_text(TTR("Screen Size Threshold:"));
		card_vb->add_child(screen_label);
		
		HBoxContainer *screen_hb = memnew(HBoxContainer);
		card_vb->add_child(screen_hb);
		
		HSlider *screen_slider = memnew(HSlider);
		screen_slider->set_min(0.01);
		screen_slider->set_max(1.0);
		screen_slider->set_step(0.01);
		screen_slider->set_value(settings.screen_size);
		screen_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		screen_slider->set_custom_minimum_size(Size2(200 * EDSCALE, 0));
		screen_slider->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_on_lod_screen_size_changed).bind(i));
		screen_hb->add_child(screen_slider);
		
		SpinBox *screen_spin = memnew(SpinBox);
		screen_spin->set_min(0.01);
		screen_spin->set_max(1.0);
		screen_spin->set_step(0.01);
		screen_spin->set_value(settings.screen_size);
		screen_spin->share(screen_slider);
		screen_hb->add_child(screen_spin);
		
		Label *percent_label = memnew(Label);
		percent_label->set_text("%");
		screen_hb->add_child(percent_label);
		
		// Distance display (live-updated from the slider callback)
		Label *distance_label = memnew(Label);
		distance_label->set_text(vformat(TTR("Estimated Distance: ~%.1f m"), settings.distance));
		distance_label->add_theme_font_size_override("font_size", 10);
		card_vb->add_child(distance_label);
		lod_distance_labels.push_back(distance_label);
		
		// Help text for LOD 0
		if (i == 0) {
			Label *hint = memnew(Label);
			hint->set_text(TTR("This is the highest quality LOD (base mesh)"));
			hint->add_theme_font_size_override("font_size", 10);
			hint->add_theme_color_override("font_color", Color(0.7, 0.7, 0.7));
			card_vb->add_child(hint);
		}
		
		// Add spacing between cards
		if (i < lod_level_settings.size() - 1) {
			lod_settings_panel->add_child(memnew(HSeparator));
		}
	}

	_update_lod_button_states();
}

// Callback when a per-level screen size slider changes: update the stored
// threshold and its estimated switch distance without rebuilding the panel
// (so the drag is not interrupted).
void SceneImportSettingsScreen::_on_lod_screen_size_changed(float p_value, int p_level) {
	if (p_level < 0 || p_level >= lod_level_settings.size()) {
		return;
	}

	lod_level_settings.write[p_level].screen_size = p_value;
	lod_level_settings.write[p_level].distance = _calculate_distance_from_screen_size(p_value);

	if (p_level < lod_distance_labels.size() && lod_distance_labels[p_level] != nullptr) {
		lod_distance_labels[p_level]->set_text(vformat(TTR("Estimated Distance: ~%.1f m"), lod_level_settings[p_level].distance));
	}
}

SceneImportSettingsScreen::~SceneImportSettingsScreen() {
	memdelete(scene_import_settings_data);
}
