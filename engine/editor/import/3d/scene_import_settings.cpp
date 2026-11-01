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
#include "scene/resources/3d/box_shape_3d.h"
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
static constexpr real_t PREVIEW_FOV_DEGREES = 75.0;
static constexpr real_t REFERENCE_VIEWPORT_WIDTH = 1920.0;
static constexpr real_t REFERENCE_ASPECT = 16.0 / 9.0;
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

static Ref<Shape3D> create_model_hitbox_shape(const Vector3 &p_minimum, const Vector3 &p_maximum) {
	Ref<BoxShape3D> shape;
	shape.instantiate();
	const Vector3 dimensions = p_maximum - p_minimum;
	shape->set_size(Vector3(MAX(dimensions.x, 0.01), MAX(dimensions.y, 0.01), MAX(dimensions.z, 0.01)));
	return shape;
}

static String normalize_hitbox_bone_name(const String &p_name) {
	String normalized = p_name.to_lower();
	normalized = normalized.replace("_", "").replace("-", "").replace(".", "").replace(":", "").replace(" ", "");
	return normalized;
}

static int find_hitbox_preset_bone(Skeleton3D *p_skeleton, const Vector<String> &p_aliases, int p_parent_bone = -1, int *r_score = nullptr) {
	if (p_skeleton == nullptr) {
		if (r_score) {
			*r_score = -1;
		}
		return -1;
	}
	int best_bone = -1;
	int best_score = -1;
	for (int bone = 0; bone < p_skeleton->get_bone_count(); bone++) {
		if (p_parent_bone >= 0) {
			int ancestor = p_skeleton->get_bone_parent(bone);
			while (ancestor >= 0 && ancestor != p_parent_bone) {
				ancestor = p_skeleton->get_bone_parent(ancestor);
			}
			if (ancestor != p_parent_bone) {
				continue;
			}
		}
		const String normalized_bone = normalize_hitbox_bone_name(p_skeleton->get_bone_name(bone));
		const bool is_control_bone = normalized_bone.begins_with("ctrl") || normalized_bone.begins_with("control");
		const bool is_mixamo_bone = normalized_bone.begins_with("mixamorig") || normalized_bone.begins_with("mixamo");
		for (const String &alias : p_aliases) {
			const String normalized_alias = normalize_hitbox_bone_name(alias);
			if (normalized_alias.is_empty()) {
				continue;
			}
			int match_score = -1;
			if (normalized_bone == normalized_alias) {
				match_score = 1000 + normalized_alias.length();
			} else if (normalized_bone.ends_with(normalized_alias)) {
				match_score = normalized_alias.length();
			}
			// Mixamo animations drive the deform skeleton. Always prefer those
			// bones over a parallel Ctrl rig, even when the Ctrl name is an exact
			// match and the Mixamo name needs a suffix match.
			const int rig_priority = is_mixamo_bone ? 3 : (is_control_bone ? 1 : 2);
			const int score = match_score < 0 ? -1 : rig_priority * 10000 + match_score;
			if (score > best_score) {
				best_score = score;
				best_bone = bone;
			}
		}
	}
	if (r_score) {
		*r_score = best_score;
	}
	return best_bone;
}

static bool is_hitbox_digit_bone_name(const String &p_name) {
	const String normalized = normalize_hitbox_bone_name(p_name);
	return normalized.contains("thumb") || normalized.contains("finger") || normalized.contains("index") || normalized.contains("middle") || normalized.contains("ring") || normalized.contains("pinky") || normalized.contains("pinkie") || normalized.contains("little") || normalized.contains("toe");
}

static Color get_hitbox_hitgroup_color(int p_hitgroup) {
	static const Color hitgroup_colors[] = {
		Color(0.30, 0.76, 1.00), // Generic.
		Color(1.00, 0.24, 0.20), // Head.
		Color(1.00, 0.53, 0.12), // Chest.
		Color(1.00, 0.84, 0.18), // Stomach.
		Color(0.25, 0.86, 0.38), // Left arm.
		Color(0.12, 0.82, 0.83), // Right arm.
		Color(0.34, 0.47, 1.00), // Left leg.
		Color(0.78, 0.36, 1.00), // Right leg.
	};
	return hitgroup_colors[CLAMP(p_hitgroup, 0, 7)];
}

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
uniform vec3 cloud_color : source_color = vec3(1.0, 1.0, 1.0);
uniform vec3 cloud_shadow_color : source_color = vec3(0.55, 0.60, 0.70);

vec2 hash2(vec2 p) {
	p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
	return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float gradient_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(
		mix(dot(hash2(i), f), dot(hash2(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
		mix(dot(hash2(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)), dot(hash2(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x),
		u.y);
}

float cloud_noise(vec2 p) {
	float value = 0.0;
	float weight = 0.5;
	mat2 rotation = mat2(vec2(0.8, 0.6), vec2(-0.6, 0.8));
	for (int octave = 0; octave < 4; octave++) {
		value += weight * gradient_noise(p);
		p = rotation * p * 2.05 + vec2(13.5, 27.2);
		weight *= 0.5;
	}
	return clamp(value * 0.5 + 0.5, 0.0, 1.0);
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
		vec2 uv = (EYEDIR.xz / max(up + 0.12, 0.14)) * cloud_scale;
		vec2 wind = vec2(TIME * cloud_speed, TIME * cloud_speed * 0.35);
		float noise_value = cloud_noise(uv + wind);
		float threshold = 1.0 - cloud_coverage;
		float alpha = smoothstep(threshold, threshold + 0.12, noise_value) * smoothstep(0.01, 0.16, up);
		float shade = smoothstep(threshold - 0.05, threshold + 0.22, noise_value);
		float illumination = LIGHT0_ENABLED ? clamp(sun_dot * 0.35 + 0.65, 0.0, 1.0) : 0.7;
		vec3 cloud_surface = mix(cloud_shadow_color, cloud_color * (0.85 + 0.15 * illumination), shade);
		color = mix(color, cloud_surface, alpha);
	}
	COLOR = color;
}
)shader");

	Ref<ShaderMaterial> sky_material;
	sky_material.instantiate();
	sky_material->set_shader(sky_shader);
	return sky_material;
}

static Ref<Environment> create_model_view_environment(const Ref<ShaderMaterial> &p_sky_material) {
	Ref<Sky> sky;
	sky.instantiate();
	sky->set_material(p_sky_material);
	sky->set_process_mode(Sky::PROCESS_MODE_REALTIME);

	Ref<Environment> preview_environment;
	preview_environment.instantiate();
	preview_environment->set_background(Environment::BG_SKY);
	preview_environment->set_sky(sky);
	preview_environment->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
	preview_environment->set_ambient_light_energy(1.35);
	preview_environment->set_reflection_source(Environment::REFLECTION_SOURCE_SKY);
	preview_environment->set_fog_enabled(true);
	preview_environment->set_fog_mode(Environment::FOG_MODE_DEPTH);
	preview_environment->set_fog_light_color(Color(0.43, 0.51, 0.61));
	preview_environment->set_fog_light_energy(1.0);
	preview_environment->set_fog_sky_affect(0.0);
	preview_environment->set_fog_depth_begin(60.0);
	preview_environment->set_fog_depth_end(600.0);
	preview_environment->set_fog_depth_curve(1.2);
	return preview_environment;
}

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
		_save_hitbox_config();
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
		const int tab = p_type == "Mesh" ? 1 : (p_type == "Material" ? 6 : 5);
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
	Array import_hitboxes;
	for (const ModelHitbox &hitbox : model_hitboxes) {
		Dictionary item;
		item["name"] = hitbox.name;
		item["minimum"] = hitbox.minimum;
		item["maximum"] = hitbox.maximum;
		item["hitgroup"] = hitbox.hitgroup;
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
	return OK;
}

void SceneImportSettingsScreen::_update_empty_state() {
	// The empty-state hint is shown whenever the workspace has no model loaded
	// (e.g. when the user switches to the "Model" tab manually).
	empty_hint_panel->set_visible(scene == nullptr);
}

void SceneImportSettingsScreen::_close_screen() {
	_save_lod_config();
	_save_hitbox_config();
	if (lod_edit_timer) {
		lod_edit_timer->stop();
	}
	if (scene) {
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
				animation_slider->set_value_no_signal(animation_player->get_current_animation_position() / animation_player->get_current_animation_length());
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
	tree_split->connect(SNAME("dragged"), callable_mp(this, &SceneImportSettingsScreen::_tree_split_dragged));

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

	settings_tab_navigation = memnew(Control);
	settings_tab_navigation->set_custom_minimum_size(Size2(40 * EDSCALE, 0));
	settings_tab_navigation->set_h_size_flags(Control::SIZE_FILL);
	settings_tab_navigation->set_v_size_flags(Control::SIZE_EXPAND_FILL);
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
	settings_tab_bar->set_rotation(Math::deg_to_rad(90.0));
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
	preview_sun_animation_timer = memnew(Timer);
	preview_sun_animation_timer->set_wait_time(0.1);
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
	const real_t content_minimum_width = data_mode->get_combined_minimum_size().x;
	const real_t saved_content_width = MAX((real_t)EDITOR_DEF(MODEL_CONTENT_PANEL_WIDTH_SETTING, MODEL_CONTENT_PANEL_DEFAULT_WIDTH) * EDSCALE, content_minimum_width);
	tree_split->set_split_offset(Math::round(saved_content_width - content_minimum_width));
	const real_t panel_minimum_width = settings_panel_layout->get_combined_minimum_size().x;
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
	_apply_import_materials_to_mesh_preview(md.live_preview_mesh);
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

void SceneImportSettingsScreen::_build_hitbox_editor(VBoxContainer *p_tab) {
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	p_tab->add_child(scroll);
	VBoxContainer *content = memnew(VBoxContainer);
	content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(content);

	Label *hint = memnew(Label);
	hint->set_text(TTR("Body presets create approximate boxes from model bounds and use matching humanoid bones when available. Adjust each preset to fit the model."));
	hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	content->add_child(hint);
	Button *auto_generate_button = memnew(Button);
	auto_generate_button->set_text(TTR("Auto Generate Hitboxes"));
	auto_generate_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Skeleton3D"), EditorStringName(EditorIcons)));
	auto_generate_button->set_tooltip_text(TTR("Generate missing humanoid hitboxes from the model bounds and skeleton"));
	auto_generate_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	auto_generate_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_auto_generate_hitboxes));
	content->add_child(auto_generate_button);

	HBoxContainer *list_toolbar = memnew(HBoxContainer);
	content->add_child(list_toolbar);
	Label *list_title = memnew(Label);
	list_title->set_text(TTR("Hitbox List"));
	list_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	list_toolbar->add_child(list_title);
	hitbox_count_label = memnew(Label);
	list_toolbar->add_child(hitbox_count_label);
	Button *add_button = memnew(Button);
	add_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Add"), EditorStringName(EditorIcons)));
	add_button->set_tooltip_text(TTR("Add Hitbox"));
	add_button->set_flat(true);
	add_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_add_hitbox));
	list_toolbar->add_child(add_button);
	MenuButton *preset_button = memnew(MenuButton);
	preset_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Skeleton3D"), EditorStringName(EditorIcons)));
	preset_button->set_tooltip_text(TTR("Add Body-Part Presets"));
	preset_button->set_flat(true);
	list_toolbar->add_child(preset_button);
	PopupMenu *preset_menu = preset_button->get_popup();
	preset_menu->add_item(TTR("Add Humanoid Set"), HITBOX_PRESET_HUMANOID_SET);
	preset_menu->add_separator();
	preset_menu->add_item(TTR("Head"), HITBOX_PRESET_HEAD);
	preset_menu->add_item(TTR("Chest"), HITBOX_PRESET_CHEST);
	preset_menu->add_item(TTR("Stomach"), HITBOX_PRESET_STOMACH);
	preset_menu->add_item(TTR("Left Arm"), HITBOX_PRESET_LEFT_ARM);
	preset_menu->add_item(TTR("Right Arm"), HITBOX_PRESET_RIGHT_ARM);
	preset_menu->add_item(TTR("Left Leg"), HITBOX_PRESET_LEFT_LEG);
	preset_menu->add_item(TTR("Right Leg"), HITBOX_PRESET_RIGHT_LEG);
	preset_menu->connect(SceneStringName(id_pressed), callable_mp(this, &SceneImportSettingsScreen::_hitbox_preset_selected));
	hitbox_duplicate_button = memnew(Button);
	hitbox_duplicate_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Duplicate"), EditorStringName(EditorIcons)));
	hitbox_duplicate_button->set_tooltip_text(TTR("Duplicate Hitbox"));
	hitbox_duplicate_button->set_flat(true);
	hitbox_duplicate_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_duplicate_hitbox));
	list_toolbar->add_child(hitbox_duplicate_button);
	hitbox_remove_button = memnew(Button);
	hitbox_remove_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Remove"), EditorStringName(EditorIcons)));
	hitbox_remove_button->set_tooltip_text(TTR("Remove Hitbox"));
	hitbox_remove_button->set_flat(true);
	hitbox_remove_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_remove_hitbox));
	list_toolbar->add_child(hitbox_remove_button);

	hitbox_tree = memnew(Tree);
	hitbox_tree->set_columns(2);
	hitbox_tree->set_hide_root(true);
	hitbox_tree->set_column_titles_visible(true);
	hitbox_tree->set_column_title(0, TTR("Name"));
	hitbox_tree->set_column_title(1, TTR("Hitgroup"));
	hitbox_tree->set_column_expand(0, true);
	hitbox_tree->set_column_custom_minimum_width(0, 120 * EDSCALE);
	hitbox_tree->set_column_expand(1, false);
	hitbox_tree->set_column_custom_minimum_width(1, 105 * EDSCALE);
	hitbox_tree->set_custom_minimum_size(Size2(0, 150 * EDSCALE));
	hitbox_tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	// Keep the list at its natural full height so every hitbox is visible at
	// once. The surrounding ScrollContainer remains available for the inspector
	// fields below the list when the panel is short.
	hitbox_tree->set_v_size_flags(Control::SIZE_FILL);
	hitbox_tree->connect(SceneStringName(item_selected), callable_mp(this, &SceneImportSettingsScreen::_hitbox_tree_selected));
	content->add_child(hitbox_tree);

	Label *selected_hitbox_label = memnew(Label);
	selected_hitbox_label->set_text(TTR("Selected Hitbox"));
	content->add_child(selected_hitbox_label);

	HBoxContainer *name_row = memnew(HBoxContainer);
	content->add_child(name_row);
	Label *name_label = memnew(Label);
	name_label->set_text(TTR("Name"));
	name_label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
	name_row->add_child(name_label);
	hitbox_name_input = memnew(LineEdit);
	hitbox_name_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	hitbox_name_input->connect(SceneStringName(text_changed), callable_mp(this, &SceneImportSettingsScreen::_hitbox_name_changed));
	name_row->add_child(hitbox_name_input);

	HBoxContainer *group_row = memnew(HBoxContainer);
	content->add_child(group_row);
	Label *group_label = memnew(Label);
	group_label->set_text(TTR("Hitgroup"));
	group_label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
	group_row->add_child(group_label);
	hitbox_group_option = memnew(OptionButton);
	hitbox_group_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	const String hitgroup_names[] = { TTR("Generic"), TTR("Head"), TTR("Chest"), TTR("Stomach"), TTR("Left Arm"), TTR("Right Arm"), TTR("Left Leg"), TTR("Right Leg") };
	for (int i = 0; i < 8; i++) {
		hitbox_group_option->add_item(vformat("%d: %s", i, hitgroup_names[i]), i);
	}
	hitbox_group_option->connect(SceneStringName(item_selected), callable_mp(this, &SceneImportSettingsScreen::_hitbox_group_changed));
	group_row->add_child(hitbox_group_option);

	HBoxContainer *attachment_row = memnew(HBoxContainer);
	content->add_child(attachment_row);
	Label *attachment_label = memnew(Label);
	attachment_label->set_text(TTR("Bone"));
	attachment_label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
	attachment_row->add_child(attachment_label);
	hitbox_attachment_option = memnew(OptionButton);
	hitbox_attachment_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	hitbox_attachment_option->connect(SceneStringName(item_selected), callable_mp(this, &SceneImportSettingsScreen::_hitbox_attachment_changed));
	attachment_row->add_child(hitbox_attachment_option);

	Label *bounds_label = memnew(Label);
	bounds_label->set_text(TTR("Bounds"));
	content->add_child(bounds_label);
	const String axis_names[3] = { TTR("X"), TTR("Y"), TTR("Z") };
	auto add_vector_row = [&](const String &p_title, SpinBox **r_inputs, int p_field) {
		HBoxContainer *row = memnew(HBoxContainer);
		content->add_child(row);
		Label *label = memnew(Label);
		label->set_text(p_title);
		label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
		row->add_child(label);
		for (int axis = 0; axis < 3; axis++) {
			HBoxContainer *axis_row = memnew(HBoxContainer);
			axis_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			Label *axis_label = memnew(Label);
			axis_label->set_text(axis_names[axis]);
			axis_row->add_child(axis_label);
			SpinBox *input = memnew(SpinBox);
			input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			input->set_min(-10000.0);
			input->set_max(10000.0);
			input->set_step(0.01);
			input->set_allow_greater(true);
			input->set_allow_lesser(true);
			input->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_hitbox_transform_changed).bind(p_field, axis));
			axis_row->add_child(input);
			row->add_child(axis_row);
			r_inputs[axis] = input;
		}
	};
	add_vector_row(TTR("Minimum"), hitbox_minimum_inputs, 0);
	add_vector_row(TTR("Maximum"), hitbox_maximum_inputs, 1);

	for (int hitgroup = 0; hitgroup < 8; hitgroup++) {
		Ref<StandardMaterial3D> normal_material;
		normal_material.instantiate();
		normal_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		normal_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		normal_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
		normal_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		normal_material->set_render_priority(1);
		Color normal_color = get_hitbox_hitgroup_color(hitgroup);
		normal_color.a = 0.28;
		normal_material->set_albedo(normal_color);
		hitbox_preview_materials[hitgroup] = normal_material;

		Ref<StandardMaterial3D> selected_material;
		selected_material.instantiate();
		selected_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		selected_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		selected_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
		selected_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		selected_material->set_render_priority(1);
		Color selected_color = get_hitbox_hitgroup_color(hitgroup).lightened(0.18);
		selected_color.a = 0.48;
		selected_material->set_albedo(selected_color);
		hitbox_selected_preview_materials[hitgroup] = selected_material;
	}

	_populate_hitbox_tree();
	_select_hitbox(-1);
}

void SceneImportSettingsScreen::_populate_hitbox_tree() {
	if (!hitbox_tree) {
		return;
	}
	const String hitgroup_names[] = { TTR("Generic"), TTR("Head"), TTR("Chest"), TTR("Stomach"), TTR("Left Arm"), TTR("Right Arm"), TTR("Left Leg"), TTR("Right Leg") };
	hitbox_ui_updating = true;
	hitbox_tree->clear();
	if (hitbox_count_label) {
		hitbox_count_label->set_text(vformat(TTR("%d hitboxes"), model_hitboxes.size()));
	}
	TreeItem *root = hitbox_tree->create_item();
	for (int i = 0; i < model_hitboxes.size(); i++) {
		const ModelHitbox &hitbox = model_hitboxes[i];
		TreeItem *item = hitbox_tree->create_item(root);
		item->set_text(0, hitbox.name);
		item->set_text(1, vformat("%d: %s", CLAMP(hitbox.hitgroup, 0, 7), hitgroup_names[CLAMP(hitbox.hitgroup, 0, 7)]));
		item->set_custom_color(1, get_hitbox_hitgroup_color(hitbox.hitgroup));
		item->set_meta("hitbox_index", i);
		item->set_selectable(0, true);
		item->set_selectable(1, false);
		if (i == selected_hitbox) {
			item->select(0);
		}
	}
	const real_t tree_row_height = (hitbox_tree->get_theme_default_font_size() + 10) * EDSCALE;
	hitbox_tree->set_custom_minimum_size(Size2(0, MAX(150 * EDSCALE, (model_hitboxes.size() + 1) * tree_row_height + 8 * EDSCALE)));
	hitbox_ui_updating = false;
}

void SceneImportSettingsScreen::_populate_hitbox_attachments() {
	if (!hitbox_attachment_option) {
		return;
	}
	hitbox_ui_updating = true;
	hitbox_attachment_option->clear();
	hitbox_attachment_option->add_item(TTR("Model Root"));
	hitbox_attachment_option->set_item_metadata(0, Dictionary());
	int selected = 0;
	if (selected_hitbox >= 0 && selected_hitbox < model_hitboxes.size()) {
		const ModelHitbox &hitbox = model_hitboxes[selected_hitbox];
		for (Skeleton3D *skeleton : skeletons) {
			if (skeleton == nullptr) {
				continue;
			}
			const String skeleton_path = scene ? String(scene->get_path_to(skeleton)) : String();
			for (int bone = 0; bone < skeleton->get_bone_count(); bone++) {
				const String bone_name = skeleton->get_bone_name(bone);
				Dictionary attachment;
				attachment["skeleton_path"] = skeleton_path;
				attachment["bone_name"] = bone_name;
				const int item_index = hitbox_attachment_option->get_item_count();
				hitbox_attachment_option->add_item(vformat(TTR("%s / %s"), skeleton->get_name(), bone_name));
				hitbox_attachment_option->set_item_metadata(item_index, attachment);
				if (skeleton_path == hitbox.skeleton_path && bone_name == hitbox.bone_name) {
					selected = item_index;
				}
			}
		}
	}
	hitbox_attachment_option->select(selected);
	hitbox_attachment_option->set_disabled(selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size());
	hitbox_ui_updating = false;
}

void SceneImportSettingsScreen::_select_hitbox(int p_index, bool p_refresh_tree) {
	selected_hitbox = (p_index >= 0 && p_index < model_hitboxes.size()) ? p_index : -1;
	if (hitbox_name_input) {
		hitbox_ui_updating = true;
		const bool valid = selected_hitbox >= 0;
		hitbox_name_input->set_text(valid ? model_hitboxes[selected_hitbox].name : String());
		hitbox_name_input->set_editable(valid);
		hitbox_group_option->set_disabled(!valid);
		for (int axis = 0; axis < 3; axis++) {
			hitbox_minimum_inputs[axis]->set_editable(valid);
			hitbox_maximum_inputs[axis]->set_editable(valid);
			if (valid) {
				hitbox_minimum_inputs[axis]->set_value_no_signal(model_hitboxes[selected_hitbox].minimum[axis]);
				hitbox_maximum_inputs[axis]->set_value_no_signal(model_hitboxes[selected_hitbox].maximum[axis]);
			} else {
				hitbox_minimum_inputs[axis]->set_value_no_signal(-0.5);
				hitbox_maximum_inputs[axis]->set_value_no_signal(0.5);
			}
		}
		if (valid) {
			hitbox_group_option->select(model_hitboxes[selected_hitbox].hitgroup);
		}
		hitbox_ui_updating = false;
	}
	if (hitbox_duplicate_button) {
		hitbox_duplicate_button->set_disabled(selected_hitbox < 0);
	}
	if (hitbox_remove_button) {
		hitbox_remove_button->set_disabled(selected_hitbox < 0);
	}
	_populate_hitbox_attachments();
	if (p_refresh_tree) {
		_populate_hitbox_tree();
	}
	_update_hitbox_preview();
}

void SceneImportSettingsScreen::_add_hitbox() {
	ModelHitbox hitbox;
	hitbox.name = vformat(TTR("Hitbox %d"), model_hitboxes.size() + 1);
	if (scene) {
		const Vector3 half_size = Vector3(MAX(contents_aabb.size.x * 0.4, 0.05), MAX(contents_aabb.size.y * 0.4, 0.05), MAX(contents_aabb.size.z * 0.4, 0.05));
		hitbox.minimum = contents_aabb.get_center() - half_size;
		hitbox.maximum = contents_aabb.get_center() + half_size;
	}
	model_hitboxes.push_back(hitbox);
	selected_hitbox = model_hitboxes.size() - 1;
	_select_hitbox(selected_hitbox);
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_duplicate_hitbox() {
	if (selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	ModelHitbox duplicate = model_hitboxes[selected_hitbox];
	const String base_name = duplicate.name + TTR(" Copy");
	duplicate.name = base_name;
	int copy_number = 2;
	bool name_in_use = true;
	while (name_in_use) {
		name_in_use = false;
		for (const ModelHitbox &existing : model_hitboxes) {
			if (existing.name == duplicate.name) {
				duplicate.name = vformat(TTR("%s %d"), base_name, copy_number++);
				name_in_use = true;
				break;
			}
		}
	}
	model_hitboxes.push_back(duplicate);
	selected_hitbox = model_hitboxes.size() - 1;
	_select_hitbox(selected_hitbox);
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_auto_generate_hitboxes() {
	_generate_hitbox_preset(HITBOX_PRESET_HUMANOID_SET, true);
}

void SceneImportSettingsScreen::_hitbox_preset_selected(int p_preset) {
	_generate_hitbox_preset(p_preset, false);
}

void SceneImportSettingsScreen::_generate_hitbox_preset(int p_preset, bool p_only_missing) {
	if (p_preset < HITBOX_PRESET_HUMANOID_SET || p_preset > HITBOX_PRESET_RIGHT_LEG) {
		return;
	}

	static const char *preset_names[] = { "Head", "Chest", "Stomach", "Left Arm", "Right Arm", "Left Leg", "Right Leg" };
	static const int preset_hitgroups[] = { 1, 2, 3, 4, 5, 6, 7 };
	static const Vector3 preset_minimum_fractions[] = {
		Vector3(0.34, 0.82, 0.25),
		Vector3(0.26, 0.59, 0.25),
		Vector3(0.31, 0.40, 0.30),
		Vector3(0.02, 0.38, 0.36),
		Vector3(0.52, 0.38, 0.36),
		Vector3(0.35, 0.00, 0.28),
		Vector3(0.51, 0.00, 0.28),
	};
	static const Vector3 preset_maximum_fractions[] = {
		Vector3(0.66, 1.00, 0.75),
		Vector3(0.74, 0.84, 0.75),
		Vector3(0.69, 0.64, 0.70),
		Vector3(0.48, 0.82, 0.64),
		Vector3(0.98, 0.82, 0.64),
		Vector3(0.49, 0.54, 0.72),
		Vector3(0.65, 0.54, 0.72),
	};
	static const char *bone_aliases[][10] = {
		{ "head", nullptr },
		{ "chest", "upperchest", "spine3", "spine03", "spine2", nullptr },
		{ "abdomen", "spine1", "spine02", "spine", "pelvis", "hips", nullptr },
		{ "leftupperarm", "upperarmleft", "upperarml", "lupperarm", "leftarm", "armleft", "arml", "larm", nullptr },
		{ "rightupperarm", "upperarmright", "upperarmr", "rupperarm", "rightarm", "armright", "armr", "rarm", nullptr },
		{ "leftupleg", "uplegleft", "uplegl", "lupleg", "leftthigh", "thighleft", "thighl", "lthigh", "leftleg", "legl" },
		{ "rightupleg", "uplegright", "uplegr", "rupleg", "rightthigh", "thighright", "thighr", "rthigh", "rightleg", "legr" },
	};
	static const char *forearm_bone_aliases[][9] = {
		{ "leftforearm", "forearmleft", "forearml", "lforearm", "leftlowerarm", "lowerarmleft", "lowerarml", "llowerarm", nullptr },
		{ "rightforearm", "forearmright", "forearmr", "rforearm", "rightlowerarm", "lowerarmright", "lowerarmr", "rlowerarm", nullptr },
	};
	static const char *lower_leg_bone_aliases[][14] = {
		{ "leftlowerleg", "lowerlegleft", "lowerlegl", "llowerleg", "leftcalf", "calfleft", "calfl", "lcalf", "leftshin", "shinl", "leftleg", "legleft", "legl", "lleg" },
		{ "rightlowerleg", "lowerlegright", "lowerlegr", "rlowerleg", "rightcalf", "calfright", "calfr", "rcalf", "rightshin", "shinr", "rightleg", "legright", "legr", "rleg" },
	};
	const String segment_names[4][2] = {
		{ TTR("Left Upper Arm"), TTR("Left Forearm") },
		{ TTR("Right Upper Arm"), TTR("Right Forearm") },
		{ TTR("Left Thigh"), TTR("Left Lower Leg") },
		{ TTR("Right Thigh"), TTR("Right Lower Leg") },
	};
	const int first_part = p_preset == HITBOX_PRESET_HUMANOID_SET ? 0 : p_preset - HITBOX_PRESET_HEAD;
	const int last_part = p_preset == HITBOX_PRESET_HUMANOID_SET ? 6 : first_part;
	Node3D *scene_root = Object::cast_to<Node3D>(scene);
	int first_added = -1;
	auto make_aliases = [](const char *const *p_aliases, int p_max_count) {
		Vector<String> aliases;
		for (int i = 0; i < p_max_count && p_aliases[i] != nullptr; i++) {
			aliases.push_back(String(p_aliases[i]));
		}
		return aliases;
	};
	auto include_point = [](const Vector3 &p_point, Vector3 &r_minimum, Vector3 &r_maximum, bool &r_first) {
		if (r_first) {
			r_minimum = p_point;
			r_maximum = p_point;
			r_first = false;
			return;
		}
		for (int axis = 0; axis < 3; axis++) {
			r_minimum[axis] = MIN(r_minimum[axis], p_point[axis]);
			r_maximum[axis] = MAX(r_maximum[axis], p_point[axis]);
		}
	};
	auto transform_bounds = [](const Vector3 &p_minimum, const Vector3 &p_maximum, const Transform3D &p_transform, Vector3 &r_minimum, Vector3 &r_maximum) {
		bool first_corner = true;
		for (int x = 0; x < 2; x++) {
			for (int y = 0; y < 2; y++) {
				for (int z = 0; z < 2; z++) {
					const Vector3 corner(x == 0 ? p_minimum.x : p_maximum.x, y == 0 ? p_minimum.y : p_maximum.y, z == 0 ? p_minimum.z : p_maximum.z);
					const Vector3 transformed_corner = p_transform.xform(corner);
					if (first_corner) {
						r_minimum = transformed_corner;
						r_maximum = transformed_corner;
						first_corner = false;
					} else {
						for (int axis = 0; axis < 3; axis++) {
							r_minimum[axis] = MIN(r_minimum[axis], transformed_corner[axis]);
							r_maximum[axis] = MAX(r_maximum[axis], transformed_corner[axis]);
						}
					}
				}
			}
		}
	};
	auto unique_hitbox_name = [&](const String &p_base_name) {
		String name = p_base_name;
		int suffix = 2;
		while (true) {
			bool in_use = false;
			for (const ModelHitbox &existing : model_hitboxes) {
				if (existing.name == name) {
					in_use = true;
					break;
				}
			}
			if (!in_use) {
				return name;
			}
			name = vformat(TTR("%s %d"), p_base_name, suffix++);
		}
	};
	auto convert_root_bounds_to_bone = [&](const Vector3 &p_root_minimum, const Vector3 &p_root_maximum, Skeleton3D *p_skeleton, int p_bone, Vector3 &r_minimum, Vector3 &r_maximum) {
		if (!scene_root || !p_skeleton || p_bone < 0) {
			r_minimum = p_root_minimum;
			r_maximum = p_root_maximum;
			return;
		}
		const Transform3D root_to_bone = p_skeleton->get_bone_global_rest(p_bone).affine_inverse() * p_skeleton->get_global_transform().affine_inverse() * scene_root->get_global_transform();
		transform_bounds(p_root_minimum, p_root_maximum, root_to_bone, r_minimum, r_maximum);
	};

	for (int part = first_part; part <= last_part; part++) {
		const bool is_arm_part = part == 3 || part == 4;
		const bool is_leg_part = part == 5 || part == 6;
		const bool is_limb_part = is_arm_part || is_leg_part;
		const int side = part == 3 || part == 5 ? 0 : 1;
		Vector<String> limb_names;
		if (is_limb_part) {
			limb_names.push_back(segment_names[part - 3][0]);
			limb_names.push_back(segment_names[part - 3][1]);
		}
		bool migrate_legacy_hitbox = false;
		int legacy_hitbox_index = -1;
		Vector3 legacy_root_minimum;
		Vector3 legacy_root_maximum;
		if (p_only_missing) {
			int group_count = 0;
			bool segment_exists = false;
			for (int i = 0; i < model_hitboxes.size(); i++) {
				const ModelHitbox &existing = model_hitboxes[i];
				if (existing.hitgroup == preset_hitgroups[part]) {
					group_count++;
					if (is_limb_part && (existing.name == limb_names[0] || existing.name == limb_names[1])) {
						segment_exists = true;
					}
					if (existing.name == TTR(preset_names[part])) {
						legacy_hitbox_index = i;
					}
				}
			}
			if (group_count > 0) {
				// Earlier versions generated one default-named box for each limb.
				// Split that box on the next auto-generation while retaining its
				// edited bounds. Leave custom and already-segmented hitboxes intact.
				migrate_legacy_hitbox = is_limb_part && group_count == 1 && !segment_exists && legacy_hitbox_index >= 0;
				if (!migrate_legacy_hitbox) {
					continue;
				}
				const ModelHitbox &legacy = model_hitboxes[legacy_hitbox_index];
				Transform3D old_bone_to_root;
				if (scene_root && !legacy.skeleton_path.is_empty() && !legacy.bone_name.is_empty()) {
					Skeleton3D *old_skeleton = Object::cast_to<Skeleton3D>(scene_root->get_node_or_null(NodePath(legacy.skeleton_path)));
					const int old_bone = old_skeleton ? old_skeleton->find_bone(legacy.bone_name) : -1;
					if (old_skeleton && old_bone >= 0) {
						old_bone_to_root = scene_root->get_global_transform().affine_inverse() * old_skeleton->get_global_transform() * old_skeleton->get_bone_global_rest(old_bone);
					}
				}
				transform_bounds(legacy.minimum, legacy.maximum, old_bone_to_root, legacy_root_minimum, legacy_root_maximum);
			}
		}

		Vector<String> aliases = make_aliases(bone_aliases[part], 10);

		Skeleton3D *matched_skeleton = nullptr;
		int matched_bone = -1;
		int matched_score = -1;
		if (scene_root) {
			for (Skeleton3D *skeleton : skeletons) {
				int candidate_score = -1;
				const int candidate_bone = find_hitbox_preset_bone(skeleton, aliases, -1, &candidate_score);
				if (candidate_bone >= 0 && candidate_score > matched_score) {
					matched_skeleton = skeleton;
					matched_bone = candidate_bone;
					matched_score = candidate_score;
				}
			}
		}

		Vector3 minimum_fraction = preset_minimum_fractions[part];
		Vector3 maximum_fraction = preset_maximum_fractions[part];
		const bool is_left_side = part == 3 || part == 5;
		const bool is_right_side = part == 4 || part == 6;
		if ((is_left_side || is_right_side) && matched_skeleton && matched_bone >= 0 && contents_aabb.size.x > 0.0) {
			const Transform3D skeleton_to_scene = scene_root->get_global_transform().affine_inverse() * matched_skeleton->get_global_transform();
			const Vector3 bone_position = skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(matched_bone).origin);
			const real_t side_offset = bone_position.x - contents_aabb.get_center().x;
			if (Math::abs(side_offset) > contents_aabb.size.x * 0.025) {
				const int detected_side = side_offset < 0.0 ? -1 : 1;
				const int preset_side = is_left_side ? -1 : 1;
				if (detected_side != preset_side) {
					const real_t old_minimum_x = minimum_fraction.x;
					minimum_fraction.x = 1.0 - maximum_fraction.x;
					maximum_fraction.x = 1.0 - old_minimum_x;
				}
			}
		}

		Vector3 fallback_minimum = contents_aabb.position + contents_aabb.size * minimum_fraction;
		Vector3 fallback_maximum = contents_aabb.position + contents_aabb.size * maximum_fraction;
		const Vector3 fallback_center = (fallback_minimum + fallback_maximum) * 0.5;
		for (int axis = 0; axis < 3; axis++) {
			if (fallback_maximum[axis] - fallback_minimum[axis] < 0.02) {
				fallback_minimum[axis] = fallback_center[axis] - 0.01;
				fallback_maximum[axis] = fallback_center[axis] + 0.01;
			}
		}

		if (migrate_legacy_hitbox) {
			model_hitboxes.remove_at(legacy_hitbox_index);
			if (selected_hitbox == legacy_hitbox_index) {
				selected_hitbox = -1;
			} else if (selected_hitbox > legacy_hitbox_index) {
				selected_hitbox--;
			}
			if (first_added > legacy_hitbox_index) {
				first_added--;
			}
		}

		Vector3 split_minimum = migrate_legacy_hitbox ? legacy_root_minimum : fallback_minimum;
		Vector3 split_maximum = migrate_legacy_hitbox ? legacy_root_maximum : fallback_maximum;
		int split_axis = 1;
		real_t split_direction = 1.0;
		if (is_arm_part) {
			split_axis = split_maximum.x - split_minimum.x >= split_maximum.z - split_minimum.z ? 0 : 2;
			split_direction = is_left_side ? -1.0 : 1.0;
		} else if (is_leg_part) {
			split_axis = 1;
			split_direction = 1.0; // Thigh is the upper section of the leg.
		}
		Vector3 segment_joint = Vector3();
		int lower_bone = -1;
		if (matched_skeleton && matched_bone >= 0 && is_limb_part) {
			const Vector<String> lower_aliases = is_arm_part ? make_aliases(forearm_bone_aliases[side], 9) : make_aliases(lower_leg_bone_aliases[side], 14);
			// The lower segment must be a descendant of the upper segment. This
			// prevents a partial rig's fallback alias from binding both boxes to
			// the same bone (which makes the lower box drift during animation).
			lower_bone = find_hitbox_preset_bone(matched_skeleton, lower_aliases, matched_bone);
			if (lower_bone >= 0) {
				const Transform3D skeleton_to_scene = scene_root->get_global_transform().affine_inverse() * matched_skeleton->get_global_transform();
				segment_joint = skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(lower_bone).origin);
				const Vector3 upper_joint = skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(matched_bone).origin);
				Vector3 direction = segment_joint - upper_joint;
				if (is_arm_part) {
					const real_t horizontal_length = MAX(Math::abs(direction.x), Math::abs(direction.z));
					split_axis = Math::abs(direction.x) >= Math::abs(direction.z) ? 0 : 2;
					if (horizontal_length < 0.001) {
						split_axis = split_maximum.x - split_minimum.x >= split_maximum.z - split_minimum.z ? 0 : 2;
					}
				} else {
					split_axis = 1;
				}
				split_direction = direction[split_axis] < 0.0 ? -1.0 : 1.0;
			}
		}
		const real_t split_extent = split_maximum[split_axis] - split_minimum[split_axis];
		real_t split_value = (split_minimum[split_axis] + split_maximum[split_axis]) * 0.5;
		if (migrate_legacy_hitbox && lower_bone >= 0) {
			const real_t joint_fraction = (segment_joint[split_axis] - split_minimum[split_axis]) / MAX(split_extent, 0.001);
			if (joint_fraction > 0.2 && joint_fraction < 0.8) {
				split_value = segment_joint[split_axis];
			}
		}
		Vector3 segment_minimum[2] = { split_minimum, split_minimum };
		Vector3 segment_maximum[2] = { split_maximum, split_maximum };
		const real_t overlap = MAX(contents_aabb.size.y * 0.0125, 0.01);
		if (split_direction >= 0.0) {
			segment_maximum[0][split_axis] = MIN(split_maximum[split_axis], split_value + overlap);
			segment_minimum[1][split_axis] = MAX(split_minimum[split_axis], split_value - overlap);
		} else {
			segment_minimum[0][split_axis] = MAX(split_minimum[split_axis], split_value - overlap);
			segment_maximum[1][split_axis] = MIN(split_maximum[split_axis], split_value + overlap);
		}

		const int segment_count = is_limb_part ? 2 : 1;
		for (int segment = 0; segment < segment_count; segment++) {
			Vector3 root_minimum = is_limb_part ? segment_minimum[segment] : fallback_minimum;
			Vector3 root_maximum = is_limb_part ? segment_maximum[segment] : fallback_maximum;
			int attachment_bone = matched_bone;
			if (is_limb_part && segment == 1 && lower_bone >= 0) {
				attachment_bone = lower_bone;
			}

			// For rigged models, derive each segment from its own joint chain.
			// The lower box follows the wrist/hand or ankle/foot root, while
			// excluding fingers and toes so they cannot pull the box off the limb.
			if (is_limb_part && matched_skeleton && matched_bone >= 0 && lower_bone >= 0 && !migrate_legacy_hitbox) {
				Vector3 points_minimum;
				Vector3 points_maximum;
				bool first_point = true;
				const Transform3D skeleton_to_scene = scene_root->get_global_transform().affine_inverse() * matched_skeleton->get_global_transform();
				if (segment == 0) {
					include_point(skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(matched_bone).origin), points_minimum, points_maximum, first_point);
					include_point(skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(lower_bone).origin), points_minimum, points_maximum, first_point);
				} else {
					for (int bone = 0; bone < matched_skeleton->get_bone_count(); bone++) {
						int ancestor = bone;
						while (ancestor >= 0 && ancestor != lower_bone) {
							ancestor = matched_skeleton->get_bone_parent(ancestor);
						}
						if (ancestor == lower_bone) {
							if (is_hitbox_digit_bone_name(matched_skeleton->get_bone_name(bone))) {
								continue;
							}
							include_point(skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(bone).origin), points_minimum, points_maximum, first_point);
						}
					}
				}
				if (!first_point) {
					const real_t padding = MAX(contents_aabb.size.y * 0.025, 0.025);
					for (int axis = 0; axis < 3; axis++) {
						root_minimum[axis] = points_minimum[axis] - padding;
						root_maximum[axis] = points_maximum[axis] + padding;
					}
				}
			}

			ModelHitbox hitbox;
			hitbox.name = unique_hitbox_name(is_limb_part ? limb_names[segment] : TTR(preset_names[part]));
			hitbox.hitgroup = preset_hitgroups[part];
			if (matched_skeleton && matched_bone >= 0 && scene_root) {
				convert_root_bounds_to_bone(root_minimum, root_maximum, matched_skeleton, attachment_bone, hitbox.minimum, hitbox.maximum);
				hitbox.skeleton_path = String(scene->get_path_to(matched_skeleton));
				hitbox.bone_name = matched_skeleton->get_bone_name(attachment_bone >= 0 ? attachment_bone : matched_bone);
			} else {
				hitbox.minimum = root_minimum;
				hitbox.maximum = root_maximum;
			}

			model_hitboxes.push_back(hitbox);
			if (first_added < 0) {
				first_added = model_hitboxes.size() - 1;
			}
		}
	}

	if (first_added >= 0) {
		_select_hitbox(first_added);
		_save_hitbox_config();
	}
}

void SceneImportSettingsScreen::_remove_hitbox() {
	if (selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	model_hitboxes.remove_at(selected_hitbox);
	selected_hitbox = MIN(selected_hitbox, model_hitboxes.size() - 1);
	_select_hitbox(selected_hitbox);
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_tree_selected() {
	if (hitbox_ui_updating || !hitbox_tree) {
		return;
	}
	TreeItem *item = hitbox_tree->get_selected();
	if (item) {
		// The Tree already has the clicked item selected. Rebuilding it here would
		// delete that item while Tree::select_single_item() is still emitting its
		// selection signal.
		_select_hitbox((int)item->get_meta("hitbox_index", -1), false);
	}
}

void SceneImportSettingsScreen::_hitbox_name_changed(const String &p_name) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	model_hitboxes.write[selected_hitbox].name = p_name;
	_populate_hitbox_tree();
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_group_changed(int p_group) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	model_hitboxes.write[selected_hitbox].hitgroup = CLAMP(p_group, 0, 7);
	_populate_hitbox_tree();
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_attachment_changed(int p_attachment) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size() || !hitbox_attachment_option || p_attachment < 0) {
		return;
	}
	Dictionary attachment = hitbox_attachment_option->get_item_metadata(p_attachment);
	ModelHitbox &hitbox = model_hitboxes.write[selected_hitbox];
	hitbox.skeleton_path = attachment.get("skeleton_path", String());
	hitbox.bone_name = attachment.get("bone_name", String());
	_update_hitbox_preview();
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_transform_changed(double p_value, int p_field, int p_axis) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size() || p_axis < 0 || p_axis > 2) {
		return;
	}
	ModelHitbox &hitbox = model_hitboxes.write[selected_hitbox];
	if (p_field == 0) {
		hitbox.minimum[p_axis] = p_value;
		if (hitbox.minimum[p_axis] > hitbox.maximum[p_axis] - 0.01) {
			hitbox.maximum[p_axis] = hitbox.minimum[p_axis] + 0.01;
			hitbox_maximum_inputs[p_axis]->set_value_no_signal(hitbox.maximum[p_axis]);
		}
	} else if (p_field == 1) {
		hitbox.maximum[p_axis] = p_value;
		if (hitbox.maximum[p_axis] < hitbox.minimum[p_axis] + 0.01) {
			hitbox.minimum[p_axis] = hitbox.maximum[p_axis] - 0.01;
			hitbox_minimum_inputs[p_axis]->set_value_no_signal(hitbox.minimum[p_axis]);
		}
	}
	_update_hitbox_preview();
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_save_hitbox_config() {
	if (base_path.is_empty()) {
		return;
	}
	Array saved_hitboxes;
	for (const ModelHitbox &hitbox : model_hitboxes) {
		Dictionary item;
		item["name"] = hitbox.name;
		item["minimum"] = hitbox.minimum;
		item["maximum"] = hitbox.maximum;
		item["hitgroup"] = hitbox.hitgroup;
		item["skeleton_path"] = hitbox.skeleton_path;
		item["bone_name"] = hitbox.bone_name;
		saved_hitboxes.push_back(item);
	}
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("hitboxes", "version", 1);
	config->set_value("hitboxes", "items", saved_hitboxes);
	if (config->save(base_path + ".hitboxcfg") != OK) {
		ERR_PRINT("Unable to save model hitbox settings: " + base_path + ".hitboxcfg");
	}
}

void SceneImportSettingsScreen::_load_hitbox_config() {
	model_hitboxes.clear();
	selected_hitbox = -1;
	if (base_path.is_empty()) {
		return;
	}
	Array saved_hitboxes;
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(base_path + ".hitboxcfg") == OK) {
		saved_hitboxes = config->get_value("hitboxes", "items", Array());
	} else if (defaults.has("fps_studio/hitboxes")) {
		saved_hitboxes = defaults["fps_studio/hitboxes"];
	}
	for (const Variant &value : saved_hitboxes) {
		if (value.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary item = value;
		ModelHitbox hitbox;
		hitbox.name = item.get("name", vformat(TTR("Hitbox %d"), model_hitboxes.size() + 1));
		hitbox.minimum = item.get("minimum", Vector3(-0.5, -0.5, -0.5));
		hitbox.maximum = item.get("maximum", Vector3(0.5, 0.5, 0.5));
		hitbox.hitgroup = CLAMP((int)item.get("hitgroup", 0), 0, 7);
		hitbox.skeleton_path = item.get("skeleton_path", String());
		hitbox.bone_name = item.get("bone_name", String());
		model_hitboxes.push_back(hitbox);
	}
	selected_hitbox = model_hitboxes.is_empty() ? -1 : 0;
	_populate_hitbox_tree();
	_select_hitbox(selected_hitbox);
}

bool SceneImportSettingsScreen::_repair_hitbox_attachments() {
	Node3D *scene_root = Object::cast_to<Node3D>(scene);
	if (!scene_root) {
		return false;
	}

	static const char *preset_names[] = { "Head", "Chest", "Stomach", "Left Upper Arm", "Left Forearm", "Right Upper Arm", "Right Forearm", "Left Thigh", "Left Lower Leg", "Right Thigh", "Right Lower Leg" };
	static const int preset_hitgroups[] = { 1, 2, 3, 4, 4, 5, 5, 6, 6, 7, 7 };
	static const int parent_slots[] = { -1, -1, -1, -1, 3, -1, 5, -1, 7, -1, 9 };
	static const char *preset_aliases[][15] = {
		{ "head", nullptr },
		{ "chest", "upperchest", "spine3", "spine03", "spine2", nullptr },
		{ "abdomen", "spine1", "spine02", "spine", "pelvis", "hips", nullptr },
		{ "leftupperarm", "upperarmleft", "upperarml", "lupperarm", "leftarm", "armleft", "arml", "larm", nullptr },
		{ "leftforearm", "forearmleft", "forearml", "lforearm", "leftlowerarm", "lowerarmleft", "lowerarml", "llowerarm", nullptr },
		{ "rightupperarm", "upperarmright", "upperarmr", "rupperarm", "rightarm", "armright", "armr", "rarm", nullptr },
		{ "rightforearm", "forearmright", "forearmr", "rforearm", "rightlowerarm", "lowerarmright", "lowerarmr", "rlowerarm", nullptr },
		{ "leftupleg", "uplegleft", "uplegl", "lupleg", "leftthigh", "thighleft", "thighl", "lthigh", "leftleg", "legl", nullptr },
		{ "leftlowerleg", "lowerlegleft", "lowerlegl", "llowerleg", "leftcalf", "calfleft", "calfl", "lcalf", "leftshin", "shinl", "leftleg", "legleft", "legl", "lleg", nullptr },
		{ "rightupleg", "uplegright", "uplegr", "rupleg", "rightthigh", "thighright", "thighr", "rthigh", "rightleg", "legr", nullptr },
		{ "rightlowerleg", "lowerlegright", "lowerlegr", "rlowerleg", "rightcalf", "calfright", "calfr", "rcalf", "rightshin", "shinr", "rightleg", "legright", "legr", "rleg", nullptr },
	};
	auto make_aliases = [](const char *const *p_aliases, int p_max_count) {
		Vector<String> aliases;
		for (int i = 0; i < p_max_count && p_aliases[i] != nullptr; i++) {
			aliases.push_back(String(p_aliases[i]));
		}
		return aliases;
	};
	auto transform_bounds = [](const Vector3 &p_minimum, const Vector3 &p_maximum, const Transform3D &p_transform, Vector3 &r_minimum, Vector3 &r_maximum) {
		bool first_corner = true;
		for (int x = 0; x < 2; x++) {
			for (int y = 0; y < 2; y++) {
				for (int z = 0; z < 2; z++) {
					const Vector3 corner(x == 0 ? p_minimum.x : p_maximum.x, y == 0 ? p_minimum.y : p_maximum.y, z == 0 ? p_minimum.z : p_maximum.z);
					const Vector3 transformed_corner = p_transform.xform(corner);
					if (first_corner) {
						r_minimum = transformed_corner;
						r_maximum = transformed_corner;
						first_corner = false;
					} else {
						for (int axis = 0; axis < 3; axis++) {
							r_minimum[axis] = MIN(r_minimum[axis], transformed_corner[axis]);
							r_maximum[axis] = MAX(r_maximum[axis], transformed_corner[axis]);
						}
					}
				}
			}
		}
	};

	bool changed = false;
	for (ModelHitbox &hitbox : model_hitboxes) {
		int slot = -1;
		for (int candidate = 0; candidate < 11; candidate++) {
			if (hitbox.hitgroup == preset_hitgroups[candidate] && hitbox.name == TTR(preset_names[candidate])) {
				slot = candidate;
				break;
			}
		}
		if (slot < 0) {
			continue; // Keep custom hitboxes and names untouched.
		}

		const Vector<String> aliases = make_aliases(preset_aliases[slot], 15);
		Skeleton3D *target_skeleton = nullptr;
		int target_bone = -1;
		int target_score = -1;
		for (Skeleton3D *candidate_skeleton : skeletons) {
			if (!candidate_skeleton) {
				continue;
			}
			int parent_bone = -1;
			if (parent_slots[slot] >= 0) {
				const Vector<String> parent_aliases = make_aliases(preset_aliases[parent_slots[slot]], 15);
				parent_bone = find_hitbox_preset_bone(candidate_skeleton, parent_aliases);
				if (parent_bone < 0) {
					continue;
				}
			}
			int candidate_score = -1;
			const int candidate_bone = find_hitbox_preset_bone(candidate_skeleton, aliases, parent_bone, &candidate_score);
			if (candidate_bone >= 0 && candidate_score > target_score) {
				target_skeleton = candidate_skeleton;
				target_bone = candidate_bone;
				target_score = candidate_score;
			}
		}
		if (!target_skeleton || target_bone < 0) {
			continue;
		}

		Skeleton3D *old_skeleton = hitbox.skeleton_path.is_empty() ? nullptr : Object::cast_to<Skeleton3D>(scene_root->get_node_or_null(NodePath(hitbox.skeleton_path)));
		const int old_bone = old_skeleton ? old_skeleton->find_bone(hitbox.bone_name) : -1;
		const String target_skeleton_path = String(scene->get_path_to(target_skeleton));
		const String target_bone_name = target_skeleton->get_bone_name(target_bone);
		if (old_skeleton == target_skeleton && old_bone == target_bone && hitbox.skeleton_path == target_skeleton_path) {
			continue;
		}

		Vector3 root_minimum = hitbox.minimum;
		Vector3 root_maximum = hitbox.maximum;
		if (old_skeleton && old_bone >= 0) {
			const Transform3D old_bone_to_root = scene_root->get_global_transform().affine_inverse() * old_skeleton->get_global_transform() * old_skeleton->get_bone_global_rest(old_bone);
			transform_bounds(hitbox.minimum, hitbox.maximum, old_bone_to_root, root_minimum, root_maximum);
		}
		const Transform3D root_to_new_bone = target_skeleton->get_bone_global_rest(target_bone).affine_inverse() * target_skeleton->get_global_transform().affine_inverse() * scene_root->get_global_transform();
		transform_bounds(root_minimum, root_maximum, root_to_new_bone, hitbox.minimum, hitbox.maximum);
		hitbox.skeleton_path = target_skeleton_path;
		hitbox.bone_name = target_bone_name;
		changed = true;
	}
	return changed;
}

void SceneImportSettingsScreen::_clear_hitbox_preview() {
	for (int i = hitbox_preview_nodes.size() - 1; i >= 0; i--) {
		Node3D *node = hitbox_preview_nodes[i];
		if (node && node->get_parent()) {
			node->get_parent()->remove_child(node);
			memdelete(node);
		}
	}
	hitbox_preview_nodes.clear();
}

void SceneImportSettingsScreen::_update_hitbox_preview() {
	_clear_hitbox_preview();
	if (!scene) {
		return;
	}
	for (int i = 0; i < model_hitboxes.size(); i++) {
		const ModelHitbox &hitbox = model_hitboxes[i];
		const int hitgroup = CLAMP(hitbox.hitgroup, 0, 7);
		if (!hitbox_preview_materials[hitgroup].is_valid() || !hitbox_selected_preview_materials[hitgroup].is_valid()) {
			continue;
		}
		Ref<Shape3D> shape = create_model_hitbox_shape(hitbox.minimum, hitbox.maximum);
		Ref<ArrayMesh> debug_mesh = shape->get_debug_mesh();
		if (debug_mesh.is_null()) {
			continue;
		}
		Node *parent = scene;
		if (!hitbox.skeleton_path.is_empty() && !hitbox.bone_name.is_empty()) {
			Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(scene->get_node_or_null(NodePath(hitbox.skeleton_path)));
			if (skeleton && skeleton->find_bone(hitbox.bone_name) >= 0) {
				BoneAttachment3D *attachment = memnew(BoneAttachment3D);
				attachment->set_name(String("HitboxPreviewAttachment_") + itos(i));
				attachment->set_bone_name(hitbox.bone_name);
				skeleton->add_child(attachment, true);
				attachment->set_owner(scene);
				hitbox_preview_nodes.push_back(attachment);
				parent = attachment;
			}
		}
		MeshInstance3D *preview = memnew(MeshInstance3D);
		preview->set_name(String("HitboxPreview_") + itos(i));
		preview->set_mesh(debug_mesh);
		preview->set_material_override(i == selected_hitbox ? hitbox_selected_preview_materials[hitgroup] : hitbox_preview_materials[hitgroup]);
		preview->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		preview->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
		preview->set_transform(Transform3D(Basis(), (hitbox.minimum + hitbox.maximum) * 0.5));
		parent->add_child(preview, true);
		preview->set_owner(scene);
		if (parent == scene) {
			hitbox_preview_nodes.push_back(preview);
		}
	}
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

void SceneImportSettingsScreen::_setup_model_preview_environment() {
	if (!base_viewport || !camera || !light1 || !preview_world_environment_node || preview_sky_material.is_null()) {
		return;
	}

	environment = create_model_view_environment(preview_sky_material);
	preview_world_environment_node->set_environment(environment);
	camera->set_environment(environment);

	preview_time_of_day = Math::fposmod((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "day_night_time", 12.0), 24.0f);
	const float cycle_duration_seconds = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_cycle_duration_seconds", 60.0), 10.0f, 600.0f);
	const float sun_azimuth_degrees = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_rotation_degrees", 0.0), -180.0f, 180.0f);
	const float sun_path_tilt = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_path_tilt_degrees", 35.0), 0.0f, 80.0f);
	const float sun_energy = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_energy", 1.2), 0.0f, 5.0f);
	preview_sun_animation_enabled = EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_animation_enabled", false);
	const bool clouds_enabled = EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "clouds_enabled", true);
	const float cloud_coverage = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "cloud_coverage", 0.5), 0.0f, 1.0f);
	const float cloud_scale = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "cloud_scale", 1.5), 0.2f, 5.0f);
	const float cloud_speed = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "cloud_speed", 0.01), 0.0f, 0.1f);
	preview_sun_cycle_duration_seconds = cycle_duration_seconds;

	preview_time_of_day_slider->set_value_no_signal(preview_time_of_day);
	preview_sun_cycle_duration_slider->set_value_no_signal(cycle_duration_seconds);
	preview_sun_azimuth_slider->set_value_no_signal(sun_azimuth_degrees);
	preview_sun_path_tilt_slider->set_value_no_signal(sun_path_tilt);
	preview_sun_energy_slider->set_value_no_signal(sun_energy);
	preview_sun_animation_toggle->set_pressed_no_signal(preview_sun_animation_enabled);
	preview_clouds_toggle->set_pressed_no_signal(clouds_enabled);
	preview_cloud_coverage_slider->set_value_no_signal(cloud_coverage);
	preview_cloud_scale_slider->set_value_no_signal(cloud_scale);
	preview_cloud_speed_slider->set_value_no_signal(cloud_speed);

	_update_preview_time_of_day_label(preview_time_of_day);
	_update_preview_sun_cycle_duration_label(cycle_duration_seconds);
	_update_preview_sun_azimuth_label(sun_azimuth_degrees);
	_update_preview_sun_path_tilt_label(sun_path_tilt);
	_update_preview_sun_energy_label(sun_energy);
	_update_preview_cloud_coverage_label(cloud_coverage);
	_update_preview_cloud_scale_label(cloud_scale);
	_update_preview_cloud_speed_label(cloud_speed);
	_update_model_preview_environment();

	if (preview_sun_animation_timer != nullptr) {
		preview_sun_animation_timer->set_wait_time(0.1);
		if (preview_sun_animation_enabled && is_visible_in_tree()) {
			preview_sun_animation_timer->start();
		} else {
			preview_sun_animation_timer->stop();
		}
	}
}

void SceneImportSettingsScreen::_update_model_preview_environment() {
	if (!light1 || !environment.is_valid() || preview_sky_material.is_null()) {
		return;
	}
	const float time_of_day = preview_time_of_day;
	const float azimuth_degrees = preview_sun_azimuth_slider != nullptr ? preview_sun_azimuth_slider->get_value() : 0.0f;
	const float path_tilt_degrees = preview_sun_path_tilt_slider != nullptr ? preview_sun_path_tilt_slider->get_value() : 35.0f;
	const float sun_max_energy = preview_sun_energy_slider != nullptr ? preview_sun_energy_slider->get_value() : 1.2f;
	const float cloud_coverage = preview_cloud_coverage_slider != nullptr ? preview_cloud_coverage_slider->get_value() : 0.5f;
	const float cloud_scale = preview_cloud_scale_slider != nullptr ? preview_cloud_scale_slider->get_value() : 1.5f;
	const float cloud_speed = preview_cloud_speed_slider != nullptr ? preview_cloud_speed_slider->get_value() : 0.01f;
	const bool clouds_enabled = preview_clouds_toggle != nullptr && preview_clouds_toggle->is_pressed();

	const float theta = ((time_of_day - 6.0f) / 24.0f) * Math::TAU;
	const float path_tilt = Math::deg_to_rad(path_tilt_degrees);
	Vector3 sun_direction(Math::cos(theta), Math::sin(theta) * Math::cos(path_tilt), Math::sin(theta) * Math::sin(path_tilt));
	sun_direction = sun_direction.rotated(Vector3::UP, Math::deg_to_rad(azimuth_degrees)).normalized();
	const Vector3 light_target = -sun_direction;
	const Vector3 light_up = Math::abs(sun_direction.y) < 0.98f ? Vector3::UP : Vector3::FORWARD;
	light1->look_at_from_position(Vector3(), light_target, light_up);

	const float daylight_amount = CLAMP((sun_direction.y + 0.02f) / 0.14f, 0.0f, 1.0f);
	const float daylight = daylight_amount * daylight_amount * (3.0f - 2.0f * daylight_amount);
	const float dawn = (1.0f - CLAMP(Math::abs(time_of_day - 6.5f) / 1.8f, 0.0f, 1.0f));
	const float dusk = (1.0f - CLAMP(Math::abs(time_of_day - 18.5f) / 1.8f, 0.0f, 1.0f));
	const float twilight = MAX(dawn, dusk);
	const Color night_top(0.025, 0.045, 0.075);
	const Color day_top(0.30, 0.42, 0.58);
	const Color night_horizon(0.07, 0.095, 0.14);
	const Color day_horizon(0.43, 0.51, 0.61);
	const Color twilight_horizon(0.72, 0.43, 0.34);
	const Color night_ground(0.06, 0.075, 0.10);
	const Color day_ground(0.43, 0.51, 0.61);
	const Color sky_top = night_top.lerp(day_top, daylight);
	const Color sky_horizon = night_horizon.lerp(day_horizon, daylight).lerp(twilight_horizon, twilight * 0.55f);
	const Color ground_color = night_ground.lerp(day_ground, daylight);
	const Color sun_color = Color(1.0, 0.68, 0.42).lerp(Color(1.0, 0.96, 0.88), daylight);
	light1->set_color(sun_color);
	light1->set_param(Light3D::PARAM_ENERGY, sun_max_energy * daylight);
	light1->set_visible(sun_direction.y > -0.06f);
	preview_sky_material->set_shader_parameter("sky_top_color", sky_top);
	preview_sky_material->set_shader_parameter("sky_horizon_color", sky_horizon);
	preview_sky_material->set_shader_parameter("ground_color", ground_color);
	preview_sky_material->set_shader_parameter("sun_color", sun_color);
	preview_sky_material->set_shader_parameter("clouds_enabled", clouds_enabled);
	preview_sky_material->set_shader_parameter("cloud_coverage", cloud_coverage);
	preview_sky_material->set_shader_parameter("cloud_scale", cloud_scale);
	preview_sky_material->set_shader_parameter("cloud_speed", cloud_speed);
	const float ambient_energy = Math::lerp(0.2f, 1.35f, daylight);
	environment->set_ambient_light_color(Color(0.72, 0.78, 0.9).lerp(Color(1.0, 1.0, 1.0), daylight));
	environment->set_ambient_light_energy(ambient_energy);
	environment->set_fog_light_color(sky_horizon);
	if (preview_floor_material.is_valid()) {
		preview_floor_material->set_shader_parameter("horizon_color", sky_horizon);
	}
}

void SceneImportSettingsScreen::_preview_time_of_day_changed(double p_value) {
	preview_time_of_day = Math::fposmod((float)p_value, 24.0f);
	preview_time_of_day_slider->set_value_no_signal(preview_time_of_day);
	_update_preview_time_of_day_label(preview_time_of_day);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "day_night_time", preview_time_of_day);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_sun_animation_step() {
	if (!preview_sun_animation_enabled || preview_sun_animation_timer == nullptr) {
		return;
	}
	preview_time_of_day = Math::fposmod(preview_time_of_day + 24.0f * (float)preview_sun_animation_timer->get_wait_time() / preview_sun_cycle_duration_seconds, 24.0f);
	preview_time_of_day_slider->set_value_no_signal(preview_time_of_day);
	_update_preview_time_of_day_label(preview_time_of_day);
	_update_model_preview_environment();
	preview_sun_animation_metadata_elapsed += (float)preview_sun_animation_timer->get_wait_time();
	if (preview_sun_animation_metadata_elapsed >= 1.0f) {
		preview_sun_animation_metadata_elapsed = 0.0f;
		EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "day_night_time", preview_time_of_day);
	}
}

void SceneImportSettingsScreen::_preview_sun_animation_toggled(bool p_enabled) {
	preview_sun_animation_enabled = p_enabled;
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_animation_enabled", p_enabled);
	if (preview_sun_animation_timer != nullptr) {
		if (p_enabled && is_visible_in_tree()) {
			preview_sun_animation_timer->start();
		} else {
			preview_sun_animation_timer->stop();
		}
	}
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "day_night_time", preview_time_of_day);
}

void SceneImportSettingsScreen::_preview_sun_cycle_duration_changed(double p_value) {
	preview_sun_cycle_duration_seconds = CLAMP((float)p_value, 10.0f, 600.0f);
	preview_sun_cycle_duration_slider->set_value_no_signal(preview_sun_cycle_duration_seconds);
	_update_preview_sun_cycle_duration_label(preview_sun_cycle_duration_seconds);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_cycle_duration_seconds", preview_sun_cycle_duration_seconds);
}

void SceneImportSettingsScreen::_preview_sun_azimuth_changed(double p_value) {
	const float azimuth_degrees = CLAMP((float)p_value, -180.0f, 180.0f);
	preview_sun_azimuth_slider->set_value_no_signal(azimuth_degrees);
	_update_preview_sun_azimuth_label(azimuth_degrees);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_rotation_degrees", azimuth_degrees);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_sun_path_tilt_changed(double p_value) {
	const float path_tilt_degrees = CLAMP((float)p_value, 0.0f, 80.0f);
	preview_sun_path_tilt_slider->set_value_no_signal(path_tilt_degrees);
	_update_preview_sun_path_tilt_label(path_tilt_degrees);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_path_tilt_degrees", path_tilt_degrees);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_sun_energy_changed(double p_value) {
	const float sun_energy = CLAMP((float)p_value, 0.0f, 5.0f);
	preview_sun_energy_slider->set_value_no_signal(sun_energy);
	_update_preview_sun_energy_label(sun_energy);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_energy", sun_energy);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_clouds_toggled(bool p_enabled) {
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "clouds_enabled", p_enabled);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_cloud_coverage_changed(double p_value) {
	const float coverage = CLAMP((float)p_value, 0.0f, 1.0f);
	preview_cloud_coverage_slider->set_value_no_signal(coverage);
	_update_preview_cloud_coverage_label(coverage);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "cloud_coverage", coverage);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_cloud_scale_changed(double p_value) {
	const float scale = CLAMP((float)p_value, 0.2f, 5.0f);
	preview_cloud_scale_slider->set_value_no_signal(scale);
	_update_preview_cloud_scale_label(scale);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "cloud_scale", scale);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_cloud_speed_changed(double p_value) {
	const float speed = CLAMP((float)p_value, 0.0f, 0.1f);
	preview_cloud_speed_slider->set_value_no_signal(speed);
	_update_preview_cloud_speed_label(speed);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "cloud_speed", speed);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_update_preview_time_of_day_label(double p_value) {
	if (!preview_time_of_day_label) {
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
	preview_time_of_day_label->set_text(vformat(TTR("Time of Day: %02d:%02d"), display_hour, minute));
}

void SceneImportSettingsScreen::_update_preview_sun_cycle_duration_label(double p_value) {
	if (!preview_sun_cycle_duration_label) {
		return;
	}
	preview_sun_cycle_duration_label->set_text(vformat(TTR("Cycle Duration: %d s"), (int64_t)Math::round(p_value)));
}

void SceneImportSettingsScreen::_update_preview_sun_azimuth_label(double p_value) {
	if (preview_sun_azimuth_label) {
		preview_sun_azimuth_label->set_text(vformat(TTR("Sun Azimuth: %d deg"), (int64_t)Math::round(p_value)));
	}
}

void SceneImportSettingsScreen::_update_preview_sun_path_tilt_label(double p_value) {
	if (preview_sun_path_tilt_label) {
		preview_sun_path_tilt_label->set_text(vformat(TTR("Sun Path Tilt: %d deg"), (int64_t)Math::round(p_value)));
	}
}

void SceneImportSettingsScreen::_update_preview_sun_energy_label(double p_value) {
	if (preview_sun_energy_label) {
		preview_sun_energy_label->set_text(vformat(TTR("Sun Energy: %s"), String::num(p_value, 2)));
	}
}

void SceneImportSettingsScreen::_update_preview_cloud_coverage_label(double p_value) {
	if (preview_cloud_coverage_label) {
		preview_cloud_coverage_label->set_text(vformat(TTR("Cloud Coverage: %d percent"), (int64_t)Math::round(p_value * 100.0)));
	}
}

void SceneImportSettingsScreen::_update_preview_cloud_scale_label(double p_value) {
	if (preview_cloud_scale_label) {
		preview_cloud_scale_label->set_text(vformat(TTR("Cloud Scale: %s"), String::num(p_value, 2)));
	}
}

void SceneImportSettingsScreen::_update_preview_cloud_speed_label(double p_value) {
	if (preview_cloud_speed_label) {
		preview_cloud_speed_label->set_text(vformat(TTR("Cloud Speed: %s"), String::num(p_value, 3)));
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
