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
#include "scene_import_settings_internal.h"

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

// Camera framing margin applied around the fitted content so it never
// touches the viewport edges.
static constexpr real_t FRAME_MARGIN = 1.15;
static constexpr real_t MODEL_SETTINGS_PANEL_MIN_WIDTH = 420.0;
static constexpr real_t MODEL_SETTINGS_PANEL_DEFAULT_WIDTH = 480.0;
static constexpr real_t MODEL_CONTENT_PANEL_DEFAULT_WIDTH = 300.0;
static const String MODEL_CONTENT_PANEL_WIDTH_SETTING = "interface/fps_studio/model_view/content_width";
static const String MODEL_SETTINGS_PANEL_WIDTH_SETTING = "interface/fps_studio/model_view/inspector_width";
static const char *MODEL_SETTINGS_TAB_IDS[] = { "lod", "mesh", "physics", "hitboxes", "lighting", "details", "material", "environment" };
static constexpr int MODEL_SETTINGS_TAB_COUNT = sizeof(MODEL_SETTINGS_TAB_IDS) / sizeof(MODEL_SETTINGS_TAB_IDS[0]);
// Orthographic zoom limits (fraction of the fitted content size).
static constexpr real_t ZOOM_MIN = 0.05;
static constexpr real_t ZOOM_MAX = 20.0;

static String get_model_settings_tab_id(int p_tab) {
	if (p_tab < 0 || p_tab >= MODEL_SETTINGS_TAB_COUNT) {
		return String(MODEL_SETTINGS_TAB_IDS[0]);
	}
	return String(MODEL_SETTINGS_TAB_IDS[p_tab]);
}

static int get_model_settings_tab_index(const Variant &p_saved_tab) {
	if (p_saved_tab.get_type() == Variant::STRING) {
		const String saved_id = p_saved_tab;
		for (int i = 0; i < MODEL_SETTINGS_TAB_COUNT; i++) {
			if (saved_id == MODEL_SETTINGS_TAB_IDS[i]) {
				return i;
			}
		}
	} else if (p_saved_tab.get_type() == Variant::INT) {
		// Migrate the previous horizontal-tab order, which did not include Hitboxes.
		static const int legacy_tab_indices[] = { 0, 1, 2, 4, 5, 6, 7 };
		const int legacy_tab = (int)p_saved_tab;
		if (legacy_tab >= 0 && legacy_tab < (int)(sizeof(legacy_tab_indices) / sizeof(legacy_tab_indices[0]))) {
			return legacy_tab_indices[legacy_tab];
		}
	}
	return 0;
}

static Ref<ShaderMaterial> create_model_view_sky_material() {
	Ref<Shader> sky_shader;
	sky_shader.instantiate();
	sky_shader->set_code(R"shader(
shader_type sky;
render_mode use_debanding;

uniform vec3 sky_top_color : source_color = vec3(0.30, 0.42, 0.58);
uniform vec3 sky_horizon_color : source_color = vec3(0.43, 0.51, 0.61);
uniform vec3 ground_color : source_color = vec3(0.43, 0.51, 0.61);
uniform vec3 sun_color : source_color = vec3(1.0, 0.96, 0.88);
uniform bool clouds_enabled = true;
uniform float cloud_coverage : hint_range(0.0, 1.0) = 0.5;
uniform float cloud_scale : hint_range(0.2, 5.0) = 1.5;
uniform float cloud_speed : hint_range(0.0, 0.1) = 0.01;
// Advanced by the editor only while clouds move (no TIME: a TIME sky is
// re-rendered and redrawn every frame even when nothing changes).
uniform float cloud_time = 0.0;
uniform vec3 cloud_color : source_color = vec3(1.0, 1.0, 1.0);
uniform vec3 cloud_shadow_color : source_color = vec3(0.55, 0.60, 0.70);

const int CLOUD_MARCH_STEPS = 8;
const float CLOUD_BASE_HEIGHT = 120.0;
const float CLOUD_LAYER_THICKNESS = 55.0;

float hash3(vec3 p) {
	return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
}

float cloud_noise(vec3 p) {
	vec3 cell = floor(p);
	vec3 local = fract(p);
	vec3 blend = local * local * (3.0 - 2.0 * local);
	float n000 = hash3(cell + vec3(0.0, 0.0, 0.0));
	float n100 = hash3(cell + vec3(1.0, 0.0, 0.0));
	float n010 = hash3(cell + vec3(0.0, 1.0, 0.0));
	float n110 = hash3(cell + vec3(1.0, 1.0, 0.0));
	float n001 = hash3(cell + vec3(0.0, 0.0, 1.0));
	float n101 = hash3(cell + vec3(1.0, 0.0, 1.0));
	float n011 = hash3(cell + vec3(0.0, 1.0, 1.0));
	float n111 = hash3(cell + vec3(1.0, 1.0, 1.0));
	float lower = mix(mix(n000, n100, blend.x), mix(n010, n110, blend.x), blend.y);
	float upper = mix(mix(n001, n101, blend.x), mix(n011, n111, blend.x), blend.y);
	return mix(lower, upper, blend.z);
}

float cloud_fbm(vec3 p) {
	float value = 0.0;
	float weight = 0.5;
	for (int octave = 0; octave < 3; octave++) {
		value += weight * cloud_noise(p);
		p = p * 2.03 + vec3(13.5, 27.2, 9.2);
		weight *= 0.5;
	}
	return value / 0.875;
}

float cloud_density(vec3 world_position, float layer_height, vec2 wind) {
	vec3 p = vec3(world_position.xz * (0.0012 * cloud_scale) + wind, layer_height * 0.85);
	float shape = cloud_fbm(p);
	float detail = cloud_noise(p * 3.1 + vec3(5.2, 11.7, 2.8));
	float threshold = mix(0.72, 0.31, cloud_coverage);
	float cloud_shape = smoothstep(threshold, threshold + 0.16, shape + (detail - 0.5) * 0.12);
	float height_profile = smoothstep(0.0, 0.16, layer_height) * (1.0 - smoothstep(0.68, 1.0, layer_height));
	return cloud_shape * height_profile;
}

void sky() {
	float up = EYEDIR.y;
	vec3 color = up < 0.0 ? mix(ground_color, sky_horizon_color, clamp((up + 0.12) * 5.0, 0.0, 1.0)) : mix(sky_horizon_color, sky_top_color, pow(clamp(up * 1.35, 0.0, 1.0), 0.7));
	float sun_dot = 0.0;
	if (LIGHT0_ENABLED) {
		sun_dot = dot(EYEDIR, LIGHT0_DIRECTION);
		if (sun_dot > 0.0 && LIGHT0_DIRECTION.y > -0.06) {
			float disk = smoothstep(0.99955, 0.99985, sun_dot);
			float horizon_fade = clamp((LIGHT0_DIRECTION.y + 0.05) * 8.0, 0.0, 1.0);
			float glow = pow(max(sun_dot, 0.0), 24.0) * 0.12 * horizon_fade;
			color += sun_color * (disk * 3.0 + glow);
		}
	}

	if (clouds_enabled && up > 0.01 && cloud_coverage > 0.0) {
		vec3 ray_direction = normalize(EYEDIR);
		float ray_up = max(ray_direction.y, 0.015);
		float start_distance = CLOUD_BASE_HEIGHT / ray_up;
		float step_distance = CLOUD_LAYER_THICKNESS / (float(CLOUD_MARCH_STEPS) * ray_up);
		vec2 wind = vec2(cloud_time * cloud_speed, cloud_time * cloud_speed * 0.35);
		float transmittance = 1.0;
		vec3 cloud_light = vec3(0.0);
		float horizon_fade = smoothstep(0.01, 0.18, up);

		for (int step_index = 0; step_index < CLOUD_MARCH_STEPS; step_index++) {
			float layer_height = (float(step_index) + 0.5) / float(CLOUD_MARCH_STEPS);
			float distance_along_ray = start_distance + (float(step_index) + 0.5) * step_distance;
			vec3 sample_position = ray_direction * distance_along_ray;
			float density = cloud_density(sample_position, layer_height, wind);
			float sample_opacity = 1.0 - exp(-density * step_distance * 0.018);

			float top_light = smoothstep(0.18, 0.82, layer_height);
			float sun_illumination = LIGHT0_ENABLED ? clamp(0.72 + sun_dot * 0.32, 0.35, 1.08) : 0.72;
			vec3 sample_color = mix(cloud_shadow_color, cloud_color * sun_illumination, top_light);
			cloud_light += transmittance * sample_opacity * sample_color;
			transmittance *= 1.0 - sample_opacity;
			if (transmittance < 0.02) {
				break;
			}
		}

		float cloud_opacity = (1.0 - transmittance) * horizon_fade;
		color = mix(color, cloud_light / max(1.0 - transmittance, 0.001), cloud_opacity);
	}
	COLOR = color;
}
)shader");

	Ref<ShaderMaterial> sky_material;
	sky_material.instantiate();
	sky_material->set_shader(sky_shader);
	return sky_material;
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

	// Reuse the cached instance: it is the one the open level and the
	// Inspector edit, and replacing it reloaded it from disk, throwing away
	// their unsaved changes.
	Ref<Material> external_material;
	if (p_settings.has("use_external/path")) {
		const String path = p_settings["use_external/path"];
		external_material = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_REUSE);
	}
	if (external_material.is_null() && p_settings.has("use_external/fallback_path")) {
		const String fallback_path = p_settings["use_external/fallback_path"];
		if (!fallback_path.is_empty()) {
			external_material = ResourceLoader::load(fallback_path, "", ResourceFormatLoader::CACHE_MODE_REUSE);
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
		// Keep the import type: the default would turn an AnimationLibrary
		// into a PackedScene import on the next Reimport.
		const String import_type = current_import_type;
		open_settings(base_path, import_type);
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

void SceneImportSettingsScreen::_clear_helper_preview() {
	if (helper_preview_root != nullptr) {
		Node *parent = helper_preview_root->get_parent();
		if (parent != nullptr) {
			parent->remove_child(helper_preview_root);
		}
		memdelete(helper_preview_root);
		helper_preview_root = nullptr;
	}
	helper_preview_aabb = AABB();
	has_helper_preview_aabb = false;
}

void SceneImportSettingsScreen::_update_helper_preview() {
	_clear_helper_preview();
	Node3D *scene_node = Object::cast_to<Node3D>(scene);
	if (scene_node == nullptr) {
		if (helper_visibility_toggle != nullptr) {
			helper_visibility_toggle->set_disabled(true);
		}
		return;
	}

	Ref<SphereMesh> marker_mesh;
	marker_mesh.instantiate();
	marker_mesh->set_radius(0.035);
	marker_mesh->set_height(0.07);

	Ref<StandardMaterial3D> marker_material;
	marker_material.instantiate();
	marker_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	marker_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
	marker_material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	marker_material->set_albedo(Color(1.0, 0.66, 0.12));

	helper_preview_root = memnew(Node3D);
	helper_preview_root->set_name("ModelViewHelperNodePreview");
	scene_node->add_child(helper_preview_root, true);
	const Transform3D scene_inverse = scene_node->get_global_transform().affine_inverse();
	const String helper_tooltip = TTR("Transform-only helper outside the visible mesh bounds. "
			"Enable Helpers to see its origin. In Details, Skip Import omits it on reimport; "
			"Node Type changes its imported node class.");

	for (const KeyValue<String, NodeData> &entry : node_map) {
		Node *node = entry.value.node;
		if (node == nullptr || node == scene || node->get_class() != "Node3D" ||
				node->get_child_count() != 0) {
			continue;
		}
		Node3D *helper = Object::cast_to<Node3D>(node);
		if (helper == nullptr) {
			continue;
		}
		const Vector3 helper_position = scene_inverse.xform(helper->get_global_transform().origin);
		if (contents_aabb.has_point(helper_position)) {
			continue;
		}

		MeshInstance3D *marker = memnew(MeshInstance3D);
		marker->set_name(String("HelperMarker_") + String(helper->get_name()).validate_node_name());
		marker->set_mesh(marker_mesh);
		marker->set_material_override(marker_material);
		marker->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		marker->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
		marker->set_position(helper_position);
		helper_preview_root->add_child(marker, true);

		Label3D *label = memnew(Label3D);
		label->set_name(String("HelperLabel_") + String(helper->get_name()).validate_node_name());
		label->set_text(vformat("%s  (%.2f, %.2f, %.2f)", helper->get_name(),
				helper_position.x, helper_position.y, helper_position.z));
		label->set_position(helper_position + Vector3(0.0, 0.09, 0.0));
		label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
		label->set_draw_flag(Label3D::FLAG_DISABLE_DEPTH_TEST, true);
		label->set_font_size(32);
		label->set_pixel_size(0.01);
		label->set_modulate(Color(1.0, 0.78, 0.34));
		label->set_outline_size(5);
		label->set_outline_modulate(Color(0.07, 0.08, 0.1, 1.0));
		helper_preview_root->add_child(label, true);

		const real_t marker_radius = 0.045;
		const AABB marker_bounds(
				helper_position - Vector3(marker_radius, marker_radius, marker_radius),
				Vector3(marker_radius * 2.0, marker_radius * 2.0, marker_radius * 2.0));
		if (has_helper_preview_aabb) {
			helper_preview_aabb.merge_with(marker_bounds);
		} else {
			helper_preview_aabb = marker_bounds;
			has_helper_preview_aabb = true;
		}

		if (entry.value.scene_node != nullptr) {
			entry.value.scene_node->set_tooltip_text(0, entry.value.scene_node->get_tooltip_text(0) + "\n\n" + helper_tooltip);
		}
	}

	helper_preview_root->set_visible(show_helper_nodes);
	if (helper_visibility_toggle != nullptr) {
		helper_visibility_toggle->set_disabled(!has_helper_preview_aabb);
		if (!has_helper_preview_aabb) {
			helper_visibility_toggle->set_pressed_no_signal(false);
		}
	}
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
	const bool using_scene_preview = selected_type == "Node" || selected_type == "Animation" ||
			selected_type.is_empty() || (selected_type == "Material" && material_preview_on_model);
	if (using_scene_preview && show_helper_nodes && has_helper_preview_aabb) {
		camera_aabb.merge_with(helper_preview_aabb);
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

void SceneImportSettingsScreen::_wireframe_toggled(bool p_enabled) {
	base_viewport->set_debug_draw(p_enabled ? Viewport::DEBUG_DRAW_WIREFRAME : Viewport::DEBUG_DRAW_DISABLED);
	_update_mesh_stats();
}

void SceneImportSettingsScreen::_helper_visibility_toggled(bool p_enabled) {
	show_helper_nodes = p_enabled;
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "show_helper_nodes", p_enabled);
	if (helper_preview_root != nullptr) {
		helper_preview_root->set_visible(p_enabled);
	}
	_reset_camera();
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
	if (_ask_about_unsaved_changes(false, p_path, p_scene_import_type)) {
		return;
	}
	import_options_dirty = false; // A fresh model, nothing edited yet.
	if (lod_edit_timer) {
		lod_edit_timer->stop();
	}
	if (scene) {
		_save_lod_config();
		_save_hitbox_config();
		_clear_helper_preview();
		_clear_hitbox_preview();
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
	_setup_model_preview_environment();

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
	// The bones overlay of the previous model must not show over this one.
	bones_mesh_preview->set_mesh(Ref<Mesh>());

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
	_load_hitbox_config();
	show_helper_nodes = EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "show_helper_nodes", false);
	helper_visibility_toggle->set_pressed_no_signal(show_helper_nodes);

	// Regardless of p_scene_import_type, use PackedScene for pre_import because we want to see the full thing.
	_resource_importer_scene->set_scene_import_type("PackedScene");
	scene = _resource_importer_scene->pre_import(p_path, defaults);
	_resource_importer_scene->set_scene_import_type(p_scene_import_type);
	if (scene == nullptr) {
		// Nothing of the previous model may stay clickable: its maps are gone.
		mesh_stats_panel->hide();
		scene_tree->clear();
		mesh_tree->clear();
		material_tree->clear();
		_update_empty_state();
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
	if (_repair_hitbox_attachments()) {
		_select_hitbox(selected_hitbox);
		_save_hitbox_config();
	}
	_populate_hitbox_attachments();
	_update_hitbox_preview();

	inspector->edit(nullptr);

	if (first_aabb) {
		// The file has no visible mesh content (e.g. an animation-only import).
		// Fall back to the skeleton gizmo extents so the preview still frames
		// something; otherwise use the default preview cube.
		contents_aabb = has_skeleton_aabb ? skeleton_aabb : AABB(Vector3(-1, -1, -1), Vector3(2, 2, 2));
		first_aabb = false;
	}
	_update_helper_preview();

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
	// A LOD edit waiting for its timer belongs to the mesh selected now:
	// finish it before the selection moves, or it is never regenerated.
	if (lod_edit_timer && !lod_edit_timer->is_stopped()) {
		lod_edit_timer->stop();
		_finish_lod_edit_update();
	}
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

	// Bones follow the Skeleton toggle; only the isolated Mesh and Material
	// previews hide them.
	if (p_type == "Mesh" || p_type == "Material") {
		bones_mesh_preview->hide();
	} else {
		_update_skeleton_preview_visibility();
	}
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
		_apply_import_materials_to_mesh_preview(mesh_preview->get_mesh());
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
			_clear_mesh_preview_material_overrides();
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
		settings_tabs->set_current_tab(6);
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
		// Keep the open tab when it still applies (Hitboxes, Lighting,
		// Environment, Details work for any selection); switch only from a
		// tab made for another kind of item. Tabs: 0 LOD, 1 Mesh, 2 Physics,
		// 3 Hitboxes, 4 Lighting, 5 Details, 6 Material, 7 Environment.
		const int current = settings_tabs->get_current_tab();
		int tab = current;
		if (p_type == "Material") {
			tab = 6;
		} else if (p_type == "Mesh") {
			tab = current == 6 ? 1 : current;
		} else if (current <= 2 || current == 6) {
			tab = 5;
		}
		if (tab != current) {
			settings_tabs->set_current_tab(tab);
			_settings_tab_changed(tab);
		}
	}
}

void SceneImportSettingsScreen::_inspector_property_edited(const String &p_name) {
	import_options_dirty = true;
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
	if (animation_player) {
		animation_player->stop();
	}
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

		// Save to the file the material was loaded from: the material's own
		// path, else the uid (follows a moved file), the fallback path last
		// (it still names the old place after a move).
		String path = md.material->get_path();
		if (!path.is_resource_file() && md.settings.has("use_external/path")) {
			path = md.settings["use_external/path"];
			if (path.begins_with("uid://")) {
				path = ResourceUID::uid_to_path(path);
			}
		}
		if (!path.is_resource_file() && md.settings.has("use_external/fallback_path")) {
			path = md.settings["use_external/fallback_path"];
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
	import_options_dirty = false;
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
	Array import_hitboxes;
	for (const ModelHitbox &hitbox : model_hitboxes) {
		Dictionary item;
		item["name"] = hitbox.name;
		item["minimum"] = hitbox.minimum;
		item["maximum"] = hitbox.maximum;
		item["hitgroup"] = hitbox.hitgroup;
		item["damage"] = hitbox.damage;
		item["skeleton_path"] = hitbox.skeleton_path;
		item["bone_name"] = hitbox.bone_name;
		import_hitboxes.push_back(item);
	}
	main_settings["fps_studio/hitboxes"] = import_hitboxes;
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
	// Unlike a normal reimport this path does not notify the docks: refresh
	// the Content Browser/FileSystem so the model's entry and thumbnail update.
	EditorFileSystem::get_singleton()->emit_signal(SNAME("filesystem_changed"));
	return OK;
}

bool SceneImportSettingsScreen::_has_unsaved_changes() const {
	if (import_options_dirty) {
		return true;
	}
	for (const KeyValue<String, MaterialData> &entry : material_map) {
		if (entry.value.modified) {
			return true;
		}
	}
	return false;
}

// True when the dialog was shown and the open/close waits for the answer.
// LOD and hitbox edits never ask: they are kept in their sidecar files.
bool SceneImportSettingsScreen::_ask_about_unsaved_changes(bool p_close, const String &p_path, const String &p_type) {
	if (skip_unsaved_check || scene == nullptr || unsaved_changes_dialog == nullptr || !_has_unsaved_changes()) {
		return false;
	}
	if (!p_close && p_path == base_path) {
		return false; // Reopening the same model (after a reimport).
	}
	pending_close = p_close;
	pending_open_path = p_path;
	pending_open_type = p_type;
	unsaved_changes_dialog->set_text(vformat(TTR("\"%s\" has import setting or material changes that Apply Changes has not written yet.\nApply them before continuing?"), base_path.get_file()));
	unsaved_changes_dialog->popup_centered();
	return true;
}

void SceneImportSettingsScreen::_unsaved_changes_apply() {
	if (_re_import() != OK) {
		return; // The error is shown; stay on this model.
	}
	_continue_after_unsaved_changes();
}

void SceneImportSettingsScreen::_unsaved_changes_action(const StringName &p_action) {
	if (p_action != StringName("discard")) {
		return;
	}
	unsaved_changes_dialog->hide();
	import_options_dirty = false;
	for (KeyValue<String, MaterialData> &entry : material_map) {
		entry.value.modified = false;
	}
	_continue_after_unsaved_changes();
}

void SceneImportSettingsScreen::_continue_after_unsaved_changes() {
	skip_unsaved_check = true;
	if (pending_close) {
		_close_screen();
	} else if (!pending_open_path.is_empty()) {
		open_settings(pending_open_path, pending_open_type);
	}
	skip_unsaved_check = false;
	pending_close = false;
	pending_open_path = String();
	pending_open_type = String();
}

void SceneImportSettingsScreen::_update_empty_state() {
	// The empty-state hint is shown whenever the workspace has no model loaded
	// (e.g. when the user switches to the "Model" tab manually).
	empty_hint_panel->set_visible(scene == nullptr);
}

void SceneImportSettingsScreen::_close_screen() {
	if (_ask_about_unsaved_changes(true, String(), String())) {
		return;
	}
	import_options_dirty = false;
	_save_lod_config();
	_save_hitbox_config();
	if (lod_edit_timer) {
		lod_edit_timer->stop();
	}
	if (scene) {
		_clear_helper_preview();
		_clear_hitbox_preview();
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
	_save_hitbox_config();
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
				const double length = animation_player->get_current_animation_length();
				animation_slider->set_value_no_signal(length > 0.0 ? animation_player->get_current_animation_position() / length : 0.0);
			}
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			// Unlike the old modal dialog, switching to another workspace tab
			// must NOT free the previewed model; it stays alive so the user
			// can come back to it. Cleanup happens in open_settings() and
			// _close_screen().
			_update_empty_state();
			if (preview_sun_animation_timer != nullptr) {
				if (is_visible_in_tree() && preview_sun_animation_enabled) {
					preview_sun_animation_timer->start();
				} else {
					preview_sun_animation_timer->stop();
				}
			}
			_update_preview_cloud_timer();
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
		const String import_type = current_import_type;
		open_settings(base_path, import_type);
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
	const Ref<Theme> editor_theme = EditorNode::get_singleton()->get_editor_theme();
	const Color model_view_accent = editor_theme->get_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color model_view_surface = editor_theme->get_color(SNAME("base_color"), EditorStringName(Editor));

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
	file_label->add_theme_color_override(SceneStringName(font_color), model_view_accent);
	file_label->add_theme_font_override(SceneStringName(font), editor_theme->get_font(SNAME("bold"), EditorStringName(EditorFonts)));
	menu_hb->add_child(file_label);

	tree_split = memnew(HSplitContainer);
	main_vb->add_child(tree_split);
	tree_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tree_split->connect(SNAME("dragged"), callable_mp(this, &SceneImportSettingsScreen::_tree_split_dragged));

	data_mode = memnew(TabContainer);
	tree_split->add_child(data_mode);
	data_mode->set_custom_minimum_size(Size2(300 * EDSCALE, 0));
	data_mode->set_theme_type_variation("TabContainerOdd");
	Ref<StyleBoxFlat> data_tab_selected = editor_theme->get_stylebox(SNAME("tab_selected"), SNAME("TabContainerOdd"))->duplicate();
	data_tab_selected->set_border_width(SIDE_BOTTOM, 2 * EDSCALE);
	data_tab_selected->set_border_color(model_view_accent);
	data_mode->add_theme_style_override(SNAME("tab_selected"), data_tab_selected);
	data_mode->add_theme_color_override(SNAME("font_selected_color"), model_view_accent);
	data_mode->add_theme_color_override(SNAME("font_hovered_color"), model_view_accent.lightened(0.12));

	property_split = memnew(HSplitContainer);
	tree_split->add_child(property_split);
	property_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);

	scene_tree = memnew(Tree);
	scene_tree->set_name(TTR("Scene"));
	scene_tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	scene_tree->set_theme_type_variation("TreeSecondary");
	data_mode->add_child(scene_tree);
	Ref<StyleBoxFlat> tree_selected = editor_theme->get_stylebox(SNAME("selected"), SNAME("Tree"))->duplicate();
	Color tree_selected_bg = model_view_accent;
	tree_selected_bg.a = 0.2;
	tree_selected->set_bg_color(tree_selected_bg);
	tree_selected->set_border_width_all(0);
	tree_selected->set_border_width(SIDE_LEFT, 2 * EDSCALE);
	tree_selected->set_border_color(model_view_accent);
	tree_selected->set_corner_radius_all(0);
	scene_tree->add_theme_style_override(SNAME("selected"), tree_selected);
	scene_tree->add_theme_style_override(SNAME("selected_focus"), tree_selected);
	scene_tree->add_theme_color_override(SNAME("font_selected_color"), editor_theme->get_color(SNAME("font_color"), EditorStringName(Editor)));
	scene_tree->connect("cell_selected", callable_mp(this, &SceneImportSettingsScreen::_scene_tree_selected));

	mesh_tree = memnew(Tree);
	mesh_tree->set_name(TTR("Meshes"));
	mesh_tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	mesh_tree->set_theme_type_variation("TreeSecondary");
	data_mode->add_child(mesh_tree);
	mesh_tree->add_theme_style_override(SNAME("selected"), tree_selected);
	mesh_tree->add_theme_style_override(SNAME("selected_focus"), tree_selected);
	mesh_tree->add_theme_color_override(SNAME("font_selected_color"), editor_theme->get_color(SNAME("font_color"), EditorStringName(Editor)));
	mesh_tree->set_hide_root(true);
	mesh_tree->connect("cell_selected", callable_mp(this, &SceneImportSettingsScreen::_mesh_tree_selected));

	material_tree = memnew(Tree);
	material_tree->set_name(TTR("Materials"));
	material_tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	material_tree->set_theme_type_variation("TreeSecondary");
	data_mode->add_child(material_tree);
	material_tree->add_theme_style_override(SNAME("selected"), tree_selected);
	material_tree->add_theme_style_override(SNAME("selected_focus"), tree_selected);
	material_tree->add_theme_color_override(SNAME("font_selected_color"), editor_theme->get_color(SNAME("font_color"), EditorStringName(Editor)));
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

	helper_visibility_toggle = memnew(Button);
	helper_visibility_toggle->set_text(TTR("Helpers"));
	helper_visibility_toggle->set_theme_type_variation("PreviewLightButton");
	helper_visibility_toggle->set_toggle_mode(true);
	helper_visibility_toggle->set_focus_mode(Control::FOCUS_NONE);
	helper_visibility_toggle->set_tooltip_text(TTR("Show transform-only helper nodes outside the mesh bounds. "
			"Select a helper in the Scene tree to skip it or change its Node Type in Details."));
	helper_visibility_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_helper_visibility_toggled));
	hb_tools->add_child(helper_visibility_toggle);
	for (Button *button : { frame_button, wireframe_toggle, skeleton_visibility_toggle, helper_visibility_toggle }) {
		button->add_theme_color_override(SNAME("font_pressed_color"), model_view_accent.lightened(0.18));
		button->add_theme_color_override(SNAME("font_hover_pressed_color"), model_view_accent.lightened(0.18));
	}

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
	stats_bg->set_border_width(SIDE_LEFT, 2 * EDSCALE);
	stats_bg->set_border_color(model_view_accent);

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

	// Model View owns its complete preview environment. It never inherits the
	// open Level's sky, lights, or day/night cycle.
	preview_sky_material = create_model_view_sky_material();
	environment = create_model_view_environment(preview_sky_material);
	camera->set_environment(environment);
	preview_world_environment_node = memnew(WorldEnvironment);
	preview_world_environment_node->set_environment(environment);
	base_viewport->add_child(preview_world_environment_node);

	light1 = memnew(DirectionalLight3D);
	light1->set_color(Color(1.0, 1.0, 1.0));
	light1->set_param(Light3D::PARAM_ENERGY, 1.2);
	light1->set_shadow(true);
	light1->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
	light1->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 100.0);
	light1->set_param(Light3D::PARAM_SHADOW_OPACITY, 0.55);
	light1->set_param(Light3D::PARAM_SHADOW_BLUR, 2.0);
	light1->set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_AND_SKY);
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
	preview_floor_material->set_shader_parameter("horizon_color", Color(0.43, 0.51, 0.61));
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
	settings_tabs->set_custom_minimum_size(Size2(300 * EDSCALE, 0));
	settings_tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->set_tabs_visible(false);
	Ref<StyleBoxFlat> settings_surface_style = editor_theme->get_stylebox(SceneStringName(panel), SNAME("TabContainer"))->duplicate();
	Color settings_surface_color = model_view_surface.lerp(model_view_accent, 0.025);
	settings_surface_style->set_bg_color(settings_surface_color);
	settings_surface_style->set_border_width(SIDE_LEFT, 1 * EDSCALE);
	settings_surface_style->set_border_color(Color(model_view_accent, 0.35));
	settings_tabs->add_theme_style_override(SceneStringName(panel), settings_surface_style);
	settings_panel_layout = memnew(HBoxContainer);
	settings_panel_layout->set_h_size_flags(Control::SIZE_FILL);
	settings_panel_layout->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	property_split->add_child(settings_panel_layout);
	settings_panel_layout->add_child(settings_tabs);
	property_split->connect(SNAME("dragged"), callable_mp(this, &SceneImportSettingsScreen::_property_split_dragged));
	_build_lod_settings_panel();
	ScrollContainer *lod_panel_scroll = memnew(ScrollContainer);
	lod_panel_scroll->set_name(TTR("LOD"));
	lod_panel_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_panel_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	lod_panel_scroll->add_child(lod_settings_panel);
	settings_tabs->add_child(lod_panel_scroll);
	int tab_index = 1;
	for (const String &tab_name : { TTR("Mesh"), TTR("Physics"), TTR("Hitboxes"), TTR("Lighting"), TTR("Details") }) {
		VBoxContainer *tab = memnew(VBoxContainer);
		tab->set_name(tab_name);
		tab->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		tab->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		settings_tabs->add_child(tab);
		if (tab_index == 3) {
			_build_hitbox_editor(tab);
		} else {
			Label *hint = memnew(Label);
			hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			hint->hide();
			tab->add_child(hint);
			settings_tab_hints[tab_index] = hint;
		}
		tab_index++;
	}
	inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->get_tab_control(5)->add_child(inspector);

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

	Panel *settings_tab_navigation_panel = memnew(Panel);
	settings_tab_navigation = settings_tab_navigation_panel;
	settings_tab_navigation->set_custom_minimum_size(Size2(40 * EDSCALE, 0));
	settings_tab_navigation->set_h_size_flags(Control::SIZE_FILL);
	settings_tab_navigation->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	const Color settings_navigation_background_color = model_view_surface.lightened(0.05);
	Ref<StyleBoxFlat> settings_navigation_background_style;
	settings_navigation_background_style.instantiate();
	settings_navigation_background_style->set_bg_color(settings_navigation_background_color);
	settings_tab_navigation->add_theme_style_override(SceneStringName(panel), settings_navigation_background_style);
	Vector<String> settings_tab_names;
	settings_tab_names.push_back(TTR("LOD"));
	settings_tab_names.push_back(TTR("Mesh"));
	settings_tab_names.push_back(TTR("Physics"));
	settings_tab_names.push_back(TTR("Hitboxes"));
	settings_tab_names.push_back(TTR("Lighting"));
	settings_tab_names.push_back(TTR("Details"));
	settings_tab_names.push_back(TTR("Material"));
	settings_tab_names.push_back(TTR("Environment"));
	settings_tab_bar = memnew(TabBar);
	// The first tab should begin at the top edge after the bar is rotated.
	settings_tab_bar->set_tab_alignment(TabBar::ALIGNMENT_LEFT);
	settings_tab_bar->set_scrolling_enabled(true);
	settings_tab_bar->set_scroll_to_selected(true);
	settings_tab_bar->add_theme_constant_override(SNAME("tab_separation"), 2 * EDSCALE);
	settings_tab_bar->set_rotation(Math::deg_to_rad(90.0));
	Ref<StyleBoxFlat> settings_tab_selected = editor_theme->get_stylebox(SNAME("tab_selected"), SNAME("TabBar"))->duplicate();
	settings_tab_selected->set_bg_color(model_view_surface.lerp(model_view_accent, 0.16));
	settings_tab_selected->set_border_width(SIDE_TOP, 3 * EDSCALE);
	settings_tab_selected->set_border_color(model_view_accent);
	settings_tab_bar->add_theme_style_override(SNAME("tab_selected"), settings_tab_selected);
	Ref<StyleBoxFlat> settings_tab_unselected = editor_theme->get_stylebox(SNAME("tab_unselected"), SNAME("TabBar"))->duplicate();
	settings_tab_unselected->set_bg_color(settings_navigation_background_color.lightened(0.12));
	settings_tab_unselected->set_border_width_all(1 * EDSCALE);
	Color settings_tab_border = model_view_accent;
	settings_tab_border.a = 0.22;
	settings_tab_unselected->set_border_color(settings_tab_border);
	settings_tab_unselected->set_corner_radius_all(2 * EDSCALE);
	settings_tab_bar->add_theme_style_override(SNAME("tab_unselected"), settings_tab_unselected);
	Ref<StyleBoxFlat> settings_tab_hovered = editor_theme->get_stylebox(SNAME("tab_hovered"), SNAME("TabBar"))->duplicate();
	settings_tab_hovered->set_bg_color(settings_navigation_background_color.lightened(0.16));
	settings_tab_hovered->set_border_width_all(1 * EDSCALE);
	settings_tab_border.a = 0.55;
	settings_tab_hovered->set_border_color(settings_tab_border);
	settings_tab_hovered->set_corner_radius_all(2 * EDSCALE);
	settings_tab_bar->add_theme_style_override(SNAME("tab_hovered"), settings_tab_hovered);
	settings_tab_bar->add_theme_color_override(SNAME("font_selected_color"), model_view_accent);
	settings_tab_bar->add_theme_color_override(SNAME("font_hovered_color"), model_view_accent.lightened(0.12));
	settings_tab_navigation->add_child(settings_tab_bar);
	for (int i = 0; i < settings_tab_names.size(); i++) {
		settings_tab_bar->add_tab(settings_tab_names[i]);
		settings_tab_bar->set_tab_tooltip(i, vformat(TTR("Show %s settings"), settings_tab_names[i]));
	}
	settings_tab_bar->connect(SNAME("tab_changed"), callable_mp(this, &SceneImportSettingsScreen::_settings_tab_bar_changed));
	settings_tab_navigation->connect(SceneStringName(resized), callable_mp(this, &SceneImportSettingsScreen::_settings_tab_navigation_resized));
	VSeparator *settings_navigation_separator = memnew(VSeparator);
	settings_navigation_separator->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_panel_layout->add_child(settings_navigation_separator);
	settings_panel_layout->add_child(settings_tab_navigation);

	ScrollContainer *environment_scroll = memnew(ScrollContainer);
	environment_scroll->set_name(TTR("Environment"));
	environment_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	environment_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->add_child(environment_scroll);
	VBoxContainer *environment_tab = memnew(VBoxContainer);
	environment_tab->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	environment_tab->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	environment_scroll->add_child(environment_tab);

	Label *environment_hint = memnew(Label);
	environment_hint->set_text(TTR("These settings affect only the Model View preview."));
	environment_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	environment_tab->add_child(environment_hint);

	Label *sun_section_label = memnew(Label);
	sun_section_label->set_text(TTR("Sun"));
	environment_tab->add_child(sun_section_label);
	auto add_environment_slider = [&](Label *&r_label, HSlider *&r_slider, const String &p_title, double p_min, double p_max, double p_step, const String &p_tooltip, const Callable &p_callback) {
		r_label = memnew(Label);
		r_label->set_text(p_title);
		environment_tab->add_child(r_label);
		r_slider = memnew(HSlider);
		r_slider->set_min(p_min);
		r_slider->set_max(p_max);
		r_slider->set_step(p_step);
		r_slider->set_tooltip_text(p_tooltip);
		r_slider->connect(SceneStringName(value_changed), p_callback);
		environment_tab->add_child(r_slider);
	};

	add_environment_slider(preview_time_of_day_label, preview_time_of_day_slider, TTR("Time of Day: 12:00"), 0.0, 24.0, 0.05, TTR("Set the sun position along its daily path."), callable_mp(this, &SceneImportSettingsScreen::_preview_time_of_day_changed));
	add_environment_slider(preview_sun_azimuth_label, preview_sun_azimuth_slider, TTR("Sun Azimuth: 0 deg"), -180.0, 180.0, 1.0, TTR("Rotate the sun direction around the model horizontally."), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_azimuth_changed));
	add_environment_slider(preview_sun_path_tilt_label, preview_sun_path_tilt_slider, TTR("Sun Path Tilt: 35 deg"), 0.0, 80.0, 1.0, TTR("Set the tilt of the sun's daily path above the horizon."), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_path_tilt_changed));
	add_environment_slider(preview_sun_energy_label, preview_sun_energy_slider, TTR("Sun Energy: 1.20"), 0.0, 5.0, 0.05, TTR("Set the maximum direct light energy at midday."), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_energy_changed));

	preview_sun_animation_toggle = memnew(CheckButton);
	preview_sun_animation_toggle->set_text(TTR("Animate Sun"));
	preview_sun_animation_toggle->set_tooltip_text(TTR("Run the Model View's day/night cycle."));
	preview_sun_animation_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_animation_toggled));
	environment_tab->add_child(preview_sun_animation_toggle);
	add_environment_slider(preview_sun_cycle_duration_label, preview_sun_cycle_duration_slider, TTR("Cycle Duration: 60 s"), 10.0, 600.0, 5.0, TTR("Set the duration of a complete day and night in the preview."), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_cycle_duration_changed));

	Label *clouds_section_label = memnew(Label);
	clouds_section_label->set_text(TTR("Clouds"));
	environment_tab->add_child(clouds_section_label);
	preview_clouds_toggle = memnew(CheckButton);
	preview_clouds_toggle->set_text(TTR("Show Clouds"));
	preview_clouds_toggle->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_preview_clouds_toggled));
	environment_tab->add_child(preview_clouds_toggle);
	add_environment_slider(preview_cloud_coverage_label, preview_cloud_coverage_slider, TTR("Cloud Coverage: 50%"), 0.0, 1.0, 0.01, TTR("Set how much of the preview sky is covered by clouds."), callable_mp(this, &SceneImportSettingsScreen::_preview_cloud_coverage_changed));
	add_environment_slider(preview_cloud_scale_label, preview_cloud_scale_slider, TTR("Cloud Scale: 1.50"), 0.2, 5.0, 0.1, TTR("Set the size of the procedural clouds."), callable_mp(this, &SceneImportSettingsScreen::_preview_cloud_scale_changed));
	add_environment_slider(preview_cloud_speed_label, preview_cloud_speed_slider, TTR("Cloud Speed: 0.010"), 0.0, 0.1, 0.001, TTR("Set the speed of the procedural cloud movement."), callable_mp(this, &SceneImportSettingsScreen::_preview_cloud_speed_changed));
	preview_cloud_timer = memnew(Timer);
	preview_cloud_timer->set_wait_time(1.0 / 30.0);
	preview_cloud_timer->connect(SNAME("timeout"), callable_mp(this, &SceneImportSettingsScreen::_preview_cloud_step));
	add_child(preview_cloud_timer);
	preview_sun_animation_timer = memnew(Timer);
	preview_sun_animation_timer->set_wait_time(1.0 / 30.0);
	preview_sun_animation_timer->connect(SNAME("timeout"), callable_mp(this, &SceneImportSettingsScreen::_preview_sun_animation_step));
	add_child(preview_sun_animation_timer);

	scene_import_settings_data = memnew(SceneImportSettingsData);
	settings_tabs->connect(SNAME("tab_changed"), callable_mp(this, &SceneImportSettingsScreen::_settings_tab_changed));
	const int saved_settings_tab = CLAMP(get_model_settings_tab_index(EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "settings_tab", 0)), 0, MODEL_SETTINGS_TAB_COUNT - 1);
	settings_tabs->set_current_tab(saved_settings_tab);
	_settings_tab_changed(saved_settings_tab);

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

	unsaved_changes_dialog = memnew(ConfirmationDialog);
	unsaved_changes_dialog->set_title(TTR("Unsaved Model Changes"));
	unsaved_changes_dialog->set_ok_button_text(TTR("Apply Changes"));
	unsaved_changes_dialog->add_button(TTR("Discard"), DisplayServer::get_singleton()->get_swap_cancel_ok(), "discard");
	unsaved_changes_dialog->connect(SceneStringName(confirmed), callable_mp(this, &SceneImportSettingsScreen::_unsaved_changes_apply));
	unsaved_changes_dialog->connect("custom_action", callable_mp(this, &SceneImportSettingsScreen::_unsaved_changes_action));
	add_child(unsaved_changes_dialog);
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
	lod_edit_timer->connect("timeout", callable_mp(this, &SceneImportSettingsScreen::_lod_edit_timer_timeout));
	add_child(lod_edit_timer);

	// Restore the user's panel width, or use a 400 px default while enforcing
	// the inspector's 320 px minimum. Widths are stored before editor scaling.
	const real_t content_minimum_width = data_mode->get_combined_minimum_size().x;
	const real_t saved_content_width = MAX((real_t)EDITOR_DEF(MODEL_CONTENT_PANEL_WIDTH_SETTING, MODEL_CONTENT_PANEL_DEFAULT_WIDTH) * EDSCALE, content_minimum_width);
	tree_split->set_split_offset(Math::round(saved_content_width - content_minimum_width));
	const real_t panel_minimum_width = settings_panel_layout->get_combined_minimum_size().x;
	const real_t saved_panel_width = MAX((real_t)EDITOR_DEF(MODEL_SETTINGS_PANEL_WIDTH_SETTING, MODEL_SETTINGS_PANEL_DEFAULT_WIDTH), MODEL_SETTINGS_PANEL_MIN_WIDTH) * EDSCALE;
	const real_t desired_panel_width = MAX(panel_minimum_width, saved_panel_width);
	property_split->set_split_offset(Math::round(panel_minimum_width - desired_panel_width));

	EditorNode::get_singleton()->register_hdr_viewport(base_viewport);
	
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
	if (!settings_tabs || !scene_import_settings_data || p_tab < 0 || p_tab >= MODEL_SETTINGS_TAB_COUNT) {
		return;
	}
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "settings_tab", get_model_settings_tab_id(p_tab));
	if (settings_tab_bar && settings_tab_bar->get_current_tab() != p_tab) {
		settings_tab_bar->set_current_tab(p_tab);
	}
	if (p_tab == 6) {
		_update_material_editor();
		return;
	}
	if (p_tab == 0 || p_tab == 3 || p_tab == 7) {
		return;
	}
	Control *container = settings_tabs->get_tab_control(p_tab);
	if (!container) {
		return;
	}
	if (inspector->get_parent() != container) {
		inspector->reparent(container);
	}
	const int inspector_section = p_tab == 4 ? 3 : (p_tab == 5 ? 4 : p_tab);
	scene_import_settings_data->section_filter = inspector_section;
	Label *hint = settings_tab_hints[p_tab];
	if (hint) {
		String message;
		if (p_tab == 1 && scene_import_settings_data->category != ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH) {
			message = TTR("Select a mesh resource in the scene tree or Meshes tab.");
		} else if (p_tab == 2 && scene_import_settings_data->category != ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH_3D_NODE) {
			message = TTR("Select a mesh instance node in the Scene tree to configure collision and physics.");
		} else if (p_tab == 4 && scene_import_settings_data->category != ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH && scene_import_settings_data->category != ResourceImporterScene::INTERNAL_IMPORT_CATEGORY_MESH_3D_NODE) {
			message = TTR("Select a mesh or mesh instance to configure shadows and lightmap options.");
		}
		hint->set_text(message);
		hint->set_visible(!message.is_empty());
	}
	scene_import_settings_data->notify_property_list_changed();
}

void SceneImportSettingsScreen::_settings_tab_bar_changed(int p_tab) {
	if (settings_tabs && p_tab >= 0 && p_tab < MODEL_SETTINGS_TAB_COUNT) {
		settings_tabs->set_current_tab(p_tab);
	}
}

void SceneImportSettingsScreen::_settings_tab_navigation_resized() {
	if (!settings_tab_navigation || !settings_tab_bar) {
		return;
	}
	const Size2 navigation_size = settings_tab_navigation->get_size();
	if (navigation_size.x <= 0.0 || navigation_size.y <= 0.0) {
		return;
	}
	const Size2 tab_bar_size(navigation_size.y, navigation_size.x);
	settings_tab_bar->set_size(tab_bar_size);
	settings_tab_bar->set_pivot_offset(tab_bar_size * 0.5);
	settings_tab_bar->set_position(Size2((navigation_size.x - tab_bar_size.x) * 0.5, (navigation_size.y - tab_bar_size.y) * 0.5));
}

void SceneImportSettingsScreen::_tree_split_dragged(int) {
	if (!data_mode) {
		return;
	}
	const real_t content_width = data_mode->get_size().x / EDSCALE;
	EditorSettings::get_singleton()->set_setting(MODEL_CONTENT_PANEL_WIDTH_SETTING, Math::round(content_width));
}

void SceneImportSettingsScreen::_property_split_dragged(int p_offset) {
	if (!settings_tabs) {
		return;
	}
	const real_t minimum_width = settings_panel_layout->get_combined_minimum_size().x;
	const real_t panel_width = MAX(minimum_width, minimum_width - p_offset) / EDSCALE;
	EditorSettings::get_singleton()->set_setting(MODEL_SETTINGS_PANEL_WIDTH_SETTING, Math::round(panel_width));
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

SceneImportSettingsScreen::~SceneImportSettingsScreen() {
	memdelete(scene_import_settings_data);
}
