/**************************************************************************/
/*  particle_editor_plugin.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_editor_plugin.h"

#include "core/input/input_event.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/string/print_string.h"
#include "editor/editor_data.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/curve_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "modules/visual_shader/editor/visual_shader_editor_plugin.h"
#include "modules/visual_shader/visual_shader.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_button.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/curve.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/environment.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/particle_process_material.h"

enum ParticleTemplate {
	PARTICLE_TEMPLATE_GPU = 0,
	PARTICLE_TEMPLATE_CPU = 1,
	PARTICLE_TEMPLATE_BURST = 2,
	PARTICLE_TEMPLATE_FOUNTAIN = 3,
	PARTICLE_TEMPLATE_SMOKE = 4,
};

enum VFXTexturePreset {
	VFX_TEXTURE_FIRE = 0,
	VFX_TEXTURE_SMOKE = 1,
	VFX_TEXTURE_EXPLOSION = 2,
	VFX_TEXTURE_SPARK = 3,
};

enum VFXTextureOutput {
	VFX_TEXTURE_OUTPUT_STILL = 0,
	VFX_TEXTURE_OUTPUT_SHEET_4X4 = 4,
	VFX_TEXTURE_OUTPUT_SHEET_8X8 = 8,
};

enum ParticleAlphaCurvePreset {
	PARTICLE_ALPHA_CURVE_FADE_OUT_LINEAR = 0,
	PARTICLE_ALPHA_CURVE_FADE_OUT_SMOOTH,
	PARTICLE_ALPHA_CURVE_FADE_IN,
	PARTICLE_ALPHA_CURVE_FADE_IN_OUT,
	PARTICLE_ALPHA_CURVE_HOLD_THEN_FADE,
};

enum ParticleScaleCurvePreset {
	PARTICLE_SCALE_CURVE_GROW = 0,
	PARTICLE_SCALE_CURVE_SHRINK,
	PARTICLE_SCALE_CURVE_GROW_AND_SHRINK,
	PARTICLE_SCALE_CURVE_HOLD_THEN_GROW,
	PARTICLE_SCALE_CURVE_HOLD,
};

static uint32_t particle_texture_hash(int p_x, int p_y, int p_seed) {
	uint32_t value = uint32_t(p_x) * 0x8da6b343u ^ uint32_t(p_y) * 0xd8163841u ^ uint32_t(p_seed) * 0xcb1ab31fu;
	value ^= value >> 13;
	value *= 0x85ebca6bu;
	value ^= value >> 16;
	return value;
}

static real_t particle_texture_noise(real_t p_x, real_t p_y, int p_seed) {
	const int x0 = int(Math::floor(p_x));
	const int y0 = int(Math::floor(p_y));
	const real_t tx = p_x - x0;
	const real_t ty = p_y - y0;
	const real_t sx = tx * tx * (3.0 - 2.0 * tx);
	const real_t sy = ty * ty * (3.0 - 2.0 * ty);
	auto sample = [&](int p_ix, int p_iy) -> real_t {
		return real_t(particle_texture_hash(p_ix, p_iy, p_seed) & 0xffffu) / 65535.0;
	};
	const real_t top = Math::lerp(sample(x0, y0), sample(x0 + 1, y0), sx);
	const real_t bottom = Math::lerp(sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), sx);
	return Math::lerp(top, bottom, sy);
}

static real_t particle_texture_fractal(real_t p_x, real_t p_y, int p_seed) {
	real_t value = 0.0;
	real_t weight = 0.0;
	for (int octave = 0; octave < 4; octave++) {
		const real_t amplitude = 1.0 / (1 << octave);
		value += particle_texture_noise(p_x * (1 << octave), p_y * (1 << octave), p_seed + octave * 193) * amplitude;
		weight += amplitude;
	}
	return value / weight;
}

// Native, deterministic VFX texture recipe. It follows the publicly documented
// workflow (procedural templates, time samples, sprite sheets and alpha) without
// embedding or translating the website's shader source.
static Ref<Image> generate_particle_texture_frame(int p_size, int p_preset, int p_seed, const Color &p_color_a, const Color &p_color_b, real_t p_time) {
	Ref<Image> image;
	image.instantiate();
	image->initialize_data(p_size, p_size, false, Image::FORMAT_RGBA8);
	const real_t phase = Math::fposmod(p_time, real_t(1.0));
	for (int py = 0; py < p_size; py++) {
		const real_t v = (real_t(py) + 0.5) / p_size;
		const real_t h = 1.0 - v;
		for (int px = 0; px < p_size; px++) {
			const real_t u = (real_t(px) + 0.5) / p_size;
			const real_t x = (u - 0.5) * 2.0;
			const real_t y = (v - 0.5) * 2.0;
			const real_t noise = particle_texture_fractal(u * 6.0 + p_time * 0.19, v * 6.0 - p_time * 0.13, p_seed);
			real_t alpha = 0.0;
			real_t color_mix = noise;
			if (p_preset == VFX_TEXTURE_FIRE) {
				const real_t center = 0.5 + (particle_texture_noise(u * 4.0 + p_time * 0.31, h * 4.0, p_seed + 41) - 0.5) * 0.3 * h;
				const real_t width = 0.035 + 0.44 * Math::pow(MAX(0.0, 1.0 - h), 0.68);
				const real_t flicker = Math::sin(p_time * Math::PI * 4.0 + noise * 6.0) * 0.08;
				const real_t edge = 1.0 - Math::abs(u - center) / width + (noise - 0.5) * 0.55 + flicker;
				const real_t body = CLAMP(edge / 0.48, 0.0, 1.0);
				const real_t bottom_fade = CLAMP(h / 0.16, 0.0, 1.0);
				const real_t tip_fade = CLAMP((1.0 - h) / 0.2, 0.0, 1.0);
				alpha = body * bottom_fade * tip_fade;
				color_mix = CLAMP(0.15 + body * 0.72 + noise * 0.2, 0.0, 1.0);
			} else if (p_preset == VFX_TEXTURE_SMOKE) {
				const real_t plume_width = 0.3 + 0.17 * h;
				const real_t center = 0.5 + (particle_texture_noise(u * 2.0 + p_time * 0.22, v * 2.0 - p_time * 0.17, p_seed + 17) - 0.5) * 0.24;
				const real_t dx = (u - center) / plume_width;
				const real_t dy = (v - 0.5) / 0.72;
				const real_t puff = Math::sqrt(dx * dx + dy * dy) + (noise - 0.5) * 0.5;
				alpha = CLAMP((1.0 - puff) * 1.6, 0.0, 0.72);
				alpha *= CLAMP((1.0 - v) * 5.0, 0.0, 1.0);
				color_mix = CLAMP(noise * 0.8 + 0.1, 0.0, 1.0);
			} else if (p_preset == VFX_TEXTURE_EXPLOSION) {
				const real_t radius = Vector2(x, y).length();
				const real_t expanding_radius = 0.24 + phase * 0.5;
				const real_t irregular_radius = radius + (noise - 0.5) * 0.26;
				const real_t core = 1.0 - CLAMP(radius / expanding_radius, 0.0, 1.0);
				const real_t shell = 1.0 - CLAMP(Math::abs(irregular_radius - (0.22 + phase * 0.42)) / (0.15 + phase * 0.2), 0.0, 1.0);
				alpha = MAX(core * 0.72, shell) * CLAMP(1.0 - phase * 0.55, 0.0, 1.0) * CLAMP((1.25 - radius) * 2.2, 0.0, 1.0);
				color_mix = CLAMP(core * 0.7 + shell * 0.3 + noise * 0.12, 0.0, 1.0);
			} else {
				const real_t angle = phase * Math::TAU;
				const real_t center_line = 0.5 + 0.62 * (0.5 - v) + Math::sin(angle) * 0.16;
				const real_t taper = 0.008 + 0.042 * (1.0 - Math::abs(v - 0.5) * 1.8);
				const real_t distance = Math::abs(u - center_line) + (noise - 0.5) * 0.012;
				const real_t streak = 1.0 - CLAMP(distance / taper, 0.0, 1.0);
				const real_t length_fade = CLAMP(1.0 - Math::abs(v - 0.5) * 2.0, 0.0, 1.0);
				alpha = streak * length_fade * (0.62 + 0.38 * Math::sin(phase * Math::TAU));
				color_mix = CLAMP(streak * 0.78 + noise * 0.22, 0.0, 1.0);
			}
			Color color = p_color_a.lerp(p_color_b, color_mix);
			color.a = CLAMP(alpha, 0.0, 1.0);
			image->set_pixel(px, py, color);
		}
	}
	return image;
}

static MeshInstance3D *create_particle_preview_grid() {
	Ref<StandardMaterial3D> grid_material;
	grid_material.instantiate();
	grid_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	grid_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	grid_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);

	Ref<ImmediateMesh> grid_mesh;
	grid_mesh.instantiate();
	grid_mesh->surface_begin(Mesh::PRIMITIVE_LINES, grid_material);
	constexpr real_t grid_step = 0.5;
	constexpr real_t grid_extent = 24.0;
	constexpr int grid_divisions = 48;
	auto add_grid_line = [&](const Vector3 &p_from, const Vector3 &p_to, const Color &p_color) {
		const int segment_count = MAX(1, int((p_to - p_from).length()));
		for (int segment = 0; segment < segment_count; segment++) {
			const Vector3 from = p_from.lerp(p_to, real_t(segment) / segment_count);
			const Vector3 to = p_from.lerp(p_to, real_t(segment + 1) / segment_count);
			Color from_color = p_color;
			Color to_color = p_color;
			from_color.a *= MAX(0.0, 1.0 - Vector2(from.x, from.z).length() / grid_extent);
			to_color.a *= MAX(0.0, 1.0 - Vector2(to.x, to.z).length() / grid_extent);
			grid_mesh->surface_set_color(from_color);
			grid_mesh->surface_add_vertex(from);
			grid_mesh->surface_set_color(to_color);
			grid_mesh->surface_add_vertex(to);
		}
	};
	for (int i = -grid_divisions; i <= grid_divisions; i++) {
		const real_t coordinate = i * grid_step;
		const Color grid_color = i == 0 ? Color(0.78, 0.29, 0.24, 0.72) : Color(0.48, 0.56, 0.7, 0.32);
		add_grid_line(Vector3(-grid_extent, -0.1, coordinate), Vector3(grid_extent, -0.1, coordinate), grid_color);
		const Color side_color = i == 0 ? Color(0.26, 0.48, 0.86, 0.75) : Color(0.48, 0.56, 0.7, 0.32);
		add_grid_line(Vector3(coordinate, -0.1, -grid_extent), Vector3(coordinate, -0.1, grid_extent), side_color);
	}
	grid_mesh->surface_end();

	MeshInstance3D *grid_instance = memnew(MeshInstance3D);
	grid_instance->set_name("ParticlePreviewGrid");
	grid_instance->set_mesh(grid_mesh);
	grid_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	return grid_instance;
}

static Ref<QuadMesh> create_particle_quad(real_t p_size = 0.16) {
	Ref<QuadMesh> particle_mesh;
	particle_mesh.instantiate();
	particle_mesh->set_size(Vector2(p_size, p_size));
	Ref<StandardMaterial3D> particle_material;
	particle_material.instantiate();
	particle_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	particle_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	particle_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	particle_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
	particle_mesh->set_material(particle_material);
	return particle_mesh;
}

static void set_scene_subtree_owners(Node *p_node, Node *p_scene_root) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		child->set_owner(p_scene_root);
		set_scene_subtree_owners(child, p_scene_root);
	}
}

static bool is_particle_node(Node *p_node) {
	return Object::cast_to<GPUParticles3D>(p_node) || Object::cast_to<CPUParticles3D>(p_node);
}

static Node3D *create_particle_emitter(int p_template, const Ref<QuadMesh> &p_mesh) {
	const bool cpu = p_template == PARTICLE_TEMPLATE_CPU;
	const bool burst = p_template == PARTICLE_TEMPLATE_BURST;
	const bool fountain = p_template == PARTICLE_TEMPLATE_FOUNTAIN;
	const bool smoke = p_template == PARTICLE_TEMPLATE_SMOKE;

	Node3D *emitter = nullptr;
	if (cpu) {
		CPUParticles3D *cpu_emitter = memnew(CPUParticles3D);
		cpu_emitter->set_amount(32);
		cpu_emitter->set_lifetime(2.5);
		cpu_emitter->set_mesh(p_mesh);
		emitter = cpu_emitter;
	} else {
		GPUParticles3D *gpu_emitter = memnew(GPUParticles3D);
		gpu_emitter->set_amount(burst ? 64 : smoke ? 48 : 32);
		gpu_emitter->set_lifetime(burst ? 1.25 : smoke ? 4.0 : 2.5);
		gpu_emitter->set_emitting(true);
		gpu_emitter->set_visibility_aabb(AABB(Vector3(-6.0, -2.0, -6.0), Vector3(12.0, 14.0, 12.0)));
		gpu_emitter->set_one_shot(burst);
		if (burst) {
			gpu_emitter->set_explosiveness_ratio(1.0);
		}

		Ref<ParticleProcessMaterial> process_material;
		process_material.instantiate();
		process_material->set_direction(Vector3(0.0, 1.0, 0.0));
		process_material->set_spread(smoke ? 12.0 : burst ? 180.0 : fountain ? 18.0 : 28.0);
		process_material->set_param_min(ParticleProcessMaterial::PARAM_SCALE, smoke ? 0.7 : 0.5);
		process_material->set_param_max(ParticleProcessMaterial::PARAM_SCALE, smoke ? 1.6 : 1.1);
		process_material->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, burst ? 3.0 : smoke ? 0.4 : 1.0);
		process_material->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, burst ? 7.0 : smoke ? 0.9 : 2.4);
		process_material->set_gravity(smoke ? Vector3(0.0, 0.45, 0.0) : Vector3(0.0, burst ? -3.0 : -0.25, 0.0));
		process_material->set_color(smoke ? Color(0.72, 0.78, 0.88, 0.45) : burst ? Color(1.0, 0.38, 0.08, 1.0) : fountain ? Color(0.2, 0.7, 1.0, 1.0) : Color(0.55, 0.76, 1.0, 0.9));
		gpu_emitter->set_process_material(process_material);
		gpu_emitter->set_draw_pass_mesh(0, p_mesh);
		emitter = gpu_emitter;
	}

	if (burst) {
		emitter->set_name("BurstEmitter");
	} else if (fountain) {
		emitter->set_name("FountainEmitter");
	} else if (smoke) {
		emitter->set_name("SmokeEmitter");
	} else if (cpu) {
		emitter->set_name("CPUParticles3D");
	} else {
		emitter->set_name("GPUParticles3D");
	}
	return emitter;
}

static void clear_preview_scripts(Node *p_node) {
	if (!p_node) {
		return;
	}
	p_node->set_script(Ref<Script>());
	for (int i = 0; i < p_node->get_child_count(); i++) {
		clear_preview_scripts(p_node->get_child(i));
	}
}

static void apply_preview_defaults(Node *p_node, const Ref<QuadMesh> &p_quad) {
	if (!p_node) {
		return;
	}
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_node)) {
		if (gpu->get_process_material().is_null()) {
			Ref<ParticleProcessMaterial> material;
			material.instantiate();
			gpu->set_process_material(material);
		}
		if (gpu->get_draw_passes() == 0) {
			gpu->set_draw_passes(1);
		}
		if (gpu->get_draw_pass_mesh(0).is_null()) {
			gpu->set_draw_pass_mesh(0, p_quad);
		}
		gpu->set_emitting(true);
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_node)) {
		if (cpu->get_mesh().is_null()) {
			cpu->set_mesh(p_quad);
		}
		cpu->set_emitting(true);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		apply_preview_defaults(p_node->get_child(i), p_quad);
	}
}

static void restart_particle_nodes(Node *p_node, bool p_process_now = false, double p_time = 0.0) {
	if (!p_node) {
		return;
	}
	if (is_particle_node(p_node)) {
		p_node->call("restart", true);
		p_node->set("emitting", true);
		if (p_process_now && p_time > 0.0) {
			p_node->call("request_particles_process", p_time, 0.0);
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		restart_particle_nodes(p_node->get_child(i), p_process_now, p_time);
	}
}

static void set_preview_speed_recursive(Node *p_node, double p_speed) {
	if (!p_node) {
		return;
	}
	if (is_particle_node(p_node)) {
		p_node->call("set_speed_scale", p_speed);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		set_preview_speed_recursive(p_node->get_child(i), p_speed);
	}
}

static void clear_preview_transform_origin(Node *p_node) {
	if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		Transform3D transform = node_3d->get_transform();
		transform.origin = Vector3();
		node_3d->set_transform(transform);
	}
}

static void copy_preview_properties(Node *p_source, Node *p_preview) {
	if (!p_source || !p_preview) {
		return;
	}
	List<PropertyInfo> properties;
	p_source->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!(property.usage & PROPERTY_USAGE_STORAGE) || (property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) || property.name == "name" || property.name == "owner" || property.name == "script" ||
				property.name == "scene_file_path" || property.name == "emitting" || property.name == "visible" || property.name == "transform") {
			continue;
		}
		bool valid = false;
		Variant value = p_source->get(property.name, &valid);
		if (valid) {
			p_preview->set(property.name, value);
		}
	}
	if (Node3D *source_3d = Object::cast_to<Node3D>(p_source)) {
		if (Node3D *preview_3d = Object::cast_to<Node3D>(p_preview)) {
			Transform3D transform = source_3d->get_transform();
			transform.origin = Vector3();
			preview_3d->set_transform(transform);
		}
	}
	p_preview->set("visible", true);
	for (int i = 0; i < MIN(p_source->get_child_count(), p_preview->get_child_count()); i++) {
		copy_preview_properties(p_source->get_child(i), p_preview->get_child(i));
	}
}

Node *FPSStudioParticleEditorPlugin::_get_active_scene_root() const {
	EditorData &editor_data = EditorNode::get_editor_data();
	const int scene_index = editor_data.get_edited_scene();
	if (scene_index < 0 || scene_index >= editor_data.get_edited_scene_count()) {
		return nullptr;
	}
	return editor_data.get_edited_scene_root(scene_index);
}

Node *FPSStudioParticleEditorPlugin::_get_effect_root() const {
	return Object::cast_to<Node>(ObjectDB::get_instance(effect_root_id));
}

Node *FPSStudioParticleEditorPlugin::_get_edited_emitter() const {
	return Object::cast_to<Node>(ObjectDB::get_instance(edited_emitter_id));
}

void FPSStudioParticleEditorPlugin::_collect_emitters(Node *p_node, Vector<Node *> &r_emitters) const {
	if (!p_node) {
		return;
	}
	if (is_particle_node(p_node)) {
		r_emitters.push_back(p_node);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_emitters(p_node->get_child(i), r_emitters);
	}
}

void FPSStudioParticleEditorPlugin::_load_recent_effects() {
	recent_effect_paths.clear();
	if (!EditorSettings::get_singleton()) {
		return;
	}
	Array recent = EditorSettings::get_singleton()->get_project_metadata("fps_studio_particle", "recent_effects", Array());
	for (int i = 0; i < recent.size() && recent_effect_paths.size() < 16; i++) {
		const String path = recent[i];
		if (!path.is_empty() && ResourceLoader::exists(path)) {
			recent_effect_paths.push_back(path);
		}
	}
}

void FPSStudioParticleEditorPlugin::_remember_effect(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}
	Vector<String> updated;
	updated.push_back(p_path);
	for (const String &path : recent_effect_paths) {
		if (path != p_path && updated.size() < 16) {
			updated.push_back(path);
		}
	}
	recent_effect_paths = updated;
	Array recent;
	for (const String &path : recent_effect_paths) {
		recent.push_back(path);
	}
	if (EditorSettings::get_singleton()) {
		EditorSettings::get_singleton()->set_project_metadata("fps_studio_particle", "recent_effects", recent);
	}
	_update_effects_tree();
}

void FPSStudioParticleEditorPlugin::_update_effects_tree() {
	if (!effects_tree) {
		return;
	}
	effects_tree->clear();
	TreeItem *root = effects_tree->create_item();
	const String filter = effect_filter ? effect_filter->get_text().to_lower() : String();
	TreeItem *selected = nullptr;
	for (const String &path : recent_effect_paths) {
		const String name = path.get_file().get_basename();
		if (!filter.is_empty() && !name.to_lower().contains(filter) && !path.to_lower().contains(filter)) {
			continue;
		}
		TreeItem *item = effects_tree->create_item(root);
		item->set_text(0, name);
		item->set_tooltip_text(0, path);
		item->set_metadata(0, path);
		if (path == effect_path) {
			selected = item;
		}
	}
	if (selected) {
		selected->select(0);
	}
	if (root->get_child_count() == 0) {
		TreeItem *empty_item = effects_tree->create_item(root);
		empty_item->set_text(0, TTR("No recent effects"));
		empty_item->set_selectable(0, false);
	}
}

void FPSStudioParticleEditorPlugin::_update_emitter_tree() {
	if (!emitter_tree) {
		return;
	}
	emitter_tree->clear();
	TreeItem *root_item = emitter_tree->create_item();
	Node *effect_root = _get_effect_root();
	Vector<Node *> emitters;
	_collect_emitters(effect_root, emitters);
	Node *edited_emitter = _get_edited_emitter();
	TreeItem *selected_item = nullptr;
	for (Node *emitter : emitters) {
		TreeItem *item = emitter_tree->create_item(root_item);
		item->set_text(0, emitter->get_name());
		item->set_tooltip_text(0, String(emitter->get_class()) + "\n" + emitter->get_name() + "\n" + TTR("Double-click to rename"));
		item->set_editable(0, true);
		item->set_metadata(0, uint64_t(emitter->get_instance_id()));
		if (emitter == edited_emitter) {
			selected_item = item;
		}
	}
	if (selected_item) {
		selected_item->select(0);
	}
	if (!effect_root) {
		TreeItem *empty_item = emitter_tree->create_item(root_item);
		empty_item->set_text(0, TTR("Open or create an effect"));
		empty_item->set_selectable(0, false);
	} else if (emitters.is_empty()) {
		TreeItem *empty_item = emitter_tree->create_item(root_item);
		empty_item->set_text(0, TTR("Add an emitter to this effect"));
		empty_item->set_selectable(0, false);
	}
}

void FPSStudioParticleEditorPlugin::_update_document_ui() {
	Node *root = _get_effect_root();
	const bool has_document = root != nullptr;
	if (document_title) {
		document_title->set_text(has_document ? String(root->get_name()) : TTR("No Particle Effect Open"));
		const String rename_hint = TTR("Double-click to rename this particle effect.");
		document_title->set_tooltip_text(effect_path.is_empty() ? rename_hint : effect_path + "\n" + rename_hint);
	}
	if (document_path_label) {
		document_path_label->set_text(effect_path.is_empty() ? TTR("Unsaved effect") : effect_path);
		document_path_label->set_tooltip_text(effect_path);
	}
	if (document_status_label) {
		document_status_label->set_text(effect_dirty ? TTR("Unsaved changes") : effect_path.is_empty() ? TTR("New") : TTR("Saved"));
	}
	if (save_effect_button) {
		save_effect_button->set_disabled(!has_document);
		if (effect_dirty) {
			save_effect_button->set_text(TTR("Save *"));
		} else {
			save_effect_button->set_text(TTR("Save"));
		}
	}
	if (add_to_level_button) {
		add_to_level_button->set_disabled(!has_document || _get_active_scene_root() == nullptr);
	}
	if (new_effect_menu) {
		new_effect_menu->set_disabled(false);
	}
	if (open_effect_button) {
		open_effect_button->set_disabled(false);
	}
	_update_document_toolbar_layout();
	_update_effects_tree();
	_update_emitter_tree();
}

void FPSStudioParticleEditorPlugin::_update_document_toolbar_layout() {
	const bool compact = screen && screen->get_size().x < 1200.0 * EDSCALE;
	if (document_path_label) {
		document_path_label->set_visible(!compact);
	}
	if (document_status_label) {
		document_status_label->set_visible(!compact);
	}
	if (save_effect_button) {
		save_effect_button->set_text(effect_dirty ? (compact ? TTR("Save*") : TTR("Save *")) : TTR("Save"));
	}
	if (add_to_level_button) {
		add_to_level_button->set_text(compact ? TTR("Add") : TTR("Add to Level"));
	}
}

void FPSStudioParticleEditorPlugin::_mark_document_dirty() {
	if (!_get_effect_root()) {
		return;
	}
	effect_dirty = true;
	_update_document_ui();
}

void FPSStudioParticleEditorPlugin::_set_effect_document(Node *p_root, const String &p_path, bool p_dirty) {
	Node *previous = _get_effect_root();
	if (previous && previous != p_root) {
		// Clear inspector sub-properties and the inline shader editor while their
		// resources are still alive. Resource properties fold themselves when
		// removed from the inspector and need their edited object to remain valid.
		if (emitter_inspector) {
			emitter_inspector->edit(nullptr);
		}
		if (process_material_inspector) {
			process_material_inspector->edit(nullptr);
		}
		if (renderer_inspector) {
			renderer_inspector->edit(nullptr);
		}
		if (particle_graph_editor) {
			particle_graph_editor->edit_shader(Ref<Shader>());
		}
		if (previous->is_inside_tree()) {
			previous->queue_free();
		} else {
			memdelete(previous);
		}
	}
	effect_root_id = p_root ? p_root->get_instance_id() : ObjectID();
	effect_path = p_path;
	effect_dirty = p_dirty;
	edited_emitter_id = ObjectID();
	Vector<Node *> emitters;
	_collect_emitters(p_root, emitters);
	_set_edited_emitter(emitters.is_empty() ? nullptr : emitters[0], false);
	if (!p_path.is_empty()) {
		_remember_effect(p_path);
	}
	_update_document_ui();
}

void FPSStudioParticleEditorPlugin::_set_edited_emitter(Node *p_node, bool p_update_tree) {
	Node *effect_root = _get_effect_root();
	if (p_node && !is_particle_node(p_node)) {
		p_node = nullptr;
	}
	if (p_node && effect_root && p_node != effect_root && !effect_root->is_ancestor_of(p_node)) {
		p_node = nullptr;
	}
	if (p_node && _get_edited_emitter() == p_node) {
		return;
	}
	edited_emitter_id = p_node ? p_node->get_instance_id() : ObjectID();
	if (emitter_inspector) {
		emitter_inspector->edit(p_node);
	}
	Ref<Material> process_material;
	Ref<Mesh> render_mesh;
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_node)) {
		process_material = gpu->get_process_material();
		render_mesh = gpu->get_draw_pass_mesh(0);
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_node)) {
		render_mesh = cpu->get_mesh();
	}
	if (process_material_inspector) {
		process_material_inspector->edit(process_material.ptr());
	}
	if (renderer_inspector) {
		renderer_inspector->edit(render_mesh.ptr());
	}
	if (selection_label) {
		selection_label->set_text(p_node ? vformat(TTR("Previewing %s"), p_node->get_name()) : TTR("Create or open a particle effect to start."));
	}
	if (timeline) {
		timeline->set_editable(p_node != nullptr);
	}
	if (play_button) {
		play_button->set_disabled(p_node == nullptr);
	}
	if (restart_button) {
		restart_button->set_disabled(p_node == nullptr);
	}
	_refresh_curve_editors();
	_update_particle_graph_controls();
	if (p_update_tree) {
		_update_emitter_tree();
	}
	_rebuild_preview();
}

void FPSStudioParticleEditorPlugin::_rebuild_preview() {
	if (!preview_root || rebuilding_preview) {
		return;
	}
	rebuilding_preview = true;
	if (preview_effect) {
		preview_root->remove_child(preview_effect);
		preview_effect->queue_free();
		preview_effect = nullptr;
	}
	Node *source = _get_effect_root();
	if (!source) {
		rebuilding_preview = false;
		return;
	}
	preview_effect = source->duplicate(Node::DUPLICATE_SCRIPTS);
	if (!preview_effect) {
		rebuilding_preview = false;
		return;
	}
	preview_effect->set_name("ParticlePreviewEffect");
	clear_preview_scripts(preview_effect);
	apply_preview_defaults(preview_effect, preview_quad);
	clear_preview_transform_origin(preview_effect);
	preview_root->add_child(preview_effect);

	Vector<Node *> emitters;
	_collect_emitters(source, emitters);
	preview_duration = 0.1;
	for (Node *emitter : emitters) {
		if (emitter->has_method("get_lifetime")) {
			preview_duration = MAX(preview_duration, double(emitter->call("get_lifetime")));
		}
	}
	preview_duration = MAX(preview_duration, 0.1);
	preview_time = 0.0;
	preview_time_label_update_accumulator = 0.0;
	if (timeline) {
		timeline->set_max(preview_duration);
		timeline->set_value_no_signal(0.0);
	}
	restart_particle_nodes(preview_effect);
	_set_preview_playing(preview_playing);
	_frame_preview();
	_update_time_label();
	rebuilding_preview = false;
}

void FPSStudioParticleEditorPlugin::_clear_preview_scripts(Node *p_node) {
	clear_preview_scripts(p_node);
}

void FPSStudioParticleEditorPlugin::_apply_preview_defaults(Node *p_node) {
	apply_preview_defaults(p_node, preview_quad);
}

void FPSStudioParticleEditorPlugin::_sync_preview_properties() {
	Node *source = _get_effect_root();
	if (!source || !preview_effect) {
		return;
	}
	copy_preview_properties(source, preview_effect);
	_apply_preview_defaults(preview_effect);
	_set_preview_playing(preview_playing);
}

void FPSStudioParticleEditorPlugin::_set_preview_playing(bool p_playing) {
	preview_playing = p_playing;
	if (play_button) {
		play_button->set_pressed_no_signal(preview_playing);
		play_button->set_text(preview_playing ? TTR("Pause") : TTR("Play"));
	}
	set_preview_speed_recursive(preview_effect, preview_playing ? preview_speed : 0.0);
}

void FPSStudioParticleEditorPlugin::_on_play_toggled(bool p_pressed) {
	if (!_get_effect_root()) {
		return;
	}
	_set_preview_playing(p_pressed);
}

void FPSStudioParticleEditorPlugin::_on_restart_pressed() {
	if (!preview_effect) {
		return;
	}
	preview_time = 0.0;
	preview_time_label_update_accumulator = 0.0;
	if (timeline) {
		timeline->set_value_no_signal(0.0);
	}
	restart_particle_nodes(preview_effect);
	_set_preview_playing(true);
	_update_time_label();
}

void FPSStudioParticleEditorPlugin::_on_timeline_changed(double p_value) {
	if (!preview_effect) {
		return;
	}
	_set_preview_playing(false);
	preview_time = p_value;
	preview_time_label_update_accumulator = 0.0;
	restart_particle_nodes(preview_effect, true, preview_time);
	_update_time_label();
}

void FPSStudioParticleEditorPlugin::_on_speed_changed(double p_value) {
	preview_speed = p_value;
	if (preview_playing && preview_effect) {
		set_preview_speed_recursive(preview_effect, preview_speed);
	}
}

void FPSStudioParticleEditorPlugin::_on_loop_toggled(bool p_enabled) {
	preview_loop = p_enabled;
}

void FPSStudioParticleEditorPlugin::_on_reset_view_pressed() {
	preview_camera_yaw = 0.0;
	preview_camera_pitch = 0.245;
	_frame_preview();
}

void FPSStudioParticleEditorPlugin::_on_grid_toggled(bool p_enabled) {
	if (preview_grid) {
		preview_grid->set_visible(p_enabled);
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_pressed() {
	if (!texture_generator_dialog) {
		return;
	}
	if (!generated_texture_image.is_valid()) {
		_regenerate_texture();
	}
	texture_generator_dialog->popup_centered_clamped(Size2i(900, 680) * EDSCALE, 0.85);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_preset_selected(int p_index) {
	if (p_index == VFX_TEXTURE_SMOKE) {
		texture_generator_color_a->set_pick_color(Color(0.16, 0.19, 0.24, 1.0));
		texture_generator_color_b->set_pick_color(Color(0.72, 0.76, 0.82, 1.0));
	} else if (p_index == VFX_TEXTURE_SPARK) {
		texture_generator_color_a->set_pick_color(Color(1.0, 0.46, 0.08, 1.0));
		texture_generator_color_b->set_pick_color(Color(1.0, 0.94, 0.64, 1.0));
	} else {
		texture_generator_color_a->set_pick_color(Color(0.95, 0.12, 0.025, 1.0));
		texture_generator_color_b->set_pick_color(Color(1.0, 0.82, 0.18, 1.0));
	}
	_regenerate_texture();
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_resolution_selected(int p_index) {
	_regenerate_texture();
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_output_selected(int p_index) {
	const int columns = texture_generator_output ? texture_generator_output->get_selected_id() : 1;
	if (texture_generator_fps_label) {
		texture_generator_fps_label->set_visible(columns > 1);
	}
	if (texture_generator_fps) {
		texture_generator_fps->set_visible(columns > 1);
	}
	_update_texture_time_controls();
	_regenerate_texture();
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_seed_changed(double p_value) {
	_regenerate_texture();
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_fps_changed(double p_value) {
	_update_texture_time_controls();
	_regenerate_texture();
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_animate_toggled(bool p_enabled) {
	texture_generator_playback_accumulator = 0.0;
	texture_generator_preview_refresh = 0.0;
	if (p_enabled && generated_texture_frame_columns == 1) {
		// The current-time sample changes while playing, so any previously exported
		// single frame no longer represents the selected generator state.
		generated_texture_path = String();
		if (texture_generator_apply_button) {
			texture_generator_apply_button->set_disabled(true);
		}
		if (texture_generator_status) {
			texture_generator_status->set_text(String());
		}
	} else if (!p_enabled && generated_texture_frame_columns == 1) {
		_regenerate_texture();
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_time_changed(double p_value) {
	texture_generator_time_value = p_value;
	texture_generator_preview_frame = -1;
	if (texture_generator_time_label) {
		texture_generator_time_label->set_text(vformat("%s s", String::num(texture_generator_time_value, 2)));
	}
	if (generated_texture_frame_columns > 1) {
		_update_texture_preview();
		return;
	}
	generated_texture_path = String();
	if (texture_generator_apply_button) {
		texture_generator_apply_button->set_disabled(true);
	}
	if (texture_generator_status) {
		texture_generator_status->set_text(String());
	}
	_regenerate_texture();
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_color_changed(const Color &p_color) {
	_regenerate_texture();
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_action(const String &p_action) {
	if (p_action == "export") {
		// A playing still preview is intentionally rendered at a smaller size for
		// responsiveness. Export always regenerates the full-resolution sample.
		_regenerate_texture();
		if (!generated_texture_image.is_valid() || !texture_save_dialog) {
			return;
		}
		const String output_suffix = generated_texture_frame_columns > 1 ? vformat("_flipbook_%dx%d", generated_texture_frame_columns, generated_texture_frame_columns) : String();
		texture_save_dialog->set_current_file(vformat("%s%s_%d.png", texture_generator_preset->get_text().to_lower(), output_suffix, int(texture_generator_seed->get_value())));
		texture_generator_return_after_export = true;
		texture_generator_dialog->hide();
		texture_save_dialog->popup_file_dialog();
	} else if (p_action == "apply") {
		_apply_generated_texture();
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_save_dialog_visibility_changed() {
	if (texture_save_dialog && !texture_save_dialog->is_visible()) {
		callable_mp(this, &FPSStudioParticleEditorPlugin::_show_texture_generator_after_export).call_deferred();
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_save_dialog_cancelled() {
	callable_mp(this, &FPSStudioParticleEditorPlugin::_show_texture_generator_after_export).call_deferred();
}

void FPSStudioParticleEditorPlugin::_show_texture_generator_after_export() {
	if (!texture_generator_return_after_export) {
		return;
	}
	texture_generator_return_after_export = false;
	if (texture_generator_dialog) {
		texture_generator_dialog->popup_centered_clamped(Size2i(900, 680) * EDSCALE, 0.85);
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_save_path_selected(const String &p_path) {
	if (!generated_texture_image.is_valid()) {
		texture_generator_return_after_export = false;
		return;
	}
	String path = p_path;
	if (path.get_extension().to_lower() != "png") {
		path += ".png";
	}
	const Error save_error = generated_texture_image->save_png(path);
	if (save_error != OK) {
		texture_generator_return_after_export = false;
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not save generated texture to %s (error %d)."), path, save_error));
		return;
	}
	generated_texture_path = path;
	EditorFileSystem::get_singleton()->update_file(path);
	Vector<String> files_to_reimport;
	files_to_reimport.push_back(path);
	EditorFileSystem::get_singleton()->reimport_files(files_to_reimport);
	Ref<Texture2D> imported_texture = ResourceLoader::load(path, "Texture2D");
	if (imported_texture.is_null()) {
		generated_texture_path = String();
		texture_generator_return_after_export = false;
		EditorNode::get_singleton()->show_warning(TTR("The PNG was saved, but Godot could not load the imported texture."));
		return;
	}
	generated_texture = imported_texture;
	if (texture_generator_apply_button) {
		texture_generator_apply_button->set_disabled(false);
	}
	if (texture_generator_status) {
		texture_generator_status->set_text(vformat(TTR("Exported: %s"), path.get_file()));
	}
	callable_mp(this, &FPSStudioParticleEditorPlugin::_show_texture_generator_after_export).call_deferred();
}

void FPSStudioParticleEditorPlugin::_update_texture_time_controls() {
	if (!texture_generator_time) {
		return;
	}
	const int columns = texture_generator_output ? texture_generator_output->get_selected_id() : 1;
	const int fps = texture_generator_fps ? MAX(1, int(texture_generator_fps->get_value())) : 12;
	const double max_time = columns > 1 ? (double(columns * columns - 1) / fps) : 1.0;
	texture_generator_time->set_min(0.0);
	texture_generator_time->set_max(max_time);
	texture_generator_time->set_step(columns > 1 ? 1.0 / fps : 0.01);
	texture_generator_time_value = CLAMP(texture_generator_time_value, 0.0, max_time);
	texture_generator_time->set_value_no_signal(texture_generator_time_value);
	if (texture_generator_time_label) {
		texture_generator_time_label->set_text(vformat("%s s", String::num(texture_generator_time_value, 2)));
	}
}

void FPSStudioParticleEditorPlugin::_update_texture_preview() {
	if (!texture_generator_preview || !generated_texture.is_valid()) {
		return;
	}
	if (generated_texture_frame_columns <= 1) {
		texture_generator_preview->set_texture(generated_texture);
		texture_generator_preview_frame = 0;
		return;
	}
	const int frame_count = generated_texture_frame_columns * generated_texture_frame_columns;
	const int frame_index = CLAMP(int(Math::floor(texture_generator_time_value * generated_texture_fps)), 0, frame_count - 1);
	if (frame_index == texture_generator_preview_frame) {
		return;
	}
	const int frame_size = generated_texture_image->get_width() / generated_texture_frame_columns;
	Ref<AtlasTexture> frame_texture;
	frame_texture.instantiate();
	frame_texture->set_atlas(generated_texture);
	frame_texture->set_region(Rect2((frame_index % generated_texture_frame_columns) * frame_size, (frame_index / generated_texture_frame_columns) * frame_size, frame_size, frame_size));
	texture_generator_preview->set_texture(frame_texture);
	texture_generator_preview_frame = frame_index;
}

void FPSStudioParticleEditorPlugin::_regenerate_texture(bool p_preview_only) {
	if (!texture_generator_preset || !texture_generator_resolution || !texture_generator_output || !texture_generator_seed || !texture_generator_fps || !texture_generator_color_a || !texture_generator_color_b) {
		return;
	}
	static const int resolutions[] = { 128, 256, 512, 1024 };
	const int resolution_index = CLAMP(texture_generator_resolution->get_selected(), 0, 3);
	const int size = resolutions[resolution_index];
	const int preset = texture_generator_preset->get_selected();
	const int output_id = texture_generator_output->get_selected_id();
	const int frame_columns = output_id == VFX_TEXTURE_OUTPUT_SHEET_8X8 ? 8 : (output_id == VFX_TEXTURE_OUTPUT_SHEET_4X4 ? 4 : 1);
	const int frame_size = size / frame_columns;
	const int seed = int(texture_generator_seed->get_value());
	const int fps = MAX(1, int(texture_generator_fps->get_value()));
	const Color color_a = texture_generator_color_a->get_pick_color();
	const Color color_b = texture_generator_color_b->get_pick_color();
	if (p_preview_only && frame_columns == 1) {
		const int preview_size = MIN(frame_size, 256);
		Ref<Image> preview_image = generate_particle_texture_frame(preview_size, preset, seed, color_a, color_b, texture_generator_time_value);
		texture_generator_preview->set_texture(ImageTexture::create_from_image(preview_image));
		return;
	}
	if (p_preview_only) {
		_update_texture_preview();
		return;
	}
	generated_texture_image.instantiate();
	generated_texture_image->initialize_data(size, size, false, Image::FORMAT_RGBA8);
	for (int frame_index = 0; frame_index < frame_columns * frame_columns; frame_index++) {
		const real_t sample_time = frame_columns == 1 ? texture_generator_time_value : real_t(frame_index) / fps;
		Ref<Image> frame_image = generate_particle_texture_frame(frame_size, preset, seed, color_a, color_b, sample_time);
		const int origin_x = (frame_index % frame_columns) * frame_size;
		const int origin_y = (frame_index / frame_columns) * frame_size;
		for (int py = 0; py < frame_size; py++) {
			for (int px = 0; px < frame_size; px++) {
				generated_texture_image->set_pixel(origin_x + px, origin_y + py, frame_image->get_pixel(px, py));
			}
		}
	}
	generated_texture_frame_columns = frame_columns;
	generated_texture_fps = fps;
	generated_texture = ImageTexture::create_from_image(generated_texture_image);
	generated_texture_path = String();
	texture_generator_preview_frame = -1;
	_update_texture_preview();
	if (texture_generator_apply_button) {
		texture_generator_apply_button->set_disabled(true);
	}
	if (texture_generator_status) {
		texture_generator_status->set_text(String());
	}
}

void FPSStudioParticleEditorPlugin::_apply_generated_texture() {
	if (!generated_texture.is_valid() || generated_texture_path.is_empty()) {
		EditorNode::get_singleton()->show_warning(TTR("Export the generated PNG first so the effect can keep a project texture reference."));
		return;
	}
	Node *emitter = _get_edited_emitter();
	if (!emitter) {
		EditorNode::get_singleton()->show_warning(TTR("Select a particle emitter before applying a texture."));
		return;
	}
	Ref<Mesh> mesh;
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(emitter);
	CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(emitter);
	if (gpu) {
		mesh = gpu->get_draw_pass_mesh(0);
	} else if (cpu) {
		mesh = cpu->get_mesh();
	}
	if (mesh.is_null()) {
		mesh = create_particle_quad();
	}
	Ref<Mesh> unique_mesh = mesh->duplicate(true);
	if (unique_mesh.is_valid()) {
		mesh = unique_mesh;
	}
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, generated_texture);
	material->set_particles_anim_h_frames(generated_texture_frame_columns);
	material->set_particles_anim_v_frames(generated_texture_frame_columns);
	material->set_particles_anim_loop(generated_texture_frame_columns > 1);
	if (generated_texture_frame_columns > 1) {
		material->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
		if (gpu) {
			Ref<Material> process_resource = gpu->get_process_material();
			ParticleProcessMaterial *process_material = Object::cast_to<ParticleProcessMaterial>(process_resource.ptr());
			if (process_material) {
				process_material->set_param_min(ParticleProcessMaterial::PARAM_ANIM_SPEED, generated_texture_fps);
				process_material->set_param_max(ParticleProcessMaterial::PARAM_ANIM_SPEED, generated_texture_fps);
			}
		} else if (cpu) {
			cpu->set_param_min(CPUParticles3D::PARAM_ANIM_SPEED, generated_texture_fps);
			cpu->set_param_max(CPUParticles3D::PARAM_ANIM_SPEED, generated_texture_fps);
		}
	}
	for (int surface = 0; surface < mesh->get_surface_count(); surface++) {
		mesh->surface_set_material(surface, material);
	}
	// The renderer inspector edits the old mesh by raw Object pointer. Detach it
	// before replacing the emitter's resource, then bind it to the new mesh while
	// this local Ref keeps the replacement alive.
	if (renderer_inspector) {
		renderer_inspector->edit(nullptr);
	}
	if (gpu) {
		gpu->set_draw_pass_mesh(0, mesh);
	} else if (cpu) {
		cpu->set_mesh(mesh);
	}
	if (renderer_inspector) {
		renderer_inspector->edit(mesh.ptr());
	}
	_mark_document_dirty();
	_sync_preview_properties();
}

void FPSStudioParticleEditorPlugin::_create_effect_document(int p_template) {
	Node3D *effect = memnew(Node3D);
	if (p_template == PARTICLE_TEMPLATE_BURST) {
		effect->set_name("BurstEffect");
	} else if (p_template == PARTICLE_TEMPLATE_FOUNTAIN) {
		effect->set_name("FountainEffect");
	} else if (p_template == PARTICLE_TEMPLATE_SMOKE) {
		effect->set_name("SmokeEffect");
	} else {
		effect->set_name("ParticleEffect");
	}
	Node3D *emitter = create_particle_emitter(p_template, preview_quad);
	effect->add_child(emitter, true);
	emitter->set_owner(effect);
	_set_effect_document(effect, String(), true);
}

void FPSStudioParticleEditorPlugin::_add_emitter(int p_type) {
	Node *effect = _get_effect_root();
	if (!effect) {
		_create_effect_document(p_type);
		return;
	}
	Node *previous_emitter = _get_edited_emitter();
	Node3D *emitter = create_particle_emitter(p_type, create_particle_quad());
	emitter->set_name(effect->prevalidate_child_name(emitter, emitter->get_name()));
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(vformat(TTR("Add %s to Particle Effect"), emitter->get_name()));
	undo_redo->add_do_method(effect, "add_child", emitter, true);
	undo_redo->add_do_method(emitter, "set_owner", effect);
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", emitter);
	undo_redo->add_do_reference(emitter);
	undo_redo->add_undo_method(emitter, "set_owner", (Object *)nullptr);
	undo_redo->add_undo_method(effect, "remove_child", emitter);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", previous_emitter);
	undo_redo->commit_action();
	_set_edited_emitter(emitter);
	_mark_document_dirty();
}

void FPSStudioParticleEditorPlugin::_remove_selected_emitter() {
	Node *effect = _get_effect_root();
	Node *emitter = _get_edited_emitter();
	if (!effect || !emitter || emitter == effect || emitter->get_parent() != effect) {
		return;
	}
	Vector<Node *> emitters;
	_collect_emitters(effect, emitters);
	if (emitters.size() <= 1) {
		EditorNode::get_singleton()->show_warning(TTR("A particle effect must keep at least one emitter. Add another emitter before removing this one."));
		return;
	}
	Node *next_emitter = nullptr;
	for (Node *candidate : emitters) {
		if (candidate != emitter) {
			next_emitter = candidate;
			break;
		}
	}
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Remove Particle Emitter"));
	undo_redo->add_do_method(emitter, "set_owner", (Object *)nullptr);
	undo_redo->add_do_method(effect, "remove_child", emitter);
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", next_emitter);
	undo_redo->add_undo_method(effect, "add_child", emitter, true);
	undo_redo->add_undo_method(emitter, "set_owner", effect);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", emitter);
	undo_redo->add_do_reference(emitter);
	undo_redo->commit_action();
	_set_edited_emitter(next_emitter);
	_mark_document_dirty();
}

void FPSStudioParticleEditorPlugin::_on_emitter_hierarchy_history_changed(Node *p_preferred_emitter) {
	Node *effect = _get_effect_root();
	if (!effect) {
		_set_edited_emitter(nullptr, false);
		_update_document_ui();
		return;
	}
	Vector<Node *> emitters;
	_collect_emitters(effect, emitters);
	Node *selected_emitter = p_preferred_emitter;
	if (!selected_emitter || (selected_emitter != effect && !effect->is_ancestor_of(selected_emitter))) {
		selected_emitter = emitters.is_empty() ? nullptr : emitters[0];
	}
	_set_edited_emitter(selected_emitter, false);
	_mark_document_dirty();
}

void FPSStudioParticleEditorPlugin::_on_new_effect_menu_pressed(int p_id) {
	if (effect_dirty) {
		pending_new_template = p_id;
		pending_load_path = String();
		replace_document_confirmation->set_text(TTR("The current particle effect has unsaved changes. Discard them and create a new effect?"));
		replace_document_confirmation->popup_centered();
		return;
	}
	_create_effect_document(p_id);
}

void FPSStudioParticleEditorPlugin::_on_add_emitter_menu_pressed(int p_id) {
	_add_emitter(p_id);
}

void FPSStudioParticleEditorPlugin::_on_effect_filter_changed(const String &p_text) {
	_update_effects_tree();
}

void FPSStudioParticleEditorPlugin::_on_open_effect_pressed() {
	if (load_scene_dialog) {
		load_scene_dialog->popup_file_dialog();
	}
}

void FPSStudioParticleEditorPlugin::_on_effect_activated() {
	TreeItem *item = effects_tree ? effects_tree->get_selected() : nullptr;
	if (!item) {
		return;
	}
	const String path = item->get_metadata(0);
	if (!path.is_empty() && path != effect_path) {
		if (effect_dirty) {
			pending_load_path = path;
			pending_new_template = -1;
			replace_document_confirmation->set_text(TTR("The current particle effect has unsaved changes. Discard them and open the selected effect?"));
			replace_document_confirmation->popup_centered();
		} else {
			_load_particle_scene(path);
		}
	}
}

void FPSStudioParticleEditorPlugin::_on_document_title_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mouse_button = p_event;
	if (!mouse_button.is_valid() || !mouse_button->is_pressed() || mouse_button->get_button_index() != MouseButton::LEFT || !mouse_button->is_double_click()) {
		return;
	}
	_on_rename_effect_requested();
}

void FPSStudioParticleEditorPlugin::_on_rename_effect_requested() {
	Node *effect = _get_effect_root();
	if (!effect || !rename_effect_dialog || !rename_effect_line_edit) {
		return;
	}
	rename_effect_line_edit->set_text(effect->get_name());
	rename_effect_dialog->popup_centered();
	rename_effect_line_edit->grab_focus();
	rename_effect_line_edit->select_all();
}

void FPSStudioParticleEditorPlugin::_on_rename_effect_confirmed() {
	Node *effect = _get_effect_root();
	if (!effect || !rename_effect_line_edit) {
		return;
	}
	String new_name = rename_effect_line_edit->get_text().strip_edges().validate_node_name();
	if (new_name.is_empty()) {
		_update_document_ui();
		return;
	}
	const String old_name = effect->get_name();
	if (new_name == old_name) {
		return;
	}
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Rename Particle Effect"), UndoRedo::MERGE_DISABLE, effect);
	undo_redo->add_do_method(effect, "set_name", new_name);
	undo_redo->add_undo_method(effect, "set_name", old_name);
	undo_redo->add_do_method(this, "_on_effect_name_changed");
	undo_redo->add_undo_method(this, "_on_effect_name_changed");
	undo_redo->commit_action();
}

void FPSStudioParticleEditorPlugin::_on_effect_name_changed() {
	_mark_document_dirty();
}

void FPSStudioParticleEditorPlugin::_on_emitter_selected() {
	TreeItem *item = emitter_tree ? emitter_tree->get_selected() : nullptr;
	if (!item) {
		return;
	}
	const uint64_t id = uint64_t(item->get_metadata(0));
	_set_edited_emitter(Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(id))), false);
}

void FPSStudioParticleEditorPlugin::_on_emitter_renamed() {
	TreeItem *item = emitter_tree ? emitter_tree->get_selected() : nullptr;
	Node *effect = _get_effect_root();
	if (!item || !effect) {
		_update_emitter_tree();
		return;
	}
	const uint64_t id = uint64_t(item->get_metadata(0));
	Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(id)));
	if (!emitter || emitter->get_parent() != effect) {
		_update_emitter_tree();
		return;
	}

	const String old_name = emitter->get_name();
	String new_name = item->get_text(0).strip_edges().validate_node_name();
	if (new_name.is_empty()) {
		_update_emitter_tree();
		return;
	}
	new_name = effect->prevalidate_child_name(emitter, new_name);
	if (new_name == old_name) {
		_update_emitter_tree();
		return;
	}

	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Rename Particle Emitter"), UndoRedo::MERGE_DISABLE, emitter);
	undo_redo->add_do_method(emitter, "set_name", new_name);
	undo_redo->add_undo_method(emitter, "set_name", old_name);
	undo_redo->add_do_method(this, "_on_emitter_name_changed");
	undo_redo->add_undo_method(this, "_on_emitter_name_changed");
	undo_redo->commit_action();
}

void FPSStudioParticleEditorPlugin::_on_emitter_name_changed() {
	Node *emitter = _get_edited_emitter();
	if (selection_label) {
		selection_label->set_text(emitter ? vformat(TTR("Previewing %s"), emitter->get_name()) : TTR("Create or open a particle effect to start."));
	}
	_update_emitter_tree();
	_rebuild_preview();
	_mark_document_dirty();
}

void FPSStudioParticleEditorPlugin::_confirm_replace_document() {
	if (pending_new_template >= 0) {
		const int template_type = pending_new_template;
		pending_new_template = -1;
		pending_load_path = String();
		_create_effect_document(template_type);
		return;
	}
	const String path = pending_load_path;
	pending_load_path = String();
	if (!path.is_empty()) {
		_load_particle_scene(path);
	}
}

void FPSStudioParticleEditorPlugin::_on_save_scene_pressed() {
	Node *effect = _get_effect_root();
	if (!effect || !save_scene_dialog) {
		return;
	}
	if (!effect_path.is_empty()) {
		_save_emitter_scene(effect_path);
		return;
	}
	save_scene_dialog->set_current_dir("res://Particles");
	save_scene_dialog->set_current_file(String(effect->get_name()).validate_filename() + ".tscn");
	save_scene_dialog->popup_file_dialog();
}

void FPSStudioParticleEditorPlugin::_save_emitter_scene(const String &p_path) {
	Node *effect = _get_effect_root();
	if (!effect) {
		return;
	}
	set_scene_subtree_owners(effect, effect);
	Ref<PackedScene> packed_scene;
	packed_scene.instantiate();
	const Error pack_error = packed_scene->pack(effect);
	if (pack_error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not pack the particle effect (error %d)."), pack_error));
		return;
	}
	String save_path = p_path;
	if (save_path.get_extension().to_lower() != "tscn") {
		save_path += ".tscn";
	}
	const Error save_error = ResourceSaver::save(packed_scene, save_path);
	if (save_error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not save particle effect to %s (error %d)."), save_path, save_error));
		return;
	}
	effect_path = save_path;
	effect->set_scene_file_path(save_path);
	effect_dirty = false;
	_remember_effect(save_path);
	_update_document_ui();
	EditorFileSystem::get_singleton()->scan_changes();
}

void FPSStudioParticleEditorPlugin::_load_particle_scene(const String &p_path) {
	Ref<PackedScene> packed_scene = ResourceLoader::load(p_path, "PackedScene");
	if (packed_scene.is_null()) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not load particle effect from %s."), p_path));
		return;
	}
	Node *particle_scene = packed_scene->instantiate();
	if (!particle_scene) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not instantiate particle effect from %s."), p_path));
		return;
	}
	Vector<Node *> emitters;
	_collect_emitters(particle_scene, emitters);
	if (emitters.is_empty()) {
		memdelete(particle_scene);
		EditorNode::get_singleton()->show_warning(TTR("The selected scene does not contain a 3D particle emitter."));
		return;
	}
	particle_scene->set_scene_file_path(p_path);
	_set_effect_document(particle_scene, p_path, false);
}

void FPSStudioParticleEditorPlugin::_add_effect_to_level() {
	Node *scene_root = _get_active_scene_root();
	Node *effect = _get_effect_root();
	if (!scene_root || !effect) {
		return;
	}
	Node *instance = nullptr;
	if (!effect_path.is_empty()) {
		Ref<PackedScene> packed_scene = ResourceLoader::load(effect_path, "PackedScene");
		if (packed_scene.is_valid()) {
			instance = packed_scene->instantiate();
		}
	}
	if (!instance) {
		instance = effect->duplicate(Node::DUPLICATE_SIGNALS | Node::DUPLICATE_SCRIPTS);
		if (instance) {
			set_scene_subtree_owners(instance, instance);
		}
	}
	if (!instance) {
		EditorNode::get_singleton()->show_warning(TTR("Could not create a Level instance of this particle effect."));
		return;
	}
	instance->set_name(scene_root->prevalidate_child_name(instance, effect->get_name()));
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(vformat(TTR("Add %s to Level"), effect->get_name()));
	undo_redo->add_do_method(scene_root, "add_child", instance, true);
	undo_redo->add_do_method(instance, "set_owner", scene_root);
	undo_redo->add_do_reference(instance);
	undo_redo->add_undo_method(scene_root, "remove_child", instance);
	undo_redo->commit_action();
	EditorSelection *selection = EditorNode::get_singleton()->get_editor_selection();
	selection->clear();
	selection->add_node(instance);
}

void FPSStudioParticleEditorPlugin::_on_emitter_property_edited(const String &p_property) {
	Node *emitter = _get_edited_emitter();
	if (!emitter) {
		return;
	}
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(emitter)) {
		Ref<Material> material = gpu->get_process_material();
		if (process_material_inspector->get_edited_object() != material.ptr()) {
			process_material_inspector->edit(material.ptr());
		}
		Ref<Mesh> mesh = gpu->get_draw_pass_mesh(0);
		if (renderer_inspector->get_edited_object() != mesh.ptr()) {
			renderer_inspector->edit(mesh.ptr());
		}
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(emitter)) {
		Ref<Mesh> mesh = cpu->get_mesh();
		if (renderer_inspector->get_edited_object() != mesh.ptr()) {
			renderer_inspector->edit(mesh.ptr());
		}
	}
	_mark_document_dirty();
	_refresh_curve_editors();
	_update_particle_graph_controls();
	_sync_preview_properties();
	if (p_property == "lifetime") {
		_rebuild_preview();
	}
}

Ref<VisualShader> FPSStudioParticleEditorPlugin::_get_particle_visual_shader(Node *p_emitter) const {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_emitter);
	if (!gpu) {
		return Ref<VisualShader>();
	}
	Ref<Material> process_material = gpu->get_process_material();
	ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(process_material.ptr());
	if (!shader_material) {
		return Ref<VisualShader>();
	}
	Ref<Shader> shader = shader_material->get_shader();
	VisualShader *visual_shader = Object::cast_to<VisualShader>(shader.ptr());
	if (!visual_shader || visual_shader->get_mode() != Shader::MODE_PARTICLES) {
		return Ref<VisualShader>();
	}
	return Ref<VisualShader>(visual_shader);
}

void FPSStudioParticleEditorPlugin::_update_particle_graph_controls() {
	if (!particle_graph_button || !particle_logic_status) {
		return;
	}
	Node *emitter = _get_edited_emitter();
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(emitter);
	Ref<VisualShader> visual_shader = _get_particle_visual_shader(emitter);
	particle_graph_button->set_disabled(!gpu);
	particle_graph_button->set_text(visual_shader.is_valid() ? TTR("Open in Shader Editor") : TTR("Create Node Graph"));
	if (!emitter) {
		particle_logic_status->set_text(TTR("Select an emitter to edit its particle logic."));
	} else if (!gpu) {
		particle_logic_status->set_text(TTR("CPU particles use built-in simulation settings. Particle shader graphs are available for GPU emitters."));
	} else if (visual_shader.is_valid()) {
		particle_logic_status->set_text(TTR("Editing this emitter's native particle VisualShader."));
	} else {
		particle_logic_status->set_text(TTR("Using ParticleProcessMaterial. Create a native VisualShader to edit particle behavior as nodes."));
	}
	if (visual_shader.is_valid()) {
		if (!particle_graph_editor && particle_logic_content) {
			particle_graph_editor = memnew(VisualShaderEditor);
			particle_graph_editor->set_toggle_list_control(nullptr);
			particle_graph_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			particle_graph_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			particle_graph_editor->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
			particle_logic_content->add_child(particle_graph_editor);
		}
		if (particle_graph_editor) {
			particle_graph_editor->edit_shader(visual_shader);
			particle_graph_editor->show();
		}
	} else if (particle_graph_editor) {
		particle_graph_editor->edit_shader(Ref<Shader>());
		particle_graph_editor->hide();
	}
	if (particle_logic_hint) {
		particle_logic_hint->set_visible(!visual_shader.is_valid());
	}
	if (visual_shader.is_valid()) {
		const Callable changed_callable = callable_mp(this, &FPSStudioParticleEditorPlugin::_on_curve_changed);
		if (!visual_shader->is_connected(SNAME("changed"), changed_callable)) {
			visual_shader->connect(SNAME("changed"), changed_callable);
		}
	}
}

void FPSStudioParticleEditorPlugin::_on_particle_graph_button_pressed() {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	if (!gpu) {
		return;
	}
	Ref<VisualShader> visual_shader = _get_particle_visual_shader(gpu);
	if (visual_shader.is_valid()) {
		_open_particle_graph(visual_shader);
		return;
	}
	if (gpu->get_process_material().is_valid()) {
		particle_graph_confirmation->set_text(TTR("Creating a node graph replaces this emitter's current Process Material. You can restore the previous material with Undo."));
		particle_graph_confirmation->popup_centered();
		return;
	}
	_create_particle_graph();
}

void FPSStudioParticleEditorPlugin::_create_particle_graph() {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	if (!gpu) {
		return;
	}
	Ref<VisualShader> visual_shader;
	visual_shader.instantiate();
	visual_shader->set_mode(Shader::MODE_PARTICLES);
	Ref<ShaderMaterial> shader_material;
	shader_material.instantiate();
	shader_material->set_shader(visual_shader);
	Ref<Material> previous_material = gpu->get_process_material();
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Create Particle Node Graph"), UndoRedo::MERGE_DISABLE, gpu);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(gpu, "set_process_material", shader_material);
	undo_redo->add_undo_method(gpu, "set_process_material", previous_material);
	undo_redo->add_do_reference(shader_material.ptr());
	if (previous_material.is_valid()) {
		undo_redo->add_undo_reference(previous_material.ptr());
	}
	undo_redo->commit_action();
	process_material_inspector->edit(shader_material.ptr());
	_mark_document_dirty();
	_sync_preview_properties();
	_update_particle_graph_controls();
}

void FPSStudioParticleEditorPlugin::_open_particle_graph(const Ref<VisualShader> &p_visual_shader) {
	if (p_visual_shader.is_valid()) {
		EditorNode::get_singleton()->edit_item(p_visual_shader.ptr(), this);
	}
}

static void make_lifetime_curve(const Ref<Curve> &p_curve, bool p_fade_out) {
	if (p_curve.is_null()) {
		return;
	}
	p_curve->set_min_domain(0.0);
	p_curve->set_max_domain(1.0);
	p_curve->set_min_value(0.0);
	p_curve->set_max_value(1.0);
	p_curve->clear_points();
	p_curve->add_point(Vector2(0.0, p_fade_out ? 1.0 : 0.15));
	p_curve->add_point(Vector2(1.0, p_fade_out ? 0.0 : 1.0));
	p_curve->set_point_right_mode(0, Curve::TANGENT_LINEAR);
	p_curve->set_point_left_mode(1, Curve::TANGENT_LINEAR);
}

static void append_particle_curve_point(Array &r_data, real_t p_x, real_t p_y, real_t p_left_tangent = 0.0, real_t p_right_tangent = 0.0) {
	r_data.push_back(Vector2(p_x, p_y));
	r_data.push_back(p_left_tangent);
	r_data.push_back(p_right_tangent);
	r_data.push_back(int(Curve::TANGENT_FREE));
	r_data.push_back(int(Curve::TANGENT_FREE));
}

static Array make_particle_curve_preset_data(const Ref<Curve> &p_curve, bool p_alpha, int p_preset_id) {
	Array data;
	if (p_curve.is_null()) {
		return data;
	}

	const real_t x0 = p_curve->get_min_domain();
	const real_t x1 = p_curve->get_max_domain();
	const real_t x_width = x1 - x0;
	const real_t x_mid = x0 + x_width * 0.5;
	const real_t y_min = p_curve->get_min_value();
	const real_t y_max = p_curve->get_max_value();
	const real_t y_range = y_max - y_min;
	const real_t low = p_alpha ? y_min : y_min + y_range * 0.15;
	const real_t high = y_max;
	const real_t slope = (high - low) / x_width;

	if (p_alpha) {
		switch (p_preset_id) {
			case PARTICLE_ALPHA_CURVE_FADE_OUT_LINEAR:
				append_particle_curve_point(data, x0, high, 0.0, -slope);
				append_particle_curve_point(data, x1, y_min, -slope, 0.0);
				break;
			case PARTICLE_ALPHA_CURVE_FADE_OUT_SMOOTH:
				append_particle_curve_point(data, x0, high);
				append_particle_curve_point(data, x1, y_min);
				break;
			case PARTICLE_ALPHA_CURVE_FADE_IN:
				append_particle_curve_point(data, x0, y_min, 0.0, slope);
				append_particle_curve_point(data, x1, high, slope, 0.0);
				break;
			case PARTICLE_ALPHA_CURVE_FADE_IN_OUT:
				append_particle_curve_point(data, x0, y_min);
				append_particle_curve_point(data, x_mid, high);
				append_particle_curve_point(data, x1, y_min);
				break;
			case PARTICLE_ALPHA_CURVE_HOLD_THEN_FADE: {
				const real_t fade_start = x0 + x_width * 0.7;
				const real_t fade_slope = (y_min - high) / (x1 - fade_start);
				append_particle_curve_point(data, x0, high);
				append_particle_curve_point(data, fade_start, high, 0.0, fade_slope);
				append_particle_curve_point(data, x1, y_min, fade_slope, 0.0);
			} break;
		}
	} else {
		switch (p_preset_id) {
			case PARTICLE_SCALE_CURVE_GROW:
				append_particle_curve_point(data, x0, low, 0.0, slope);
				append_particle_curve_point(data, x1, high, slope, 0.0);
				break;
			case PARTICLE_SCALE_CURVE_SHRINK:
				append_particle_curve_point(data, x0, high, 0.0, -slope);
				append_particle_curve_point(data, x1, low, -slope, 0.0);
				break;
			case PARTICLE_SCALE_CURVE_GROW_AND_SHRINK:
				append_particle_curve_point(data, x0, low);
				append_particle_curve_point(data, x_mid, high);
				append_particle_curve_point(data, x1, low);
				break;
			case PARTICLE_SCALE_CURVE_HOLD_THEN_GROW: {
				const real_t grow_start = x0 + x_width * 0.65;
				const real_t grow_slope = (high - low) / (x1 - grow_start);
				append_particle_curve_point(data, x0, low);
				append_particle_curve_point(data, grow_start, low, 0.0, grow_slope);
				append_particle_curve_point(data, x1, high, grow_slope, 0.0);
			} break;
			case PARTICLE_SCALE_CURVE_HOLD: {
				const real_t steady = y_min + y_range * 0.8;
				append_particle_curve_point(data, x0, steady);
				append_particle_curve_point(data, x1, steady);
			} break;
		}
	}
	return data;
}

void FPSStudioParticleEditorPlugin::_on_add_alpha_curve_pressed() {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	Ref<ParticleProcessMaterial> material;
	if (gpu) {
		material = Ref<ParticleProcessMaterial>(Object::cast_to<ParticleProcessMaterial>(gpu->get_process_material().ptr()));
	}
	if (material.is_null()) {
		return;
	}
	Ref<Curve> curve;
	curve.instantiate();
	make_lifetime_curve(curve, true);
	Ref<CurveTexture> texture;
	texture.instantiate();
	texture->set_curve(curve);
	Ref<Texture2D> previous = material->get_alpha_curve();
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Add Alpha Over Life Curve"));
	undo_redo->add_do_method(material.ptr(), "set_alpha_curve", Ref<Texture2D>(texture));
	undo_redo->add_undo_method(material.ptr(), "set_alpha_curve", previous);
	undo_redo->add_do_reference(texture.ptr());
	if (previous.is_valid()) {
		undo_redo->add_undo_reference(previous.ptr());
	}
	undo_redo->commit_action();
	texture->connect(SNAME("changed"), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_curve_changed));
	curve->connect(SNAME("changed"), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_curve_changed));
	_refresh_curve_editors();
	_mark_document_dirty();
	_sync_preview_properties();
}

void FPSStudioParticleEditorPlugin::_on_add_scale_curve_pressed() {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	Ref<ParticleProcessMaterial> material;
	if (gpu) {
		material = Ref<ParticleProcessMaterial>(Object::cast_to<ParticleProcessMaterial>(gpu->get_process_material().ptr()));
	}
	if (material.is_null()) {
		return;
	}
	Ref<Curve> curve;
	curve.instantiate();
	make_lifetime_curve(curve, false);
	Ref<CurveTexture> texture;
	texture.instantiate();
	texture->set_curve(curve);
	Ref<Texture2D> previous = material->get_param_texture(ParticleProcessMaterial::PARAM_SCALE);
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Add Scale Over Life Curve"));
	undo_redo->add_do_method(material.ptr(), "set_param_texture", ParticleProcessMaterial::PARAM_SCALE, Ref<Texture2D>(texture));
	undo_redo->add_undo_method(material.ptr(), "set_param_texture", ParticleProcessMaterial::PARAM_SCALE, previous);
	undo_redo->add_do_reference(texture.ptr());
	if (previous.is_valid()) {
		undo_redo->add_undo_reference(previous.ptr());
	}
	undo_redo->commit_action();
	texture->connect(SNAME("changed"), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_curve_changed));
	curve->connect(SNAME("changed"), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_curve_changed));
	_refresh_curve_editors();
	_mark_document_dirty();
	_sync_preview_properties();
}

void FPSStudioParticleEditorPlugin::_on_alpha_curve_preset_selected(int p_preset_id) {
	_apply_curve_preset(true, p_preset_id);
}

void FPSStudioParticleEditorPlugin::_on_scale_curve_preset_selected(int p_preset_id) {
	_apply_curve_preset(false, p_preset_id);
}

void FPSStudioParticleEditorPlugin::_apply_curve_preset(bool p_alpha, int p_preset_id) {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	Ref<ParticleProcessMaterial> material;
	if (gpu) {
		material = Ref<ParticleProcessMaterial>(Object::cast_to<ParticleProcessMaterial>(gpu->get_process_material().ptr()));
	}
	if (material.is_null()) {
		return;
	}

	Ref<Texture2D> previous_texture = p_alpha ? material->get_alpha_curve() : material->get_param_texture(ParticleProcessMaterial::PARAM_SCALE);
	Ref<CurveTexture> curve_texture = Ref<CurveTexture>(Object::cast_to<CurveTexture>(previous_texture.ptr()));
	if (previous_texture.is_valid() && curve_texture.is_null()) {
		return;
	}
	Ref<Curve> curve = curve_texture.is_valid() ? curve_texture->get_curve() : Ref<Curve>();
	const bool create_curve_resource = curve.is_null();
	if (create_curve_resource) {
		curve.instantiate();
		curve->set_min_domain(0.0);
		curve->set_max_domain(1.0);
		curve->set_min_value(0.0);
		curve->set_max_value(1.0);
		curve_texture.instantiate();
		curve_texture->set_curve(curve);
	}

	const Array previous_data = curve->get_data();
	const Array preset_data = make_particle_curve_preset_data(curve, p_alpha, p_preset_id);
	if (preset_data.is_empty()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(p_alpha ? TTR("Apply Alpha over Life Preset") : TTR("Apply Scale over Life Preset"));
	if (create_curve_resource) {
		if (p_alpha) {
			undo_redo->add_do_method(material.ptr(), "set_alpha_curve", Ref<Texture2D>(curve_texture));
			undo_redo->add_undo_method(material.ptr(), "set_alpha_curve", previous_texture);
		} else {
			undo_redo->add_do_method(material.ptr(), "set_param_texture", ParticleProcessMaterial::PARAM_SCALE, Ref<Texture2D>(curve_texture));
			undo_redo->add_undo_method(material.ptr(), "set_param_texture", ParticleProcessMaterial::PARAM_SCALE, previous_texture);
		}
		undo_redo->add_do_reference(curve_texture.ptr());
		if (previous_texture.is_valid()) {
			undo_redo->add_undo_reference(previous_texture.ptr());
		}
	}
	undo_redo->add_do_method(curve.ptr(), "_set_data", preset_data);
	undo_redo->add_undo_method(curve.ptr(), "_set_data", previous_data);
	undo_redo->commit_action();
	_refresh_curve_editors();
	_mark_document_dirty();
	_sync_preview_properties();
}

void FPSStudioParticleEditorPlugin::_refresh_curve_editors() {
	if (!curve_panel) {
		return;
	}
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	Ref<ParticleProcessMaterial> material;
	if (gpu) {
		material = Ref<ParticleProcessMaterial>(Object::cast_to<ParticleProcessMaterial>(gpu->get_process_material().ptr()));
	}
	curve_panel->set_visible(material.is_valid());
	if (material.is_null()) {
		return;
	}
	Ref<Texture2D> alpha_texture = material->get_alpha_curve();
	CurveTexture *alpha_curve_texture = Object::cast_to<CurveTexture>(alpha_texture.ptr());
	const bool has_alpha_curve = alpha_curve_texture && alpha_curve_texture->get_curve().is_valid();
	alpha_curve_editor->set_visible(has_alpha_curve);
	alpha_curve_add_button->set_visible(!alpha_texture.is_valid());
	alpha_curve_presets_button->set_disabled(alpha_texture.is_valid() && !has_alpha_curve);
	alpha_curve_info->set_visible(alpha_texture.is_valid() && !has_alpha_curve);
	if (has_alpha_curve) {
		alpha_curve_editor->set_curve(alpha_curve_texture->get_curve());
		const Callable changed_callable = callable_mp(this, &FPSStudioParticleEditorPlugin::_on_curve_changed);
		if (!alpha_curve_texture->is_connected(SNAME("changed"), changed_callable)) {
			alpha_curve_texture->connect(SNAME("changed"), changed_callable);
		}
		if (!alpha_curve_texture->get_curve()->is_connected(SNAME("changed"), changed_callable)) {
			alpha_curve_texture->get_curve()->connect(SNAME("changed"), changed_callable);
		}
	} else {
		alpha_curve_editor->set_curve(Ref<Curve>());
	}
	if (alpha_curve_info->is_visible()) {
		alpha_curve_info->set_text(TTR("This curve resource is not a CurveTexture. Open Advanced properties to inspect it."));
	}

	Ref<Texture2D> scale_texture = material->get_param_texture(ParticleProcessMaterial::PARAM_SCALE);
	CurveTexture *scale_curve_texture = Object::cast_to<CurveTexture>(scale_texture.ptr());
	const bool has_scale_curve = scale_curve_texture && scale_curve_texture->get_curve().is_valid();
	scale_curve_editor->set_visible(has_scale_curve);
	scale_curve_add_button->set_visible(!scale_texture.is_valid());
	scale_curve_presets_button->set_disabled(scale_texture.is_valid() && !has_scale_curve);
	scale_curve_info->set_visible(scale_texture.is_valid() && !has_scale_curve);
	if (has_scale_curve) {
		scale_curve_editor->set_curve(scale_curve_texture->get_curve());
		const Callable changed_callable = callable_mp(this, &FPSStudioParticleEditorPlugin::_on_curve_changed);
		if (!scale_curve_texture->is_connected(SNAME("changed"), changed_callable)) {
			scale_curve_texture->connect(SNAME("changed"), changed_callable);
		}
		if (!scale_curve_texture->get_curve()->is_connected(SNAME("changed"), changed_callable)) {
			scale_curve_texture->get_curve()->connect(SNAME("changed"), changed_callable);
		}
	} else {
		scale_curve_editor->set_curve(Ref<Curve>());
	}
	if (scale_curve_info->is_visible()) {
		scale_curve_info->set_text(TTR("This is a 3D curve or another texture type. Edit it in Advanced properties."));
	}
}

void FPSStudioParticleEditorPlugin::_on_curve_changed() {
	_mark_document_dirty();
	_sync_preview_properties();
}

void FPSStudioParticleEditorPlugin::_on_preview_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mouse_button = p_event;
	if (mouse_button.is_valid()) {
		const MouseButton button_index = mouse_button->get_button_index();
		if (button_index == MouseButton::LEFT || button_index == MouseButton::RIGHT) {
			preview_orbit_dragging = mouse_button->is_pressed();
			if (mouse_button->is_pressed()) {
				preview_pan_dragging = false;
			}
			preview_container->accept_event();
			return;
		}
		if (button_index == MouseButton::MIDDLE) {
			preview_pan_dragging = mouse_button->is_pressed();
			if (mouse_button->is_pressed()) {
				preview_orbit_dragging = false;
			}
			preview_container->accept_event();
			return;
		}
		if (mouse_button->is_pressed() && (button_index == MouseButton::WHEEL_UP || button_index == MouseButton::WHEEL_DOWN)) {
			preview_camera_distance = CLAMP(preview_camera_distance * (button_index == MouseButton::WHEEL_UP ? 0.85 : 1.0 / 0.85), 0.2, 120.0);
			_update_preview_camera();
			preview_container->accept_event();
			return;
		}
	}
	Ref<InputEventMouseMotion> mouse_motion = p_event;
	if (mouse_motion.is_valid() && (preview_orbit_dragging || preview_pan_dragging)) {
		const BitField<MouseButtonMask> button_mask = mouse_motion->get_button_mask();
		if (preview_pan_dragging && !button_mask.has_flag(MouseButtonMask::MIDDLE)) {
			preview_pan_dragging = false;
			return;
		}
		if (preview_orbit_dragging && !button_mask.has_flag(MouseButtonMask::LEFT) && !button_mask.has_flag(MouseButtonMask::RIGHT)) {
			preview_orbit_dragging = false;
			return;
		}
		const Vector2 relative = mouse_motion->get_relative();
		if (preview_pan_dragging) {
			const real_t viewport_height = MAX(1.0, preview_container->get_size().height);
			const real_t world_per_pixel = 2.0 * preview_camera_distance * Math::tan(Math::deg_to_rad(preview_camera->get_fov()) * 0.5) / viewport_height;
			const Basis camera_basis = preview_camera->get_global_transform().basis;
			preview_camera_target += camera_basis.get_column(0) * (-relative.x * world_per_pixel) + camera_basis.get_column(1) * (relative.y * world_per_pixel);
		} else {
			preview_camera_yaw -= relative.x * 0.006;
			preview_camera_pitch = CLAMP(preview_camera_pitch - relative.y * 0.006, Math::deg_to_rad(-85.0), Math::deg_to_rad(85.0));
		}
		_update_preview_camera();
		preview_container->accept_event();
	}
}

void FPSStudioParticleEditorPlugin::_update_preview_camera() {
	if (!preview_camera) {
		return;
	}
	const real_t horizontal_distance = preview_camera_distance * Math::cos(preview_camera_pitch);
	const Vector3 camera_position = preview_camera_target + Vector3(Math::sin(preview_camera_yaw) * horizontal_distance, Math::sin(preview_camera_pitch) * preview_camera_distance, Math::cos(preview_camera_yaw) * horizontal_distance);
	preview_camera->look_at_from_position(camera_position, preview_camera_target, Vector3(0.0, 1.0, 0.0));
}

void FPSStudioParticleEditorPlugin::_frame_preview() {
	preview_camera_target = Vector3(0.0, 1.0, 0.0);
	preview_camera_distance = 5.5;
	Node *root = preview_effect;
	if (root) {
		Vector<Node *> emitters;
		_collect_emitters(root, emitters);
		AABB bounds;
		bool bounds_valid = false;
		for (Node *emitter : emitters) {
			Node3D *node_3d = Object::cast_to<Node3D>(emitter);
			if (!node_3d) {
				continue;
			}
			AABB emitter_bounds;
			if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(emitter)) {
				emitter_bounds = gpu->get_visibility_aabb();
			} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(emitter)) {
				emitter_bounds = cpu->get_visibility_aabb();
			}
			if (emitter_bounds.size.length_squared() < 0.0001) {
				emitter_bounds = AABB(Vector3(-1.5, -0.2, -1.5), Vector3(3.0, 4.0, 3.0));
			}
			emitter_bounds = node_3d->get_global_transform().xform(emitter_bounds);
			if (!bounds_valid) {
				bounds = emitter_bounds;
				bounds_valid = true;
			} else {
				bounds = bounds.merge(emitter_bounds);
			}
		}
		if (bounds_valid) {
			preview_camera_target = bounds.get_center();
			const real_t radius = MAX(real_t(0.5), bounds.size.length() * 0.5);
			preview_camera_distance = CLAMP(radius / Math::tan(Math::deg_to_rad(preview_camera->get_fov()) * 0.5) * 1.2, 1.2, 48.0);
		}
	}
	_update_preview_camera();
}

void FPSStudioParticleEditorPlugin::_update_time_label() {
	if (time_label) {
		time_label->set_text(vformat("%s / %s s", String::num(preview_time, 2), String::num(preview_duration, 2)));
	}
}

void FPSStudioParticleEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS && texture_generator_dialog && texture_generator_dialog->is_visible() && texture_generator_animate && texture_generator_animate->is_pressed()) {
		const double delta = get_process_delta_time();
		const bool is_sheet = generated_texture_frame_columns > 1;
		const double duration = is_sheet ? double(generated_texture_frame_columns * generated_texture_frame_columns) / generated_texture_fps : 1.0;
		texture_generator_time_value += delta;
		if (texture_generator_time_value >= duration) {
			texture_generator_time_value = Math::fposmod(texture_generator_time_value, duration);
			texture_generator_preview_frame = -1;
		}
		if (texture_generator_time) {
			texture_generator_time->set_value_no_signal(MIN(texture_generator_time_value, texture_generator_time->get_max()));
		}
		if (texture_generator_time_label) {
			texture_generator_time_label->set_text(vformat("%s s", String::num(texture_generator_time_value, 2)));
		}
		if (is_sheet) {
			const int current_frame = CLAMP(int(Math::floor(texture_generator_time_value * generated_texture_fps)), 0, generated_texture_frame_columns * generated_texture_frame_columns - 1);
			if (current_frame != texture_generator_preview_frame) {
				_update_texture_preview();
			}
		} else {
			texture_generator_playback_accumulator += delta;
			const double refresh_interval = 1.0 / 12.0;
			if (texture_generator_playback_accumulator >= refresh_interval) {
				texture_generator_playback_accumulator = Math::fmod(texture_generator_playback_accumulator, refresh_interval);
				// Playback changes only the live preview. Keep an exported PNG available
				// for Apply to Emitter until the user changes its generator parameters
				// or manually scrubs to a different sample.
				_regenerate_texture(true);
			}
		}
	}
	if (p_what == NOTIFICATION_PROCESS && screen && screen->is_visible_in_tree() && preview_playing && preview_effect) {
		const double delta = get_process_delta_time();
		preview_time += delta * preview_speed;
		if (preview_time >= preview_duration) {
			if (preview_loop) {
				preview_time = Math::fmod(preview_time, preview_duration);
				restart_particle_nodes(preview_effect);
			} else {
				preview_time = preview_duration;
				_set_preview_playing(false);
			}
		}
		if (timeline) {
			timeline->set_value_no_signal(preview_time);
		}
		// Keep the slider in sync every frame, but avoid invalidating the transport
		// row and its parent split containers whenever the formatted time changes.
		preview_time_label_update_accumulator += delta;
		if (preview_time_label_update_accumulator >= 0.1 || !preview_playing) {
			preview_time_label_update_accumulator = Math::fmod(preview_time_label_update_accumulator, 0.1);
			_update_time_label();
		}
	}
	if (p_what == NOTIFICATION_PREDELETE && preview_effect) {
		preview_effect->queue_free();
		preview_effect = nullptr;
	}
}

void FPSStudioParticleEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_emitter_name_changed"), &FPSStudioParticleEditorPlugin::_on_emitter_name_changed);
	ClassDB::bind_method(D_METHOD("_on_effect_name_changed"), &FPSStudioParticleEditorPlugin::_on_effect_name_changed);
	ClassDB::bind_method(D_METHOD("_on_emitter_hierarchy_history_changed", "preferred_emitter"), &FPSStudioParticleEditorPlugin::_on_emitter_hierarchy_history_changed);
}

void FPSStudioParticleEditorPlugin::make_visible(bool p_visible) {
	if (screen) {
		screen->set_visible(p_visible);
	}
	if (p_visible) {
		_update_document_ui();
		if (_get_effect_root()) {
			_rebuild_preview();
		}
	}
}

void FPSStudioParticleEditorPlugin::edit(Object *p_object) {
	// Level-scene selection does not implicitly replace the independent effect document.
}

bool FPSStudioParticleEditorPlugin::handles(Object *p_object) const {
	return false;
}

void FPSStudioParticleEditorPlugin::edited_scene_changed() {
	_update_document_ui();
}

void FPSStudioParticleEditorPlugin::set_window_layout(Ref<ConfigFile> p_layout) {
	if (p_layout.is_null() || !p_layout->has_section("ParticleWorkspace")) {
		return;
	}
	if (!p_layout->has_section_key("ParticleWorkspace", "main_split_ratio") ||
			!p_layout->has_section_key("ParticleWorkspace", "properties_split_ratio") ||
			!p_layout->has_section_key("ParticleWorkspace", "logic_split_ratio")) {
		return;
	}
	panel_layout_main_ratio = p_layout->get_value("ParticleWorkspace", "main_split_ratio", 0.0);
	panel_layout_properties_ratio = p_layout->get_value("ParticleWorkspace", "properties_split_ratio", 0.0);
	panel_layout_logic_ratio = p_layout->get_value("ParticleWorkspace", "logic_split_ratio", 0.0);
	panel_layout_pending = true;
	callable_mp(this, &FPSStudioParticleEditorPlugin::_restore_panel_layout).call_deferred();
}

void FPSStudioParticleEditorPlugin::get_window_layout(Ref<ConfigFile> p_layout) {
	if (p_layout.is_null() || !main_panel_split || !preview_properties_split || !preview_logic_split) {
		return;
	}
	const real_t main_extent = main_panel_split->get_size().width;
	const real_t properties_extent = preview_properties_split->get_size().width;
	const real_t logic_extent = preview_logic_split->get_size().height;
	if (main_extent <= 0.0 || properties_extent <= 0.0 || logic_extent <= 0.0) {
		return;
	}
	p_layout->set_value("ParticleWorkspace", "main_split_ratio", main_panel_split->get_split_offset() / main_extent);
	p_layout->set_value("ParticleWorkspace", "properties_split_ratio", preview_properties_split->get_split_offset() / properties_extent);
	p_layout->set_value("ParticleWorkspace", "logic_split_ratio", preview_logic_split->get_split_offset() / logic_extent);
}

void FPSStudioParticleEditorPlugin::_restore_panel_layout() {
	if (!panel_layout_pending || !main_panel_split || !preview_properties_split || !preview_logic_split) {
		return;
	}
	const real_t main_extent = main_panel_split->get_size().width;
	const real_t properties_extent = preview_properties_split->get_size().width;
	const real_t logic_extent = preview_logic_split->get_size().height;
	if (main_extent <= 0.0 || properties_extent <= 0.0 || logic_extent <= 0.0) {
		return;
	}
	main_panel_split->set_split_offset(Math::round(panel_layout_main_ratio * main_extent));
	preview_properties_split->set_split_offset(Math::round(panel_layout_properties_ratio * properties_extent));
	preview_logic_split->set_split_offset(Math::round(panel_layout_logic_ratio * logic_extent));
	panel_layout_pending = false;
}

void FPSStudioParticleEditorPlugin::_on_panel_split_drag_ended() {
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
}

const Ref<Texture2D> FPSStudioParticleEditorPlugin::get_plugin_icon() const {
	return EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("GPUParticles3D"), EditorStringName(EditorIcons));
}

FPSStudioParticleEditorPlugin::FPSStudioParticleEditorPlugin() {
	set_process(true);
	preview_quad = create_particle_quad();
	_load_recent_effects();

	VBoxContainer *workspace = memnew(VBoxContainer);
	workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	screen = workspace;
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(workspace);
	workspace->hide();

	HBoxContainer *document_toolbar = memnew(HBoxContainer);
	document_toolbar->set_custom_minimum_size(Size2(0, 34) * EDSCALE);
	document_toolbar->connect("resized", callable_mp(this, &FPSStudioParticleEditorPlugin::_update_document_toolbar_layout));
	workspace->add_child(document_toolbar);
	document_title = memnew(Label);
	document_title->add_theme_font_size_override(SceneStringName(font_size), 16 * EDSCALE);
	document_title->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	document_title->set_clip_text(true);
	document_title->set_custom_minimum_size(Size2(180, 0) * EDSCALE);
	document_title->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	document_title->connect(SceneStringName(gui_input), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_document_title_gui_input));
	document_toolbar->add_child(document_title);
	document_path_label = memnew(Label);
	document_path_label->set_clip_text(true);
	document_path_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	document_toolbar->add_child(document_path_label);
	document_status_label = memnew(Label);
	document_toolbar->add_child(document_status_label);
	save_effect_button = memnew(Button);
	save_effect_button->set_text(TTR("Save"));
	save_effect_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_save_scene_pressed));
	document_toolbar->add_child(save_effect_button);
	add_to_level_button = memnew(Button);
	add_to_level_button->set_text(TTR("Add to Level"));
	add_to_level_button->set_tooltip_text(TTR("Create an instance of the complete particle effect in the open Level scene."));
	add_to_level_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_add_effect_to_level));
	document_toolbar->add_child(add_to_level_button);

	HSplitContainer *main_split = memnew(HSplitContainer);
	main_panel_split = main_split;
	main_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_split->connect("drag_ended", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_panel_split_drag_ended));
	workspace->add_child(main_split);

	VBoxContainer *left_panel = memnew(VBoxContainer);
	left_panel->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
	main_split->add_child(left_panel);
	HBoxContainer *effects_header = memnew(HBoxContainer);
	left_panel->add_child(effects_header);
	Label *effects_title = memnew(Label);
	effects_title->set_text(TTR("Effects"));
	effects_title->add_theme_font_size_override(SceneStringName(font_size), 15 * EDSCALE);
	effects_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	effects_header->add_child(effects_title);
	new_effect_menu = memnew(MenuButton);
	new_effect_menu->set_text(TTR("New"));
	new_effect_menu->get_popup()->add_item(TTR("New GPU Effect"), PARTICLE_TEMPLATE_GPU);
	new_effect_menu->get_popup()->add_item(TTR("New CPU Effect"), PARTICLE_TEMPLATE_CPU);
	new_effect_menu->get_popup()->add_separator(TTR("Templates"));
	new_effect_menu->get_popup()->add_item(TTR("Burst"), PARTICLE_TEMPLATE_BURST);
	new_effect_menu->get_popup()->add_item(TTR("Fountain"), PARTICLE_TEMPLATE_FOUNTAIN);
	new_effect_menu->get_popup()->add_item(TTR("Smoke"), PARTICLE_TEMPLATE_SMOKE);
	new_effect_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_new_effect_menu_pressed));
	effects_header->add_child(new_effect_menu);
	effect_filter = memnew(LineEdit);
	effect_filter->set_placeholder(TTR("Search effects..."));
	effect_filter->connect(SceneStringName(text_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_effect_filter_changed));
	left_panel->add_child(effect_filter);
	HBoxContainer *effect_file_actions = memnew(HBoxContainer);
	left_panel->add_child(effect_file_actions);
	open_effect_button = memnew(Button);
	open_effect_button->set_text(TTR("Open..."));
	open_effect_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	effect_file_actions->add_child(open_effect_button);
	effects_tree = memnew(Tree);
	effects_tree->set_hide_root(true);
	effects_tree->set_custom_minimum_size(Size2(0, 150) * EDSCALE);
	effects_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	effects_tree->connect(SNAME("item_activated"), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_effect_activated));
	left_panel->add_child(effects_tree);

	HBoxContainer *emitter_header = memnew(HBoxContainer);
	left_panel->add_child(emitter_header);
	Label *emitter_title = memnew(Label);
	emitter_title->set_text(TTR("Emitters"));
	emitter_title->add_theme_font_size_override(SceneStringName(font_size), 15 * EDSCALE);
	emitter_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_header->add_child(emitter_title);
	add_emitter_menu = memnew(MenuButton);
	add_emitter_menu->set_text(TTR("Add"));
	add_emitter_menu->get_popup()->add_item(TTR("GPU Emitter"), PARTICLE_TEMPLATE_GPU);
	add_emitter_menu->get_popup()->add_item(TTR("CPU Emitter"), PARTICLE_TEMPLATE_CPU);
	add_emitter_menu->get_popup()->add_separator(TTR("Presets"));
	add_emitter_menu->get_popup()->add_item(TTR("Burst"), PARTICLE_TEMPLATE_BURST);
	add_emitter_menu->get_popup()->add_item(TTR("Fountain"), PARTICLE_TEMPLATE_FOUNTAIN);
	add_emitter_menu->get_popup()->add_item(TTR("Smoke"), PARTICLE_TEMPLATE_SMOKE);
	add_emitter_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_add_emitter_menu_pressed));
	emitter_header->add_child(add_emitter_menu);
	Button *remove_emitter_button = memnew(Button);
	remove_emitter_button->set_text(TTR("Remove"));
	remove_emitter_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_remove_selected_emitter));
	emitter_header->add_child(remove_emitter_button);
	emitter_tree = memnew(Tree);
	emitter_tree->set_hide_root(true);
	emitter_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_tree->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_selected));
	emitter_tree->connect("item_edited", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_renamed));
	left_panel->add_child(emitter_tree);

	HSplitContainer *center_right_split = memnew(HSplitContainer);
	preview_properties_split = center_right_split;
	center_right_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	center_right_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_right_split->connect("drag_ended", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_panel_split_drag_ended));
	main_split->add_child(center_right_split);

	VSplitContainer *center_split = memnew(VSplitContainer);
	preview_logic_split = center_split;
	center_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	center_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_split->connect("drag_ended", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_panel_split_drag_ended));
	center_right_split->add_child(center_split);

	VBoxContainer *preview_panel = memnew(VBoxContainer);
	preview_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	preview_panel->set_stretch_ratio(1.4);
	center_split->add_child(preview_panel);
	HBoxContainer *preview_toolbar = memnew(HBoxContainer);
	preview_panel->add_child(preview_toolbar);
	selection_label = memnew(Label);
	selection_label->set_text(TTR("Create or open a particle effect to start."));
	selection_label->set_clip_text(true);
	selection_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_toolbar->add_child(selection_label);
	Button *reset_view_button = memnew(Button);
	reset_view_button->set_text(TTR("Reset View"));
	reset_view_button->set_tooltip_text(TTR("Frame the full particle effect in the preview."));
	reset_view_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_reset_view_pressed));
	preview_toolbar->add_child(reset_view_button);
	CheckButton *grid_toggle = memnew(CheckButton);
	grid_toggle->set_text(TTR("Grid"));
	grid_toggle->set_pressed(true);
	grid_toggle->set_tooltip_text(TTR("Show or hide the preview grid."));
	grid_toggle->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_grid_toggled));
	preview_toolbar->add_child(grid_toggle);
	Button *texture_generator_button = memnew(Button);
	texture_generator_button->set_text(TTR("Texture Generator"));
	texture_generator_button->set_tooltip_text(TTR("Create and export a procedural VFX texture for a particle emitter."));
	texture_generator_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_pressed));
	preview_toolbar->add_child(texture_generator_button);

	preview_container = memnew(SubViewportContainer);
	preview_container->set_stretch(true);
	preview_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	preview_container->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	preview_container->set_tooltip_text(TTR("Drag with the left or right mouse button to orbit, middle mouse button to pan, and use the wheel to zoom."));
	preview_container->connect(SceneStringName(gui_input), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_preview_gui_input));
	preview_panel->add_child(preview_container);
	preview_viewport = memnew(SubViewport);
	preview_viewport->set_size(Vector2i(1280, 720));
	preview_viewport->set_update_mode(SubViewport::UPDATE_WHEN_VISIBLE);
	preview_viewport->set_use_own_world_3d(true);
	preview_container->add_child(preview_viewport);
	preview_root = memnew(Node3D);
	preview_root->set_name("ParticlePreviewWorld");
	preview_viewport->add_child(preview_root);

	Ref<Environment> preview_environment;
	preview_environment.instantiate();
	preview_environment->set_background(Environment::BG_COLOR);
	preview_environment->set_bg_color(Color(0.035, 0.046, 0.065));
	preview_environment->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	preview_environment->set_ambient_light_color(Color(0.48, 0.53, 0.64));
	preview_environment->set_ambient_light_energy(0.72);
	preview_environment->set_tonemapper(Environment::TONE_MAPPER_FILMIC);
	WorldEnvironment *world_environment = memnew(WorldEnvironment);
	world_environment->set_environment(preview_environment);
	preview_root->add_child(world_environment);
	DirectionalLight3D *key_light = memnew(DirectionalLight3D);
	key_light->set_rotation(Vector3(Math::deg_to_rad(-38.0), Math::deg_to_rad(-26.0), 0.0));
	key_light->set_param(Light3D::PARAM_ENERGY, 1.45);
	key_light->set_color(Color(0.9, 0.94, 1.0));
	preview_root->add_child(key_light);
	DirectionalLight3D *fill_light = memnew(DirectionalLight3D);
	fill_light->set_rotation(Vector3(Math::deg_to_rad(-18.0), Math::deg_to_rad(148.0), Math::deg_to_rad(12.0)));
	fill_light->set_param(Light3D::PARAM_ENERGY, 0.48);
	fill_light->set_color(Color(0.34, 0.56, 1.0));
	preview_root->add_child(fill_light);
	DirectionalLight3D *rim_light = memnew(DirectionalLight3D);
	rim_light->set_rotation(Vector3(Math::deg_to_rad(-16.0), Math::deg_to_rad(-145.0), Math::deg_to_rad(-18.0)));
	rim_light->set_param(Light3D::PARAM_ENERGY, 0.65);
	rim_light->set_color(Color(1.0, 0.66, 0.42));
	preview_root->add_child(rim_light);
	preview_grid = create_particle_preview_grid();
	preview_root->add_child(preview_grid);
	preview_camera = memnew(Camera3D);
	preview_camera->set_perspective(55.0, 0.03, 1000.0);
	preview_root->add_child(preview_camera);
	_update_preview_camera();
	preview_camera->set_current(true);

	HBoxContainer *transport = memnew(HBoxContainer);
	transport->set_custom_minimum_size(Size2(0, 34) * EDSCALE);
	preview_panel->add_child(transport);
	play_button = memnew(Button);
	play_button->set_toggle_mode(true);
	play_button->set_pressed(true);
	play_button->set_text(TTR("Pause"));
	play_button->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_play_toggled));
	transport->add_child(play_button);
	restart_button = memnew(Button);
	restart_button->set_text(TTR("Restart"));
	restart_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_restart_pressed));
	transport->add_child(restart_button);
	timeline = memnew(HSlider);
	timeline->set_min(0.0);
	timeline->set_max(preview_duration);
	timeline->set_step(0.01);
	timeline->set_value(0.0);
	timeline->set_editable(false);
	timeline->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	timeline->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_changed));
	transport->add_child(timeline);
	Control *time_label_container = memnew(Control);
	time_label_container->set_custom_minimum_size(Size2(150, 0) * EDSCALE);
	time_label_container->set_clip_contents(true);
	transport->add_child(time_label_container);
	time_label = memnew(Label);
	time_label->set_text("0.00 / 5.00 s");
	time_label->set_custom_minimum_size(Size2(150, 0) * EDSCALE);
	time_label->set_clip_text(true);
	time_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	time_label_container->add_child(time_label);
	time_label->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	Label *speed_label = memnew(Label);
	speed_label->set_text(TTR("Speed"));
	transport->add_child(speed_label);
	speed_spin = memnew(SpinBox);
	speed_spin->set_min(0.05);
	speed_spin->set_max(4.0);
	speed_spin->set_step(0.05);
	speed_spin->set_value(1.0);
	speed_spin->set_suffix("x");
	speed_spin->set_custom_minimum_size(Size2(84, 0) * EDSCALE);
	speed_spin->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_speed_changed));
	transport->add_child(speed_spin);
	CheckButton *loop_toggle = memnew(CheckButton);
	loop_toggle->set_text(TTR("Loop"));
	loop_toggle->set_pressed(preview_loop);
	loop_toggle->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_loop_toggled));
	transport->add_child(loop_toggle);

	PanelContainer *logic_panel = memnew(PanelContainer);
	logic_panel->set_custom_minimum_size(Size2(0, 108) * EDSCALE);
	logic_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	logic_panel->set_stretch_ratio(1.0);
	center_split->add_child(logic_panel);
	VBoxContainer *logic_content = memnew(VBoxContainer);
	particle_logic_content = logic_content;
	logic_panel->add_child(logic_content);
	HBoxContainer *logic_header = memnew(HBoxContainer);
	logic_content->add_child(logic_header);
	Label *logic_title = memnew(Label);
	logic_title->set_text(TTR("Particle Logic"));
	logic_title->add_theme_font_size_override(SceneStringName(font_size), 15 * EDSCALE);
	logic_header->add_child(logic_title);
	particle_logic_status = memnew(Label);
	particle_logic_status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	particle_logic_status->set_clip_text(true);
	logic_header->add_child(particle_logic_status);
	particle_graph_button = memnew(Button);
	particle_graph_button->set_text(TTR("Create Node Graph"));
	particle_graph_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_particle_graph_button_pressed));
	logic_header->add_child(particle_graph_button);
	particle_logic_hint = memnew(Label);
	particle_logic_hint->set_text(TTR("ParticleProcessMaterial and VisualShader are native Godot resources. Editing the effect refreshes the complete preview."));
	particle_logic_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	logic_content->add_child(particle_logic_hint);

	TabContainer *properties_tabs = memnew(TabContainer);
	properties_tabs->set_custom_minimum_size(Size2(340, 0) * EDSCALE);
	properties_tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	properties_tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_right_split->add_child(properties_tabs);

	ScrollContainer *emitter_scroll = memnew(ScrollContainer);
	emitter_scroll->set_name(TTR("Emitter"));
	emitter_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	properties_tabs->add_child(emitter_scroll);
	emitter_inspector = EditorInspector::create_default_inspector();
	emitter_inspector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_inspector->connect("property_edited", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_property_edited));
	emitter_scroll->add_child(emitter_inspector);

	ScrollContainer *process_scroll = memnew(ScrollContainer);
	process_scroll->set_name(TTR("Process"));
	process_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	process_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	properties_tabs->add_child(process_scroll);
	VBoxContainer *process_content = memnew(VBoxContainer);
	process_content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	process_content->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	process_scroll->add_child(process_content);
	process_material_page = process_content;
	HBoxContainer *process_header = memnew(HBoxContainer);
	process_content->add_child(process_header);
	Label *process_mode_label = memnew(Label);
	process_mode_label->set_text(TTR("Process Material"));
	process_mode_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	process_header->add_child(process_mode_label);

	VBoxContainer *curve_panel_vbox = memnew(VBoxContainer);
	curve_panel_vbox->set_custom_minimum_size(Size2(0, 0) * EDSCALE);
	process_content->add_child(curve_panel_vbox);
	curve_panel = curve_panel_vbox;
	Label *curve_section_title = memnew(Label);
	curve_section_title->set_text(TTR("Curves over Life"));
	curve_section_title->add_theme_font_size_override(SceneStringName(font_size), 14 * EDSCALE);
	curve_panel_vbox->add_child(curve_section_title);
	alpha_curve_card = memnew(VBoxContainer);
	curve_panel_vbox->add_child(alpha_curve_card);
	Label *alpha_title = memnew(Label);
	alpha_title->set_text(TTR("Alpha over Life"));
	alpha_curve_card->add_child(alpha_title);
	alpha_curve_info = memnew(Label);
	alpha_curve_info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	alpha_curve_card->add_child(alpha_curve_info);
	HBoxContainer *alpha_curve_actions = memnew(HBoxContainer);
	alpha_curve_card->add_child(alpha_curve_actions);
	alpha_curve_add_button = memnew(Button);
	alpha_curve_add_button->set_text(TTR("Add Fade Out Curve"));
	alpha_curve_add_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	alpha_curve_add_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_add_alpha_curve_pressed));
	alpha_curve_actions->add_child(alpha_curve_add_button);
	alpha_curve_presets_button = memnew(MenuButton);
	alpha_curve_presets_button->set_text(TTR("Presets"));
	alpha_curve_presets_button->set_tooltip_text(TTR("Apply a ready-made Alpha over Life curve. If no curve exists yet, this creates one."));
	alpha_curve_presets_button->get_popup()->add_item(TTR("Fade Out (Linear)"), PARTICLE_ALPHA_CURVE_FADE_OUT_LINEAR);
	alpha_curve_presets_button->get_popup()->add_item(TTR("Fade Out (Smooth)"), PARTICLE_ALPHA_CURVE_FADE_OUT_SMOOTH);
	alpha_curve_presets_button->get_popup()->add_item(TTR("Fade In"), PARTICLE_ALPHA_CURVE_FADE_IN);
	alpha_curve_presets_button->get_popup()->add_item(TTR("Fade In and Out"), PARTICLE_ALPHA_CURVE_FADE_IN_OUT);
	alpha_curve_presets_button->get_popup()->add_item(TTR("Hold then Fade"), PARTICLE_ALPHA_CURVE_HOLD_THEN_FADE);
	alpha_curve_presets_button->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_alpha_curve_preset_selected));
	alpha_curve_actions->add_child(alpha_curve_presets_button);
	alpha_curve_editor = memnew(CurveEditor);
	alpha_curve_editor->set_custom_minimum_size(Size2(0, 156) * EDSCALE);
	alpha_curve_card->add_child(alpha_curve_editor);
	scale_curve_card = memnew(VBoxContainer);
	curve_panel_vbox->add_child(scale_curve_card);
	Label *scale_title = memnew(Label);
	scale_title->set_text(TTR("Scale over Life"));
	scale_curve_card->add_child(scale_title);
	scale_curve_info = memnew(Label);
	scale_curve_info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	scale_curve_card->add_child(scale_curve_info);
	HBoxContainer *scale_curve_actions = memnew(HBoxContainer);
	scale_curve_card->add_child(scale_curve_actions);
	scale_curve_add_button = memnew(Button);
	scale_curve_add_button->set_text(TTR("Add Growth Curve"));
	scale_curve_add_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scale_curve_add_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_add_scale_curve_pressed));
	scale_curve_actions->add_child(scale_curve_add_button);
	scale_curve_presets_button = memnew(MenuButton);
	scale_curve_presets_button->set_text(TTR("Presets"));
	scale_curve_presets_button->set_tooltip_text(TTR("Apply a ready-made Scale over Life curve. If no curve exists yet, this creates one."));
	scale_curve_presets_button->get_popup()->add_item(TTR("Grow"), PARTICLE_SCALE_CURVE_GROW);
	scale_curve_presets_button->get_popup()->add_item(TTR("Shrink"), PARTICLE_SCALE_CURVE_SHRINK);
	scale_curve_presets_button->get_popup()->add_item(TTR("Grow and Shrink"), PARTICLE_SCALE_CURVE_GROW_AND_SHRINK);
	scale_curve_presets_button->get_popup()->add_item(TTR("Hold then Grow"), PARTICLE_SCALE_CURVE_HOLD_THEN_GROW);
	scale_curve_presets_button->get_popup()->add_item(TTR("Steady"), PARTICLE_SCALE_CURVE_HOLD);
	scale_curve_presets_button->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_scale_curve_preset_selected));
	scale_curve_actions->add_child(scale_curve_presets_button);
	scale_curve_editor = memnew(CurveEditor);
	scale_curve_editor->set_custom_minimum_size(Size2(0, 156) * EDSCALE);
	scale_curve_card->add_child(scale_curve_editor);
	curve_panel->hide();

	process_material_inspector = EditorInspector::create_default_inspector();
	process_material_inspector->set_custom_minimum_size(Size2(0, 250) * EDSCALE);
	process_material_inspector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	process_material_inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	process_material_inspector->connect("property_edited", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_property_edited));
	process_content->add_child(process_material_inspector);

	ScrollContainer *renderer_scroll = memnew(ScrollContainer);
	renderer_scroll->set_name(TTR("Renderer"));
	renderer_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	renderer_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	properties_tabs->add_child(renderer_scroll);
	renderer_inspector = EditorInspector::create_default_inspector();
	renderer_inspector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	renderer_inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	renderer_inspector->connect("property_edited", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_property_edited));
	renderer_scroll->add_child(renderer_inspector);

	particle_graph_confirmation = memnew(ConfirmationDialog);
	particle_graph_confirmation->set_title(TTR("Create Particle Node Graph"));
	particle_graph_confirmation->set_ok_button_text(TTR("Replace Material"));
	particle_graph_confirmation->set_cancel_button_text(TTR("Cancel"));
	particle_graph_confirmation->connect(SceneStringName(confirmed), callable_mp(this, &FPSStudioParticleEditorPlugin::_create_particle_graph));
	add_child(particle_graph_confirmation);

	rename_effect_dialog = memnew(AcceptDialog);
	rename_effect_dialog->set_title(TTR("Rename Particle Effect"));
	rename_effect_dialog->set_ok_button_text(TTR("Rename"));
	rename_effect_dialog->set_hide_on_ok(true);
	rename_effect_dialog->set_min_size(Size2(380, 120) * EDSCALE);
	rename_effect_dialog->connect(SceneStringName(confirmed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_rename_effect_confirmed));
	add_child(rename_effect_dialog);
	rename_effect_line_edit = memnew(LineEdit);
	rename_effect_line_edit->set_placeholder(TTR("Effect name"));
	rename_effect_line_edit->set_custom_minimum_size(Size2(340, 0) * EDSCALE);
	rename_effect_dialog->add_child(rename_effect_line_edit);
	rename_effect_dialog->register_text_enter(rename_effect_line_edit);

	texture_generator_dialog = memnew(AcceptDialog);
	texture_generator_dialog->set_title(TTR("VFX Texture Generator"));
	texture_generator_dialog->set_ok_button_text(TTR("Close"));
	texture_generator_dialog->set_hide_on_ok(true);
	texture_generator_dialog->set_min_size(Size2(820, 590) * EDSCALE);
	texture_generator_dialog->connect("custom_action", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_action));
	add_child(texture_generator_dialog);
	HBoxContainer *texture_generator_layout = memnew(HBoxContainer);
	texture_generator_layout->set_custom_minimum_size(Size2(790, 510) * EDSCALE);
	texture_generator_dialog->add_child(texture_generator_layout);
	PanelContainer *texture_preview_panel = memnew(PanelContainer);
	texture_preview_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_preview_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_layout->add_child(texture_preview_panel);
	VBoxContainer *texture_preview_content = memnew(VBoxContainer);
	texture_preview_panel->add_child(texture_preview_content);
	texture_generator_preview = memnew(TextureRect);
	texture_generator_preview->set_custom_minimum_size(Size2(470, 440) * EDSCALE);
	texture_generator_preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	texture_generator_preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	texture_generator_preview->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	texture_preview_content->add_child(texture_generator_preview);
	HBoxContainer *texture_preview_transport = memnew(HBoxContainer);
	texture_preview_content->add_child(texture_preview_transport);
	texture_generator_animate = memnew(CheckButton);
	texture_generator_animate->set_text(TTR("Animate"));
	texture_generator_animate->set_pressed(true);
	texture_generator_animate->set_tooltip_text(TTR("Play the generated texture or sprite sheet in the preview."));
	texture_preview_transport->add_child(texture_generator_animate);
	Label *texture_generator_time_title = memnew(Label);
	texture_generator_time_title->set_text(TTR("Time"));
	texture_preview_transport->add_child(texture_generator_time_title);
	texture_generator_time = memnew(HSlider);
	texture_generator_time->set_min(0.0);
	texture_generator_time->set_max(1.0);
	texture_generator_time->set_step(0.01);
	texture_generator_time->set_value(0.0);
	texture_generator_time->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_time->set_tooltip_text(TTR("Scrub the animation or choose a time sample."));
	texture_preview_transport->add_child(texture_generator_time);
	texture_generator_time_label = memnew(Label);
	texture_generator_time_label->set_text("0.00 s");
	texture_generator_time_label->set_custom_minimum_size(Size2(48, 0) * EDSCALE);
	texture_preview_transport->add_child(texture_generator_time_label);
	VBoxContainer *texture_controls = memnew(VBoxContainer);
	texture_controls->set_custom_minimum_size(Size2(285, 0) * EDSCALE);
	texture_controls->set_h_size_flags(Control::SIZE_SHRINK_END);
	texture_generator_layout->add_child(texture_controls);
	Label *texture_generator_intro = memnew(Label);
	texture_generator_intro->set_text(TTR("Build a particle texture or animated sprite sheet inside the editor, preview it, then export it as a project PNG."));
	texture_generator_intro->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	texture_controls->add_child(texture_generator_intro);
	Label *texture_preset_label = memnew(Label);
	texture_preset_label->set_text(TTR("Texture type"));
	texture_controls->add_child(texture_preset_label);
	texture_generator_preset = memnew(OptionButton);
	texture_generator_preset->add_item(TTR("Fire"), VFX_TEXTURE_FIRE);
	texture_generator_preset->add_item(TTR("Smoke"), VFX_TEXTURE_SMOKE);
	texture_generator_preset->add_item(TTR("Explosion"), VFX_TEXTURE_EXPLOSION);
	texture_generator_preset->add_item(TTR("Spark streak"), VFX_TEXTURE_SPARK);
	texture_generator_preset->select(VFX_TEXTURE_FIRE);
	texture_controls->add_child(texture_generator_preset);
	Label *texture_resolution_label = memnew(Label);
	texture_resolution_label->set_text(TTR("Resolution"));
	texture_controls->add_child(texture_resolution_label);
	texture_generator_resolution = memnew(OptionButton);
	texture_generator_resolution->add_item("128 x 128");
	texture_generator_resolution->add_item("256 x 256");
	texture_generator_resolution->add_item("512 x 512");
	texture_generator_resolution->add_item("1024 x 1024");
	texture_generator_resolution->select(1);
	texture_controls->add_child(texture_generator_resolution);
	Label *texture_output_label = memnew(Label);
	texture_output_label->set_text(TTR("Output layout"));
	texture_controls->add_child(texture_output_label);
	texture_generator_output = memnew(OptionButton);
	texture_generator_output->add_item(TTR("Single texture"), VFX_TEXTURE_OUTPUT_STILL);
	texture_generator_output->add_item(TTR("Sprite sheet - 4 x 4"), VFX_TEXTURE_OUTPUT_SHEET_4X4);
	texture_generator_output->add_item(TTR("Sprite sheet - 8 x 8"), VFX_TEXTURE_OUTPUT_SHEET_8X8);
	texture_generator_output->select(0);
	texture_controls->add_child(texture_generator_output);
	texture_generator_fps_label = memnew(Label);
	texture_generator_fps_label->set_text(TTR("Animation frame rate"));
	texture_generator_fps_label->hide();
	texture_controls->add_child(texture_generator_fps_label);
	texture_generator_fps = memnew(SpinBox);
	texture_generator_fps->set_min(1);
	texture_generator_fps->set_max(60);
	texture_generator_fps->set_step(1);
	texture_generator_fps->set_value(12);
	texture_generator_fps->hide();
	texture_controls->add_child(texture_generator_fps);
	Label *texture_seed_label = memnew(Label);
	texture_seed_label->set_text(TTR("Seed"));
	texture_controls->add_child(texture_seed_label);
	texture_generator_seed = memnew(SpinBox);
	texture_generator_seed->set_min(0);
	texture_generator_seed->set_max(99999);
	texture_generator_seed->set_step(1);
	texture_generator_seed->set_value(7);
	texture_controls->add_child(texture_generator_seed);
	Label *texture_color_a_label = memnew(Label);
	texture_color_a_label->set_text(TTR("Base color"));
	texture_controls->add_child(texture_color_a_label);
	texture_generator_color_a = memnew(ColorPickerButton(TTR("Choose base color")));
	texture_generator_color_a->set_pick_color(Color(0.95, 0.12, 0.025, 1.0));
	texture_controls->add_child(texture_generator_color_a);
	Label *texture_color_b_label = memnew(Label);
	texture_color_b_label->set_text(TTR("Highlight color"));
	texture_controls->add_child(texture_color_b_label);
	texture_generator_color_b = memnew(ColorPickerButton(TTR("Choose highlight color")));
	texture_generator_color_b->set_pick_color(Color(1.0, 0.82, 0.18, 1.0));
	texture_controls->add_child(texture_generator_color_b);
	Label *texture_generator_hint = memnew(Label);
	texture_generator_hint->set_text(TTR("The effect scene keeps a reference to the exported PNG. The seed recreates the same procedural shape."));
	texture_generator_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	texture_controls->add_child(texture_generator_hint);
	texture_generator_status = memnew(Label);
	texture_generator_status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	texture_controls->add_child(texture_generator_status);
	texture_generator_apply_button = texture_generator_dialog->add_button(TTR("Apply to Emitter"), true, "apply");
	texture_generator_apply_button->set_disabled(true);
	texture_generator_dialog->add_button(TTR("Export PNG"), false, "export");
	texture_generator_preset->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_preset_selected));
	texture_generator_resolution->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_resolution_selected));
	texture_generator_output->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_output_selected));
	texture_generator_seed->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_seed_changed));
	texture_generator_fps->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_fps_changed));
	texture_generator_animate->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_animate_toggled));
	texture_generator_time->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_time_changed));
	texture_generator_color_a->connect("color_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_color_changed));
	texture_generator_color_b->connect("color_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_color_changed));
	_update_texture_time_controls();
	_regenerate_texture();
	texture_save_dialog = memnew(EditorFileDialog);
	texture_save_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	texture_save_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	texture_save_dialog->set_title(TTR("Export VFX Texture"));
	texture_save_dialog->add_filter("*.png", TTR("PNG Image"));
	texture_save_dialog->set_current_dir("res://");
	add_child(texture_save_dialog);
	texture_save_dialog->connect(SceneStringName(visibility_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_save_dialog_visibility_changed));
	texture_save_dialog->connect("canceled", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_save_dialog_cancelled));
	texture_save_dialog->connect("file_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_save_path_selected));

	replace_document_confirmation = memnew(ConfirmationDialog);
	replace_document_confirmation->set_title(TTR("Discard Unsaved Particle Changes"));
	replace_document_confirmation->set_ok_button_text(TTR("Discard"));
	replace_document_confirmation->set_cancel_button_text(TTR("Cancel"));
	replace_document_confirmation->connect(SceneStringName(confirmed), callable_mp(this, &FPSStudioParticleEditorPlugin::_confirm_replace_document));
	add_child(replace_document_confirmation);

	load_scene_dialog = memnew(EditorFileDialog);
	load_scene_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	load_scene_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	load_scene_dialog->set_title(TTR("Open Particle Effect"));
	load_scene_dialog->add_filter("*.tscn,*.scn", TTR("Godot Scenes"));
	add_child(load_scene_dialog);
	load_scene_dialog->connect("file_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_load_particle_scene));
	open_effect_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_open_effect_pressed));

	save_scene_dialog = memnew(EditorFileDialog);
	save_scene_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	save_scene_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	save_scene_dialog->set_title(TTR("Save Particle Effect"));
	save_scene_dialog->add_filter("*.tscn", TTR("Godot Scene"));
	add_child(save_scene_dialog);
	save_scene_dialog->connect("file_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_save_emitter_scene));

	_update_document_ui();
	_update_particle_graph_controls();
}
