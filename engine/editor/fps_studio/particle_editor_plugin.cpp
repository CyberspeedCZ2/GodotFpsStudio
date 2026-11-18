/**************************************************************************/
/*  particle_editor_plugin.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_editor_plugin.h"

#include "particle_event_gizmos.h"
#include "particle_module_stack.h"
#include "particle_presets.h"
#include "particle_timeline.h"
#include "particle_visual_shader_converter.h"
#include "vfx_sound_generator.h"
#include "vfx_texture_generator.h"

#include "core/input/input_event.h"
#include "core/io/config_file.h"
#include "core/io/resource_uid.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/main/canvas_layer.h"
#include "core/io/dir_access.h"
#include "core/version.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "servers/display/display_server.h"
#include "core/string/print_string.h"
#include "editor/editor_data.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_spin_slider.h"
#include "editor/gui/editor_toaster.h"
#include "editor/gui/window_wrapper.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/curve_editor_plugin.h"
#include "editor/scene/material_editor_plugin.h"
#include "core/config/project_settings.h"
#include "editor/file_system/editor_paths.h"
#include "editor/inspector/editor_preview_plugins.h"
#include "editor/inspector/editor_resource_preview.h"
#include "editor/settings/editor_settings.h"
#include "editor/shader/text_shader_editor.h"
#include "editor/themes/editor_scale.h"
#include "modules/visual_shader/editor/visual_shader_editor_plugin.h"
#include "modules/visual_shader/visual_shader.h"
#include "modules/visual_shader/vs_nodes/visual_shader_particle_nodes.h"
#include "scene/3d/camera_3d.h"
#include "core/templates/sort_array.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "scene/3d/physics/character_body_3d.h"
#include "servers/audio/audio_stream.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/camera_shake_3d.h"
#include "scene/3d/radial_damage_3d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/resources/audio_stream_wav.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/gui/box_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/aspect_ratio_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_button.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/foldable_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/rich_text_label.h"
#include "servers/rendering/rendering_server.h"
#include "particle_preview_compare.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/popup.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/inspector/editor_resource_tooltip_plugins.h"
#include "core/io/config_file.h"
#include "core/io/resource_uid.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/main/canvas_layer.h"
#include "scene/resources/3d/primitive_meshes.h"
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
#include "scene/resources/style_box_flat.h"
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
	PARTICLE_TEMPLATE_PRESET_BASE = 100, // + FPSParticlePresets::Preset
	PARTICLE_EVENT_LIGHT = 200,
	PARTICLE_EVENT_SOUND = 201,
	PARTICLE_EVENT_SHAKE = 202,
	PARTICLE_EVENT_DECAL = 203,
	PARTICLE_EVENT_DAMAGE = 204,
};

enum ParticleLayoutPanel {
	PARTICLE_PANEL_LEFT = 0,
	PARTICLE_PANEL_PREVIEW = 1,
	PARTICLE_PANEL_BOTTOM = 2,
	PARTICLE_PANEL_RIGHT = 3,
	PARTICLE_PANEL_LIBRARY = 4,
};

enum ParticleLayoutMenu {
	PARTICLE_LAYOUT_LEFT = PARTICLE_PANEL_LEFT,
	PARTICLE_LAYOUT_PREVIEW = PARTICLE_PANEL_PREVIEW,
	PARTICLE_LAYOUT_BOTTOM = PARTICLE_PANEL_BOTTOM,
	PARTICLE_LAYOUT_RIGHT = PARTICLE_PANEL_RIGHT,
	PARTICLE_LAYOUT_LIBRARY = PARTICLE_PANEL_LIBRARY,
	PARTICLE_LAYOUT_SHOW_TIMELINE = 10,
	PARTICLE_LAYOUT_SHOW_LOGIC = 11,
	PARTICLE_LAYOUT_MAXIMIZE_BOTTOM = 20,
	PARTICLE_LAYOUT_FLOAT_BOTTOM = 21,
	PARTICLE_LAYOUT_RESET = 30,
};

static const int PARTICLE_LIBRARY_PRESET_BASE = 1000;
static const int PARTICLE_LIBRARY_RECENT_BASE = 2000;

static bool library_hover_preview_enabled() {
	return double(EDITOR_GET("fps_studio/particles/library_hover_preview_delay")) > 0.0;
}
static const int PARTICLE_LIBRARY_TOGGLE_PANEL = 3000;
static const int PARTICLE_BOTTOM_TAB_TIMELINE = 0;
static const int PARTICLE_BOTTOM_TAB_LOGIC = 1;

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

static real_t particle_texture_smoothstep(real_t p_edge_a, real_t p_edge_b, real_t p_value) {
	const real_t t = CLAMP((p_value - p_edge_a) / MAX(p_edge_b - p_edge_a, real_t(0.0001)), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

struct VFXTextureRenderJob {
	const VFXTextureGenerator::Settings *settings = nullptr;
	int size = 0;
	uint8_t *buffer = nullptr;
};

static void _vfx_texture_render_row(void *p_userdata, uint32_t p_row) {
	VFXTextureRenderJob *job = static_cast<VFXTextureRenderJob *>(p_userdata);
	VFXTextureGenerator::render_rows_rgba8(*job->settings, job->size, job->buffer, int(p_row), int(p_row) + 1);
}

// Renders one frame of the procedural generator on all worker threads.
static Ref<Image> render_vfx_texture_frame(const VFXTextureGenerator::Settings &p_settings, int p_size) {
	PackedByteArray bytes;
	bytes.resize(p_size * p_size * 4);
	VFXTextureRenderJob job;
	job.settings = &p_settings;
	job.size = p_size;
	job.buffer = bytes.ptrw();
	if (p_size >= 32 && WorkerThreadPool::get_singleton()) {
		const WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_native_group_task(_vfx_texture_render_row, &job, p_size, -1, true, "VFX Texture Generator");
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);
	} else {
		for (int y = 0; y < p_size; y++) {
			_vfx_texture_render_row(&job, y);
		}
	}
	return Image::create_from_data(p_size, p_size, false, Image::FORMAT_RGBA8, bytes);
}

// Generated VFX textures are always used on 3D particles. Writing the 3D
// import settings up front avoids Godot's "used in 3D" detection, which would
// otherwise queue a second reimport while ours is still running.
static void ensure_vfx_texture_import_settings(const String &p_path, bool p_normal_map = false) {
	const String import_path = p_path + ".import";
	if (FileAccess::exists(import_path)) {
		if (p_normal_map) {
			// Keep user tuning, but a normal map must be imported as one.
			Ref<ConfigFile> existing;
			existing.instantiate();
			if (existing->load(import_path) == OK && int(existing->get_value("params", "compress/normal_map", 0)) != 1) {
				existing->set_value("params", "compress/normal_map", 1);
				existing->save(import_path);
			}
		}
		return; // Keep existing (possibly user-tuned) import settings.
	}
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("remap", "importer", "texture");
	config->set_value("remap", "type", "CompressedTexture2D");
	config->set_value("params", "compress/mode", 2); // VRAM Compressed.
	config->set_value("params", "compress/high_quality", true); // BPTC/ASTC keeps smooth alpha gradients.
	config->set_value("params", "mipmaps/generate", true);
	config->set_value("params", "detect_3d/compress_to", 0);
	if (p_normal_map) {
		config->set_value("params", "compress/normal_map", 1); // Enable: RG normal compression, linear.
	}
	config->save(import_path);
}

static void process_particle_texture_frame(const Ref<Image> &p_image, real_t p_alpha_threshold, real_t p_alpha_blur, bool p_toon_enabled, real_t p_toon_dark, real_t p_toon_light, const Color &p_shadows_tint, const Color &p_midtones_tint, const Color &p_highlights_tint, bool p_generate_normal_map, real_t p_normal_height_scale, bool p_show_alpha) {
	if (p_image.is_null()) {
		return;
	}
	const Color white(1.0, 1.0, 1.0, 1.0);
	if (p_alpha_threshold <= 0.0 && p_alpha_blur <= 0.001 && !p_toon_enabled && !p_generate_normal_map && !p_show_alpha && p_shadows_tint == white && p_midtones_tint == white && p_highlights_tint == white) {
		// Nothing to do; this is the common case and keeps the preview fast.
		return;
	}
	const int width = p_image->get_width();
	const int height = p_image->get_height();
	Vector<real_t> alpha;
	alpha.resize(width * height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const real_t input_alpha = p_image->get_pixel(x, y).a;
			alpha.write[y * width + x] = CLAMP((input_alpha - p_alpha_threshold) / MAX(1.0 - p_alpha_threshold, real_t(0.0001)), 0.0, 1.0);
		}
	}
	if (p_alpha_blur > 0.001) {
		Vector<real_t> blurred;
		blurred.resize(width * height);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				real_t sum = 0.0;
				for (int oy = -1; oy <= 1; oy++) {
					const int sample_y = CLAMP(y + oy, 0, height - 1);
					const real_t weight_y = oy == 0 ? 2.0 : 1.0;
					for (int ox = -1; ox <= 1; ox++) {
						const int sample_x = CLAMP(x + ox, 0, width - 1);
						const real_t weight_x = ox == 0 ? 2.0 : 1.0;
						sum += alpha[sample_y * width + sample_x] * weight_x * weight_y;
					}
				}
				blurred.write[y * width + x] = Math::lerp(alpha[y * width + x], real_t(sum / real_t(16.0)), p_alpha_blur);
			}
		}
		alpha = blurred;
	}
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const int index = y * width + x;
			const real_t current_alpha = alpha[index];
			Color color = p_image->get_pixel(x, y);
			color.a = current_alpha;
			if (p_show_alpha) {
				color = Color(current_alpha, current_alpha, current_alpha, 1.0);
			} else if (p_generate_normal_map) {
				const real_t left = alpha[y * width + MAX(0, x - 1)];
				const real_t right = alpha[y * width + MIN(width - 1, x + 1)];
				const real_t up = alpha[MAX(0, y - 1) * width + x];
				const real_t down = alpha[MIN(height - 1, y + 1) * width + x];
				Vector3 normal(-(right - left) * p_normal_height_scale, (down - up) * p_normal_height_scale, 1.0);
				normal.normalize();
				color = Color(normal.x * 0.5 + 0.5, normal.y * 0.5 + 0.5, normal.z * 0.5 + 0.5, current_alpha);
			} else {
				const real_t luminance = color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
				const real_t shadows = 1.0 - particle_texture_smoothstep(0.0, 0.5, luminance);
				const real_t midtones = MAX(0.0, 1.0 - Math::abs(luminance - 0.5) * 2.0);
				const real_t highlights = particle_texture_smoothstep(0.5, 1.0, luminance);
				color.r = CLAMP(color.r + (p_shadows_tint.r - 1.0) * shadows + (p_midtones_tint.r - 1.0) * midtones + (p_highlights_tint.r - 1.0) * highlights, 0.0, 1.0);
				color.g = CLAMP(color.g + (p_shadows_tint.g - 1.0) * shadows + (p_midtones_tint.g - 1.0) * midtones + (p_highlights_tint.g - 1.0) * highlights, 0.0, 1.0);
				color.b = CLAMP(color.b + (p_shadows_tint.b - 1.0) * shadows + (p_midtones_tint.b - 1.0) * midtones + (p_highlights_tint.b - 1.0) * highlights, 0.0, 1.0);
				if (p_toon_enabled) {
					const real_t lum = color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
					real_t toon_factor = 1.0;
					if (lum < p_toon_dark) {
						toon_factor = 0.25;
					} else if (lum < p_toon_light) {
						toon_factor = 0.70;
					} else {
						toon_factor = 1.0;
					}
					color.r *= toon_factor;
					color.g *= toon_factor;
					color.b *= toon_factor;
					if (color.a > 0.02) {
						color.a = color.a < p_toon_dark ? 0.35 : (color.a < p_toon_light ? 0.75 : 1.0);
					}
				}
			}
			p_image->set_pixel(x, y, color);
		}
	}
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

static Node3D *create_event_node(int p_type) {
	switch (p_type) {
		case PARTICLE_EVENT_LIGHT: {
			OmniLight3D *light = memnew(OmniLight3D);
			light->set_name("LightFlash");
			light->set_color(Color(1.0, 0.62, 0.32));
			light->set_param(Light3D::PARAM_ENERGY, 6.0);
			light->set_param(Light3D::PARAM_RANGE, 6.0);
			light->set_position(Vector3(0, 0.5, 0));
			ParticleEffect3D::set_emitter_duration(light, 0.3);
			light->set_meta(ParticleEffect3D::META_FLICKER, 0.2);
			return light;
		}
		case PARTICLE_EVENT_SOUND: {
			AudioStreamPlayer3D *player = memnew(AudioStreamPlayer3D);
			player->set_name("Sound");
			player->set_unit_size(10.0);
			player->set_meta(ParticleEffect3D::META_PITCH_RANDOM, 0.1);
			return player;
		}
		case PARTICLE_EVENT_SHAKE: {
			CameraShake3D *shake = memnew(CameraShake3D);
			shake->set_name("CameraShake");
			return shake;
		}
		case PARTICLE_EVENT_DAMAGE: {
			RadialDamage3D *damage = memnew(RadialDamage3D);
			damage->set_name("Damage");
			return damage;
		}
		case PARTICLE_EVENT_DECAL: {
			Decal *decal = memnew(Decal);
			decal->set_name("Decal");
			decal->set_size(Vector3(2.0, 1.0, 2.0));
			ParticleEffect3D::set_emitter_duration(decal, 6.0);
			decal->set_meta(ParticleEffect3D::META_FADE, 0.5);
			return decal;
		}
	}
	return memnew(Node3D);
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
		// Same anti-tunnelling steps as in the game (test walls are 0.2 m thick).
		if (ParticleEffect3D::emitter_uses_collision(gpu)) {
			ParticleEffect3D::apply_collision_substeps(gpu, 0.2);
		}
		if (!gpu->get_use_fixed_seed()) {
			gpu->set_seed(Math::rand());
			gpu->set_use_fixed_seed(true);
			gpu->set_meta(SNAME("_fps_preview_seed"), true);
		}
		gpu->set_emitting(true);
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_node)) {
		if (cpu->get_mesh().is_null()) {
			cpu->set_mesh(p_quad);
		}
		if (!cpu->get_use_fixed_seed()) {
			cpu->set_seed(Math::rand());
			cpu->set_use_fixed_seed(true);
			cpu->set_meta(SNAME("_fps_preview_seed"), true);
		}
		cpu->set_emitting(true);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		apply_preview_defaults(p_node->get_child(i), p_quad);
	}
}

// A new random look for the next run (Restart, next loop); edits in between
// keep the same seed, so a paused frame does not change.
static void reseed_preview_emitters(Node *p_node) {
	if (!p_node) {
		return;
	}
	if (p_node->has_meta(SNAME("_fps_preview_seed"))) {
		p_node->set("seed", Math::rand());
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		reseed_preview_emitters(p_node->get_child(i));
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

// Copies only what differs and returns true when anything did. Setting a
// particle node property restarts the emitter even to the same value (amount,
// lifetime, ...), so copying everything on every edit dropped the paused
// preview back to time 0: the effect looked smaller after any change.
static bool copy_preview_properties(Node *p_source, Node *p_preview, bool p_is_root = true) {
	if (!p_source || !p_preview) {
		return false;
	}
	bool changed = false;
	List<PropertyInfo> properties;
	p_source->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!(property.usage & PROPERTY_USAGE_STORAGE) || (property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) || property.name == "name" || property.name == "owner" || property.name == "script" ||
				property.name == "scene_file_path" || property.name == "emitting" || property.name == "visible" || property.name == "transform" ||
				property.name == "speed_scale" || // The preview's play / pause.
				property.name == "metadata/_fps_timeline_markers") { // Editor only; would restart the preview.
			continue;
		}
		// The preview keeps its own seed so a paused frame stays the same
		// after an edit; an effect authored with a fixed seed uses that one.
		if (property.name == "seed" || property.name == "use_fixed_seed") {
			if (!bool(p_source->get("use_fixed_seed"))) {
				continue;
			}
			p_preview->remove_meta(SNAME("_fps_preview_seed"));
		}
		bool valid = false;
		Variant value = p_source->get(property.name, &valid);
		if (valid && p_preview->get(property.name) != value) {
			p_preview->set(property.name, value);
			changed = true;
		}
	}
	if (Node3D *source_3d = Object::cast_to<Node3D>(p_source)) {
		if (Node3D *preview_3d = Object::cast_to<Node3D>(p_preview)) {
			Transform3D transform = source_3d->get_transform();
			transform.origin = Vector3();
			if (preview_3d->get_transform() != transform) {
				preview_3d->set_transform(transform);
				changed = true;
			}
		}
	}
	// The effect root is always shown; muted (hidden) tracks stay hidden.
	bool source_visible = true;
	if (!p_is_root) {
		bool valid = false;
		const Variant visible = p_source->get("visible", &valid);
		source_visible = !valid || bool(visible);
	}
	if (bool(p_preview->get("visible")) != source_visible) {
		p_preview->set("visible", source_visible);
		changed = true;
	}
	for (int i = 0; i < MIN(p_source->get_child_count(), p_preview->get_child_count()); i++) {
		changed = copy_preview_properties(p_source->get_child(i), p_preview->get_child(i), false) || changed;
	}
	return changed;
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

// Emitters and timed event nodes (lights, sounds, camera shakes, decals).
static void collect_tracks(Node *p_node, Vector<Node *> &r_tracks) {
	if (!p_node) {
		return;
	}
	if (ParticleEffect3D::is_track(p_node)) {
		r_tracks.push_back(p_node);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		collect_tracks(p_node->get_child(i), r_tracks);
	}
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
	const Color header_color = effects_tree->get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));

	TreeItem *presets = effects_tree->create_item(root);
	presets->set_text(0, TTR("FPS Presets"));
	presets->set_selectable(0, false);
	presets->set_custom_color(0, header_color);
	String current_category;
	TreeItem *category_item = nullptr;
	for (int preset : FPSParticlePresets::get_presets_in_display_order()) {
		const String name = FPSParticlePresets::get_preset_name(preset);
		const String category = FPSParticlePresets::get_preset_category(preset);
		if (!filter.is_empty() && !name.to_lower().contains(filter) && !category.to_lower().contains(filter)) {
			continue;
		}
		if (category != current_category || !category_item) {
			current_category = category;
			category_item = effects_tree->create_item(presets);
			category_item->set_text(0, TTRGET(category));
			category_item->set_selectable(0, false);
			category_item->set_custom_color(0, header_color);
		}
		TreeItem *item = effects_tree->create_item(category_item);
		item->set_text(0, name);
		const Ref<Texture2D> thumbnail = _get_preset_thumbnail(preset);
		if (thumbnail.is_valid()) {
			item->set_icon(0, thumbnail);
			item->set_icon_max_width(0, int(40 * EDSCALE));
		}
		// With the hover preview the description is shown there instead.
		item->set_tooltip_text(0, library_hover_preview_enabled() ? String() : FPSParticlePresets::get_preset_description(preset) + "\n" + TTR("Double-click to create a new effect from this preset."));
		item->set_metadata(0, vformat("preset:%d", preset));
	}

	TreeItem *recent = effects_tree->create_item(root);
	recent->set_text(0, TTR("Recent Effects"));
	recent->set_selectable(0, false);
	recent->set_custom_color(0, header_color);
	TreeItem *selected = nullptr;
	for (const String &path : recent_effect_paths) {
		const String name = path.get_file().get_basename();
		if (!filter.is_empty() && !name.to_lower().contains(filter) && !path.to_lower().contains(filter)) {
			continue;
		}
		TreeItem *item = effects_tree->create_item(recent);
		item->set_text(0, name);
		item->set_tooltip_text(0, library_hover_preview_enabled() ? String() : path);
		item->set_metadata(0, path);
		const Ref<Texture2D> thumbnail = _get_cached_effect_thumbnail(path);
		if (thumbnail.is_valid()) {
			item->set_icon(0, thumbnail);
			item->set_icon_max_width(0, int(40 * EDSCALE));
		}
		if (path == effect_path) {
			selected = item;
		}
	}
	if (selected) {
		selected->select(0);
	}
	if (recent->get_child_count() == 0) {
		TreeItem *empty_item = effects_tree->create_item(recent);
		empty_item->set_text(0, TTR("No recent effects"));
		empty_item->set_selectable(0, false);
		empty_item->set_custom_color(0, header_color);
	}
	if (effects_tree->is_visible_in_tree()) {
		_queue_library_thumbnails();
	}
}

void FPSStudioParticleEditorPlugin::_update_emitter_tree() {
	if (!emitter_tree) {
		return;
	}
	emitter_tree->clear();
	if (timeline_view) {
		timeline_view->refresh();
	}
	TreeItem *root_item = emitter_tree->create_item();
	Node *effect_root = _get_effect_root();
	Vector<Node *> emitters;
	collect_tracks(effect_root, emitters);
	Node *edited_emitter = _get_edited_emitter();
	TreeItem *selected_item = nullptr;
	if (track_color_icon.is_null()) {
		// A white bar tinted per row with the same color as its timeline track.
		Ref<Image> bar = Image::create_empty(MAX(3, int(4 * EDSCALE)), MAX(10, int(16 * EDSCALE)), false, Image::FORMAT_RGBA8);
		bar->fill(Color(1, 1, 1));
		track_color_icon = ImageTexture::create_from_image(bar);
	}
	const Color detail_color = emitter_tree->get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	int row = 0;
	for (Node *emitter : emitters) {
		TreeItem *item = emitter_tree->create_item(root_item);
		item->set_text(0, emitter->get_name());
		item->set_icon(0, track_color_icon);
		item->set_icon_modulate(0, FPSParticleTimeline::get_track_color(emitter, row));
		row++;
		String detail;
		if (GPUParticles3D *gpu_track = Object::cast_to<GPUParticles3D>(emitter)) {
			detail = vformat(String::utf8("%s \u00B7 %d"), TTR("GPU"), gpu_track->get_amount());
		} else if (CPUParticles3D *cpu_track = Object::cast_to<CPUParticles3D>(emitter)) {
			detail = vformat(String::utf8("%s \u00B7 %d"), TTR("CPU"), cpu_track->get_amount());
		} else if (Object::cast_to<Light3D>(emitter)) {
			detail = TTR("Light");
		} else if (Object::cast_to<CameraShake3D>(emitter)) {
			detail = TTR("Shake");
		} else if (Object::cast_to<Decal>(emitter)) {
			detail = TTR("Decal");
		} else if (RadialDamage3D *damage_track = Object::cast_to<RadialDamage3D>(emitter)) {
			detail = vformat(TTR("Damage %d"), int(Math::round(damage_track->get_damage())));
		} else if (emitter->is_class("AudioStreamPlayer3D")) {
			detail = TTR("Sound");
		} else {
			detail = emitter->get_class();
		}
		item->set_text(1, detail);
		item->set_icon(1, EditorNode::get_singleton()->get_object_icon(emitter));
		item->set_custom_color(1, detail_color);
		item->set_text_alignment(1, HORIZONTAL_ALIGNMENT_RIGHT);
		item->set_selectable(1, true);
		Node3D *track_3d = Object::cast_to<Node3D>(emitter);
		if (track_3d && !track_3d->is_visible()) {
			item->set_custom_color(0, emitter_tree->get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
		}
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
	_update_document_header();
	_update_effects_tree();
	_update_emitter_tree();
}

void FPSStudioParticleEditorPlugin::_update_document_header() {
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
}

void FPSStudioParticleEditorPlugin::_update_document_toolbar_layout() {
	// Measure the space the editor really gives the workspace: the screen
	// itself can be stretched beyond the window by its minimum size.
	real_t available = screen ? screen->get_size().x : 0.0;
	if (screen && screen->is_inside_tree()) {
		if (Control *parent = screen->get_parent_control()) {
			available = MIN(available, parent->get_size().x);
		}
		available = MIN(available, screen->get_viewport()->get_visible_rect().size.x - screen->get_global_position().x);
	}
	bool compact = compact_layout;
	if (screen && available > 0.0) {
		const real_t needed = screen->get_combined_minimum_size().x;
		if (!compact_layout) {
			wide_layout_min_width = needed;
			compact = available < 1200.0 * EDSCALE || needed > available + 1.0;
		} else {
			compact = available < MAX(real_t(1200.0 * EDSCALE), wide_layout_min_width);
		}
	}
	if (compact != compact_layout) {
		compact_layout = compact;
		_apply_compact_layout();
	}
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

void FPSStudioParticleEditorPlugin::_apply_compact_layout() {
	if (!screen) {
		return;
	}
	const bool compact = compact_layout;
	const bool themed = screen->is_inside_tree();
	auto icon = [&](Control *p_control, const char *p_name) -> Ref<Texture2D> {
		return (compact && themed) ? p_control->get_editor_theme_icon(StringName(p_name)) : Ref<Texture2D>();
	};
	if (layout_menu) {
		layout_menu->set_text(compact ? String() : TTR("Layout"));
		layout_menu->set_button_icon(themed ? layout_menu->get_editor_theme_icon(SNAME("Panels3")) : Ref<Texture2D>());
	}
	if (duplicate_emitter_button) {
		duplicate_emitter_button->set_text(compact ? String() : TTR("Duplicate"));
		duplicate_emitter_button->set_button_icon(icon(duplicate_emitter_button, "Duplicate"));
	}
	if (remove_emitter_button) {
		remove_emitter_button->set_text(compact ? String() : TTR("Remove"));
		remove_emitter_button->set_button_icon(icon(remove_emitter_button, "Remove"));
		remove_emitter_button->set_tooltip_text(TTR("Remove the selected emitter."));
	}
	if (reset_view_button) {
		reset_view_button->set_text(compact ? String() : TTR("Reset View"));
		reset_view_button->set_button_icon(icon(reset_view_button, "CenterView"));
	}
	if (texture_generator_button) {
		texture_generator_button->set_text(compact ? TTR("Textures...") : TTR("Texture Generator..."));
	}
	if (restart_button) {
		restart_button->set_text(compact ? String() : TTR("Restart"));
		restart_button->set_button_icon(icon(restart_button, "PlayStart"));
		restart_button->set_tooltip_text(TTR("Restart the preview from the beginning."));
	}
	if (play_button) {
		play_button->set_text(compact ? String() : (preview_playing ? TTR("Pause") : TTR("Play")));
		play_button->set_button_icon(icon(play_button, preview_playing ? "Pause" : "Play"));
	}
	if (speed_label) {
		speed_label->set_visible(!compact);
	}
	if (preview_stats_label) {
		preview_stats_label->set_visible(!compact);
	}
	if (time_label) {
		const Size2 time_size = Size2(compact ? 110 : 150, 0) * EDSCALE;
		time_label->set_custom_minimum_size(time_size);
		if (Control *time_container = time_label->get_parent_control()) {
			time_container->set_custom_minimum_size(time_size);
		}
	}
	if (timeline_time_label) {
		timeline_time_label->set_custom_minimum_size(Size2(compact ? 100 : 130, 0) * EDSCALE);
	}
	if (right_panel_root) {
		right_panel_root->set_custom_minimum_size(Size2(compact ? 270 : 360, 0) * EDSCALE);
	}
	if (left_panel_root) {
		left_panel_root->set_custom_minimum_size(Size2(compact ? 190 : 220, 0) * EDSCALE);
	}
}

void FPSStudioParticleEditorPlugin::_mark_document_dirty() {
	if (!_get_effect_root()) {
		return;
	}
	effect_dirty = true;
	// Edits arrive per slider step; refresh the header and emitter list once
	// per frame. The Library only changes with the path or the recent list.
	if (!document_refresh_queued) {
		document_refresh_queued = true;
		callable_mp(this, &FPSStudioParticleEditorPlugin::_flush_document_refresh).call_deferred();
	}
}

void FPSStudioParticleEditorPlugin::_flush_document_refresh() {
	document_refresh_queued = false;
	_update_document_header();
	_update_emitter_tree();
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
		particle_graph_shader_id = ObjectID();
		particle_code_shader_id = ObjectID();
		effect_document_serial++; // Undo steps of the previous document no longer apply.
		if (previous->is_inside_tree()) {
			previous->queue_free();
		} else {
			memdelete(previous);
		}
	}
	if (p_root && document_host && p_root->get_parent() != document_host) {
		if (p_root->get_parent()) {
			p_root->get_parent()->remove_child(p_root);
		}
		document_host->add_child(p_root);
	}
	effect_root_id = p_root ? p_root->get_instance_id() : ObjectID();
	effect_path = p_path;
	if (timeline_view) {
		restoring_playback_range = true;
		timeline_view->set_effect(p_root);
		if (!p_path.is_empty()) {
			const Dictionary ranges = EditorSettings::get_singleton()->get_project_metadata("fps_studio_particle", "playback_ranges", Dictionary());
			const Vector2 range = ranges.get(p_path, Vector2(0, -1));
			if (range.y > range.x) {
				timeline_view->set_loop_region(range.x, range.y);
			}
		}
		restoring_playback_range = false;
	}
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

void FPSStudioParticleEditorPlugin::_set_edited_emitter(Node *p_node, bool p_update_tree, bool p_force) {
	Node *effect_root = _get_effect_root();
	if (p_node && !ParticleEffect3D::is_track(p_node)) {
		p_node = nullptr;
	}
	if (p_node && effect_root && p_node != effect_root && !effect_root->is_ancestor_of(p_node)) {
		p_node = nullptr;
	}
	if (!p_force && p_node && _get_edited_emitter() == p_node) {
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
	if (module_stack) {
		module_stack->edit(p_node);
	}
	if (timeline_view) {
		timeline_view->set_selected_track(p_node);
	}
	if (texture_generator_apply_button) {
		texture_generator_apply_button->set_disabled(p_node == nullptr);
	}
	_update_particle_graph_controls();
	if (p_update_tree) {
		_update_emitter_tree();
	}
	// A plain selection change keeps the running preview (and the camera);
	// only a different document or a forced refresh (undo/redo) rebuilds it.
	Node *source = _get_effect_root();
	if (p_force || !preview_effect || !source || preview_source_id != source->get_instance_id()) {
		_rebuild_preview();
	} else {
		_apply_preview_solo();
		_update_event_radii(); // The selected event's rings are drawn brighter.
	}
}

void FPSStudioParticleEditorPlugin::_rebuild_preview() {
	if (!preview_root || rebuilding_preview) {
		return;
	}
	rebuilding_preview = true;
	// Seeds the preview picked itself, by node path: a rebuild of the same
	// effect (timing change, undo) keeps the look of the paused frame.
	HashMap<String, int64_t> kept_seeds;
	if (preview_effect) {
		Vector<Node *> old_tracks;
		collect_tracks(preview_effect, old_tracks);
		for (Node *track : old_tracks) {
			if (track->has_meta(SNAME("_fps_preview_seed"))) {
				kept_seeds[String(preview_effect->get_path_to(track))] = int64_t(track->get("seed"));
			}
		}
	}
	if (preview_effect) {
		preview_root->remove_child(preview_effect);
		preview_effect->queue_free();
		preview_effect = nullptr;
	}
	Node *source = _get_effect_root();
	if (!source) {
		preview_source_id = ObjectID();
		rebuilding_preview = false;
		return;
	}
	// Only a new document resets the camera framing.
	const bool new_document = preview_source_id != source->get_instance_id();
	preview_source_id = source->get_instance_id();
	preview_effect = source->duplicate(Node::DUPLICATE_SCRIPTS);
	if (!preview_effect) {
		rebuilding_preview = false;
		return;
	}
	preview_effect->set_name("ParticlePreviewEffect");
	clear_preview_scripts(preview_effect);
	apply_preview_defaults(preview_effect, preview_quad);
	if (!new_document && !kept_seeds.is_empty()) {
		Vector<Node *> new_tracks;
		collect_tracks(preview_effect, new_tracks);
		for (Node *track : new_tracks) {
			const int64_t *seed = kept_seeds.getptr(String(preview_effect->get_path_to(track)));
			if (seed && track->has_meta(SNAME("_fps_preview_seed"))) {
				track->set("seed", *seed);
			}
		}
	}
	clear_preview_transform_origin(preview_effect);
	preview_root->add_child(preview_effect);

	Vector<Node *> emitters;
	collect_tracks(source, emitters);
	// Finite effects loop over their full timeline (delays + emission +
	// lifetime); continuous ones over the visible timeline span.
	preview_duration = 0.1;
	double max_start = 0.0;
	double max_lifetime = 0.1;
	bool infinite = false;
	for (Node *emitter : emitters) {
		Node3D *track_3d = Object::cast_to<Node3D>(emitter);
		if (track_3d && !track_3d->is_visible()) {
			continue;
		}
		double start, emission_end, visual_end;
		ParticleEffect3D::get_track_window(emitter, start, emission_end, visual_end);
		max_start = MAX(max_start, start);
		if (ParticleEffect3D::is_emitter(emitter)) {
			max_lifetime = MAX(max_lifetime, double(emitter->get("lifetime")));
		}
		if (visual_end < 0.0) {
			infinite = true;
		} else {
			preview_duration = MAX(preview_duration, visual_end);
		}
	}
	if (infinite) {
		preview_duration = MAX(preview_duration, max_start + max_lifetime);
	}
	preview_duration = MAX(preview_duration, 0.1);
	if (timeline_view) {
		timeline_view->refresh();
	}
	// A rebuild of the same effect (timing, lifetime, undo, coming back to the
	// tab) stays at the playhead, so a paused moment can be tuned.
	if (new_document) {
		preview_time = preview_loop_end > preview_loop_start ? preview_loop_start : 0.0;
	} else {
		preview_time = CLAMP(preview_time, 0.0, preview_duration);
	}
	preview_time_label_update_accumulator = 0.0;
	if (timeline) {
		timeline->set_max(preview_duration);
		timeline->set_value_no_signal(preview_time);
	}
	_apply_preview_solo();
	_apply_preview_event_toggles();
	_update_preview_bounds();
	_update_event_radii();
	_restart_preview_at(preview_time);
	_set_preview_playing(preview_playing);
	if (new_document) {
		_frame_preview();
	}
	_update_time_label();
	rebuilding_preview = false;
}

void FPSStudioParticleEditorPlugin::_clear_preview_scripts(Node *p_node) {
	clear_preview_scripts(p_node);
}

void FPSStudioParticleEditorPlugin::_apply_preview_defaults(Node *p_node) {
	apply_preview_defaults(p_node, preview_quad);
}

void FPSStudioParticleEditorPlugin::_queue_preview_sync() {
	if (!preview_sync_queued) {
		preview_sync_queued = true;
		callable_mp(this, &FPSStudioParticleEditorPlugin::_flush_preview_sync).call_deferred();
	}
}

void FPSStudioParticleEditorPlugin::_flush_preview_sync() {
	if (preview_sync_queued) {
		_sync_preview_properties();
	}
}

void FPSStudioParticleEditorPlugin::_sync_preview_properties() {
	preview_sync_queued = false;
	Node *source = _get_effect_root();
	if (!source || !preview_effect) {
		return;
	}
	// A changed emitter may have restarted from time 0: replay it at the
	// playhead (edits to shared materials and meshes change nothing here).
	if (copy_preview_properties(source, preview_effect)) {
		preview_restart_queued = true;
	}
	_apply_preview_defaults(preview_effect);
	_apply_preview_solo();
	_apply_preview_event_toggles();
	_update_preview_bounds();
	_update_event_radii();
	_set_preview_playing(preview_playing);
	if (preview_restart_queued) {
		preview_restart_queued = false;
		_restart_preview_at(preview_time);
	}
}

void FPSStudioParticleEditorPlugin::_update_event_radii() {
	if (!preview_radii_root) {
		return;
	}
	for (int i = preview_radii_root->get_child_count() - 1; i >= 0; i--) {
		Node *child = preview_radii_root->get_child(i);
		preview_radii_root->remove_child(child);
		child->queue_free();
	}
	Node *source = _get_effect_root();
	if (!preview_show_radii || !source || !preview_effect) {
		return;
	}
	Vector<Node *> tracks;
	collect_tracks(source, tracks);
	Node *selected = _get_edited_emitter();
	for (Node *track : tracks) {
		real_t inner = 0.0;
		real_t outer = 0.0;
		Color color;
		if (!FPSParticleEventRadii::get_reach(track, inner, outer, color)) {
			continue;
		}
		const Node3D *track_3d = Object::cast_to<Node3D>(track);
		if (track_3d && !track_3d->is_visible()) {
			continue;
		}
		Node3D *preview_track = Object::cast_to<Node3D>(preview_effect->get_node_or_null(source->get_path_to(track)));
		if (!preview_track || !preview_track->is_inside_tree()) {
			continue;
		}
		const real_t emphasis = track == selected ? 1.0 : 0.55;
		Ref<ImmediateMesh> mesh;
		mesh.instantiate();
		for (int ring = 0; ring < 2; ring++) {
			Vector<Vector3> lines;
			FPSParticleEventRadii::append_sphere_rings(lines, ring == 0 ? inner : outer);
			if (lines.is_empty()) {
				continue;
			}
			Ref<StandardMaterial3D> material;
			material.instantiate();
			material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			material->set_albedo(Color(color, (ring == 0 ? 0.95 : 0.5) * emphasis));
			material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
			mesh->surface_begin(Mesh::PRIMITIVE_LINES, material);
			for (const Vector3 &point : lines) {
				mesh->surface_add_vertex(point);
			}
			mesh->surface_end();
		}
		if (mesh->get_surface_count() == 0) {
			continue;
		}
		MeshInstance3D *rings = memnew(MeshInstance3D);
		rings->set_mesh(mesh);
		rings->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		preview_radii_root->add_child(rings);
		rings->set_global_position(preview_track->get_global_position());
	}
}

void FPSStudioParticleEditorPlugin::_set_preview_playing(bool p_playing) {
	preview_playing = p_playing;
	if (play_button) {
		play_button->set_pressed_no_signal(preview_playing);
		play_button->set_text(compact_layout ? String() : (preview_playing ? TTR("Pause") : TTR("Play")));
		if (play_button->is_inside_tree()) {
			play_button->set_button_icon(compact_layout ? play_button->get_editor_theme_icon(preview_playing ? SNAME("Pause") : SNAME("Play")) : Ref<Texture2D>());
		}
	}
	if (timeline_play_button && timeline_play_button->is_inside_tree()) {
		timeline_play_button->set_button_icon(timeline_play_button->get_editor_theme_icon(preview_playing ? SNAME("Pause") : SNAME("Play")));
	}
	set_preview_speed_recursive(preview_effect, preview_playing ? preview_speed : 0.0);
	if (ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(preview_effect)) {
		effect->set_speed_scale(preview_playing ? preview_speed : 0.0);
	}
}

void FPSStudioParticleEditorPlugin::_on_play_toggled(bool p_pressed) {
	if (!_get_effect_root()) {
		return;
	}
	if (p_pressed) {
		_rewind_preview_if_finished();
	}
	_set_preview_playing(p_pressed);
}

// Play after the effect has run out starts again at the playback start.
void FPSStudioParticleEditorPlugin::_rewind_preview_if_finished() {
	if (!preview_effect) {
		return;
	}
	const bool has_region = preview_loop_end > preview_loop_start;
	const double loop_to = has_region ? MIN(preview_loop_end, MAX(preview_duration, preview_loop_end)) : preview_duration;
	if (preview_time < loop_to - 0.001) {
		return;
	}
	preview_time = has_region ? preview_loop_start : 0.0;
	preview_time_label_update_accumulator = 0.0;
	if (timeline) {
		timeline->set_value_no_signal(preview_time);
	}
	if (timeline_view) {
		timeline_view->set_current_time(preview_time);
	}
	_reseed_preview_for_next_run();
	_restart_preview_at(preview_time);
	_update_time_label();
}

void FPSStudioParticleEditorPlugin::_on_restart_pressed() {
	if (!preview_effect) {
		return;
	}
	preview_time = preview_loop_end > preview_loop_start ? preview_loop_start : 0.0;
	preview_time_label_update_accumulator = 0.0;
	if (timeline) {
		timeline->set_value_no_signal(preview_time);
	}
	_reseed_preview_for_next_run();
	_restart_preview_at(preview_time);
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
	_restart_preview_at(preview_time);
	_update_time_label();
}

void FPSStudioParticleEditorPlugin::_on_speed_changed(double p_value) {
	preview_speed = p_value;
	if (preview_playing && preview_effect) {
		set_preview_speed_recursive(preview_effect, preview_speed);
		if (ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(preview_effect)) {
			effect->set_speed_scale(preview_speed);
		}
	}
}

void FPSStudioParticleEditorPlugin::_on_loop_toggled(bool p_enabled) {
	preview_loop = p_enabled;
	if (preview_loop_toggle) {
		preview_loop_toggle->set_pressed_no_signal(p_enabled);
	}
	if (timeline_loop_button) {
		timeline_loop_button->set_pressed_no_signal(p_enabled);
	}
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

static const int VFX_TEXTURE_RESOLUTIONS[] = { 128, 256, 512, 1024, 2048 };

static Color vfx_color_from_array(const float *p_color) {
	return Color(p_color[0], p_color[1], p_color[2], 1.0);
}

void FPSStudioParticleEditorPlugin::_show_texture_generator() {
	if (texture_generator_dialog) {
		// Opens at the size the user last gave it.
		const Size2i size = (texture_generator_size.x > 0 && texture_generator_size.y > 0) ? texture_generator_size : Size2i(Size2i(1280, 820) * EDSCALE);
		texture_generator_dialog->popup_centered_clamped(size, 0.95);
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_visibility_changed() {
	if (texture_generator_dialog && !texture_generator_dialog->is_visible()) {
		texture_generator_size = texture_generator_dialog->get_size();
		if (EditorNode::get_singleton()) {
			EditorNode::get_singleton()->save_editor_layout_delayed();
		}
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_pressed() {
	if (!texture_generator_dialog) {
		return;
	}
	if (!texture_generator_thumbnails_ready) {
		_build_texture_type_list();
	}
	_regenerate_texture(true);
	_show_texture_generator();
}

void FPSStudioParticleEditorPlugin::_build_texture_type_list() {
	if (!texture_generator_type_tree) {
		return;
	}
	texture_generator_type_tree->clear();
	TreeItem *root = texture_generator_type_tree->create_item();
	Vector<String> categories;
	for (int i = 0; i < VFXTextureGenerator::get_type_count(); i++) {
		const String category = String::utf8(VFXTextureGenerator::get_type_info_by_index(i)->category);
		if (!categories.has(category)) {
			categories.push_back(category);
		}
	}
	TreeItem *selected_item = nullptr;
	for (const String &category : categories) {
		TreeItem *category_item = texture_generator_type_tree->create_item(root);
		category_item->set_text(0, category);
		category_item->set_selectable(0, false);
		category_item->set_custom_color(0, texture_generator_type_tree->get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
		for (int i = 0; i < VFXTextureGenerator::get_type_count(); i++) {
			const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info_by_index(i);
			if (String::utf8(info->category) != category) {
				continue;
			}
			// Small draft thumbnail rendered with the type defaults.
			VFXTextureGenerator::Settings settings;
			settings.type = info->id;
			settings.seed = 7;
			settings.time = 0.35f;
			settings.quality = 0;
			VFXTextureGenerator::apply_type_defaults(settings);
			Ref<Image> thumbnail = render_vfx_texture_frame(settings, 48);
			TreeItem *item = texture_generator_type_tree->create_item(category_item);
			item->set_text(0, String::utf8(info->name));
			item->set_tooltip_text(0, String::utf8(info->description));
			item->set_icon(0, ImageTexture::create_from_image(thumbnail));
			item->set_icon_max_width(0, int(40 * EDSCALE));
			item->set_metadata(0, info->id);
			if (info->id == texture_generator_type) {
				selected_item = item;
			}
		}
	}
	if (selected_item) {
		texture_generator_syncing_ui = true;
		selected_item->select(0);
		texture_generator_syncing_ui = false;
	}
	texture_generator_thumbnails_ready = true;
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_type_selected() {
	if (texture_generator_syncing_ui || !texture_generator_type_tree) {
		return;
	}
	TreeItem *item = texture_generator_type_tree->get_selected();
	if (!item || item->get_metadata(0).get_type() != Variant::INT) {
		return;
	}
	const int type = int(item->get_metadata(0));
	if (type == texture_generator_type) {
		return;
	}
	_select_texture_type(type, true);
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_select_texture_type(int p_type, bool p_reset_params) {
	const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info(p_type);
	if (!info) {
		return;
	}
	texture_generator_type = p_type;
	if (p_reset_params) {
		for (int i = 0; i < VFXTextureGenerator::MAX_PARAMS; i++) {
			texture_generator_params[i] = i < info->param_count ? info->params[i].default_value : 0.0f;
		}
		texture_generator_syncing_ui = true;
		if (texture_generator_color_a) {
			texture_generator_color_a->set_pick_color(vfx_color_from_array(info->color_a));
		}
		if (texture_generator_color_b) {
			texture_generator_color_b->set_pick_color(vfx_color_from_array(info->color_b));
		}
		texture_generator_syncing_ui = false;
	}
	if (texture_generator_type_title) {
		texture_generator_type_title->set_text(String::utf8(info->name));
	}
	if (texture_generator_type_description) {
		texture_generator_type_description->set_text(String::utf8(info->description));
	}
	if (texture_generator_color_a_label) {
		texture_generator_color_a_label->set_text(String::utf8(info->color_a_label));
	}
	if (texture_generator_color_b_label) {
		texture_generator_color_b_label->set_text(String::utf8(info->color_b_label));
	}
	if (texture_generator_tiling) {
		texture_generator_tiling->set_disabled(!info->supports_tiling);
		texture_generator_tiling->set_tooltip_text(info->supports_tiling ? TTR("Generate a texture that repeats seamlessly.") : TTR("Only pattern types (Noise, Caustics) can tile seamlessly."));
	}
	if (texture_generator_type_tree) {
		// Keep the tree selection in sync (recipes and resets change the type).
		texture_generator_syncing_ui = true;
		TreeItem *root = texture_generator_type_tree->get_root();
		for (TreeItem *category = root ? root->get_first_child() : nullptr; category; category = category->get_next()) {
			for (TreeItem *item = category->get_first_child(); item; item = item->get_next()) {
				if (int(item->get_metadata(0)) == p_type && !item->is_selected(0)) {
					item->select(0);
				}
			}
		}
		texture_generator_syncing_ui = false;
	}
	_rebuild_texture_param_controls();
}

void FPSStudioParticleEditorPlugin::_rebuild_texture_param_controls() {
	if (!texture_generator_param_grid) {
		return;
	}
	for (int i = texture_generator_param_grid->get_child_count() - 1; i >= 0; i--) {
		Node *child = texture_generator_param_grid->get_child(i);
		texture_generator_param_grid->remove_child(child);
		child->queue_free();
	}
	texture_generator_param_sliders.clear();
	const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info(texture_generator_type);
	if (!info) {
		return;
	}
	for (int i = 0; i < info->param_count; i++) {
		const VFXTextureGenerator::ParamInfo &param = info->params[i];
		Label *label = memnew(Label);
		label->set_text(String::utf8(param.label));
		label->set_tooltip_text(String::utf8(param.tooltip));
		label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		texture_generator_param_grid->add_child(label);
		EditorSpinSlider *slider = memnew(EditorSpinSlider);
		slider->set_min(param.min_value);
		slider->set_max(param.max_value);
		slider->set_step(param.step);
		slider->set_value_no_signal(texture_generator_params[i]);
		slider->set_editing_integer(param.step >= 1.0f);
		slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		slider->set_tooltip_text(String::utf8(param.tooltip));
		slider->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_param_changed).bind(i));
		texture_generator_param_grid->add_child(slider);
		texture_generator_param_sliders.push_back(slider);
	}
}

VFXTextureGenerator::Settings FPSStudioParticleEditorPlugin::_make_texture_settings(double p_time, double p_life, int p_quality) const {
	VFXTextureGenerator::Settings settings;
	settings.type = texture_generator_type;
	settings.seed = texture_generator_seed ? uint32_t(texture_generator_seed->get_value()) : 7u;
	settings.time = float(p_time);
	settings.life = float(CLAMP(p_life, 0.0, 1.0));
	settings.quality = p_quality;
	const Color color_a = texture_generator_color_a ? texture_generator_color_a->get_pick_color() : Color(1, 1, 1);
	const Color color_b = texture_generator_color_b ? texture_generator_color_b->get_pick_color() : Color(1, 1, 1);
	settings.color_a[0] = color_a.r;
	settings.color_a[1] = color_a.g;
	settings.color_a[2] = color_a.b;
	settings.color_b[0] = color_b.r;
	settings.color_b[1] = color_b.g;
	settings.color_b[2] = color_b.b;
	for (int i = 0; i < VFXTextureGenerator::MAX_PARAMS; i++) {
		settings.params[i] = texture_generator_params[i];
	}
	settings.speed = texture_generator_speed ? float(texture_generator_speed->get_value()) : 1.0f;
	settings.rotation_degrees = texture_generator_rotation ? float(texture_generator_rotation->get_value()) : 0.0f;
	settings.exposure = texture_generator_exposure ? float(texture_generator_exposure->get_value()) : 1.0f;
	settings.opacity = texture_generator_opacity ? float(texture_generator_opacity->get_value()) : 1.0f;
	settings.polar = texture_generator_polar && texture_generator_polar->is_pressed();
	const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info(texture_generator_type);
	settings.tiling = info && info->supports_tiling && texture_generator_tiling && texture_generator_tiling->is_pressed();
	// Decal types render a real height-based normal map themselves; other
	// types derive one from the alpha shape in post-processing.
	if (_uses_generator_normal_map()) {
		settings.output_normal = true;
		settings.normal_strength = texture_generator_normal_height_scale ? float(texture_generator_normal_height_scale->get_value()) : 3.0f;
	}
	return settings;
}

bool FPSStudioParticleEditorPlugin::_uses_generator_normal_map() const {
	return texture_generator_normal_map && texture_generator_normal_map->is_pressed() && VFXTextureGenerator::has_height_normal(texture_generator_type);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_resolution_selected(int p_index) {
	if (texture_generator_syncing_ui) {
		return;
	}
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_output_selected(int p_index) {
	if (texture_generator_syncing_ui) {
		return;
	}
	_update_texture_time_controls();
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_seed_changed(double p_value) {
	if (texture_generator_syncing_ui) {
		return;
	}
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_randomize_seed_pressed() {
	if (texture_generator_seed) {
		texture_generator_seed->set_value(Math::rand() % 100000);
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_fps_changed(double p_value) {
	if (texture_generator_syncing_ui) {
		return;
	}
	_update_texture_time_controls();
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_duration_changed(double p_value) {
	if (texture_generator_syncing_ui) {
		return;
	}
	_update_texture_time_controls();
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_animate_toggled(bool p_enabled) {
	texture_generator_playback_accumulator = 0.0;
	texture_generator_preview_refresh = 0.0;
	if (texture_generator_animate) {
		texture_generator_animate->set_text(p_enabled ? TTR("Pause") : TTR("Play"));
		texture_generator_animate->set_tooltip_text(p_enabled ? TTR("Pause the preview animation.") : TTR("Play the preview animation."));
	}
	const bool is_single_texture = !texture_generator_output || texture_generator_output->get_selected_id() == VFX_TEXTURE_OUTPUT_STILL;
	if (p_enabled && is_single_texture) {
		// The current-time sample changes while playing, so any previously exported
		// single frame no longer represents the selected generator state.
		_invalidate_texture_export();
	} else if (!p_enabled) {
		// Paused: refine the preview at full quality.
		_regenerate_texture(true);
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_time_changed(double p_value) {
	texture_generator_time_value = p_value;
	texture_generator_preview_frame = -1;
	_update_texture_time_controls();
	if (texture_generator_output && texture_generator_output->get_selected_id() == VFX_TEXTURE_OUTPUT_STILL) {
		_invalidate_texture_export();
	}
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_color_changed(const Color &p_color) {
	if (texture_generator_syncing_ui) {
		return;
	}
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_processing_value_changed(double p_value) {
	if (texture_generator_syncing_ui) {
		return;
	}
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_param_changed(double p_value, int p_index) {
	if (p_index < 0 || p_index >= VFXTextureGenerator::MAX_PARAMS) {
		return;
	}
	texture_generator_params[p_index] = float(p_value);
	if (texture_generator_syncing_ui) {
		return;
	}
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_reset_params_pressed() {
	_select_texture_type(texture_generator_type, true);
	texture_generator_syncing_ui = true;
	if (texture_generator_speed) {
		texture_generator_speed->set_value_no_signal(1.0);
	}
	if (texture_generator_rotation) {
		texture_generator_rotation->set_value_no_signal(0.0);
	}
	if (texture_generator_exposure) {
		texture_generator_exposure->set_value_no_signal(1.0);
	}
	if (texture_generator_opacity) {
		texture_generator_opacity->set_value_no_signal(1.0);
	}
	texture_generator_syncing_ui = false;
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_toggle_changed(bool p_enabled) {
	_on_texture_generator_processing_value_changed(p_enabled ? 1.0 : 0.0);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_alpha_preview_toggled(bool p_enabled) {
	if (texture_generator_syncing_ui) {
		return;
	}
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_background_selected(int p_index) {
	if (!texture_generator_stage || !texture_generator_checker) {
		return;
	}
	static const Color backgrounds[] = { Color(0.0, 0.0, 0.0, 0.0), Color(0.0, 0.0, 0.0), Color(0.18, 0.18, 0.19), Color(0.5, 0.5, 0.5), Color(1.0, 1.0, 1.0) };
	const int index = CLAMP(p_index, 0, 4);
	texture_generator_checker->set_visible(index == 0);
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(index == 0 ? Color(0.0, 0.0, 0.0, 0.0) : backgrounds[index]);
	texture_generator_stage->add_theme_style_override(SceneStringName(panel), style);
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_show_sheet_toggled(bool p_enabled) {
	texture_generator_preview_frame = -1;
	if (p_enabled) {
		_regenerate_texture(false);
	} else {
		_regenerate_texture(true);
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_reset_color_balance_pressed() {
	texture_generator_syncing_ui = true;
	if (texture_generator_shadows_tint) {
		texture_generator_shadows_tint->set_pick_color(Color(1.0, 1.0, 1.0, 1.0));
	}
	if (texture_generator_midtones_tint) {
		texture_generator_midtones_tint->set_pick_color(Color(1.0, 1.0, 1.0, 1.0));
	}
	if (texture_generator_highlights_tint) {
		texture_generator_highlights_tint->set_pick_color(Color(1.0, 1.0, 1.0, 1.0));
	}
	texture_generator_syncing_ui = false;
	_invalidate_texture_export();
	_regenerate_texture(true);
}

void FPSStudioParticleEditorPlugin::_on_texture_recipe_save_pressed() {
	if (texture_recipe_save_dialog && texture_generator_dialog) {
		texture_generator_return_after_recipe = true;
		texture_generator_dialog->hide();
		const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info(texture_generator_type);
		if (info) {
			texture_recipe_save_dialog->set_current_file(String::utf8(info->name).to_snake_case().replace("/", "_").replace(" ", "") + ".cfg");
		}
		texture_recipe_save_dialog->popup_file_dialog();
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_recipe_load_pressed() {
	if (texture_recipe_load_dialog && texture_generator_dialog) {
		texture_generator_return_after_recipe = true;
		texture_generator_dialog->hide();
		texture_recipe_load_dialog->popup_file_dialog();
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_recipe_dialog_visibility_changed() {
	if ((texture_recipe_save_dialog && texture_recipe_save_dialog->is_visible()) || (texture_recipe_load_dialog && texture_recipe_load_dialog->is_visible())) {
		return;
	}
	if (texture_generator_return_after_recipe) {
		texture_generator_return_after_recipe = false;
		callable_mp(this, &FPSStudioParticleEditorPlugin::_show_texture_generator_after_recipe).call_deferred();
	}
}

void FPSStudioParticleEditorPlugin::_show_texture_generator_after_recipe() {
	_show_texture_generator();
}

void FPSStudioParticleEditorPlugin::_on_texture_recipe_save_path_selected(const String &p_path) {
	String path = p_path;
	if (path.get_extension().to_lower() != "cfg") {
		path += ".cfg";
	}
	if (_save_texture_recipe(path) && texture_generator_status) {
		texture_generator_status->set_text(vformat(TTR("Recipe saved: %s"), path.get_file()));
	}
}

bool FPSStudioParticleEditorPlugin::_save_texture_recipe(const String &p_path) {
	Ref<ConfigFile> recipe;
	recipe.instantiate();
	recipe->set_value("generator", "version", 3);
	recipe->set_value("generator", "type", texture_generator_type);
	recipe->set_value("generator", "resolution", VFX_TEXTURE_RESOLUTIONS[CLAMP(texture_generator_resolution->get_selected(), 0, 4)]);
	recipe->set_value("generator", "output_layout", texture_generator_output->get_selected_id());
	recipe->set_value("generator", "seed", int(texture_generator_seed->get_value()));
	recipe->set_value("generator", "fps", int(texture_generator_fps->get_value()));
	recipe->set_value("generator", "time", texture_generator_time_value);
	recipe->set_value("generator", "duration", texture_generator_duration->get_value());
	recipe->set_value("generator", "color_a", texture_generator_color_a->get_pick_color());
	recipe->set_value("generator", "color_b", texture_generator_color_b->get_pick_color());
	recipe->set_value("generator", "speed", texture_generator_speed->get_value());
	recipe->set_value("generator", "rotation", texture_generator_rotation->get_value());
	recipe->set_value("generator", "exposure", texture_generator_exposure->get_value());
	recipe->set_value("generator", "opacity", texture_generator_opacity->get_value());
	recipe->set_value("generator", "polar_conversion", texture_generator_polar->is_pressed());
	recipe->set_value("generator", "tiling_enabled", texture_generator_tiling->is_pressed());
	recipe->set_value("post", "toon_enabled", texture_generator_toon->is_pressed());
	recipe->set_value("post", "toon_dark", texture_generator_toon_dark->get_value());
	recipe->set_value("post", "toon_light", texture_generator_toon_light->get_value());
	recipe->set_value("post", "alpha_threshold", texture_generator_alpha_threshold->get_value());
	recipe->set_value("post", "alpha_blur", texture_generator_alpha_blur->get_value());
	recipe->set_value("post", "normal_map", texture_generator_normal_map->is_pressed());
	recipe->set_value("post", "normal_height_scale", texture_generator_normal_height_scale->get_value());
	recipe->set_value("post", "shadows_tint", texture_generator_shadows_tint->get_pick_color());
	recipe->set_value("post", "midtones_tint", texture_generator_midtones_tint->get_pick_color());
	recipe->set_value("post", "highlights_tint", texture_generator_highlights_tint->get_pick_color());
	const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info(texture_generator_type);
	if (info) {
		for (int i = 0; i < info->param_count; i++) {
			recipe->set_value("params", String::utf8(info->params[i].key), texture_generator_params[i]);
		}
	}
	const Error save_error = recipe->save(p_path);
	if (save_error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not save the texture recipe to %s (error %d)."), p_path, save_error));
		return false;
	}
	return true;
}

void FPSStudioParticleEditorPlugin::_on_texture_recipe_load_path_selected(const String &p_path) {
	if (_load_texture_recipe(p_path, true) && texture_generator_status) {
		texture_generator_status->set_text(vformat(TTR("Recipe loaded: %s"), p_path.get_file()));
	}
}

bool FPSStudioParticleEditorPlugin::_load_texture_recipe(const String &p_path, bool p_report_errors) {
	Ref<ConfigFile> recipe;
	recipe.instantiate();
	const Error load_error = recipe->load(p_path);
	if (load_error != OK || !recipe->has_section("generator")) {
		if (p_report_errors) {
			EditorNode::get_singleton()->show_warning(vformat(TTR("Could not load the texture recipe from %s (error %d)."), p_path, load_error));
		}
		return false;
	}
	const int version = int(recipe->get_value("generator", "version", 1));
	// Version 1 and 2 recipes stored the same type IDs under "preset".
	const int type = int(recipe->get_value("generator", version >= 3 ? "type" : "preset", VFXTextureGenerator::TYPE_FIRE));
	const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info(type);
	const int output = int(recipe->get_value("generator", "output_layout", VFX_TEXTURE_OUTPUT_STILL));
	const int output_index = texture_generator_output->get_item_index(output);
	if (!info || output_index < 0) {
		if (p_report_errors) {
			EditorNode::get_singleton()->show_warning(TTR("The texture recipe contains an unsupported template or output layout."));
		}
		return false;
	}
	const int resolution = int(recipe->get_value("generator", "resolution", 256));
	int resolution_index = 1;
	for (int i = 0; i < 5; i++) {
		if (VFX_TEXTURE_RESOLUTIONS[i] == resolution) {
			resolution_index = i;
			break;
		}
	}
	_select_texture_type(type, true);
	texture_generator_syncing_ui = true;
	if (version >= 3 && recipe->has_section("params")) {
		for (int i = 0; i < info->param_count; i++) {
			const String key = String::utf8(info->params[i].key);
			texture_generator_params[i] = float(recipe->get_value("params", key, info->params[i].default_value));
		}
	}
	texture_generator_resolution->select(resolution_index);
	texture_generator_output->select(output_index);
	texture_generator_seed->set_value_no_signal(int(recipe->get_value("generator", "seed", 7)));
	texture_generator_fps->set_value_no_signal(int(recipe->get_value("generator", "fps", 12)));
	texture_generator_time_value = real_t(recipe->get_value("generator", "time", 0.0));
	texture_generator_duration->set_value_no_signal(real_t(recipe->get_value("generator", "duration", 2.0)));
	texture_generator_color_a->set_pick_color(recipe->get_value("generator", version >= 3 ? "color_a" : "base_color", vfx_color_from_array(info->color_a)));
	texture_generator_color_b->set_pick_color(recipe->get_value("generator", version >= 3 ? "color_b" : "highlight_color", vfx_color_from_array(info->color_b)));
	texture_generator_speed->set_value_no_signal(real_t(recipe->get_value("generator", version >= 3 ? "speed" : "param_speed", 1.0)));
	texture_generator_rotation->set_value_no_signal(real_t(recipe->get_value("generator", version >= 3 ? "rotation" : "param_angle", 0.0)));
	texture_generator_exposure->set_value_no_signal(real_t(recipe->get_value("generator", "exposure", 1.0)));
	texture_generator_opacity->set_value_no_signal(real_t(recipe->get_value("generator", "opacity", 1.0)));
	texture_generator_polar->set_pressed_no_signal(bool(recipe->get_value("generator", "polar_conversion", false)));
	texture_generator_tiling->set_pressed_no_signal(bool(recipe->get_value("generator", "tiling_enabled", false)));
	const String post = version >= 3 ? "post" : "generator";
	texture_generator_toon->set_pressed_no_signal(bool(recipe->get_value(post, "toon_enabled", false)));
	texture_generator_toon_dark->set_value_no_signal(real_t(recipe->get_value(post, "toon_dark", 0.5)));
	texture_generator_toon_light->set_value_no_signal(real_t(recipe->get_value(post, "toon_light", 0.85)));
	texture_generator_alpha_threshold->set_value_no_signal(real_t(recipe->get_value(post, "alpha_threshold", 0.0)));
	texture_generator_alpha_blur->set_value_no_signal(real_t(recipe->get_value(post, "alpha_blur", 0.0)));
	texture_generator_normal_map->set_pressed_no_signal(bool(recipe->get_value(post, "normal_map", false)));
	texture_generator_normal_height_scale->set_value_no_signal(real_t(recipe->get_value(post, "normal_height_scale", 3.0)));
	texture_generator_shadows_tint->set_pick_color(recipe->get_value(post, "shadows_tint", Color(1.0, 1.0, 1.0, 1.0)));
	texture_generator_midtones_tint->set_pick_color(recipe->get_value(post, "midtones_tint", Color(1.0, 1.0, 1.0, 1.0)));
	texture_generator_highlights_tint->set_pick_color(recipe->get_value(post, "highlights_tint", Color(1.0, 1.0, 1.0, 1.0)));
	texture_generator_syncing_ui = false;
	_rebuild_texture_param_controls();
	_update_texture_time_controls();
	_invalidate_texture_export();
	_regenerate_texture(true);
	return true;
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_export_pressed() {
	// The live preview may be rendered at a smaller size and draft quality.
	// Export always regenerates the full-resolution sample.
	_regenerate_texture();
	if (!generated_texture_image.is_valid() || !texture_save_dialog) {
		return;
	}
	const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info(texture_generator_type);
	const String base_name = info ? String::utf8(info->name).to_lower().replace(" / ", "_").replace(" ", "_") : String("vfx");
	const String output_suffix = generated_texture_frame_columns > 1 ? vformat("_flipbook_%dx%d", generated_texture_frame_columns, generated_texture_frame_columns) : String();
	const String normal_suffix = texture_generator_normal_map->is_pressed() ? "_normal" : String();
	texture_save_dialog->set_current_file(vformat("%s%s%s_%d.png", base_name, output_suffix, normal_suffix, int(texture_generator_seed->get_value())));
	texture_generator_return_after_export = true;
	texture_generator_dialog->hide();
	texture_save_dialog->popup_file_dialog();
}

String FPSStudioParticleEditorPlugin::_get_auto_texture_path() const {
	Node *effect = _get_effect_root();
	Node *emitter = _get_edited_emitter();
	const String directory = effect_path.is_empty() ? String("res://vfx_textures") : effect_path.get_base_dir().path_join("textures");
	const String effect_name = effect ? String(effect->get_name()).to_snake_case() : String("effect");
	const String emitter_name = emitter ? String(emitter->get_name()).to_snake_case() : String("emitter");
	return directory.path_join(vformat("%s_%s.png", effect_name, emitter_name).validate_filename());
}

bool FPSStudioParticleEditorPlugin::_export_generated_texture(const String &p_path) {
	if (!generated_texture_image.is_valid()) {
		return false;
	}
	const String directory = p_path.get_base_dir();
	if (!DirAccess::exists(directory)) {
		// Create through the editor filesystem so the folder is known before the
		// PNG is imported.
		const Error dir_error = EditorFileSystem::get_singleton()->make_dir_recursive(directory);
		if (dir_error != OK && dir_error != ERR_ALREADY_EXISTS) {
			EditorNode::get_singleton()->show_warning(vformat(TTR("Could not create the folder %s (error %d)."), directory, dir_error));
			return false;
		}
	}
	const Error save_error = generated_texture_image->save_png(p_path);
	if (save_error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not save generated texture to %s (error %d)."), p_path, save_error));
		return false;
	}
	ensure_vfx_texture_import_settings(p_path, texture_generator_normal_map && texture_generator_normal_map->is_pressed());
	EditorFileSystem::get_singleton()->update_file(p_path);
	Vector<String> files_to_reimport;
	files_to_reimport.push_back(p_path);
	EditorFileSystem::get_singleton()->reimport_files(files_to_reimport);
	Ref<Texture2D> imported_texture = ResourceLoader::load(p_path, "Texture2D", ResourceFormatLoader::CACHE_MODE_REPLACE);
	if (imported_texture.is_null()) {
		EditorNode::get_singleton()->show_warning(TTR("The PNG was saved, but Godot could not load the imported texture."));
		return false;
	}
	generated_texture = imported_texture;
	generated_texture_path = p_path;
	// The recipe next to the PNG lets "Generate Texture" continue where it left off.
	_save_texture_recipe(p_path.get_basename() + ".vfx.cfg");
	return true;
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_apply_pressed() {
	if (texture_generator_normal_map->is_pressed() && !Object::cast_to<Decal>(_get_edited_emitter())) {
		EditorNode::get_singleton()->show_warning(TTR("Normal maps are exported as project textures and cannot be applied as an unlit particle color texture."));
		return;
	}
	if (!_get_edited_emitter()) {
		EditorNode::get_singleton()->show_warning(TTR("Select a particle emitter before applying a texture."));
		return;
	}
	if (generated_texture_path.is_empty()) {
		_regenerate_texture();
		if (!_export_generated_texture(_get_auto_texture_path())) {
			return;
		}
	}
	_apply_generated_texture();
	if (texture_generator_status && !generated_texture_path.is_empty()) {
		texture_generator_status->set_text(vformat(TTR("Applied %s to the selected emitter."), generated_texture_path.get_file()));
	}
}

void FPSStudioParticleEditorPlugin::_on_texture_generator_close_pressed() {
	if (texture_generator_dialog) {
		texture_generator_dialog->hide();
	}
}

void FPSStudioParticleEditorPlugin::_invalidate_texture_export() {
	generated_texture_path = String();
	if (texture_generator_apply_button) {
		texture_generator_apply_button->set_disabled(_get_edited_emitter() == nullptr);
		texture_generator_apply_button->set_tooltip_text(TTR("Render the texture, save it next to the effect (textures folder) and assign it to the selected emitter."));
	}
	if (texture_generator_status) {
		texture_generator_status->set_text(String());
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
	_show_texture_generator();
}

void FPSStudioParticleEditorPlugin::_on_texture_save_path_selected(const String &p_path) {
	String path = p_path;
	if (path.get_extension().to_lower() != "png") {
		path += ".png";
	}
	if (!_export_generated_texture(path)) {
		texture_generator_return_after_export = false;
		return;
	}
	if (texture_generator_apply_button) {
		texture_generator_apply_button->set_disabled((texture_generator_normal_map->is_pressed() && !Object::cast_to<Decal>(_get_edited_emitter())) || _get_edited_emitter() == nullptr);
	}
	if (texture_generator_status) {
		const String message = texture_generator_normal_map->is_pressed() ? TTR("Exported normal map: %s") : TTR("Exported: %s");
		texture_generator_status->set_text(vformat(message, path.get_file()));
	}
	callable_mp(this, &FPSStudioParticleEditorPlugin::_show_texture_generator_after_export).call_deferred();
}

void FPSStudioParticleEditorPlugin::_update_texture_time_controls() {
	if (!texture_generator_time) {
		return;
	}
	const int columns = texture_generator_output ? MAX(1, texture_generator_output->get_selected_id()) : 1;
	const bool is_sheet = columns > 1;
	if (texture_generator_fps_row) {
		texture_generator_fps_row->set_visible(is_sheet);
	}
	if (texture_generator_duration_row) {
		texture_generator_duration_row->set_visible(is_sheet);
	}
	if (texture_generator_show_sheet) {
		texture_generator_show_sheet->set_visible(is_sheet);
		if (!is_sheet && texture_generator_show_sheet->is_pressed()) {
			texture_generator_show_sheet->set_pressed_no_signal(false);
		}
	}
	const double duration = is_sheet && texture_generator_duration ? texture_generator_duration->get_value() : 1.0;
	texture_generator_time->set_min(0.0);
	texture_generator_time->set_max(duration);
	texture_generator_time->set_step(is_sheet ? duration / double(columns * columns) : 0.01);
	texture_generator_time_value = CLAMP(texture_generator_time_value, 0.0, duration);
	texture_generator_time->set_value_no_signal(texture_generator_time_value);
	if (texture_generator_time_label) {
		texture_generator_time_label->set_text(vformat("%s / %s s", String::num(texture_generator_time_value, 2), String::num(duration, 2)));
	}
	if (texture_generator_frame_label) {
		if (is_sheet) {
			const int frame_count = columns * columns;
			const int frame_index = CLAMP(int(Math::floor((texture_generator_time_value / MAX(0.001, duration)) * frame_count)), 0, frame_count - 1);
			texture_generator_frame_label->set_text(vformat(TTR("Frame %d / %d"), frame_index + 1, frame_count));
		} else {
			texture_generator_frame_label->set_text(vformat(TTR("Life %d%%"), int(Math::round(texture_generator_time_value / MAX(0.001, duration) * 100.0))));
		}
	}
}

void FPSStudioParticleEditorPlugin::_update_texture_preview() {
	if (!texture_generator_preview || !generated_texture.is_valid()) {
		return;
	}
	if (generated_texture_frame_columns <= 1 || (texture_generator_show_sheet && texture_generator_show_sheet->is_pressed())) {
		texture_generator_preview->set_texture(generated_texture);
		texture_generator_preview_frame = 0;
		return;
	}
	const int frame_count = generated_texture_frame_columns * generated_texture_frame_columns;
	const double duration = texture_generator_duration ? texture_generator_duration->get_value() : 2.0;
	const int frame_index = CLAMP(int(Math::floor((texture_generator_time_value / MAX(0.001, duration)) * frame_count)), 0, frame_count - 1);
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

void FPSStudioParticleEditorPlugin::_texture_preview_row_func(void *p_userdata, uint32_t p_row) {
	TexturePreviewJob *job = static_cast<TexturePreviewJob *>(p_userdata);
	VFXTextureGenerator::render_rows_rgba8(job->settings, job->size, job->buffer, int(p_row), int(p_row) + 1);
}

void FPSStudioParticleEditorPlugin::_start_texture_preview_task() {
	if (texture_preview_task >= 0 || !texture_preview_has_pending || !WorkerThreadPool::get_singleton()) {
		return;
	}
	texture_preview_job = texture_preview_pending;
	texture_preview_has_pending = false;
	texture_preview_job.bytes.resize(texture_preview_job.size * texture_preview_job.size * 4);
	texture_preview_job.buffer = texture_preview_job.bytes.ptrw();
	texture_preview_job.start_usec = OS::get_singleton()->get_ticks_usec();
	// Rows render on the pool without blocking the editor; NOTIFICATION_PROCESS
	// polls for completion.
	texture_preview_task = WorkerThreadPool::get_singleton()->add_native_group_task(&FPSStudioParticleEditorPlugin::_texture_preview_row_func, &texture_preview_job, texture_preview_job.size, -1, true, "VFX Texture Preview");
}

void FPSStudioParticleEditorPlugin::_poll_texture_preview_task(bool p_wait) {
	if (texture_preview_task < 0) {
		return;
	}
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	if (!p_wait && !pool->is_group_task_completed(texture_preview_task)) {
		return;
	}
	pool->wait_for_group_task_completion(texture_preview_task);
	texture_preview_task = -1;
	TexturePreviewJob &job = texture_preview_job;
	job.render_usec = OS::get_singleton()->get_ticks_usec() - job.start_usec;
	job.buffer = nullptr;
	if (!p_wait && texture_generator_preview && !(texture_generator_show_sheet && texture_generator_show_sheet->is_pressed())) {
		Ref<Image> image = Image::create_from_data(job.size, job.size, false, Image::FORMAT_RGBA8, job.bytes);
		process_particle_texture_frame(image, job.alpha_threshold, job.alpha_blur, job.toon_enabled, job.toon_dark, job.toon_light, job.shadows_tint, job.midtones_tint, job.highlights_tint, job.normal_map, job.normal_height_scale, job.show_alpha);
		// Reuse the GPU texture when the size matches; recreating it every frame is wasteful.
		if (texture_preview_texture.is_valid() && texture_preview_texture->get_width() == job.size && texture_preview_texture->get_height() == job.size) {
			texture_preview_texture->update(image);
		} else {
			texture_preview_texture = ImageTexture::create_from_image(image);
		}
		if (texture_generator_preview->get_texture() != texture_preview_texture) {
			texture_generator_preview->set_texture(texture_preview_texture);
		}
	}
	if (job.playback) {
		// Adapt the playback resolution to keep roughly 30+ preview frames per second.
		if (job.render_usec > 40000 && texture_preview_playback_size > 128) {
			texture_preview_playback_size -= 32;
		} else if (job.render_usec < 15000 && texture_preview_playback_size < 256) {
			texture_preview_playback_size += 32;
		}
	}
	if (p_wait) {
		return;
	}
	if (texture_preview_has_pending) {
		_start_texture_preview_task();
	} else if (texture_preview_needs_refine && job.settings.quality == 0) {
		texture_preview_needs_refine = false;
		texture_preview_pending = job;
		texture_preview_pending.settings.quality = 1;
		texture_preview_pending.size = job.refine_size;
		texture_preview_pending.playback = false;
		texture_preview_has_pending = true;
		_start_texture_preview_task();
	}
}

void FPSStudioParticleEditorPlugin::_regenerate_texture(bool p_preview_only) {
	if (!texture_generator_resolution || !texture_generator_output || !texture_generator_seed || !texture_generator_fps || !texture_generator_duration || !texture_generator_alpha_threshold || !texture_generator_alpha_blur || !texture_generator_alpha_preview || !texture_generator_normal_map || !texture_generator_shadows_tint || !texture_generator_midtones_tint || !texture_generator_highlights_tint || !texture_generator_toon) {
		return;
	}
	const int resolution_index = CLAMP(texture_generator_resolution->get_selected(), 0, 4);
	const int size = VFX_TEXTURE_RESOLUTIONS[resolution_index];
	const int output_id = texture_generator_output->get_selected_id();
	const int frame_columns = output_id == VFX_TEXTURE_OUTPUT_SHEET_8X8 ? 8 : (output_id == VFX_TEXTURE_OUTPUT_SHEET_4X4 ? 4 : 1);
	const int frame_size = size / frame_columns;
	const int frame_count = frame_columns * frame_columns;
	const int fps = MAX(1, int(texture_generator_fps->get_value()));
	const double duration = frame_columns > 1 ? texture_generator_duration->get_value() : 1.0;
	// A generator normal map is final data: no tints, toon steps or derived normals.
	const bool generator_normal = _uses_generator_normal_map();
	const bool toon_enabled = !generator_normal && texture_generator_toon->is_pressed();
	const real_t toon_dark = texture_generator_toon_dark->get_value();
	const real_t toon_light = texture_generator_toon_light->get_value();
	const real_t alpha_threshold = texture_generator_alpha_threshold->get_value();
	const real_t alpha_blur = texture_generator_alpha_blur->get_value();
	const Color shadows_tint = generator_normal ? Color(1, 1, 1) : texture_generator_shadows_tint->get_pick_color();
	const Color midtones_tint = generator_normal ? Color(1, 1, 1) : texture_generator_midtones_tint->get_pick_color();
	const Color highlights_tint = generator_normal ? Color(1, 1, 1) : texture_generator_highlights_tint->get_pick_color();
	const bool generate_normal_map = texture_generator_normal_map->is_pressed() && !generator_normal;
	const real_t normal_height_scale = texture_generator_normal_height_scale->get_value();
	const bool show_sheet = frame_columns > 1 && texture_generator_show_sheet && texture_generator_show_sheet->is_pressed();

	if (p_preview_only && !show_sheet) {
		const int frame_index = frame_columns == 1 ? 0 : CLAMP(int(Math::floor((texture_generator_time_value / MAX(0.001, duration)) * frame_count)), 0, frame_count - 1);
		const real_t sample_time = frame_columns == 1 ? texture_generator_time_value : (real_t(frame_index) / frame_count) * duration;
		const real_t life = frame_columns == 1 ? texture_generator_time_value / MAX(0.001, duration) : real_t(frame_index) / real_t(MAX(1, frame_count - 1));
		// While playing, render a draft at an adaptive size so the preview keeps
		// up. When paused (or after editing a value), a draft appears first and
		// is refined at full quality once the user stops changing things.
		const bool playing = texture_generator_animate && texture_generator_animate->is_pressed();
		TexturePreviewJob job;
		job.size = MIN(frame_size, playing ? texture_preview_playback_size : 256);
		job.refine_size = MIN(frame_size, 512);
		job.frame_index = frame_index;
		job.playback = playing;
		job.settings = _make_texture_settings(sample_time, life, 0);
		job.alpha_threshold = alpha_threshold;
		job.alpha_blur = alpha_blur;
		job.toon_enabled = toon_enabled;
		job.toon_dark = toon_dark;
		job.toon_light = toon_light;
		job.shadows_tint = shadows_tint;
		job.midtones_tint = midtones_tint;
		job.highlights_tint = highlights_tint;
		job.normal_map = generate_normal_map;
		job.normal_height_scale = normal_height_scale;
		job.show_alpha = texture_generator_alpha_preview->is_pressed();
		texture_preview_pending = job;
		texture_preview_has_pending = true;
		texture_preview_needs_refine = !playing;
		texture_generator_preview_frame = frame_index;
		if (texture_preview_task < 0) {
			_start_texture_preview_task();
		}
		return;
	}
	// A full render replaces whatever the preview is working on.
	_poll_texture_preview_task(true);
	texture_preview_has_pending = false;
	texture_preview_needs_refine = false;
	generated_texture_image = Image::create_empty(size, size, false, Image::FORMAT_RGBA8);
	for (int frame_index = 0; frame_index < frame_count; frame_index++) {
		const real_t sample_time = frame_columns == 1 ? texture_generator_time_value : (real_t(frame_index) / frame_count) * duration;
		const real_t life = frame_columns == 1 ? texture_generator_time_value / MAX(0.001, duration) : real_t(frame_index) / real_t(MAX(1, frame_count - 1));
		const VFXTextureGenerator::Settings settings = _make_texture_settings(sample_time, life, 1);
		Ref<Image> frame_image = render_vfx_texture_frame(settings, frame_size);
		process_particle_texture_frame(frame_image, alpha_threshold, alpha_blur, toon_enabled, toon_dark, toon_light, shadows_tint, midtones_tint, highlights_tint, generate_normal_map, normal_height_scale, false);
		const int origin_x = (frame_index % frame_columns) * frame_size;
		const int origin_y = (frame_index / frame_columns) * frame_size;
		generated_texture_image->blit_rect(frame_image, Rect2i(0, 0, frame_size, frame_size), Point2i(origin_x, origin_y));
	}
	generated_texture_frame_columns = frame_columns;
	generated_texture_fps = fps;
	generated_texture = ImageTexture::create_from_image(generated_texture_image);
	generated_texture_path = String();
	texture_generator_preview_frame = -1;
	if (texture_generator_alpha_preview->is_pressed() && !show_sheet) {
		_regenerate_texture(true);
	} else {
		_update_texture_preview();
	}
	if (texture_generator_apply_button) {
		texture_generator_apply_button->set_disabled(_get_edited_emitter() == nullptr);
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
	if (Decal *decal = Object::cast_to<Decal>(emitter)) {
		// Decal tracks take the texture directly (albedo, or normal map).
		const bool normal = texture_generator_normal_map->is_pressed();
		EditorUndoRedoManager *decal_undo = get_undo_redo();
		decal_undo->create_action_for_history(normal ? TTR("Apply Generated Decal Normal Map") : TTR("Apply Generated Decal Texture"), EditorUndoRedoManager::GLOBAL_HISTORY);
		decal_undo->force_fixed_history();
		const StringName property = normal ? StringName("texture_normal") : StringName("texture_albedo");
		decal_undo->add_do_property(decal, property, generated_texture);
		decal_undo->add_undo_property(decal, property, decal->get(property));
		decal_undo->add_do_method(this, "_on_emitter_hierarchy_history_changed", emitter, effect_document_serial);
		decal_undo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", emitter, effect_document_serial);
		decal_undo->commit_action(); // The history callback refreshes the UI.
		return;
	}
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(emitter);
	CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(emitter);
	const int frames = generated_texture_frame_columns;
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	// Particle documents are not scenes: keep node and resource edits in one (global) history.
	undo_redo->create_action_for_history(TTR("Apply Generated Texture"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	auto set_prop = [&](Object *p_object, const StringName &p_property, const Variant &p_value) {
		undo_redo->add_do_property(p_object, p_property, p_value);
		undo_redo->add_undo_property(p_object, p_property, p_object->get(p_property));
	};
	// Reuse the emitter's particle material so blend mode, tint and soft
	// particle settings survive; create one only when there is none.
	Ref<Material> existing = FPSParticleModuleStack::get_draw_material(emitter);
	BaseMaterial3D *material = Object::cast_to<BaseMaterial3D>(existing.ptr());
	if (material) {
		set_prop(material, "albedo_texture", generated_texture);
		set_prop(material, "billboard_mode", int(BaseMaterial3D::BILLBOARD_PARTICLES));
		if (material->get_transparency() == BaseMaterial3D::TRANSPARENCY_DISABLED) {
			set_prop(material, "transparency", int(BaseMaterial3D::TRANSPARENCY_ALPHA));
		}
		set_prop(material, "particles_anim_h_frames", frames);
		set_prop(material, "particles_anim_v_frames", frames);
		set_prop(material, "particles_anim_loop", frames > 1);
	} else {
		Ref<StandardMaterial3D> new_material;
		new_material.instantiate();
		new_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		new_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		new_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		new_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		new_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
		new_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, generated_texture);
		new_material->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
		new_material->set_particles_anim_h_frames(frames);
		new_material->set_particles_anim_v_frames(frames);
		new_material->set_particles_anim_loop(frames > 1);
		Ref<Mesh> mesh = FPSParticleModuleStack::get_draw_mesh(emitter);
		Ref<Mesh> new_mesh = mesh.is_valid() ? Ref<Mesh>(mesh->duplicate()) : Ref<Mesh>(create_particle_quad(0.5));
		if (PrimitiveMesh *primitive = Object::cast_to<PrimitiveMesh>(new_mesh.ptr())) {
			primitive->set_material(new_material);
		} else {
			for (int surface = 0; surface < new_mesh->get_surface_count(); surface++) {
				new_mesh->surface_set_material(surface, new_material);
			}
		}
		set_prop(emitter, gpu ? StringName("draw_pass_1") : StringName("mesh"), new_mesh);
	}
	if (frames > 1) {
		// anim_speed 1 plays the whole sheet once over a particle's life. Life
		// cycle sheets (explosions) map exactly onto the lifetime; looping sheets
		// play at their authored duration.
		const int lifecycle_index = VFXTextureGenerator::find_param(VFXTextureGenerator::get_type_info(texture_generator_type), "lifecycle");
		const bool lifecycle = lifecycle_index >= 0 && texture_generator_params[lifecycle_index] > 0.0f;
		const double lifetime = double(emitter->get("lifetime"));
		const double sheet_duration = texture_generator_duration ? texture_generator_duration->get_value() : 1.0;
		const double speed = lifecycle ? 1.0 : MAX(0.01, lifetime / MAX(0.01, sheet_duration));
		if (gpu) {
			Ref<Material> process_resource = gpu->get_process_material();
			if (ParticleProcessMaterial *process_material = Object::cast_to<ParticleProcessMaterial>(process_resource.ptr())) {
				set_prop(process_material, "anim_speed", Vector2(speed, speed));
			}
		} else if (cpu) {
			set_prop(cpu, "anim_speed_min", speed);
			set_prop(cpu, "anim_speed_max", speed);
		}
		if (material) {
			set_prop(material, "particles_anim_loop", !lifecycle);
		}
	}
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", emitter, effect_document_serial);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", emitter, effect_document_serial);
	undo_redo->commit_action(); // The history callback refreshes the UI.
}

// Renders a preset texture (a still or a columns x columns sheet) in memory.
static Ref<Image> render_preset_texture_atlas(const VFXTextureGenerator::Settings &p_settings, int p_resolution, int p_columns, double p_duration) {
	const int columns = MAX(1, p_columns);
	const int frame_size = MAX(1, p_resolution / columns);
	const int frame_count = columns * columns;
	Ref<Image> atlas = Image::create_empty(frame_size * columns, frame_size * columns, false, Image::FORMAT_RGBA8);
	for (int frame = 0; frame < frame_count; frame++) {
		VFXTextureGenerator::Settings settings = p_settings;
		settings.quality = 1;
		if (frame_count > 1) {
			settings.time = float(double(frame) / frame_count * p_duration);
			settings.life = float(frame) / float(frame_count - 1);
		}
		Ref<Image> image = render_vfx_texture_frame(settings, frame_size);
		atlas->blit_rect(image, Rect2i(0, 0, frame_size, frame_size), Point2i((frame % columns) * frame_size, (frame / columns) * frame_size));
	}
	return atlas;
}

Ref<Texture2D> FPSStudioParticleEditorPlugin::_bake_preset_texture(const VFXTextureGenerator::Settings &p_settings, const String &p_file_name, int p_resolution, int p_columns, double p_duration) {
	const String directory = "res://vfx_textures";
	const String path = directory.path_join(p_file_name + ".png");
	// Presets share textures; reuse a previously baked (or user-edited) file.
	if (FileAccess::exists(path)) {
		Ref<Texture2D> existing = ResourceLoader::load(path, "Texture2D");
		if (existing.is_valid()) {
			return existing;
		}
	}
	const int columns = MAX(1, p_columns);
	Ref<Image> atlas = render_preset_texture_atlas(p_settings, p_resolution, columns, p_duration);
	if (!DirAccess::exists(directory)) {
		EditorFileSystem::get_singleton()->make_dir_recursive(directory);
	}
	if (atlas->save_png(path) != OK) {
		return Ref<Texture2D>();
	}
	ensure_vfx_texture_import_settings(path);
	EditorFileSystem::get_singleton()->update_file(path);
	Vector<String> files;
	files.push_back(path);
	EditorFileSystem::get_singleton()->reimport_files(files);
	// Recipe next to the PNG, so "Generate Texture" can refine it later.
	Ref<ConfigFile> recipe;
	recipe.instantiate();
	recipe->set_value("generator", "version", 3);
	recipe->set_value("generator", "type", p_settings.type);
	recipe->set_value("generator", "resolution", p_resolution);
	recipe->set_value("generator", "output_layout", columns == 1 ? int(VFX_TEXTURE_OUTPUT_STILL) : columns);
	recipe->set_value("generator", "seed", int(p_settings.seed));
	recipe->set_value("generator", "fps", 12);
	recipe->set_value("generator", "time", double(p_settings.time));
	recipe->set_value("generator", "duration", p_duration);
	recipe->set_value("generator", "color_a", Color(p_settings.color_a[0], p_settings.color_a[1], p_settings.color_a[2]));
	recipe->set_value("generator", "color_b", Color(p_settings.color_b[0], p_settings.color_b[1], p_settings.color_b[2]));
	recipe->set_value("generator", "speed", double(p_settings.speed));
	recipe->set_value("generator", "rotation", double(p_settings.rotation_degrees));
	recipe->set_value("generator", "exposure", double(p_settings.exposure));
	recipe->set_value("generator", "opacity", double(p_settings.opacity));
	const VFXTextureGenerator::TypeInfo *info = VFXTextureGenerator::get_type_info(p_settings.type);
	if (info) {
		for (int i = 0; i < info->param_count; i++) {
			recipe->set_value("params", String::utf8(info->params[i].key), p_settings.params[i]);
		}
	}
	recipe->save(path.get_basename() + ".vfx.cfg");
	return ResourceLoader::load(path, "Texture2D", ResourceFormatLoader::CACHE_MODE_REPLACE);
}

// Script / tool API: writes a preset as an effect scene, with its textures and
// sounds baked into the project like the Library does.
Error FPSStudioParticleEditorPlugin::bake_preset_scene(int p_preset, const String &p_path) {
	ERR_FAIL_INDEX_V(p_preset, FPSParticlePresets::PRESET_MAX, ERR_INVALID_PARAMETER);
	Vector<FPSParticlePresets::TextureRequest> textures;
	ParticleEffect3D *effect = FPSParticlePresets::create_preset(p_preset, textures);
	ERR_FAIL_NULL_V(effect, FAILED);
	_apply_preset_textures(textures, true);
	_apply_preset_sounds(effect, true);
	set_scene_subtree_owners(effect, effect);
	Ref<PackedScene> packed_scene;
	packed_scene.instantiate();
	Error error = packed_scene->pack(effect);
	memdelete(effect);
	if (error == OK) {
		error = ResourceSaver::save(packed_scene, p_path);
	}
	return error;
}

void FPSStudioParticleEditorPlugin::_create_preset_document(int p_preset) {
	Vector<FPSParticlePresets::TextureRequest> textures;
	ParticleEffect3D *effect = FPSParticlePresets::create_preset(p_preset, textures);
	if (!effect) {
		return;
	}
	_apply_preset_textures(textures, true);
	_apply_preset_sounds(effect, true);
	_set_effect_document(effect, String(), true);
}

void FPSStudioParticleEditorPlugin::_apply_preset_sounds(Node *p_node, bool p_bake) {
	// Presets describe their sounds as generator recipes; bake them into
	// res://vfx_sounds once (a user-edited file of the same name is reused).
	if (!p_node) {
		return;
	}
	if (AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(p_node)) {
		if (player->has_meta(FPSParticlePresets::SOUND_RECIPE_META)) {
			const Dictionary recipe = player->get_meta(FPSParticlePresets::SOUND_RECIPE_META);
			player->remove_meta(FPSParticlePresets::SOUND_RECIPE_META);
			if (p_bake) { // Thumbnails strip sounds; nothing to do there.
				const int sound = recipe.get("sound", 0);
				const uint32_t seed = uint32_t(int64_t(recipe.get("seed", 0)));
				const String directory = "res://vfx_sounds";
				const String path = directory.path_join(String(recipe.get("file", "sound")) + ".wav");
				if (!FileAccess::exists(path)) {
					if (!DirAccess::exists(directory)) {
						EditorFileSystem::get_singleton()->make_dir_recursive(directory);
					}
					const Ref<AudioStreamWAV> generated = VFXSoundGenerator::generate(sound, seed);
					if (generated.is_valid() && VFXSoundGenerator::save_wav(generated, path) == OK) {
						EditorFileSystem::get_singleton()->update_file(path);
						Vector<String> files;
						files.push_back(path);
						EditorFileSystem::get_singleton()->reimport_files(files);
					}
				}
				Ref<AudioStream> stream;
				if (ResourceLoader::exists(path, "AudioStream")) {
					stream = ResourceLoader::load(path, "AudioStream");
				}
				if (stream.is_null()) {
					stream = VFXSoundGenerator::generate(sound, seed); // Import not ready: keep it in memory.
				}
				player->set_stream(stream);
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_apply_preset_sounds(p_node->get_child(i), p_bake);
	}
}

void FPSStudioParticleEditorPlugin::_apply_preset_textures(const Vector<FPSParticlePresets::TextureRequest> &p_textures, bool p_bake) {
	for (const FPSParticlePresets::TextureRequest &request : p_textures) {
		Ref<Texture2D> texture;
		if (p_bake) {
			texture = _bake_preset_texture(request.settings, request.file_name, request.resolution, request.columns, request.sheet_duration);
		} else {
			// Thumbnails: reuse a texture already baked into the project, else a
			// small in-memory render; nothing is written to the project.
			const String path = String("res://vfx_textures").path_join(request.file_name + ".png");
			if (ResourceLoader::exists(path, "Texture2D")) {
				texture = ResourceLoader::load(path, "Texture2D");
			}
			if (texture.is_null()) {
				Ref<Image> atlas = render_preset_texture_atlas(request.settings, MIN(request.resolution, request.columns > 1 ? 256 : 128), request.columns, request.sheet_duration);
				if (atlas.is_valid()) {
					texture = ImageTexture::create_from_image(atlas);
				}
			}
		}
		if (Decal *decal = Object::cast_to<Decal>(request.emitter)) {
			if (texture.is_valid()) {
				decal->set_texture(Decal::TEXTURE_ALBEDO, texture);
			}
			continue;
		}
		Ref<Material> material = FPSParticleModuleStack::get_draw_material(request.emitter);
		BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(material.ptr());
		if (ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(material.ptr())) {
			// Preset shader materials (e.g. clipped at the water surface)
			// take the texture as their albedo_texture uniform.
			if (texture.is_valid()) {
				shader_material->set_shader_parameter("albedo_texture", texture);
				shader_material->set_shader_parameter("anim_frames", MAX(1, request.columns));
			}
			// The shader picks the frame from the particle's animation value:
			// random variants get a fixed random offset, sheets play over life.
			if (request.columns > 1) {
				if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(request.emitter)) {
					Ref<Material> process = gpu->get_process_material();
					if (process.is_valid()) {
						if (request.random_frame) {
							process->set("anim_speed", Vector2(0.0, 0.0));
							process->set("anim_offset", Vector2(0.0, 1.0));
						} else if (Vector2(process->get("anim_speed")) == Vector2()) {
							process->set("anim_speed", Vector2(1.0, 1.0));
						}
					}
				}
			}
			continue;
		}
		if (texture.is_null() || !base_material) {
			continue;
		}
		base_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, texture);
		if (request.columns > 1) {
			base_material->set_particles_anim_h_frames(request.columns);
			base_material->set_particles_anim_v_frames(request.columns);
			base_material->set_particles_anim_loop(!request.lifecycle);
			if (request.random_frame) {
				// Variants: a random frame per particle that does not change.
				base_material->set_particles_anim_loop(false);
				if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(request.emitter)) {
					Ref<Material> process = gpu->get_process_material();
					if (process.is_valid()) {
						process->set("anim_speed", Vector2(0.0, 0.0));
						process->set("anim_offset", Vector2(0.0, 1.0));
					}
				}
			} else if (request.lifecycle) {
				// Life-cycle sheets play once over each particle's life.
				if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(request.emitter)) {
					Ref<Material> process = gpu->get_process_material();
					if (process.is_valid() && Vector2(process->get("anim_speed")) == Vector2()) {
						process->set("anim_speed", Vector2(1.0, 1.0));
					}
				}
			} else if (!request.lifecycle) {
				const double lifetime = double(request.emitter->get("lifetime"));
				const double speed = MAX(0.01, lifetime / MAX(0.01, request.sheet_duration));
				if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(request.emitter)) {
					Ref<Material> process = gpu->get_process_material();
					if (process.is_valid()) {
						process->set("anim_speed", Vector2(speed, speed));
					}
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Thumbnails

static String get_effect_thumbnail_cache_path(const String &p_path) {
	// Same file the editor's scene previewer reads (EditorPackedScenePreviewPlugin).
	const String cache_base = ProjectSettings::get_singleton()->globalize_path(p_path).md5_text();
	return EditorPaths::get_singleton()->get_cache_dir().path_join("resthumb-" + cache_base + ".png");
}

// Preset thumbnails are cached on disk per project (presets reuse the
// project's baked textures), keyed by the editor build so a new build with
// changed presets renders them again once, in the background.
static constexpr int PRESET_THUMBNAIL_VERSION = 2;

static String get_preset_thumbnail_cache_path(int p_preset) {
	const String build = String(GODOT_VERSION_HASH).left(12) + "_" + itos(PRESET_THUMBNAIL_VERSION);
	return EditorPaths::get_singleton()->get_project_data_dir().path_join("fps_particle_thumbnails").path_join(vformat("preset_%d_%s.png", p_preset, build));
}

static AABB get_emitter_framing_bounds(Node *p_emitter, const AABB &p_visibility);

Ref<Texture2D> FPSStudioParticleEditorPlugin::_get_preset_thumbnail(int p_preset) {
	const String key = vformat("preset:%d", p_preset);
	if (thumbnails.has(key)) {
		return thumbnails[key];
	}
	const String cache_path = get_preset_thumbnail_cache_path(p_preset);
	if (!FileAccess::exists(cache_path)) {
		return Ref<Texture2D>();
	}
	Ref<Image> image = Image::load_from_file(cache_path);
	if (image.is_null() || image->is_empty()) {
		return Ref<Texture2D>();
	}
	const Ref<Texture2D> texture = ImageTexture::create_from_image(image);
	thumbnails[key] = texture;
	return texture;
}

static void strip_thumbnail_nodes(Node *p_node) {
	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		Node *child = p_node->get_child(i);
		// No sound, camera shake or damage from a thumbnail.
		if (child->is_class("CameraShake3D") || child->is_class("RadialDamage3D")) {
			p_node->remove_child(child);
			memdelete(child);
			continue;
		}
		if (AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(child)) {
			player->set_stream(Ref<AudioStream>());
		}
		strip_thumbnail_nodes(child);
	}
}

// Dark backdrop, ambient and key light shared by the Library thumbnails and
// the hover preview. Returns the camera.
static Camera3D *create_library_preview_world(Node3D *p_root) {
	Ref<Environment> environment;
	environment.instantiate();
	environment->set_background(Environment::BG_COLOR);
	environment->set_bg_color(Color(0.035, 0.046, 0.065));
	environment->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	environment->set_ambient_light_color(Color(0.48, 0.53, 0.64));
	environment->set_ambient_light_energy(0.72);
	environment->set_tonemapper(Environment::TONE_MAPPER_FILMIC);
	environment->set_glow_enabled(true);
	WorldEnvironment *world_environment = memnew(WorldEnvironment);
	world_environment->set_environment(environment);
	p_root->add_child(world_environment);
	DirectionalLight3D *key_light = memnew(DirectionalLight3D);
	key_light->set_rotation(Vector3(Math::deg_to_rad(-38.0), Math::deg_to_rad(-26.0), 0.0));
	key_light->set_param(Light3D::PARAM_ENERGY, 1.45);
	p_root->add_child(key_light);
	Camera3D *camera = memnew(Camera3D);
	camera->set_fov(50.0);
	p_root->add_child(camera);
	camera->set_current(true);
	return camera;
}

// Frames a Library effect (already in the tree): the camera looks at where
// its particles are; r_shot_time is a moment with most of it on screen and
// r_play_length how long a looping hover preview plays before restarting.
static void frame_library_effect(Node *p_effect, Camera3D *p_camera, double &r_shot_time, double &r_play_length) {
	Vector<Node *> tracks;
	collect_tracks(p_effect, tracks);
	double duration = 0.1;
	double max_start = 0.0;
	double max_lifetime = 0.1;
	double particle_start = 1e20;
	double particle_end = 0.0;
	bool infinite = false;
	AABB bounds;
	bool has_bounds = false;
	for (Node *track : tracks) {
		Node3D *track_3d = Object::cast_to<Node3D>(track);
		if (track_3d && !track_3d->is_visible()) {
			continue;
		}
		double start, active_end, visual_end;
		ParticleEffect3D::get_track_window(track, start, active_end, visual_end);
		max_start = MAX(max_start, start);
		if (visual_end < 0.0) {
			infinite = true;
		} else {
			duration = MAX(duration, visual_end);
		}
		if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(track)) {
			max_lifetime = MAX(max_lifetime, gpu->get_lifetime());
			particle_start = MIN(particle_start, start);
			particle_end = MAX(particle_end, visual_end);
			// Where the particles are, not the generous visibility box.
			const AABB box = gpu->get_global_transform().xform(get_emitter_framing_bounds(gpu, gpu->get_visibility_aabb()));
			bounds = has_bounds ? bounds.merge(box) : box;
			has_bounds = true;
		} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(track)) {
			max_lifetime = MAX(max_lifetime, cpu->get_lifetime());
			particle_start = MIN(particle_start, start);
			particle_end = MAX(particle_end, visual_end);
		}
	}
	// Long events (a 12 s bullet hole decal) must not push the shot past the
	// particles: take it early in the particles' life.
	r_shot_time = infinite ? max_start + max_lifetime * 0.9 : duration * 0.33;
	if (!infinite && particle_start < duration) {
		r_shot_time = MIN(r_shot_time, particle_start + max_lifetime * 0.3);
	}
	// Replay once the particles are gone (decals and lights may last longer).
	r_play_length = infinite ? 6.0 : CLAMP((particle_end > 0.0 ? particle_end : duration) + 0.4, 0.8, 8.0);
	if (!has_bounds) {
		bounds = AABB(Vector3(-1, -1, -1), Vector3(2, 2, 2));
	}
	const real_t radius = MAX(real_t(0.25), bounds.size.length() * 0.5);
	const real_t distance = radius / Math::tan(Math::deg_to_rad(real_t(p_camera->get_fov()) * 0.5));
	const Vector3 direction = Vector3(0.0, 0.3, 1.0).normalized();
	p_camera->look_at_from_position(bounds.get_center() + direction * distance, bounds.get_center(), Vector3(0, 1, 0));
	p_camera->set_far(MAX(real_t(100.0), distance * 4.0));
}

void FPSStudioParticleEditorPlugin::_ensure_thumbnail_viewport() {
	if (thumbnail_viewport) {
		return;
	}
	thumbnail_viewport = memnew(SubViewport);
	thumbnail_viewport->set_name("ParticleThumbnailViewport");
	thumbnail_viewport->set_size(Vector2i(128, 128));
	thumbnail_viewport->set_use_own_world_3d(true);
	thumbnail_viewport->set_transparent_background(false);
	thumbnail_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	thumbnail_viewport->set_msaa_3d(Viewport::MSAA_4X);
	add_child(thumbnail_viewport);
	thumbnail_root = memnew(Node3D);
	thumbnail_viewport->add_child(thumbnail_root);
	thumbnail_camera = create_library_preview_world(thumbnail_root);
}

// What the Content Browser can preview, and how.
enum AssetPreviewKind {
	ASSET_PREVIEW_NONE,
	ASSET_PREVIEW_EFFECT, // ParticleEffect3D scene: plays in a loop.
	ASSET_PREVIEW_SCENE, // 3D scene or imported model: turntable (old particle effects loop instead).
	ASSET_PREVIEW_LEVEL, // Level: static view of the whole level.
	ASSET_PREVIEW_MATERIAL, // 3D material on a turning sphere.
	ASSET_PREVIEW_SHADER, // Spatial shader on a turning sphere.
	ASSET_PREVIEW_MESH, // Mesh resource (.obj, saved meshes): turntable tooltip, Godot's thumbnail.
};

// Bump to render every Content Browser thumbnail again.
static constexpr int ASSET_THUMBNAIL_VERSION = 2;

static bool is_level_scene_path(const String &p_path) {
	const String lower = p_path.to_lower();
	return lower.contains("/levels/") || lower.get_file().contains("level");
}

// Reads only file text, so the whole project can be classified without
// loading scenes.
static int get_asset_preview_kind(const String &p_path, const String &p_type) {
	const String ext = p_path.get_extension().to_lower();
	if (p_type == "PackedScene") {
		if (ext == "tscn") {
			const String text = FileAccess::get_file_as_string(p_path);
			const int node = text.find("\n[node ");
			if (node < 0) {
				return ASSET_PREVIEW_NONE;
			}
			const int end = text.find("]", node);
			const String header = end > node ? text.substr(node, end - node) : String();
			String root_type;
			const int type_at = header.find("type=\"");
			if (type_at >= 0) {
				root_type = header.substr(type_at + 6, header.find("\"", type_at + 6) - type_at - 6);
			}
			if (root_type == "ParticleEffect3D") {
				return ASSET_PREVIEW_EFFECT;
			}
			// An inherited / instanced root has no type: guess from its nodes.
			const bool is_3d = root_type.is_empty() ? text.contains("3D\"") : ClassDB::is_parent_class(root_type, "Node3D");
			if (!is_3d) {
				return ASSET_PREVIEW_NONE;
			}
			return is_level_scene_path(p_path) ? ASSET_PREVIEW_LEVEL : ASSET_PREVIEW_SCENE;
		}
		if (ext == "glb" || ext == "gltf" || ext == "fbx" || ext == "blend" || ext == "dae" || ext == "obj") {
			return ASSET_PREVIEW_SCENE; // Imported 3D model.
		}
		return ASSET_PREVIEW_NONE;
	}
	if (ClassDB::is_parent_class(p_type, "BaseMaterial3D") || p_type == "ShaderMaterial") {
		return ASSET_PREVIEW_MATERIAL;
	}
	if (ClassDB::is_parent_class(p_type, "Mesh")) {
		return ASSET_PREVIEW_MESH;
	}
	if (p_type == "Shader" && ext == "gdshader") {
		const String text = FileAccess::get_file_as_string(p_path);
		const int at = text.find("shader_type");
		if (at >= 0 && text.substr(at, 40).replace(" ", "").replace("\t", "").begins_with("shader_typespatial")) {
			return ASSET_PREVIEW_SHADER;
		}
	}
	return ASSET_PREVIEW_NONE;
}

void FPSStudioParticleEditorPlugin::_queue_thumbnail(const String &p_key, const String &p_path, int p_preset, bool p_write_cache, int p_kind) {
	if (thumbnail_queued.has(p_key) || (thumbnail_frames >= 0 && thumbnail_current.key == p_key)) {
		return;
	}
	if (DisplayServer::get_singleton()->get_name() == "headless") {
		return; // No renderer to capture from (same rule as EditorResourcePreview).
	}
	ThumbnailJob job;
	job.key = p_key;
	job.path = p_path;
	job.preset = p_preset;
	job.write_cache = p_write_cache;
	job.kind = p_kind;
	thumbnail_queue.push_back(job);
	thumbnail_queued.insert(p_key);
}

Ref<Texture2D> FPSStudioParticleEditorPlugin::_get_cached_effect_thumbnail(const String &p_path) {
	const uint64_t modified = FileAccess::get_modified_time(p_path);
	if (thumbnails.has(p_path) && thumbnail_mtimes.has(p_path) && thumbnail_mtimes[p_path] == modified) {
		return thumbnails[p_path];
	}
	// A thumbnail from an earlier session, newer than the effect file.
	const String cache_path = get_effect_thumbnail_cache_path(p_path);
	if (FileAccess::exists(cache_path) && FileAccess::get_modified_time(cache_path) >= modified) {
		Ref<Image> image;
		image.instantiate();
		if (image->load(cache_path) == OK) {
			Ref<Texture2D> texture = ImageTexture::create_from_image(image);
			thumbnails[p_path] = texture;
			thumbnail_mtimes[p_path] = modified;
			return texture;
		}
	}
	return Ref<Texture2D>();
}

void FPSStudioParticleEditorPlugin::_queue_library_thumbnails() {
	for (int preset : FPSParticlePresets::get_presets_in_display_order()) {
		if (_get_preset_thumbnail(preset).is_null()) {
			_queue_thumbnail(vformat("preset:%d", preset), String(), preset, false);
		}
	}
	for (const String &path : recent_effect_paths) {
		if (FileAccess::exists(path) && _get_cached_effect_thumbnail(path).is_null()) {
			_queue_thumbnail(path, path, -1, true);
		}
	}
}

void FPSStudioParticleEditorPlugin::_set_library_icon(const String &p_key, const Ref<Texture2D> &p_texture) {
	if (library_menu) {
		PopupMenu *popup = library_menu->get_popup();
		int id = -1;
		if (p_key.begins_with("preset:")) {
			id = PARTICLE_LIBRARY_PRESET_BASE + p_key.get_slicec(':', 1).to_int();
		} else {
			const int index = recent_effect_paths.find(p_key);
			id = index >= 0 ? PARTICLE_LIBRARY_RECENT_BASE + index : -1;
		}
		const int item = id >= 0 ? popup->get_item_index(id) : -1;
		if (item >= 0) {
			popup->set_item_icon(item, p_texture);
			popup->set_item_icon_max_width(item, int(32 * EDSCALE));
		}
	}
	if (!effects_tree || !effects_tree->get_root()) {
		return;
	}
	for (TreeItem *item = effects_tree->get_root(); item; item = item->get_next_in_tree()) {
		if (item->get_metadata(0).get_type() == Variant::STRING && String(item->get_metadata(0)) == p_key) {
			item->set_icon(0, p_texture);
			item->set_icon_max_width(0, int(40 * EDSCALE));
		}
	}
}

// A Library entry ("preset:<id>" or an effect path) instanced for a
// thumbnail or the hover preview: no scripts, sound, shake or damage.
Node *FPSStudioParticleEditorPlugin::_instantiate_library_effect(const String &p_key) {
	Node *effect = nullptr;
	if (p_key.begins_with("preset:")) {
		Vector<FPSParticlePresets::TextureRequest> textures;
		effect = FPSParticlePresets::create_preset(p_key.get_slicec(':', 1).to_int(), textures);
		if (effect) {
			_apply_preset_textures(textures, false);
			_apply_preset_sounds(effect, false);
		}
	} else if (!p_key.is_empty() && ResourceLoader::exists(p_key, "PackedScene")) {
		Ref<PackedScene> packed_scene = ResourceLoader::load(p_key, "PackedScene");
		if (packed_scene.is_valid() && packed_scene->can_instantiate()) {
			effect = packed_scene->instantiate();
			if (effect && !Object::cast_to<ParticleEffect3D>(effect)) {
				memdelete(effect); // Only particle effects get a particle preview.
				effect = nullptr;
			}
		}
	}
	if (!effect) {
		return nullptr;
	}
	clear_preview_scripts(effect);
	strip_thumbnail_nodes(effect);
	apply_preview_defaults(effect, preview_quad);
	clear_preview_transform_origin(effect);
	return effect;
}

static void set_preview_particles_preprocess(Node *p_node, double p_ratio);
static void frame_preview_subject(Node *p_subject, Camera3D *p_camera, int p_kind, AABB &r_bounds, double &r_play_length, bool &r_has_particles);

bool FPSStudioParticleEditorPlugin::_start_thumbnail_job(const ThumbnailJob &p_job) {
	if (p_job.preset < 0 && p_job.kind != ASSET_PREVIEW_NONE && p_job.kind != ASSET_PREVIEW_EFFECT) {
		Node *subject = _instantiate_preview_subject(p_job.path, p_job.kind);
		if (!subject) {
			return false;
		}
		// Particles of old effects: show them mid-way through their life.
		set_preview_particles_preprocess(subject, 0.35);
		_ensure_thumbnail_viewport();
		thumbnail_root->add_child(subject);
		thumbnail_effect_id = subject->get_instance_id();
		AABB bounds;
		double play_length = 0.0;
		bool has_particles = false;
		frame_preview_subject(subject, thumbnail_camera, p_job.kind, bounds, play_length, has_particles);
		thumbnail_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
		return true;
	}
	Node *effect = _instantiate_library_effect(p_job.preset >= 0 ? vformat("preset:%d", p_job.preset) : p_job.path);
	if (!effect) {
		return false;
	}
	_ensure_thumbnail_viewport();
	thumbnail_root->add_child(effect);
	thumbnail_effect_id = effect->get_instance_id();
	double time = 0.0;
	double play_length = 0.0;
	frame_library_effect(effect, thumbnail_camera, time, play_length);
	if (ParticleEffect3D *particle_effect = Object::cast_to<ParticleEffect3D>(effect)) {
		particle_effect->seek(time);
	}
	thumbnail_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	return true;
}

// ---------------------------------------------------------------------------
// Hover preview: resting the mouse on a Library item shows a bigger preview
// beside it that plays the effect in a loop.

void FPSStudioParticleEditorPlugin::_ensure_hover_preview() {
	if (hover_preview_popup) {
		return;
	}
	// Like a tooltip: never takes focus, so the Library menu stays open.
	hover_preview_popup = memnew(PopupPanel);
	hover_preview_popup->set_name("ParticleLibraryHoverPreview");
	hover_preview_popup->set_flag(Window::FLAG_NO_FOCUS, true);
	hover_preview_popup->set_flag(Window::FLAG_POPUP, false);
	hover_preview_popup->set_flag(Window::FLAG_MOUSE_PASSTHROUGH, true);
	hover_preview_popup->set_transient(true);
	add_child(hover_preview_popup);
	VBoxContainer *box = memnew(VBoxContainer);
	hover_preview_popup->add_child(box);
	SubViewportContainer *container = memnew(SubViewportContainer);
	container->set_stretch(true);
	const int size = int(HOVER_PREVIEW_SIZE * EDSCALE);
	container->set_custom_minimum_size(Size2(size, size));
	box->add_child(container);
	hover_preview_viewport = memnew(SubViewport);
	hover_preview_viewport->set_use_own_world_3d(true);
	hover_preview_viewport->set_msaa_3d(Viewport::MSAA_4X);
	hover_preview_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	container->add_child(hover_preview_viewport);
	hover_preview_root = memnew(Node3D);
	hover_preview_viewport->add_child(hover_preview_root);
	hover_preview_camera = create_library_preview_world(hover_preview_root);
	hover_preview_label = memnew(Label);
	hover_preview_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	hover_preview_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	hover_preview_label->set_custom_minimum_size(Size2(size, 0));
	box->add_child(hover_preview_label);
	hover_preview_description = memnew(Label);
	hover_preview_description->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	hover_preview_description->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	hover_preview_description->set_custom_minimum_size(Size2(size, 0));
	hover_preview_description->add_theme_color_override(SceneStringName(font_color), EditorNode::get_singleton()->get_gui_base()->get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
	hover_preview_description->add_theme_font_size_override(SceneStringName(font_size), int(EditorNode::get_singleton()->get_gui_base()->get_theme_font_size(SceneStringName(font_size), SNAME("Label")) * 0.9));
	box->add_child(hover_preview_description);
}

String FPSStudioParticleEditorPlugin::_get_library_hover(Rect2 &r_anchor) const {
	// Library menu: the highlighted item, anchored to the menu's left edge.
	if (library_menu && library_menu->get_popup()->is_visible()) {
		PopupMenu *popup = library_menu->get_popup();
		const int index = popup->get_focused_item();
		const Rect2 popup_rect(popup->get_position(), popup->get_size());
		if (index < 0 || !popup_rect.has_point(Point2(popup->get_position()) + popup->get_mouse_position())) {
			return String();
		}
		const int id = popup->get_item_id(index);
		String key;
		if (id >= PARTICLE_LIBRARY_RECENT_BASE && id - PARTICLE_LIBRARY_RECENT_BASE < recent_effect_paths.size()) {
			key = recent_effect_paths[id - PARTICLE_LIBRARY_RECENT_BASE];
		} else if (id >= PARTICLE_LIBRARY_PRESET_BASE && id - PARTICLE_LIBRARY_PRESET_BASE < FPSParticlePresets::PRESET_MAX) {
			key = vformat("preset:%d", id - PARTICLE_LIBRARY_PRESET_BASE);
		}
		const real_t mouse_y = popup->get_position().y + popup->get_mouse_position().y;
		r_anchor = Rect2(popup_rect.position.x, mouse_y, popup_rect.size.x, 1.0);
		return key;
	}
	// Effect Library panel: the item under the mouse, anchored to the panel.
	if (effects_tree && effects_tree->is_visible_in_tree()) {
		const Point2 local = effects_tree->get_local_mouse_position();
		if (!Rect2(Point2(), effects_tree->get_size()).has_point(local)) {
			return String();
		}
		TreeItem *item = effects_tree->get_item_at_position(local);
		if (!item || item->get_metadata(0).get_type() != Variant::STRING) {
			return String();
		}
		const Rect2 tree_rect = (hover_preview_popup && hover_preview_popup->is_embedded()) ? effects_tree->get_global_rect() : effects_tree->get_screen_rect();
		r_anchor = Rect2(tree_rect.position.x, tree_rect.position.y + local.y, tree_rect.size.x, 1.0);
		return String(item->get_metadata(0));
	}
	return String();
}

void FPSStudioParticleEditorPlugin::_process_library_hover(double p_delta) {
	const double delay = double(EDITOR_GET("fps_studio/particles/library_hover_preview_delay"));
	Rect2 anchor;
	const String key = delay > 0.0 ? _get_library_hover(anchor) : String();
	if (key != hover_key) {
		hover_key = key;
		hover_elapsed = 0.0;
		if (hover_shown_key != key) {
			_hide_hover_preview();
		}
	}
	if (key.is_empty()) {
		return;
	}
	hover_anchor = anchor;
	hover_elapsed += p_delta;
	if (hover_shown_key.is_empty() && hover_elapsed >= delay) {
		_show_hover_preview();
	}
	if (hover_shown_key.is_empty()) {
		return;
	}
	ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(hover_preview_effect_id));
	hover_play_time += p_delta;
	if (effect && hover_play_time >= hover_play_length) {
		hover_play_time = 0.0;
		effect->seek(0.0);
	}
}

void FPSStudioParticleEditorPlugin::_show_hover_preview() {
	_ensure_hover_preview();
	_hide_hover_preview();
	Node *effect = _instantiate_library_effect(hover_key);
	if (!effect) {
		hover_shown_key = hover_key; // Nothing to preview; do not retry every frame.
		return;
	}
	hover_preview_root->add_child(effect);
	hover_preview_effect_id = effect->get_instance_id();
	double shot_time = 0.0;
	frame_library_effect(effect, hover_preview_camera, shot_time, hover_play_length);
	hover_play_time = 0.0;
	if (ParticleEffect3D *particle_effect = Object::cast_to<ParticleEffect3D>(effect)) {
		particle_effect->seek(0.0);
	}
	hover_preview_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	const bool preset = hover_key.begins_with("preset:");
	hover_preview_label->set_text(preset ? FPSParticlePresets::get_preset_name(hover_key.get_slicec(':', 1).to_int()) : hover_key.get_file().get_basename());
	hover_preview_description->set_text(preset ? FPSParticlePresets::get_preset_description(hover_key.get_slicec(':', 1).to_int()) : hover_key);
	hover_preview_popup->reset_size(); // Fit the description's height.
	hover_shown_key = hover_key;

	// Beside the item: left of the Library menu (it opens at the right edge),
	// right of the Library panel; flipped if it would leave the window.
	const Size2 size = hover_preview_popup->get_contents_minimum_size();
	const real_t gap = 6 * EDSCALE;
	const bool from_menu = library_menu && library_menu->get_popup()->is_visible();
	Point2 position(from_menu ? hover_anchor.position.x - size.x - gap : hover_anchor.get_end().x + gap, hover_anchor.position.y - size.y * 0.5);
	Rect2 bounds;
	if (hover_preview_popup->is_embedded()) {
		bounds = hover_preview_popup->get_embedder()->get_visible_rect();
	} else {
		bounds = Rect2(get_window()->get_position(), get_window()->get_size());
	}
	if (position.x < bounds.position.x || position.x + size.x > bounds.get_end().x) {
		position.x = from_menu ? hover_anchor.get_end().x + gap : hover_anchor.position.x - size.x - gap;
	}
	position.x = CLAMP(position.x, bounds.position.x, MAX(bounds.position.x, bounds.get_end().x - size.x));
	position.y = CLAMP(position.y, bounds.position.y, MAX(bounds.position.y, bounds.get_end().y - size.y));
	hover_preview_popup->set_position(position.round());
	hover_preview_popup->set_size(size.ceil());
	hover_preview_popup->show();
}

void FPSStudioParticleEditorPlugin::_hide_hover_preview() {
	hover_shown_key = String();
	if (Node *effect = Object::cast_to<Node>(ObjectDB::get_instance(hover_preview_effect_id))) {
		effect->get_parent()->remove_child(effect);
		effect->queue_free();
	}
	hover_preview_effect_id = ObjectID();
	if (hover_preview_viewport) {
		hover_preview_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	}
	if (hover_preview_popup && hover_preview_popup->is_visible()) {
		hover_preview_popup->hide();
	}
}

// ---------------------------------------------------------------------------
// Content Browser (FileSystem dock): thumbnails and animated tooltips for 3D
// scenes, imported models, particle effects (new and old), levels, materials
// and spatial shaders.

// Removes what must not run or show in a preview: cameras (they would take
// over the preview camera), the scene's own environment, sound, UI, shake and
// damage. Scripts are cleared separately.
static void prepare_scene_for_preview(Node *p_node) {
	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		Node *child = p_node->get_child(i);
		if (Object::cast_to<Camera3D>(child) || Object::cast_to<WorldEnvironment>(child) || Object::cast_to<CanvasItem>(child) || Object::cast_to<CanvasLayer>(child) ||
				Object::cast_to<Viewport>(child) || Object::cast_to<AudioStreamPlayer>(child) || Object::cast_to<AudioStreamPlayer3D>(child) ||
				child->is_class("CameraShake3D") || child->is_class("RadialDamage3D")) {
			p_node->remove_child(child);
			memdelete(child);
			continue;
		}
		prepare_scene_for_preview(child);
	}
}

// Old particle effects start their emitters from scripts; a preview starts
// them itself.
static void start_preview_particles(Node *p_node) {
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_node)) {
		gpu->set_emitting(true);
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_node)) {
		cpu->set_emitting(true);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		start_preview_particles(p_node->get_child(i));
	}
}

static void restart_preview_particles(Node *p_node) {
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_node)) {
		gpu->restart();
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_node)) {
		cpu->restart();
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		restart_preview_particles(p_node->get_child(i));
	}
}

static void set_preview_particles_preprocess(Node *p_node, double p_ratio) {
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_node)) {
		gpu->set_pre_process_time(gpu->get_lifetime() * p_ratio);
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_node)) {
		cpu->set_pre_process_time(cpu->get_lifetime() * p_ratio);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		set_preview_particles_preprocess(p_node->get_child(i), p_ratio);
	}
}

static int count_preview_geometry(Node *p_node) {
	int count = (Object::cast_to<GeometryInstance3D>(p_node) && !Object::cast_to<GPUParticles3D>(p_node) && !Object::cast_to<CPUParticles3D>(p_node)) ? 1 : 0;
	for (int i = 0; i < p_node->get_child_count(); i++) {
		count += count_preview_geometry(p_node->get_child(i));
	}
	return count;
}

// First Decal below p_node and its transform relative to p_node.
static Decal *find_preview_decal(Node *p_node, Transform3D &r_transform, const Transform3D &p_parent = Transform3D()) {
	Transform3D transform = p_parent;
	if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		transform = p_parent * node_3d->get_transform();
	}
	if (Decal *decal = Object::cast_to<Decal>(p_node)) {
		r_transform = transform;
		return decal;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (Decal *decal = find_preview_decal(p_node->get_child(i), r_transform, transform)) {
			return decal;
		}
	}
	return nullptr;
}

// Bounds of what is visible (meshes, particles where they fly, decals) and
// the longest particle lifetime.
static void collect_preview_bounds(Node *p_node, AABB &r_bounds, bool &r_has_bounds, double &r_longest_particles) {
	AABB box;
	bool has_box = false;
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_node)) {
		r_longest_particles = MAX(r_longest_particles, gpu->get_lifetime());
		if (gpu->is_visible_in_tree()) {
			box = gpu->get_global_transform().xform(get_emitter_framing_bounds(gpu, gpu->get_visibility_aabb()));
			has_box = true;
		}
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_node)) {
		r_longest_particles = MAX(r_longest_particles, cpu->get_lifetime());
		if (cpu->is_visible_in_tree()) {
			box = cpu->get_global_transform().xform(AABB(Vector3(-0.5, -0.5, -0.5), Vector3(1, 1, 1)));
			has_box = true;
		}
	} else if (GeometryInstance3D *geometry = Object::cast_to<GeometryInstance3D>(p_node)) {
		const AABB local = geometry->get_aabb();
		if (geometry->is_visible_in_tree() && local.size.length_squared() > 1e-8) {
			box = geometry->get_global_transform().xform(local);
			has_box = true;
		}
	} else if (Decal *decal = Object::cast_to<Decal>(p_node)) {
		box = decal->get_global_transform().xform(AABB(-decal->get_size() * 0.5, decal->get_size()));
		has_box = true;
	}
	if (has_box) {
		r_bounds = r_has_bounds ? r_bounds.merge(box) : box;
		r_has_bounds = true;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		collect_preview_bounds(p_node->get_child(i), r_bounds, r_has_bounds, r_longest_particles);
	}
}

// Frames a Content Browser subject (in the tree). r_play_length > 0 when it
// has particles to replay.
static void frame_preview_subject(Node *p_subject, Camera3D *p_camera, int p_kind, AABB &r_bounds, double &r_play_length, bool &r_has_particles) {
	bool has_bounds = false;
	double longest = 0.0;
	collect_preview_bounds(p_subject, r_bounds, has_bounds, longest);
	if (!has_bounds) {
		r_bounds = AABB(Vector3(-0.5, -0.5, -0.5), Vector3(1, 1, 1));
	}
	r_has_particles = longest > 0.0;
	r_play_length = r_has_particles ? CLAMP(longest + 0.4, 0.8, 5.0) : 0.0;
	// Levels from higher up, objects from a three-quarter view.
	const Vector3 direction = (p_kind == ASSET_PREVIEW_LEVEL ? Vector3(0.7, 1.0, 1.0) : Vector3(0.75, 0.45, 1.0)).normalized();
	const real_t radius = MAX(real_t(0.01), r_bounds.size.length() * 0.5);
	const real_t distance = radius / Math::sin(Math::deg_to_rad(real_t(p_camera->get_fov()) * 0.5)) * 0.95;
	const Vector3 center = r_bounds.get_center();
	p_camera->look_at_from_position(center + direction * distance, center, Vector3(0, 1, 0));
	p_camera->set_near(MAX(real_t(0.002), (distance - radius) * 0.5));
	p_camera->set_far(distance + radius * 4.0 + 10.0);
}

Node *FPSStudioParticleEditorPlugin::_instantiate_preview_subject(const String &p_path, int p_kind) {
	if (p_kind == ASSET_PREVIEW_EFFECT) {
		return _instantiate_library_effect(p_path);
	}
	if (p_kind == ASSET_PREVIEW_SCENE || p_kind == ASSET_PREVIEW_LEVEL) {
		Ref<PackedScene> packed_scene = ResourceLoader::load(p_path, "PackedScene");
		if (packed_scene.is_null() || !packed_scene->can_instantiate()) {
			return nullptr;
		}
		Node *scene = packed_scene->instantiate();
		if (!scene) {
			return nullptr;
		}
		if (!Object::cast_to<Node3D>(scene)) {
			memdelete(scene);
			return nullptr;
		}
		clear_preview_scripts(scene);
		prepare_scene_for_preview(scene);
		start_preview_particles(scene);
		Node3D *wrapper = memnew(Node3D);
		wrapper->add_child(scene);
		// A decal needs a surface to project onto.
		Transform3D decal_transform;
		Decal *decal = count_preview_geometry(scene) == 0 ? find_preview_decal(scene, decal_transform) : nullptr;
		if (decal) {
			Ref<PlaneMesh> plane;
			plane.instantiate();
			const real_t extent = MAX(decal->get_size().x, decal->get_size().z) * 2.5;
			plane->set_size(Vector2(extent, extent));
			Ref<StandardMaterial3D> ground;
			ground.instantiate();
			ground->set_albedo(Color(0.42, 0.42, 0.42));
			plane->set_material(ground);
			MeshInstance3D *surface = memnew(MeshInstance3D);
			surface->set_mesh(plane);
			// The decal projects along its -Y onto the plane through its centre.
			surface->set_transform(Transform3D(decal_transform.basis.orthonormalized(), decal_transform.origin));
			wrapper->add_child(surface);
		}
		return wrapper;
	}
	if (p_kind == ASSET_PREVIEW_MESH) {
		Ref<Mesh> mesh = ResourceLoader::load(p_path, "Mesh");
		if (mesh.is_null()) {
			return nullptr;
		}
		MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_mesh(mesh);
		Node3D *wrapper = memnew(Node3D);
		wrapper->add_child(mesh_instance);
		return wrapper;
	}
	if (p_kind == ASSET_PREVIEW_MATERIAL || p_kind == ASSET_PREVIEW_SHADER) {
		Ref<Material> material;
		if (p_kind == ASSET_PREVIEW_SHADER) {
			Ref<Shader> shader = ResourceLoader::load(p_path, "Shader");
			if (shader.is_null() || shader->get_mode() != Shader::MODE_SPATIAL) {
				return nullptr;
			}
			Ref<ShaderMaterial> shader_material;
			shader_material.instantiate();
			shader_material->set_shader(shader);
			material = shader_material;
		} else {
			material = ResourceLoader::load(p_path, "Material");
			const ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(material.ptr());
			if (material.is_null() || (shader_material && (shader_material->get_shader().is_null() || shader_material->get_shader()->get_mode() != Shader::MODE_SPATIAL))) {
				return nullptr;
			}
		}
		Ref<SphereMesh> sphere;
		sphere.instantiate();
		sphere->set_radius(0.5);
		sphere->set_height(1.0);
		sphere->set_radial_segments(48);
		sphere->set_rings(24);
		MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_mesh(sphere);
		mesh_instance->set_material_override(material);
		Node3D *wrapper = memnew(Node3D);
		wrapper->add_child(mesh_instance);
		return wrapper;
	}
	return nullptr;
}

// Tooltip body: the asset live in its own little world. Models and materials
// turn slowly, effects replay, levels stand still.
class FPSAssetTooltipPreview : public SubViewportContainer {
	GDCLASS(FPSAssetTooltipPreview, SubViewportContainer);

	ObjectID plugin_id;
	String path;
	int kind = ASSET_PREVIEW_NONE;
	SubViewport *viewport = nullptr;
	Node3D *world_root = nullptr;
	Camera3D *camera = nullptr;
	ObjectID subject_id;
	bool turn = false;
	double play_time = 0.0;
	double play_length = 0.0;

protected:
	void _notification(int p_what) {
		if (p_what == NOTIFICATION_READY) {
			FPSStudioParticleEditorPlugin *plugin = Object::cast_to<FPSStudioParticleEditorPlugin>(ObjectDB::get_instance(plugin_id));
			Node *subject = plugin ? plugin->instantiate_preview_subject(path, kind) : nullptr;
			if (!subject) {
				return;
			}
			world_root->add_child(subject);
			subject_id = subject->get_instance_id();
			if (kind == ASSET_PREVIEW_EFFECT) {
				double shot_time = 0.0;
				frame_library_effect(subject, camera, shot_time, play_length);
				if (ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(subject)) {
					effect->seek(0.0);
				}
			} else {
				AABB bounds;
				bool has_particles = false;
				frame_preview_subject(subject, camera, kind, bounds, play_length, has_particles);
				// Turn around the centre of the bounds (the wrapper is the pivot).
				turn = kind != ASSET_PREVIEW_LEVEL && !has_particles;
				if (Node3D *wrapper = Object::cast_to<Node3D>(subject)) {
					const Vector3 center = bounds.get_center();
					for (int i = 0; i < wrapper->get_child_count(); i++) {
						if (Node3D *child = Object::cast_to<Node3D>(wrapper->get_child(i))) {
							child->set_position(child->get_position() - center);
						}
					}
					wrapper->set_position(center);
				}
			}
			set_process(turn || play_length > 0.0);
		} else if (p_what == NOTIFICATION_PROCESS) {
			const double delta = get_process_delta_time();
			Node *subject = Object::cast_to<Node>(ObjectDB::get_instance(subject_id));
			if (!subject) {
				return;
			}
			if (turn) {
				if (Node3D *wrapper = Object::cast_to<Node3D>(subject)) {
					wrapper->rotate_y(delta * 0.6);
				}
			}
			if (play_length > 0.0) {
				play_time += delta;
				if (play_time >= play_length) {
					play_time = 0.0;
					if (ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(subject)) {
						effect->seek(0.0);
					} else {
						restart_preview_particles(subject);
					}
				}
			}
		}
	}

public:
	FPSAssetTooltipPreview(ObjectID p_plugin, const String &p_path, int p_kind) {
		plugin_id = p_plugin;
		path = p_path;
		kind = p_kind;
		set_stretch(true);
		const int size = int(EditorResourcePreview::TOOLTIP_PREVIEW_SIZE * EDSCALE);
		set_custom_minimum_size(Size2(size, size * 5 / 6));
		viewport = memnew(SubViewport);
		viewport->set_use_own_world_3d(true);
		viewport->set_msaa_3d(Viewport::MSAA_4X);
		add_child(viewport);
		world_root = memnew(Node3D);
		viewport->add_child(world_root);
		camera = create_library_preview_world(world_root);
	}
};

class FPSAssetTooltipPlugin : public EditorResourceTooltipPlugin {
	GDCLASS(FPSAssetTooltipPlugin, EditorResourceTooltipPlugin);

public:
	ObjectID plugin_id;

	bool handles(const String &p_resource_type) const override {
		return p_resource_type == "PackedScene" || p_resource_type == "Shader" || p_resource_type == "ShaderMaterial" || ClassDB::is_parent_class(p_resource_type, "BaseMaterial3D") || ClassDB::is_parent_class(p_resource_type, "Mesh");
	}

	Control *make_tooltip_for_path(const String &p_resource_path, const Dictionary &p_metadata, Control *p_base) const override {
		VBoxContainer *vb = Object::cast_to<VBoxContainer>(p_base);
		const int kind = vb ? get_asset_preview_kind(p_resource_path, ResourceLoader::get_resource_type(p_resource_path)) : int(ASSET_PREVIEW_NONE);
		if (kind == ASSET_PREVIEW_NONE) {
			return p_base;
		}
		// Like the texture tooltip: a big live preview above the file info.
		vb->add_theme_constant_override("separation", 4 * EDSCALE);
		FPSAssetTooltipPreview *preview = memnew(FPSAssetTooltipPreview(plugin_id, p_resource_path, kind));
		vb->add_child(preview);
		vb->move_child(preview, 0);
		return vb;
	}
};

// Godot has no shader thumbnails: show the one rendered by the scan.
class FPSCachedShaderPreviewGenerator : public EditorResourcePreviewGenerator {
	GDCLASS(FPSCachedShaderPreviewGenerator, EditorResourcePreviewGenerator);

public:
	bool handles(const String &p_type) const override {
		return p_type == "Shader";
	}
	Ref<Texture2D> generate(const Ref<Resource> &p_from, const Size2 &p_size, Dictionary &p_metadata) const override {
		return Ref<Texture2D>();
	}
	Ref<Texture2D> generate_from_path(const String &p_path, const Size2 &p_size, Dictionary &p_metadata) const override {
		const String cache_path = get_effect_thumbnail_cache_path(p_path);
		if (!FileAccess::exists(cache_path)) {
			return Ref<Texture2D>();
		}
		Ref<Image> image = Image::load_from_file(cache_path);
		if (image.is_null() || image->is_empty()) {
			return Ref<Texture2D>();
		}
		if (image->get_width() != int(p_size.x)) {
			image->resize(int(p_size.x), int(p_size.y), Image::INTERPOLATE_LANCZOS);
		}
		return ImageTexture::create_from_image(image);
	}
};

// A thumbnail is current while the file (and its import settings) are
// unchanged since it was rendered. Godot overwrites a scene's thumbnail with a
// viewport snapshot when the scene is saved, so this is tracked separately.
// Newest change of a file, its import settings and what it shows: textures,
// materials, meshes, sub-scenes and shaders, a few levels deep (scripts do not
// change how it looks in a preview).
static uint64_t get_asset_visual_mtime(const String &p_path, int p_depth, HashSet<String> &r_visited) {
	if (r_visited.has(p_path)) {
		return 0;
	}
	r_visited.insert(p_path);
	uint64_t stamp = FileAccess::get_modified_time(p_path);
	if (FileAccess::exists(p_path + ".import")) {
		stamp = MAX(stamp, FileAccess::get_modified_time(p_path + ".import"));
	}
	if (p_depth <= 0) {
		return stamp;
	}
	List<String> dependencies;
	ResourceLoader::get_dependencies(p_path, &dependencies);
	for (const String &dependency : dependencies) {
		// "res://path", or "uid://...::::res://fallback_path".
		String path = dependency.get_slice("::", 0);
		if (path.begins_with("uid://")) {
			path = ResourceUID::ensure_path(path);
			if (path.is_empty() || path.begins_with("uid://")) {
				path = dependency.get_slice("::", dependency.get_slice_count("::") - 1);
			}
		}
		const String ext = path.get_extension().to_lower();
		if (path.is_empty() || !path.begins_with("res://") || ext == "gd" || ext == "cs" || !FileAccess::exists(path)) {
			continue;
		}
		stamp = MAX(stamp, get_asset_visual_mtime(path, p_depth - 1, r_visited));
	}
	return stamp;
}

String FPSStudioParticleEditorPlugin::_get_asset_thumbnail_stamp(const String &p_path) const {
	HashSet<String> visited;
	const uint64_t stamp = get_asset_visual_mtime(p_path, 3, visited);
	return vformat("%d:%d", int64_t(stamp), ASSET_THUMBNAIL_VERSION);
}

Ref<ConfigFile> FPSStudioParticleEditorPlugin::_get_asset_thumbnail_manifest() {
	if (asset_thumbnail_manifest.is_null()) {
		asset_thumbnail_manifest.instantiate();
		asset_thumbnail_manifest->load(EditorPaths::get_singleton()->get_project_data_dir().path_join("fps_asset_thumbnails.cfg"));
	}
	return asset_thumbnail_manifest;
}

void FPSStudioParticleEditorPlugin::_record_asset_thumbnail(const String &p_path) {
	Ref<ConfigFile> manifest = _get_asset_thumbnail_manifest();
	manifest->set_value("thumbnails", p_path, _get_asset_thumbnail_stamp(p_path));
	manifest->save(EditorPaths::get_singleton()->get_project_data_dir().path_join("fps_asset_thumbnails.cfg"));
}

void FPSStudioParticleEditorPlugin::_scan_asset_thumbnails_dir(EditorFileSystemDirectory *p_dir) {
	Ref<ConfigFile> manifest = _get_asset_thumbnail_manifest();
	for (int i = 0; i < p_dir->get_file_count(); i++) {
		const String type = p_dir->get_file_type(i);
		if (type != "PackedScene" && type != "Shader") {
			continue; // Materials already get Godot's sphere thumbnails.
		}
		const String path = p_dir->get_file_path(i);
		// The kind only changes with the file itself: classify once per change.
		const uint64_t modified = FileAccess::get_modified_time(path);
		int kind = ASSET_PREVIEW_NONE;
		if (asset_scan_mtimes.has(path) && asset_scan_mtimes[path] == modified) {
			kind = asset_scan_kinds[path];
		} else {
			kind = get_asset_preview_kind(path, type);
			asset_scan_mtimes[path] = modified;
			asset_scan_kinds[path] = kind;
		}
		if (kind == ASSET_PREVIEW_NONE || kind == ASSET_PREVIEW_MESH) {
			continue;
		}
		if (String(manifest->get_value("thumbnails", path, String())) == _get_asset_thumbnail_stamp(path)) {
			continue; // Neither the file nor anything it shows changed.
		}
		_queue_thumbnail(path, path, -1, true, kind);
	}
	for (int i = 0; i < p_dir->get_subdir_count(); i++) {
		_scan_asset_thumbnails_dir(p_dir->get_subdir(i));
	}
}

void FPSStudioParticleEditorPlugin::_scan_asset_thumbnails() {
	asset_thumbnail_scan_queued = false;
	EditorFileSystem *file_system = EditorFileSystem::get_singleton();
	if (file_system && file_system->get_filesystem() && !file_system->is_scanning()) {
		_scan_asset_thumbnails_dir(file_system->get_filesystem());
	}
}

void FPSStudioParticleEditorPlugin::_queue_asset_thumbnail_scan() {
	if (!asset_thumbnail_scan_queued) {
		asset_thumbnail_scan_queued = true;
		callable_mp(this, &FPSStudioParticleEditorPlugin::_scan_asset_thumbnails).call_deferred();
	}
}

void FPSStudioParticleEditorPlugin::_finish_thumbnail_job() {
	Ref<Image> image;
	if (thumbnail_viewport && thumbnail_viewport->get_texture().is_valid()) {
		image = thumbnail_viewport->get_texture()->get_image();
	}
	if (image.is_valid() && !image->is_empty()) {
		image = image->duplicate();
		image->convert(Image::FORMAT_RGB8);
		const Ref<Texture2D> texture = ImageTexture::create_from_image(image);
		thumbnails[thumbnail_current.key] = texture;
		if (!thumbnail_current.path.is_empty()) {
			thumbnail_mtimes[thumbnail_current.key] = FileAccess::get_modified_time(thumbnail_current.path);
		}
		_set_library_icon(thumbnail_current.key, texture);
		if (thumbnail_current.preset >= 0) {
			const String cache_path = get_preset_thumbnail_cache_path(thumbnail_current.preset);
			DirAccess::make_dir_recursive_absolute(cache_path.get_base_dir());
			image->save_png(cache_path);
		}
		if (thumbnail_current.write_cache && !thumbnail_current.path.is_empty()) {
			// FileSystem dock / Content Browser thumbnail, like the editor writes
			// for a scene saved from the 3D view.
			Ref<Image> cache_image = image->duplicate();
			const int preview_size = MAX(16, int(int(EDITOR_GET("filesystem/file_dialog/thumbnail_size")) * EDSCALE));
			cache_image->resize(preview_size, preview_size, Image::INTERPOLATE_LANCZOS);
			post_process_preview(cache_image);
			if (cache_image->save_png(get_effect_thumbnail_cache_path(thumbnail_current.path)) == OK && EditorResourcePreview::get_singleton()) {
				// The file did not change, only its thumbnail: force the refresh.
				EditorResourcePreview::get_singleton()->invalidate(thumbnail_current.path);
			}
			_record_asset_thumbnail(thumbnail_current.path);
		}
	}
	if (Node *effect = Object::cast_to<Node>(ObjectDB::get_instance(thumbnail_effect_id))) {
		effect->get_parent()->remove_child(effect);
		effect->queue_free();
	}
	thumbnail_effect_id = ObjectID();
	if (thumbnail_viewport) {
		thumbnail_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	}
	thumbnail_frames = -1;
}

void FPSStudioParticleEditorPlugin::_process_thumbnails() {
	// The Library asks for its thumbnails once it is actually on screen.
	const bool library_visible = effects_tree && effects_tree->is_visible_in_tree();
	if (library_visible && !library_thumbnails_visible) {
		_queue_library_thumbnails();
	}
	// Missing thumbnails render in the background as soon as the workspace is
	// open, so the Library menu has them before it is first opened.
	if (!library_thumbnails_prepared && screen && screen->is_visible_in_tree()) {
		library_thumbnails_prepared = true;
		_queue_library_thumbnails();
	}
	library_thumbnails_visible = library_visible;
	if (thumbnail_frames >= 0) {
		// A few rendered frames let the particles simulate up to the seek time.
		if (++thumbnail_frames >= 4) {
			_finish_thumbnail_job();
		}
		return;
	}
	while (!thumbnail_queue.is_empty()) {
		thumbnail_current = thumbnail_queue[0];
		thumbnail_queue.remove_at(0);
		thumbnail_queued.erase(thumbnail_current.key);
		if (_start_thumbnail_job(thumbnail_current)) {
			thumbnail_frames = 0;
			return;
		}
	}
}


void FPSStudioParticleEditorPlugin::_assign_level_owners(Node *p_instance, Node *p_owner) {
	if (p_instance && p_owner) {
		set_scene_subtree_owners(p_instance, p_owner);
	}
}

void FPSStudioParticleEditorPlugin::_restart_preview_at(double p_time) {
	if (!preview_effect) {
		return;
	}
	if (ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(preview_effect)) {
		effect->seek(p_time);
	} else {
		restart_particle_nodes(preview_effect, p_time > 0.0, p_time);
	}
}

void FPSStudioParticleEditorPlugin::_on_timeline_scrub(double p_time) {
	if (!preview_effect) {
		return;
	}
	const double clamped = CLAMP(p_time, 0.0, preview_duration);
	if (timeline) {
		timeline->set_value(clamped);
	}
	if (timeline_view) {
		timeline_view->set_current_time(clamped);
	}
}

void FPSStudioParticleEditorPlugin::_on_timeline_loop_region_changed(double p_start, double p_end) {
	preview_loop_start = p_start;
	preview_loop_end = p_end;
	// Remembered per effect file in the project's editor metadata (not in the
	// effect itself, so the game never sees it).
	if (!restoring_playback_range && !effect_path.is_empty()) {
		Dictionary ranges = EditorSettings::get_singleton()->get_project_metadata("fps_studio_particle", "playback_ranges", Dictionary());
		if (p_end > p_start) {
			ranges[effect_path] = Vector2(p_start, p_end);
		} else {
			ranges.erase(effect_path);
		}
		EditorSettings::get_singleton()->set_project_metadata("fps_studio_particle", "playback_ranges", ranges);
	}
	if (p_end > p_start && preview_effect && (preview_time < p_start || preview_time > p_end)) {
		// Jump into the region so the change is visible right away.
		preview_time = p_start;
		preview_time_label_update_accumulator = 0.0;
		if (timeline) {
			timeline->set_value_no_signal(preview_time);
		}
		if (timeline_view) {
			timeline_view->set_current_time(preview_time);
		}
		_restart_preview_at(preview_time);
		_update_time_label();
	}
}

void FPSStudioParticleEditorPlugin::_on_timeline_emitter_selected(Object *p_emitter) {
	Node *emitter = Object::cast_to<Node>(p_emitter);
	if (emitter && emitter != _get_edited_emitter()) {
		_set_edited_emitter(emitter);
	}
}

void FPSStudioParticleEditorPlugin::_apply_emitter_timing(Object *p_emitter, double p_delay, double p_duration) {
	Node *emitter = Object::cast_to<Node>(p_emitter);
	if (!emitter) {
		return;
	}
	ParticleEffect3D::set_emitter_delay(emitter, p_delay);
	ParticleEffect3D::set_emitter_duration(emitter, p_duration);
	_mark_document_dirty();
	_rebuild_preview(); // Keeps the camera framing of the same document.
}

void FPSStudioParticleEditorPlugin::_on_timeline_timing_committed(Object *p_emitter, double p_old_delay, double p_old_duration) {
	Node *emitter = Object::cast_to<Node>(p_emitter);
	if (!emitter) {
		return;
	}
	const double delay = ParticleEffect3D::get_emitter_delay(emitter);
	const double duration = ParticleEffect3D::get_emitter_duration(emitter);
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(vformat(TTR("Change Timing of %s"), emitter->get_name()), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(this, "_apply_emitter_timing", emitter, delay, duration);
	undo_redo->add_undo_method(this, "_apply_emitter_timing", emitter, p_old_delay, p_old_duration);
	undo_redo->commit_action();
}

Node *FPSStudioParticleEditorPlugin::_convert_to_effect_root(Node *p_root) {
	// Older effects were saved with a plain Node3D root. Replace it with a
	// ParticleEffect3D so timing and game-side playback work; children,
	// transform and metadata are kept. The change is written on next save.
	if (!p_root || p_root->get_class_name() != Node3D::get_class_static()) {
		return p_root;
	}
	// A scripted root (old effects started by game code, e.g. init_splash())
	// stays as it is: replacing it would drop the script and break the game.
	if (!Ref<Script>(p_root->get_script()).is_null()) {
		EditorToaster::get_singleton()->popup_str(vformat(TTR("%s keeps its Node3D root and script (game code drives it). Timeline events need a ParticleEffect3D root."), p_root->get_name()), EditorToaster::SEVERITY_INFO);
		return p_root;
	}
	Node3D *old_root = Object::cast_to<Node3D>(p_root);
	ParticleEffect3D *effect = memnew(ParticleEffect3D);
	effect->set_name(old_root->get_name());
	effect->set_transform(old_root->get_transform());
	List<StringName> meta_keys;
	old_root->get_meta_list(&meta_keys);
	for (const StringName &key : meta_keys) {
		effect->set_meta(key, old_root->get_meta(key));
	}
	while (old_root->get_child_count() > 0) {
		Node *child = old_root->get_child(0);
		old_root->remove_child(child);
		child->set_owner(nullptr); // Re-owned below; avoids "owner inconsistent" warnings.
		effect->add_child(child);
	}
	set_scene_subtree_owners(effect, effect);
	memdelete(old_root);
	return effect;
}

void FPSStudioParticleEditorPlugin::_on_properties_mode_toggled(bool p_pressed, bool p_advanced) {
	if (!p_pressed) {
		return;
	}
	if (module_stack_scroll) {
		module_stack_scroll->set_visible(!p_advanced);
	}
	if (properties_tabs) {
		properties_tabs->set_visible(p_advanced);
	}
	if (!p_advanced && module_stack) {
		module_stack->refresh_values();
	}
}

void FPSStudioParticleEditorPlugin::_on_module_stack_property_edited(const String &p_property) {
	module_stack_editing = true;
	_on_emitter_property_edited(p_property);
	module_stack_editing = false;
}

void FPSStudioParticleEditorPlugin::_on_module_stack_generate_texture() {
	Node *emitter = _get_edited_emitter();
	if (!emitter || !texture_generator_dialog) {
		return;
	}
	if (!texture_generator_thumbnails_ready) {
		_build_texture_type_list();
	}
	// Continue editing the recipe saved next to the emitter's current texture.
	Ref<Texture2D> texture;
	Decal *decal = Object::cast_to<Decal>(emitter);
	if (decal) {
		texture = decal->get_texture(Decal::TEXTURE_ALBEDO);
		// Start a decal track on a decal type.
		const VFXTextureGenerator::TypeInfo *current = VFXTextureGenerator::get_type_info(texture_generator_type);
		if (!current || String::utf8(current->category) != "Decals") {
			_select_texture_type(VFXTextureGenerator::TYPE_SCORCH, true);
		}
		if (texture_generator_normal_map) {
			texture_generator_normal_map->set_pressed_no_signal(false);
		}
	} else {
		Ref<Material> material = FPSParticleModuleStack::get_draw_material(emitter);
		BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(material.ptr());
		texture = base_material ? base_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO) : Ref<Texture2D>();
	}
	if (texture.is_valid() && !texture->get_path().is_empty()) {
		const String recipe_path = texture->get_path().get_basename() + ".vfx.cfg";
		if (FileAccess::exists(recipe_path)) {
			_load_texture_recipe(recipe_path, false);
		}
	}
	_invalidate_texture_export();
	if (texture_generator_status) {
		texture_generator_status->set_text(vformat(TTR("Apply saves to %s"), _get_auto_texture_path()));
	}
	_regenerate_texture(true);
	_show_texture_generator();
}

void FPSStudioParticleEditorPlugin::_create_effect_document(int p_template) {
	if (p_template >= PARTICLE_TEMPLATE_PRESET_BASE) {
		_create_preset_document(p_template - PARTICLE_TEMPLATE_PRESET_BASE);
		return;
	}
	ParticleEffect3D *effect = memnew(ParticleEffect3D);
	if (p_template == PARTICLE_TEMPLATE_BURST) {
		effect->set_name("BurstEffect");
	} else if (p_template == PARTICLE_TEMPLATE_FOUNTAIN) {
		effect->set_name("FountainEffect");
	} else if (p_template == PARTICLE_TEMPLATE_SMOKE) {
		effect->set_name("SmokeEffect");
	} else {
		effect->set_name("ParticleEffect");
	}
	Node3D *emitter = create_particle_emitter(p_template, create_particle_quad());
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
	_add_track_node(p_type >= PARTICLE_EVENT_LIGHT ? create_event_node(p_type) : create_particle_emitter(p_type, create_particle_quad()));
}

void FPSStudioParticleEditorPlugin::_add_track_node(Node3D *p_track) {
	Node *effect = _get_effect_root();
	if (!effect || !p_track) {
		if (p_track) {
			memdelete(p_track);
		}
		return;
	}
	Node *previous_emitter = _get_edited_emitter();
	Node3D *emitter = p_track;
	emitter->set_name(effect->prevalidate_child_name(emitter, emitter->get_name()));
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(vformat(TTR("Add %s to Particle Effect"), emitter->get_name()), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(effect, "add_child", emitter, true);
	undo_redo->add_do_method(emitter, "set_owner", effect);
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", emitter, effect_document_serial);
	undo_redo->add_do_reference(emitter);
	undo_redo->add_undo_method(emitter, "set_owner", (Object *)nullptr);
	undo_redo->add_undo_method(effect, "remove_child", emitter);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", previous_emitter, effect_document_serial);
	undo_redo->commit_action(); // The history callback selects it and refreshes the UI.
}

static Vector<String> get_dropped_audio_files(const Variant &p_data) {
	Vector<String> files;
	const Dictionary data = p_data;
	if (String(data.get("type", "")) != "files") {
		return files;
	}
	const Vector<String> paths = data.get("files", Vector<String>());
	for (const String &path : paths) {
		const String type = ResourceLoader::get_resource_type(path);
		if (!type.is_empty() && ClassDB::is_parent_class(type, "AudioStream")) {
			files.push_back(path);
		}
	}
	return files;
}

bool FPSStudioParticleEditorPlugin::_can_drop_on_emitter_tree(const Point2 &p_point, const Variant &p_data) const {
	return _get_effect_root() && !get_dropped_audio_files(p_data).is_empty();
}

void FPSStudioParticleEditorPlugin::_drop_on_emitter_tree(const Point2 &p_point, const Variant &p_data) {
	const Vector<String> files = get_dropped_audio_files(p_data);
	if (files.is_empty() || !_get_effect_root()) {
		return;
	}
	// Dropped onto an existing Sound track: replace its sound.
	TreeItem *item = emitter_tree->get_item_at_position(p_point);
	if (item && files.size() == 1) {
		AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(ObjectDB::get_instance(ObjectID(uint64_t(item->get_metadata(0)))));
		Ref<AudioStream> stream = ResourceLoader::load(files[0]);
		if (player && stream.is_valid()) {
			EditorUndoRedoManager *undo_redo = get_undo_redo();
			undo_redo->create_action_for_history(TTR("Set Sound"), EditorUndoRedoManager::GLOBAL_HISTORY);
			undo_redo->force_fixed_history();
			undo_redo->add_do_property(player, "stream", stream);
			undo_redo->add_undo_property(player, "stream", player->get_stream());
			undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", player, effect_document_serial);
			undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", player, effect_document_serial);
			undo_redo->commit_action();
			return;
		}
	}
	for (const String &path : files) {
		Ref<AudioStream> stream = ResourceLoader::load(path);
		if (stream.is_null()) {
			continue;
		}
		AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(create_event_node(PARTICLE_EVENT_SOUND));
		player->set_stream(stream);
		player->set_name(path.get_file().get_basename().to_pascal_case());
		_add_track_node(player);
	}
}

static void merge_level_mesh_bounds(Node *p_node, AABB &r_bounds, bool &r_has_bounds) {
	// Moving things are not baked: they would leave ghost collision behind.
	if (Object::cast_to<CharacterBody3D>(p_node) || Object::cast_to<RigidBody3D>(p_node) || Object::cast_to<ParticleEffect3D>(p_node) || Object::cast_to<GPUParticlesCollision3D>(p_node)) {
		return;
	}
	if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
		if (mesh_instance->is_visible_in_tree() && mesh_instance->get_mesh().is_valid()) {
			const AABB bounds = mesh_instance->get_global_transform().xform(mesh_instance->get_aabb());
			if (r_has_bounds) {
				r_bounds.merge_with(bounds);
			} else {
				r_bounds = bounds;
				r_has_bounds = true;
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		merge_level_mesh_bounds(p_node->get_child(i), r_bounds, r_has_bounds);
	}
}

static void hide_dynamic_nodes(Node *p_node, Vector<Node3D *> &r_hidden) {
	Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	if (node_3d && node_3d->is_visible() && (Object::cast_to<CharacterBody3D>(p_node) || Object::cast_to<RigidBody3D>(p_node) || Object::cast_to<ParticleEffect3D>(p_node))) {
		node_3d->set_visible(false);
		r_hidden.push_back(node_3d);
		return;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		hide_dynamic_nodes(p_node->get_child(i), r_hidden);
	}
}

void FPSStudioParticleEditorPlugin::_bake_level_particle_collision() {
	Node3D *level = Object::cast_to<Node3D>(EditorNode::get_singleton()->get_edited_scene());
	if (!level) {
		EditorNode::get_singleton()->show_warning(TTR("Open a 3D Level scene first. The particle collision is baked from its walls, floors and props."));
		return;
	}
	AABB bounds;
	bool has_bounds = false;
	merge_level_mesh_bounds(level, bounds, has_bounds);
	if (!has_bounds) {
		EditorNode::get_singleton()->show_warning(TTR("The open Level scene has no visible meshes to bake particle collision from."));
		return;
	}
	bounds = bounds.grow(1.0);

	const String node_name = "ParticleCollision";
	GPUParticlesCollisionSDF3D *existing = Object::cast_to<GPUParticlesCollisionSDF3D>(level->get_node_or_null(NodePath(node_name)));
	GPUParticlesCollisionSDF3D *sdf = existing ? existing : memnew(GPUParticlesCollisionSDF3D);
	const Vector3 old_size = existing ? existing->get_size() : Vector3();
	const Transform3D old_transform = existing ? existing->get_transform() : Transform3D();
	const Ref<Texture3D> old_texture = existing ? existing->get_texture() : Ref<Texture3D>();
	const GPUParticlesCollisionSDF3D::Resolution old_resolution = existing ? existing->get_resolution() : GPUParticlesCollisionSDF3D::RESOLUTION_128;
	// Every failure leaves an existing node exactly as it was.
	auto restore_existing = [&]() {
		if (existing) {
			existing->set_resolution(old_resolution);
			existing->set_size(old_size);
			existing->set_transform(old_transform);
		}
	};
	if (!existing) {
		sdf->set_name(node_name);
		level->add_child(sdf, true); // Temporarily, the bake searches meshes from its parent.
	}
	const real_t longest = bounds.get_longest_axis_size();
	const GPUParticlesCollisionSDF3D::Resolution new_resolution = longest > 80.0 ? GPUParticlesCollisionSDF3D::RESOLUTION_256 : GPUParticlesCollisionSDF3D::RESOLUTION_128;
	sdf->set_resolution(new_resolution);
	sdf->set_size(bounds.size);
	sdf->set_global_transform(Transform3D(Basis(), bounds.get_center()));
	const Transform3D new_transform = sdf->get_transform();

	Vector<Node3D *> hidden;
	hide_dynamic_nodes(level, hidden);
	Ref<Image> image = sdf->bake();
	for (Node3D *node : hidden) {
		node->set_visible(true);
	}
	if (!existing) {
		level->remove_child(sdf);
	}
	if (image.is_null()) {
		if (!existing) {
			memdelete(sdf);
		} else {
			restore_existing();
		}
		EditorNode::get_singleton()->show_warning(TTR("No level geometry was found to bake particle collision from."));
		return;
	}

	const String scene_path = level->get_scene_file_path();
	const String data_path = scene_path.is_empty() ? String("res://particle_collision_data.exr") : scene_path.get_basename() + "." + node_name + "_data.exr";
	Ref<ConfigFile> config;
	config.instantiate();
	if (FileAccess::exists(data_path + ".import")) {
		config->load(data_path + ".import");
	}
	config->set_value("remap", "importer", "3d_texture");
	config->set_value("remap", "type", "CompressedTexture3D");
	if (!config->has_section_key("params", "compress/mode")) {
		config->set_value("params", "compress/mode", 3);
	}
	config->set_value("params", "compress/channel_pack", 1);
	config->set_value("params", "mipmaps/generate", false);
	config->set_value("params", "slices/horizontal", 1);
	config->set_value("params", "slices/vertical", image->get_meta("depth"));
	config->save(data_path + ".import");
	if (image->save_exr(data_path, false) != OK) {
		if (!existing) {
			memdelete(sdf);
		} else {
			restore_existing();
		}
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not save the particle collision data to %s."), data_path));
		return;
	}
	ResourceLoader::import(data_path);
	Ref<Texture3D> texture = ResourceLoader::load(data_path, "", ResourceFormatLoader::CACHE_MODE_REPLACE);

	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Bake Particle Collision"), UndoRedo::MERGE_DISABLE, level);
	if (!existing) {
		sdf->set_texture(texture);
		undo_redo->add_do_method(level, "add_child", sdf, true);
		undo_redo->add_do_method(sdf, "set_owner", level);
		undo_redo->add_do_reference(sdf);
		undo_redo->add_undo_method(level, "remove_child", sdf);
	} else {
		// The node already holds the new values; restore the old ones first so
		// the undo snapshot and the do step are both exact.
		restore_existing();
		undo_redo->add_do_property(sdf, "resolution", new_resolution);
		undo_redo->add_undo_property(sdf, "resolution", old_resolution);
		undo_redo->add_do_property(sdf, "size", bounds.size);
		undo_redo->add_do_property(sdf, "transform", new_transform);
		undo_redo->add_do_property(sdf, "texture", texture);
		undo_redo->add_undo_property(sdf, "size", old_size);
		undo_redo->add_undo_property(sdf, "transform", old_transform);
		undo_redo->add_undo_property(sdf, "texture", old_texture);
	}
	undo_redo->commit_action();
	EditorToaster::get_singleton()->popup_str(vformat(TTR("Particle collision baked into %s (%s). Particles with the Collision module now bounce off the level. Save the level to keep it."), level->get_name(), data_path.get_file()));
}

void FPSStudioParticleEditorPlugin::_remove_selected_emitter() {
	Node *effect = _get_effect_root();
	Node *emitter = _get_edited_emitter();
	if (!effect || !emitter || emitter == effect || emitter->get_parent() != effect) {
		return;
	}
	Vector<Node *> particle_emitters;
	_collect_emitters(effect, particle_emitters);
	if (ParticleEffect3D::is_emitter(emitter) && particle_emitters.size() <= 1) {
		EditorNode::get_singleton()->show_warning(TTR("A particle effect must keep at least one emitter. Add another emitter before removing this one."));
		return;
	}
	Vector<Node *> emitters;
	collect_tracks(effect, emitters);
	Node *next_emitter = nullptr;
	for (Node *candidate : emitters) {
		if (candidate != emitter) {
			next_emitter = candidate;
			break;
		}
	}
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(TTR("Remove Particle Emitter"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(emitter, "set_owner", (Object *)nullptr);
	undo_redo->add_do_method(effect, "remove_child", emitter);
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", next_emitter, effect_document_serial);
	undo_redo->add_undo_method(effect, "add_child", emitter, true);
	undo_redo->add_undo_method(emitter, "set_owner", effect);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", emitter, effect_document_serial);
	undo_redo->add_undo_reference(emitter);
	undo_redo->commit_action();
}

void FPSStudioParticleEditorPlugin::_on_emitter_hierarchy_history_changed(Node *p_preferred_emitter, int64_t p_document_serial) {
	if (p_document_serial != effect_document_serial) {
		return; // A step recorded for an earlier document; its nodes are gone.
	}
	Node *effect = _get_effect_root();
	if (!effect) {
		_set_edited_emitter(nullptr, false);
		_update_document_ui();
		return;
	}
	Vector<Node *> emitters;
	collect_tracks(effect, emitters);
	Node *selected_emitter = p_preferred_emitter;
	if (!selected_emitter || (selected_emitter != effect && !effect->is_ancestor_of(selected_emitter))) {
		selected_emitter = emitters.is_empty() ? nullptr : emitters[0];
	}
	_set_edited_emitter(selected_emitter, false, true); // Rebuilds inspectors, module stack and preview.
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
	_open_library_entry(item->get_metadata(0));
}

void FPSStudioParticleEditorPlugin::_update_library_menu() {
	if (!library_menu) {
		return;
	}
	PopupMenu *popup = library_menu->get_popup();
	popup->clear();
	String current_category;
	for (int preset : FPSParticlePresets::get_presets_in_display_order()) {
		const String category = FPSParticlePresets::get_preset_category(preset);
		if (category != current_category) {
			current_category = category;
			popup->add_separator(TTRGET(category));
		}
		popup->add_item(FPSParticlePresets::get_preset_name(preset), PARTICLE_LIBRARY_PRESET_BASE + preset);
		if (!library_hover_preview_enabled()) { // The hover preview shows the description.
			popup->set_item_tooltip(-1, FPSParticlePresets::get_preset_description(preset));
		}
		const Ref<Texture2D> thumbnail = _get_preset_thumbnail(preset);
		if (thumbnail.is_valid()) {
			popup->set_item_icon(-1, thumbnail);
			popup->set_item_icon_max_width(-1, int(32 * EDSCALE));
		}
	}
	popup->add_separator(TTR("Recent Effects"));
	if (recent_effect_paths.is_empty()) {
		popup->add_item(TTR("No recent effects"), PARTICLE_LIBRARY_RECENT_BASE - 1);
		popup->set_item_disabled(-1, true);
	}
	for (int i = 0; i < recent_effect_paths.size(); i++) {
		const String &path = recent_effect_paths[i];
		popup->add_item(path.get_file().get_basename(), PARTICLE_LIBRARY_RECENT_BASE + i);
		if (!library_hover_preview_enabled()) {
			popup->set_item_tooltip(-1, path);
		}
		const Ref<Texture2D> thumbnail = _get_cached_effect_thumbnail(path);
		if (thumbnail.is_valid()) {
			popup->set_item_icon(-1, thumbnail);
			popup->set_item_icon_max_width(-1, int(32 * EDSCALE));
		}
	}
	_queue_library_thumbnails(); // Icons fill in while the menu is open.
	popup->add_separator();
	popup->add_check_item(TTR("Show Library Panel"), PARTICLE_LIBRARY_TOGGLE_PANEL);
	popup->set_item_checked(-1, show_left_panel && show_library_panel);
	popup->set_item_tooltip(-1, TTR("Show the searchable Effect Library below the emitter list."));
}

void FPSStudioParticleEditorPlugin::_on_library_menu_id(int p_id) {
	if (p_id == PARTICLE_LIBRARY_TOGGLE_PANEL) {
		_set_panel_visible(PARTICLE_PANEL_LIBRARY, !(show_left_panel && show_library_panel));
	} else if (p_id >= PARTICLE_LIBRARY_RECENT_BASE && p_id - PARTICLE_LIBRARY_RECENT_BASE < recent_effect_paths.size()) {
		_open_library_entry(recent_effect_paths[p_id - PARTICLE_LIBRARY_RECENT_BASE]);
	} else if (p_id >= PARTICLE_LIBRARY_PRESET_BASE && p_id - PARTICLE_LIBRARY_PRESET_BASE < FPSParticlePresets::PRESET_MAX) {
		_open_library_entry(vformat("preset:%d", p_id - PARTICLE_LIBRARY_PRESET_BASE));
	}
}

void FPSStudioParticleEditorPlugin::_open_library_entry(const String &p_entry) {
	const String path = p_entry;
	if (path.begins_with("preset:")) {
		const int template_type = PARTICLE_TEMPLATE_PRESET_BASE + path.trim_prefix("preset:").to_int();
		if (effect_dirty) {
			pending_new_template = template_type;
			pending_load_path = String();
			replace_document_confirmation->set_text(TTR("The current particle effect has unsaved changes. Discard them and create a new effect from the preset?"));
			replace_document_confirmation->popup_centered();
		} else {
			_create_effect_document(template_type);
		}
		return;
	}
	_request_load_particle_scene(path);
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
	undo_redo->create_action_for_history(TTR("Rename Particle Effect"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
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
	undo_redo->create_action_for_history(TTR("Rename Particle Emitter"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
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
	// Take over the cached scene so Add to Level and loads get this version.
	packed_scene->set_path(save_path, true);
	const Error save_error = ResourceSaver::save(packed_scene, save_path);
	if (save_error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not save particle effect to %s (error %d)."), save_path, save_error));
		return;
	}
	effect_path = save_path;
	effect->set_scene_file_path(save_path);
	effect_dirty = false;
	_on_timeline_loop_region_changed(preview_loop_start, preview_loop_end); // The range goes with a Save As.

	_remember_effect(save_path);
	_update_document_ui();
	EditorFileSystem::get_singleton()->scan_changes();
	// New Library / FileSystem thumbnail for the saved version.
	thumbnails.erase(save_path);
	thumbnail_mtimes.erase(save_path);
	_queue_thumbnail(save_path, save_path, -1, true);
	// Levels that already instance this effect pick up the new version.
	List<String> saved_paths;
	saved_paths.push_back(save_path);
	EditorNode::get_singleton()->preload_reimporting_with_path_in_edited_scenes(saved_paths);
	EditorNode::get_singleton()->reload_instances_with_path_in_edited_scenes();
}

void FPSStudioParticleEditorPlugin::_request_load_particle_scene(const String &p_path) {
	if (p_path.is_empty() || p_path == effect_path) {
		return;
	}
	if (effect_dirty) {
		pending_load_path = p_path;
		pending_new_template = -1;
		replace_document_confirmation->set_text(TTR("The current particle effect has unsaved changes. Discard them and open the selected effect?"));
		replace_document_confirmation->popup_centered();
	} else {
		_load_particle_scene(p_path);
	}
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
	particle_scene = _convert_to_effect_root(particle_scene);
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
	if (!effect_path.is_empty() && effect_dirty) {
		// The level instances the file, so it must contain what is on screen.
		_save_emitter_scene(effect_path);
	}
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
	// A saved effect is added as a scene instance (its children belong to the
	// effect scene). An unsaved effect is copied into the Level, so all of its
	// nodes must be owned by the Level root, otherwise saving the Level would
	// drop the emitters.
	const bool is_scene_instance = !instance->get_scene_file_path().is_empty();
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(vformat(TTR("Add %s to Level"), effect->get_name()));
	undo_redo->add_do_method(scene_root, "add_child", instance, true);
	undo_redo->add_do_method(instance, "set_owner", scene_root);
	if (!is_scene_instance) {
		undo_redo->add_do_method(this, "_assign_level_owners", instance, scene_root);
	}
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
	if (p_property == "lifetime" || p_property == "one_shot") {
		_rebuild_preview();
	} else {
		// ParticleEffect3D reads timing and event settings (light curve,
		// flicker, fade, base energy, ...) when it starts playing, so such
		// edits replay the preview at the current time to show up at once.
		if (p_property.begins_with("metadata/") || !ParticleEffect3D::is_emitter(emitter)) {
			preview_restart_queued = true;
		}
		_queue_preview_sync();
	}
	// The module stack refreshes itself for its own edits.
	if (!module_stack_editing && module_stack && module_stack->is_visible_in_tree()) {
		module_stack->refresh_values();
	}
}

static bool hide_files_toggle_button(Node *p_node, const String &p_tooltip_prefix) {
	if (Button *button = Object::cast_to<Button>(p_node)) {
		if (button->get_tooltip_text().begins_with(p_tooltip_prefix)) {
			button->hide();
			return true;
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (hide_files_toggle_button(p_node->get_child(i), p_tooltip_prefix)) {
			return true;
		}
	}
	return false;
}

void FPSStudioParticleEditorPlugin::_hide_code_editor_files_toggle() {
	if (particle_code_editor) {
		hide_files_toggle_button(particle_code_editor, TTR("Toggle Files Panel"));
	}
}

Ref<Shader> FPSStudioParticleEditorPlugin::_get_particle_code_shader(Node *p_emitter) const {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_emitter);
	if (!gpu) {
		return Ref<Shader>();
	}
	ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(gpu->get_process_material().ptr());
	if (!shader_material) {
		return Ref<Shader>();
	}
	Ref<Shader> shader = shader_material->get_shader();
	if (shader.is_null() || Object::cast_to<VisualShader>(shader.ptr()) || shader->get_mode() != Shader::MODE_PARTICLES) {
		return Ref<Shader>();
	}
	return shader;
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
	if (convert_shader_button) {
		convert_shader_button->set_disabled(!gpu || !Object::cast_to<ParticleProcessMaterial>(gpu->get_process_material().ptr()));
	}
	if (convert_graph_button) {
		convert_graph_button->set_disabled(!gpu || !Object::cast_to<ParticleProcessMaterial>(gpu->get_process_material().ptr()));
	}
	Ref<Shader> code_shader = _get_particle_code_shader(emitter);
	particle_graph_button->set_text(TTR("Create Node Graph"));
	particle_graph_button->set_visible(!visual_shader.is_valid() && code_shader.is_null());
	if (!emitter) {
		particle_logic_status->set_text(TTR("No emitter selected"));
	} else if (!ParticleEffect3D::is_emitter(emitter)) {
		particle_logic_status->set_text(TTR("Event track - no particle logic"));
	} else if (!gpu) {
		particle_logic_status->set_text(TTR("CPU - Built-in simulation"));
	} else if (visual_shader.is_valid()) {
		particle_logic_status->set_text(TTR("GPU - Particle VisualShader"));
	} else if (code_shader.is_valid()) {
		particle_logic_status->set_text(TTR("GPU - Particle Shader Code (changes apply live, Undo restores the modules)"));
	} else {
		particle_logic_status->set_text(TTR("GPU - ParticleProcessMaterial"));
	}
	if (visual_shader.is_valid()) {
		if (!particle_graph_editor && particle_logic_content) {
			particle_graph_editor = memnew(VisualShaderEditor);
			particle_graph_editor->set_toggle_list_control(nullptr);
			particle_graph_editor->set_shader_preview_visible(false);
			particle_graph_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			particle_graph_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			particle_graph_editor->set_custom_minimum_size(Size2(0, 120) * EDSCALE);
			particle_logic_content->add_child(particle_graph_editor);
		}
		if (particle_graph_editor) {
			// Re-editing the same shader resets the graph's view state.
			if (particle_graph_shader_id != visual_shader->get_instance_id()) {
				particle_graph_shader_id = visual_shader->get_instance_id();
				particle_graph_editor->edit_shader(visual_shader);
			}
			particle_graph_editor->show();
		}
	} else if (particle_graph_editor) {
		if (particle_graph_shader_id.is_valid()) {
			particle_graph_shader_id = ObjectID();
			particle_graph_editor->edit_shader(Ref<Shader>());
		}
		particle_graph_editor->hide();
	}
	if (code_shader.is_valid()) {
		if (!particle_code_editor && particle_logic_content) {
			particle_code_editor = memnew(TextShaderEditor);
			// The editor expects the Shader Editor's file list; give it a hidden
			// stand-in and hide the then meaningless "Toggle Files Panel" button.
			Control *files_stand_in = memnew(Control);
			files_stand_in->hide();
			particle_code_editor->add_child(files_stand_in);
			particle_code_editor->set_toggle_list_control(files_stand_in);
			particle_code_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			particle_code_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			// Size it only inside the tree: a resize starts its preview timer.
			particle_logic_content->add_child(particle_code_editor);
			particle_code_editor->set_custom_minimum_size(Size2(0, 120) * EDSCALE);
			callable_mp(this, &FPSStudioParticleEditorPlugin::_hide_code_editor_files_toggle).call_deferred();
		}
		if (particle_code_editor) {
			if (particle_code_shader_id != code_shader->get_instance_id()) {
				particle_code_shader_id = code_shader->get_instance_id();
				particle_code_editor->edit_shader(code_shader);
			}
			particle_code_editor->show();
		}
	} else if (particle_code_editor) {
		particle_code_shader_id = ObjectID();
		particle_code_editor->hide();
	}
	if (particle_logic_hint) {
		particle_logic_hint->set_visible(!visual_shader.is_valid() && code_shader.is_null());
	}
	Ref<Shader> watched_shader = visual_shader.is_valid() ? Ref<Shader>(visual_shader) : code_shader;
	if (watched_shader.is_valid()) {
		const Callable changed_callable = callable_mp(this, &FPSStudioParticleEditorPlugin::_on_curve_changed);
		if (!watched_shader->is_connected(SNAME("changed"), changed_callable)) {
			watched_shader->connect(SNAME("changed"), changed_callable);
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
		// The graph is edited inline; just bring it into view.
		_show_bottom_tab(PARTICLE_BOTTOM_TAB_LOGIC);
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

	// Start with a small, valid particle setup instead of an empty shader. This
	// gives a newly-created graph visible behavior and a clear place to continue
	// editing in the native VisualShader editor.
	Ref<VisualShaderNodeParticleSphereEmitter> spawn_shape;
	spawn_shape.instantiate();
	spawn_shape->set_input_port_default_value(0, 0.25);
	spawn_shape->set_input_port_default_value(1, 0.0);
	const int spawn_shape_id = visual_shader->get_valid_node_id(VisualShader::TYPE_START);
	visual_shader->add_node(VisualShader::TYPE_START, spawn_shape, Vector2(40.0, 90.0), spawn_shape_id);

	Ref<VisualShaderNodeParticleConeVelocity> initial_velocity;
	initial_velocity.instantiate();
	initial_velocity->set_input_port_default_value(0, Vector3(0.0, 1.0, 0.0));
	initial_velocity->set_input_port_default_value(1, 18.0);
	const int initial_velocity_id = visual_shader->get_valid_node_id(VisualShader::TYPE_START);
	visual_shader->add_node(VisualShader::TYPE_START, initial_velocity, Vector2(40.0, 300.0), initial_velocity_id);

	if (visual_shader->can_connect_nodes(VisualShader::TYPE_START, spawn_shape_id, 0, VisualShader::NODE_ID_OUTPUT, 4)) {
		visual_shader->connect_nodes(VisualShader::TYPE_START, spawn_shape_id, 0, VisualShader::NODE_ID_OUTPUT, 4);
	}
	if (visual_shader->can_connect_nodes(VisualShader::TYPE_START, initial_velocity_id, 0, VisualShader::NODE_ID_OUTPUT, 1)) {
		visual_shader->connect_nodes(VisualShader::TYPE_START, initial_velocity_id, 0, VisualShader::NODE_ID_OUTPUT, 1);
	}

	Ref<ShaderMaterial> shader_material;
	shader_material.instantiate();
	shader_material->set_shader(visual_shader);
	Ref<Material> previous_material = gpu->get_process_material();
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(TTR("Create Particle Node Graph"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(gpu, "set_process_material", shader_material);
	undo_redo->add_undo_method(gpu, "set_process_material", previous_material);
	undo_redo->add_do_reference(shader_material.ptr());
	if (previous_material.is_valid()) {
		undo_redo->add_undo_reference(previous_material.ptr());
	}
	Node *history_emitter = _get_edited_emitter();
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", history_emitter, effect_document_serial);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", history_emitter, effect_document_serial);
	undo_redo->commit_action();
	_show_bottom_tab(PARTICLE_BOTTOM_TAB_LOGIC);
	if (particle_graph_editor) {
		particle_graph_editor->fit_graph_to_content();
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
	undo_redo->create_action_for_history(TTR("Add Alpha Over Life Curve"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(material.ptr(), "set_alpha_curve", Ref<Texture2D>(texture));
	undo_redo->add_undo_method(material.ptr(), "set_alpha_curve", previous);
	undo_redo->add_do_reference(texture.ptr());
	if (previous.is_valid()) {
		undo_redo->add_undo_reference(previous.ptr());
	}
	Node *history_emitter = _get_edited_emitter();
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", history_emitter, effect_document_serial);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", history_emitter, effect_document_serial);
	undo_redo->commit_action();
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
	undo_redo->create_action_for_history(TTR("Add Scale Over Life Curve"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(material.ptr(), "set_param_texture", ParticleProcessMaterial::PARAM_SCALE, Ref<Texture2D>(texture));
	undo_redo->add_undo_method(material.ptr(), "set_param_texture", ParticleProcessMaterial::PARAM_SCALE, previous);
	undo_redo->add_do_reference(texture.ptr());
	if (previous.is_valid()) {
		undo_redo->add_undo_reference(previous.ptr());
	}
	Node *history_emitter = _get_edited_emitter();
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", history_emitter, effect_document_serial);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", history_emitter, effect_document_serial);
	undo_redo->commit_action();
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
	undo_redo->create_action_for_history(p_alpha ? TTR("Apply Alpha over Life Preset") : TTR("Apply Scale over Life Preset"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
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
	Node *history_emitter = _get_edited_emitter();
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", history_emitter, effect_document_serial);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", history_emitter, effect_document_serial);
	undo_redo->commit_action();
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
	alpha_curve_card->set_visible(material.is_valid());
	scale_curve_card->set_visible(material.is_valid());
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
	// The curve and its texture both emit "changed"; shaders emit per keystroke.
	_mark_document_dirty();
	_queue_preview_sync();
}

void FPSStudioParticleEditorPlugin::_on_preview_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && (key->get_keycode() == Key::COMMA || key->get_keycode() == Key::PERIOD)) {
		// One frame (1/30 s) back / forward, Shift: 0.1 s; held down it repeats.
		// Counted in whole steps: the time slider rounds to 0.01 s.
		const double step = key->is_shift_pressed() ? 0.1 : 1.0 / 30.0;
		const double dir = key->get_keycode() == Key::COMMA ? -1.0 : 1.0;
		_on_timeline_scrub((Math::round(preview_time / step) + dir) * step);
		preview_container->accept_event();
		return;
	}
	if (key.is_valid() && key->is_pressed() && timeline_view && (key->get_keycode() == Key::UP || key->get_keycode() == Key::DOWN) && !key->is_command_or_control_pressed() && !key->is_alt_pressed()) {
		timeline_view->set_current_time(preview_time);
		timeline_view->jump(key->get_keycode() == Key::UP ? -1 : 1, key->is_shift_pressed());
		preview_container->accept_event();
		return;
	}
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == Key::M && timeline_view && !key->is_command_or_control_pressed() && !key->is_alt_pressed()) {
			timeline_view->set_current_time(preview_time);
			timeline_view->add_marker_at_playhead();
		} else if (key->get_keycode() == Key::SPACE) {
			_on_timeline_play_toggle();
		} else if (key->get_keycode() == Key::HOME || (key->get_keycode() == Key::R && !key->is_command_or_control_pressed())) {
			_on_restart_pressed();
		} else if (key->get_keycode() == Key::F && !key->is_command_or_control_pressed()) {
			_frame_preview();
		} else if (!_handle_track_clipboard_key(key)) {
			return;
		}
		preview_container->accept_event();
		return;
	}
	Ref<InputEventMouseButton> mouse_button = p_event;
	if (mouse_button.is_valid() && mouse_button->is_pressed()) {
		preview_container->grab_focus();
	}
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
			// Orbit like the 3D viewport: dragging down raises the camera, dragging
			// right turns the scene with the mouse; honors the editor's orbit
			// sensitivity and axis inversion settings.
			const real_t radians_per_pixel = Math::deg_to_rad(real_t(EDITOR_GET("editors/3d/navigation_feel/orbit_sensitivity")));
			const real_t invert_x = bool(EDITOR_GET("editors/3d/navigation/invert_x_axis")) ? -1.0 : 1.0;
			const real_t invert_y = bool(EDITOR_GET("editors/3d/navigation/invert_y_axis")) ? -1.0 : 1.0;
			preview_camera_yaw -= relative.x * radians_per_pixel * invert_x;
			preview_camera_pitch = CLAMP(preview_camera_pitch + relative.y * radians_per_pixel * invert_y, Math::deg_to_rad(-85.0), Math::deg_to_rad(85.0));
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

// Where an emitter's particles mostly are: the emission shape, the cone they
// fly out in during their first ~0.3 s at their average speed (direction,
// spread, damping, gravity)
// and their size. The visibility AABB is a conservative culling box (e.g.
// debris falling 10 m with no floor) and would put the camera far away from
// small effects such as bullet impacts.
static AABB get_emitter_framing_bounds(Node *p_emitter, const AABB &p_visibility) {
	const GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_emitter);
	const Ref<ParticleProcessMaterial> process = gpu ? Ref<ParticleProcessMaterial>(gpu->get_process_material()) : Ref<ParticleProcessMaterial>();
	if (process.is_null()) {
		return p_visibility;
	}
	Vector3 extents;
	switch (process->get_emission_shape()) {
		case ParticleProcessMaterial::EMISSION_SHAPE_SPHERE:
		case ParticleProcessMaterial::EMISSION_SHAPE_SPHERE_SURFACE:
			extents = Vector3(1, 1, 1) * process->get_emission_sphere_radius();
			break;
		case ParticleProcessMaterial::EMISSION_SHAPE_BOX:
			extents = process->get_emission_box_extents();
			break;
		case ParticleProcessMaterial::EMISSION_SHAPE_RING:
			extents = Vector3(1, 1, 1) * MAX(process->get_emission_ring_radius(), process->get_emission_ring_height() * 0.5f);
			break;
		default:
			break;
	}
	const real_t t = MIN(real_t(gpu->get_lifetime()), real_t(0.3));
	const real_t speed = 0.5 * (process->get_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY) + process->get_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY));
	const real_t damping = process->get_param_max(ParticleProcessMaterial::PARAM_DAMPING);
	real_t reach = speed * t - 0.5 * damping * t * t;
	if (damping > 0.0) {
		reach = MIN(reach, speed * speed / (2.0 * damping)); // Stopped by damping.
	}
	reach = MAX(reach, real_t(0.0));
	const Vector3 fall = process->get_gravity() * (0.5 * t * t);

	// Cone of directions: its axis and a ring at the spread angle.
	Vector3 axis = process->get_direction();
	axis = axis.length_squared() > 0.0001 ? axis.normalized() : Vector3(1, 0, 0);
	const real_t spread = Math::deg_to_rad(CLAMP(real_t(process->get_spread()), real_t(0.0), real_t(180.0)));
	const Vector3 side = (Math::abs(axis.y) < 0.99 ? axis.cross(Vector3(0, 1, 0)) : axis.cross(Vector3(1, 0, 0))).normalized();
	const Vector3 up = axis.cross(side);
	AABB framing = AABB(Vector3(), Vector3());
	framing.expand_to(fall);
	for (int i = 0; i <= 8; i++) {
		Vector3 dir = axis;
		if (i < 8) {
			const real_t around = Math::TAU * i / 8.0;
			dir = axis * Math::cos(spread) + (side * Math::cos(around) + up * Math::sin(around)) * Math::sin(spread);
		}
		framing.expand_to(dir * reach + fall);
	}
	real_t particle_size = 0.2;
	if (const QuadMesh *quad = Object::cast_to<QuadMesh>(gpu->get_draw_pass_mesh(0).ptr())) {
		particle_size = quad->get_size().length() * 0.5 * process->get_param_max(ParticleProcessMaterial::PARAM_SCALE);
	}
	framing = framing.grow(particle_size);
	framing.position += process->get_emission_shape_offset() - extents;
	framing.size += extents * 2.0;
	const AABB clipped = framing.intersection(p_visibility);
	return clipped.has_volume() ? clipped : framing;
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
			emitter_bounds = node_3d->get_global_transform().xform(get_emitter_framing_bounds(emitter, emitter_bounds));
			if (!bounds_valid) {
				bounds = emitter_bounds;
				bounds_valid = true;
			} else {
				bounds = bounds.merge(emitter_bounds);
			}
		}
		if (bounds_valid) {
			preview_camera_target = bounds.get_center();
			const real_t radius = MAX(real_t(0.3), bounds.size.length() * 0.5);
			preview_camera_distance = CLAMP(radius / Math::tan(Math::deg_to_rad(preview_camera->get_fov()) * 0.5) * 1.1, 0.8, 48.0);
		}
	}
	_update_preview_camera();
}

// View menu check items that are remembered in the editor layout.
static const int PREVIEW_VIEW_OPTIONS[] = { 10, 11, 12, 13, 14, 15, 16, 17, 18 };

void FPSStudioParticleEditorPlugin::_on_preview_view_menu_id(int p_id) {
	PopupMenu *popup = preview_view_menu->get_popup();
	if (p_id >= 0 && p_id <= 3) {
		_set_preview_view_option(p_id, true);
	} else {
		_set_preview_view_option(p_id, !popup->is_item_checked(popup->get_item_index(p_id)));
	}
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
}

void FPSStudioParticleEditorPlugin::_set_preview_view_option(int p_id, bool p_checked) {
	PopupMenu *popup = preview_view_menu ? preview_view_menu->get_popup() : nullptr;
	if (!popup || popup->get_item_index(p_id) < 0) {
		return;
	}
	if (p_id >= 0 && p_id <= 3) {
		for (int i = 0; i <= 3; i++) {
			popup->set_item_checked(popup->get_item_index(i), i == p_id);
		}
		_apply_preview_environment(p_id);
		return;
	}
	const int index = popup->get_item_index(p_id);
	const bool checked = p_checked;
	popup->set_item_checked(index, checked);
	switch (p_id) {
		case 10:
			_on_grid_toggled(checked);
			break;
		case 11:
			if (preview_environment.is_valid()) {
				preview_environment->set_glow_enabled(checked);
				preview_environment->set_glow_intensity(0.9);
				preview_environment->set_glow_bloom(0.08);
				preview_environment->set_glow_hdr_bleed_threshold(0.85);
			}
			break;
		case 12:
			preview_show_bounds = checked;
			_update_preview_bounds();
			break;
		case 13:
			if (preview_floor_collision) {
				preview_floor_collision->set_visible(checked);
			}
			break;
		case 14:
			preview_show_radii = checked;
			_update_event_radii();
			break;
		case 15:
			if (preview_floor) {
				preview_floor->set_visible(checked);
			}
			break;
		case 16:
			preview_play_sound = checked;
			// Re-copying the source restores the volume when sound is turned back on.
			_sync_preview_properties();
			break;
		case 17:
			preview_play_shake = checked;
			_sync_preview_properties();
			break;
		case 18:
			preview_show_perf = checked;
			_update_preview_stats();
			break;
	}
}

void FPSStudioParticleEditorPlugin::_apply_preview_environment(int p_preset) {
	preview_environment_preset = p_preset;
	if (preview_environment.is_null() || !preview_key_light) {
		return;
	}
	struct Look {
		Color background;
		Color ambient;
		real_t ambient_energy;
		real_t key, fill, rim;
		Color key_color;
	};
	static const Look looks[] = {
		{ Color(0.035, 0.046, 0.065), Color(0.48, 0.53, 0.64), 0.72, 1.45, 0.48, 0.65, Color(0.9, 0.94, 1.0) }, // Studio
		{ Color(0.52, 0.66, 0.86), Color(0.78, 0.84, 0.95), 1.0, 2.3, 0.6, 0.2, Color(1.0, 0.96, 0.88) }, // Daylight
		{ Color(0.006, 0.008, 0.016), Color(0.2, 0.24, 0.36), 0.18, 0.12, 0.08, 0.05, Color(0.6, 0.7, 1.0) }, // Night
		{ Color(0.11, 0.1, 0.09), Color(0.62, 0.55, 0.47), 0.55, 0.9, 0.3, 0.25, Color(1.0, 0.85, 0.65) }, // Interior
	};
	const Look &look = looks[CLAMP(p_preset, 0, 3)];
	preview_environment->set_bg_color(look.background);
	preview_environment->set_ambient_light_color(look.ambient);
	preview_environment->set_ambient_light_energy(look.ambient_energy);
	preview_key_light->set_param(Light3D::PARAM_ENERGY, look.key);
	preview_key_light->set_color(look.key_color);
	preview_fill_light->set_param(Light3D::PARAM_ENERGY, look.fill);
	preview_rim_light->set_param(Light3D::PARAM_ENERGY, look.rim);
}

void FPSStudioParticleEditorPlugin::_update_preview_bounds() {
	if (!preview_effect) {
		return;
	}
	Node *source = _get_effect_root();
	Node *emitter = _get_edited_emitter();
	// Remove old overlays from every preview emitter.
	Vector<Node *> preview_emitters;
	_collect_emitters(preview_effect, preview_emitters);
	for (Node *preview_emitter : preview_emitters) {
		Node *old = preview_emitter->get_node_or_null(NodePath("PreviewBounds"));
		if (old) {
			preview_emitter->remove_child(old);
			old->queue_free();
		}
	}
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(emitter);
	if (!preview_show_bounds || !source || !gpu || !source->is_ancestor_of(gpu)) {
		return;
	}
	Node3D *target = Object::cast_to<Node3D>(preview_effect->get_node_or_null(source->get_path_to(gpu)));
	if (!target) {
		return;
	}
	const AABB box = gpu->get_visibility_aabb();
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_albedo(Color(1.0, 0.8, 0.2, 0.9));
	material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	Ref<ImmediateMesh> mesh;
	mesh.instantiate();
	mesh->surface_begin(Mesh::PRIMITIVE_LINES, material);
	for (int i = 0; i < 12; i++) {
		Vector3 a, b;
		box.get_edge(i, a, b);
		mesh->surface_add_vertex(a);
		mesh->surface_add_vertex(b);
	}
	mesh->surface_end();
	MeshInstance3D *overlay = memnew(MeshInstance3D);
	overlay->set_name("PreviewBounds");
	overlay->set_mesh(mesh);
	overlay->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	target->add_child(overlay);
}

// Rough number of live particles of an emitter at preview time p_time:
// ramps up over one lifetime after the start, holds while emitting and
// fades over one lifetime after emission ends.
static double estimate_live_particles(const Node *p_emitter, double p_time) {
	double start, emission_end, visual_end;
	ParticleEffect3D::get_emitter_window(p_emitter, start, emission_end, visual_end);
	const double amount = double(int(p_emitter->get("amount")));
	const double lifetime = MAX(0.01, double(p_emitter->get("lifetime")));
	if (p_time < start || (visual_end >= 0.0 && p_time > visual_end)) {
		return 0.0;
	}
	if (bool(p_emitter->get("one_shot"))) {
		const double explosiveness = CLAMP(double(p_emitter->get("explosiveness")), 0.0, 1.0);
		const double age = p_time - start;
		const double spawned = explosiveness >= 0.99 ? 1.0 : CLAMP(age / MAX(0.01, lifetime * (1.0 - explosiveness)), 0.0, 1.0);
		const double dying = CLAMP((age - lifetime) / lifetime, 0.0, 1.0);
		return amount * MAX(0.0, spawned - dying);
	}
	double fraction = CLAMP((p_time - start) / lifetime, 0.0, 1.0);
	if (emission_end >= 0.0 && p_time > emission_end) {
		fraction *= CLAMP(1.0 - (p_time - emission_end) / lifetime, 0.0, 1.0);
	}
	return amount * fraction;
}

// World-space area of one particle quad: mesh face times the average scale.
static double estimate_particle_area(Node *p_emitter) {
	const Ref<Mesh> mesh = FPSParticleModuleStack::get_draw_mesh(p_emitter);
	double face = 1.0;
	if (mesh.is_valid()) {
		Vector3 size = mesh->get_aabb().size;
		real_t axes[3] = { size.x, size.y, size.z };
		SortArray<real_t> sorter;
		sorter.sort(axes, 3);
		face = MAX(0.0001, double(axes[1]) * double(axes[2])); // The two largest axes.
	}
	double scale = 1.0;
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_emitter)) {
		if (ParticleProcessMaterial *process = Object::cast_to<ParticleProcessMaterial>(gpu->get_process_material().ptr())) {
			scale = 0.5 * (process->get_param_min(ParticleProcessMaterial::PARAM_SCALE) + process->get_param_max(ParticleProcessMaterial::PARAM_SCALE));
		}
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_emitter)) {
		scale = 0.5 * (cpu->get_param_min(CPUParticles3D::PARAM_SCALE) + cpu->get_param_max(CPUParticles3D::PARAM_SCALE));
	}
	return face * scale * scale;
}

// Cost model for one effect at 2560x1440, the resolution the game is tuned
// for. Rough rules of thumb, calibrated against tests/perf_water_explosion.gd
// (surface + underwater water explosion 3.8 m from the camera: ~3.6 screen
// layers of overdraw, measured ~1.2 ms GPU). Meant to point at what is
// expensive, not to replace a profiler.
static const double COST_SCREEN_LAYER_MS = 0.25; // One blended, textured layer over the whole screen.
static const double COST_EMITTER_GPU_MS = 0.03; // Process dispatch + draw of one live emitter.
static const double COST_CLOSE_UP_DISTANCE = 3.0; // Typical "it went off next to me" distance.
static const double COST_CLOSE_UP_FOV = 85.0; // The player camera (fps_character.tscn).
static const double COST_GPU_PARTICLE_MS = 0.0000025; // Simulating one GPU particle.
static const double COST_GPU_VERTEX_MS = 0.000002; // Drawing one particle quad.
static const double COST_CPU_PARTICLE_MS = 0.00035; // One live CPUParticles3D particle per frame.
static const double COST_EFFECT_GPU_WARN = 0.5;
static const double COST_EFFECT_GPU_HEAVY = 1.5;
static const double COST_EFFECT_CPU_WARN = 0.25;
static const double COST_EFFECT_CPU_HEAVY = 0.75;
static const double COST_TRACK_GPU_WARN = 0.3;
static const double COST_TRACK_GPU_HEAVY = 0.8;

struct FPSTrackCostTraits {
	bool emitter = false;
	bool cpu = false;
	bool opaque = false;
	bool lit = false;
	bool lit_per_vertex = false;
	bool soft = false;
	bool screen_read = false;
	bool collision = false;
	bool trails = false;
	bool sorted = false;
	bool light = false;
	bool light_shadow = false;
	bool decal = false;
	double area = 1.0;
	int amount = 0;
};

static Ref<Material> get_track_draw_material(Node *p_track) {
	if (GeometryInstance3D *geometry = Object::cast_to<GeometryInstance3D>(p_track)) {
		if (geometry->get_material_override().is_valid()) {
			return geometry->get_material_override();
		}
	}
	const Ref<Mesh> mesh = FPSParticleModuleStack::get_draw_mesh(p_track);
	if (mesh.is_valid() && mesh->get_surface_count() > 0) {
		return mesh->surface_get_material(0);
	}
	return Ref<Material>();
}

static FPSTrackCostTraits get_track_cost_traits(Node *p_track) {
	FPSTrackCostTraits traits;
	if (Light3D *light = Object::cast_to<Light3D>(p_track)) {
		traits.light = true;
		traits.light_shadow = light->has_shadow();
		return traits;
	}
	if (Object::cast_to<Decal>(p_track)) {
		traits.decal = true;
		return traits;
	}
	if (!ParticleEffect3D::is_emitter(p_track)) {
		return traits;
	}
	traits.emitter = true;
	traits.amount = int(p_track->get("amount"));
	traits.area = estimate_particle_area(p_track);
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_track)) {
		traits.collision = ParticleEffect3D::emitter_uses_collision(gpu);
		traits.trails = gpu->is_trail_enabled();
		traits.sorted = gpu->get_draw_order() == GPUParticles3D::DRAW_ORDER_VIEW_DEPTH;
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_track)) {
		traits.cpu = true;
		traits.sorted = cpu->get_draw_order() == CPUParticles3D::DRAW_ORDER_VIEW_DEPTH;
	}
	const Ref<Material> material = get_track_draw_material(p_track);
	if (BaseMaterial3D *base = Object::cast_to<BaseMaterial3D>(material.ptr())) {
		traits.lit = base->get_shading_mode() != BaseMaterial3D::SHADING_MODE_UNSHADED;
		traits.lit_per_vertex = base->get_shading_mode() == BaseMaterial3D::SHADING_MODE_PER_VERTEX;
		traits.opaque = base->get_transparency() == BaseMaterial3D::TRANSPARENCY_DISABLED;
		traits.soft = base->is_proximity_fade_enabled();
		traits.screen_read = base->get_feature(BaseMaterial3D::FEATURE_REFRACTION);
	} else if (ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(material.ptr())) {
		const Ref<Shader> shader = shader_material->get_shader();
		const String code = shader.is_valid() ? shader->get_code() : String();
		traits.lit = !code.contains("unshaded");
		traits.soft = code.contains("hint_depth_texture");
		traits.screen_read = code.contains("hint_screen_texture");
	}
	return traits;
}

struct FPSTrackCost {
	double gpu_ms = 0.0;
	double cpu_ms = 0.0;
	double live = 0.0;
	double overdraw = 0.0;
};

// Cost of one track at effect time p_time; p_screen_share is the share of
// the screen one particle covers from the current camera.
static FPSTrackCost estimate_track_cost(Node *p_track, const FPSTrackCostTraits &p_traits, double p_time, double p_screen_share) {
	FPSTrackCost cost;
	double start, active_end, visual_end;
	ParticleEffect3D::get_track_window(p_track, start, active_end, visual_end);
	const bool active = p_time >= start && (visual_end < 0.0 || p_time <= visual_end);
	if (p_traits.light) {
		if (active) {
			cost.gpu_ms = p_traits.light_shadow ? 0.35 : 0.05;
			cost.cpu_ms = p_traits.light_shadow ? 0.08 : 0.01;
		}
		return cost;
	}
	if (p_traits.decal) {
		cost.gpu_ms = active ? 0.02 : 0.0;
		return cost;
	}
	if (!p_traits.emitter) {
		cost.cpu_ms = active ? 0.005 : 0.0; // Sound, shake, damage: a little script time.
		return cost;
	}
	cost.live = estimate_live_particles(p_track, p_time);
	if (cost.live <= 0.0) {
		return cost;
	}
	cost.overdraw = cost.live * MIN(p_screen_share, 1.0);
	double fill = COST_SCREEN_LAYER_MS * (p_traits.opaque ? 0.3 : 1.0);
	if (p_traits.lit) {
		fill *= p_traits.lit_per_vertex ? 1.2 : 1.6;
	}
	if (p_traits.soft) {
		fill *= 1.2;
	}
	if (p_traits.screen_read) {
		fill *= 1.6;
	}
	double vertex = cost.live * COST_GPU_VERTEX_MS * (p_traits.trails ? 8.0 : 1.0);
	cost.gpu_ms = cost.overdraw * fill + vertex + COST_EMITTER_GPU_MS;
	if (p_traits.cpu) {
		cost.cpu_ms = cost.live * COST_CPU_PARTICLE_MS + p_traits.amount * 0.00003 + 0.01;
		if (p_traits.sorted) {
			cost.cpu_ms += cost.live * 0.0002;
		}
	} else {
		double simulate = p_traits.amount * COST_GPU_PARTICLE_MS;
		if (p_traits.collision) {
			simulate *= 3.0;
		}
		if (p_traits.trails) {
			simulate *= 4.0;
		}
		if (p_traits.sorted) {
			simulate += p_traits.amount * 0.000005;
		}
		cost.gpu_ms += simulate;
		cost.cpu_ms = 0.01;
	}
	return cost;
}

static String cost_color_tag(double p_value, double p_warn, double p_heavy) {
	if (p_value >= p_heavy) {
		return "[color=#ff6b6b]";
	}
	if (p_value >= p_warn) {
		return "[color=#ffcc55]";
	}
	return "[color=#8fe39a]";
}

static String format_ms(double p_ms) {
	return String::num(p_ms, p_ms < 0.1 ? 3 : 2) + " ms";
}

void FPSStudioParticleEditorPlugin::_update_preview_stats() {
	if (!preview_stats_label) {
		return;
	}
	Node *source = _get_effect_root();
	if (!source) {
		preview_stats_label->set_text(String());
		_update_preview_perf(String());
		return;
	}
	Vector<Node *> tracks;
	collect_tracks(source, tracks);
	const double fov = preview_camera ? Math::deg_to_rad(double(preview_camera->get_fov())) : Math::deg_to_rad(70.0);
	const double tan_half = Math::tan(fov * 0.5);
	const Vector3 camera_position = preview_camera && preview_camera->is_inside_tree() ? preview_camera->get_global_position() : Vector3(0, 0, preview_camera_distance);

	struct TrackInfo {
		Node *track = nullptr;
		FPSTrackCostTraits traits;
		double screen_share = 0.0;
		double close_share = 0.0; // From COST_CLOSE_UP_DISTANCE.
		double peak_gpu = 0.0;
		double peak_overdraw = 0.0;
	};
	Vector<TrackInfo> infos;
	int visible_tracks = 0;
	int max_particles = 0;
	bool any_screen_read = false;
	for (Node *track : tracks) {
		Node3D *track_3d = Object::cast_to<Node3D>(track);
		if (track_3d && !track_3d->is_visible()) {
			continue;
		}
		visible_tracks++;
		TrackInfo info;
		info.track = track;
		info.traits = get_track_cost_traits(track);
		if (info.traits.emitter) {
			max_particles += info.traits.amount;
			// Overdraw: the share of the preview image one particle covers
			// from this camera distance. Blended quads cost fill rate per layer,
			// which dominates on GPUs.
			Node3D *preview_track = preview_effect ? Object::cast_to<Node3D>(preview_effect->get_node_or_null(source->get_path_to(track))) : nullptr;
			const Vector3 position = preview_track && preview_track->is_inside_tree() ? preview_track->get_global_position() : Vector3();
			const double distance = MAX(0.25, double(camera_position.distance_to(position)));
			const double view_height = 2.0 * distance * tan_half;
			info.screen_share = info.traits.area / (view_height * view_height * (16.0 / 9.0));
			const double close_height = 2.0 * COST_CLOSE_UP_DISTANCE * Math::tan(Math::deg_to_rad(COST_CLOSE_UP_FOV) * 0.5);
			info.close_share = info.traits.area / (close_height * close_height * (16.0 / 9.0));
			any_screen_read = any_screen_read || info.traits.screen_read;
		}
		infos.push_back(info);
	}

	// Now, and the peak over the whole effect (sampled).
	double now_gpu = any_screen_read ? 0.1 : 0.0; // Copying the screen once for refraction.
	double now_cpu = 0.0;
	double now_live = 0.0;
	double now_overdraw = 0.0;
	for (TrackInfo &info : infos) {
		const FPSTrackCost cost = estimate_track_cost(info.track, info.traits, preview_time, info.screen_share);
		now_gpu += cost.gpu_ms;
		now_cpu += cost.cpu_ms;
		now_live += cost.live;
		now_overdraw += cost.overdraw;
	}
	double peak_gpu = 0.0;
	double peak_cpu = 0.0;
	double peak_overdraw = 0.0;
	double peak_time = 0.0;
	double close_peak_gpu = 0.0;
	const int samples = 60;
	const double span = MAX(0.1, preview_duration);
	for (int i = 0; i <= samples; i++) {
		const double t = span * i / samples;
		double gpu = any_screen_read ? 0.1 : 0.0;
		double cpu = 0.0;
		double overdraw = 0.0;
		double close_gpu = gpu;
		for (TrackInfo &info : infos) {
			const FPSTrackCost close_cost = estimate_track_cost(info.track, info.traits, t, info.close_share);
			const double close_track = close_cost.gpu_ms;
			close_gpu += close_track;
			const FPSTrackCost cost = estimate_track_cost(info.track, info.traits, t, info.screen_share);
			gpu += cost.gpu_ms;
			cpu += cost.cpu_ms;
			overdraw += cost.overdraw;
			info.peak_gpu = MAX(info.peak_gpu, MAX(cost.gpu_ms, close_track));
			info.peak_overdraw = MAX(info.peak_overdraw, MAX(cost.overdraw, close_cost.overdraw));
		}
		if (gpu > peak_gpu) {
			peak_gpu = gpu;
			peak_time = t;
		}
		peak_cpu = MAX(peak_cpu, cpu);
		peak_overdraw = MAX(peak_overdraw, overdraw);
		close_peak_gpu = MAX(close_peak_gpu, close_gpu);
	}

	// Per track: what makes it expensive (shown in the overlay and as a
	// colored cost next to each emitter in the list).
	Vector<String> issues;
	HashMap<ObjectID, double> track_peak;
	for (const TrackInfo &info : infos) {
		const int issues_before = issues.size();
		track_peak[info.track->get_instance_id()] = info.peak_gpu;
		const String name = info.track->get_name();
		const bool heavy = info.peak_gpu >= COST_TRACK_GPU_HEAVY;
		const bool warn = info.peak_gpu >= COST_TRACK_GPU_WARN;
		const String tag = heavy ? "[color=#ff6b6b]" : "[color=#ffcc55]";
		if (info.traits.light_shadow) {
			issues.push_back(tag + vformat(TTR("%s: the light casts shadows (expensive; turn Shadow off unless it really shows)."), name) + "[/color]");
		}
		if (!info.traits.emitter) {
			continue;
		}
		if (info.traits.cpu && info.traits.amount >= 300) {
			issues.push_back(String(info.traits.amount >= 1000 ? "[color=#ff6b6b]" : "[color=#ffcc55]") + vformat(TTR("%s: %d CPU particles cost CPU time every frame; GPU particles are much cheaper."), name, info.traits.amount) + "[/color]");
		}
		if (!warn) {
			continue;
		}
		if (info.peak_overdraw >= 4.0) {
			issues.push_back(tag + vformat(TTR("%s: overdraw up to %sx; big particles cover the screen many times (smaller Scale, fewer particles or shorter lifetime)."), name, String::num(info.peak_overdraw, 0)) + "[/color]");
		}
		if (info.traits.lit && info.peak_overdraw >= 2.0) {
			issues.push_back(tag + vformat(TTR("%s: lit material; Unshaded is cheaper for smoke, mist and sparks."), name) + "[/color]");
		}
		if (info.traits.collision && info.traits.amount >= 1000) {
			issues.push_back(tag + vformat(TTR("%s: collision on %d particles."), name, info.traits.amount) + "[/color]");
		}
		if (info.traits.trails && info.traits.amount >= 200) {
			issues.push_back(tag + vformat(TTR("%s: trails on %d particles."), name, info.traits.amount) + "[/color]");
		}
		if (issues.size() == issues_before) {
			issues.push_back(tag + vformat(TTR("%s: up to %s GPU."), name, format_ms(info.peak_gpu)) + "[/color]");
		}
	}
	if (emitter_tree && emitter_tree->get_root()) {
		for (TreeItem *item = emitter_tree->get_root()->get_first_child(); item; item = item->get_next()) {
			const Variant id = item->get_metadata(0);
			if (id.get_type() != Variant::INT) {
				continue;
			}
			const ObjectID object_id = ObjectID(uint64_t(id));
			Node *track = Object::cast_to<Node>(ObjectDB::get_instance(object_id));
			if (!track || !ParticleEffect3D::is_emitter(track)) {
				continue;
			}
			String detail = item->get_text(1);
			const int cut = detail.find(String::utf8(" · ~"));
			if (cut >= 0) {
				detail = detail.substr(0, cut);
			}
			const double *peak = track_peak.getptr(object_id);
			if (!peak) {
				item->set_text(1, detail);
				item->set_custom_color(1, emitter_tree->get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
				continue;
			}
			item->set_text(1, detail + String::utf8(" · ~") + String::num(*peak, *peak < 0.1 ? 2 : 1) + " ms");
			if (*peak >= COST_TRACK_GPU_HEAVY) {
				item->set_custom_color(1, Color(1.0, 0.42, 0.42));
			} else if (*peak >= COST_TRACK_GPU_WARN) {
				item->set_custom_color(1, Color(1.0, 0.8, 0.33));
			} else {
				item->set_custom_color(1, emitter_tree->get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
			}
			item->set_tooltip_text(1, vformat(TTR("Estimated GPU cost of this emitter at its busiest moment, at 2560x1440, from the current preview camera or from 3 m, whichever is more. Yellow from %s ms, red from %s ms."), String::num(COST_TRACK_GPU_WARN, 1), String::num(COST_TRACK_GPU_HEAVY, 1)));
		}
	}

	// Short summary next to the transport controls.
	// Judged by the worse of this view and a close-up: fill cost grows fast
	// as the camera gets near.
	const double worst_gpu = MAX(peak_gpu, close_peak_gpu);
	const bool heavy = worst_gpu >= COST_EFFECT_GPU_HEAVY || peak_cpu >= COST_EFFECT_CPU_HEAVY;
	const bool warn = worst_gpu >= COST_EFFECT_GPU_WARN || peak_cpu >= COST_EFFECT_CPU_WARN;
	String text = vformat(TTR("%d tracks, ~%d / %d particles, GPU ~%s"), visible_tracks, int(Math::round(now_live)), max_particles, format_ms(now_gpu));
	preview_stats_label->set_text(warn ? String::utf8("⚠ ") + text : text);
	preview_stats_label->set_modulate(Color(1, 1, 1, warn ? 1.0 : 0.6));
	if (!warn) {
		preview_stats_label->remove_theme_color_override(SceneStringName(font_color));
	} else {
		preview_stats_label->add_theme_color_override(SceneStringName(font_color), preview_stats_label->get_theme_color(heavy ? SNAME("error_color") : SNAME("warning_color"), EditorStringName(Editor)));
	}
	preview_stats_label->set_tooltip_text(TTR("Visible tracks, estimated live / maximum particles and estimated GPU time of the effect right now. Details in the Performance Info box over the preview (View menu)."));

	// Measured render time of the preview viewport (includes the grid, floor
	// and post effects, at the preview's own resolution).
	String measured;
	if (preview_viewport) {
		const RID rid = preview_viewport->get_viewport_rid();
		const double gpu = RS::get_singleton()->viewport_get_measured_render_time_gpu(rid);
		const double cpu = RS::get_singleton()->viewport_get_measured_render_time_cpu(rid);
		preview_measured_gpu_ms = preview_measured_gpu_ms <= 0.0 ? gpu : Math::lerp(preview_measured_gpu_ms, gpu, 0.3);
		preview_measured_cpu_ms = preview_measured_cpu_ms <= 0.0 ? cpu : Math::lerp(preview_measured_cpu_ms, cpu, 0.3);
		if (preview_measured_gpu_ms > 0.0) {
			const Vector2i size = preview_viewport->get_size();
			measured = vformat(TTR("Preview render %dx%d (measured): GPU %s, CPU %s"), size.x, size.y, format_ms(preview_measured_gpu_ms), format_ms(preview_measured_cpu_ms));
		}
	}

	String bbcode = "[b]" + TTR("Cost estimate") + "[/b] [color=#ffffff88]" + TTR("one effect, 2560x1440, this camera distance") + "[/color]\n";
	bbcode += "GPU  " + cost_color_tag(now_gpu, COST_EFFECT_GPU_WARN, COST_EFFECT_GPU_HEAVY) + format_ms(now_gpu) + "[/color]   " + TTR("peak") + " " + cost_color_tag(peak_gpu, COST_EFFECT_GPU_WARN, COST_EFFECT_GPU_HEAVY) + format_ms(peak_gpu) + "[/color] " + vformat(TTR("at %s s"), String::num(peak_time, 2)) + "\n";
	bbcode += "     " + vformat(TTR("close-up (%s m, game FOV): peak"), String::num(COST_CLOSE_UP_DISTANCE, 0)) + " " + cost_color_tag(close_peak_gpu, COST_EFFECT_GPU_WARN, COST_EFFECT_GPU_HEAVY) + format_ms(close_peak_gpu) + "[/color]\n";
	bbcode += "CPU  " + cost_color_tag(now_cpu, COST_EFFECT_CPU_WARN, COST_EFFECT_CPU_HEAVY) + format_ms(now_cpu) + "[/color]   " + TTR("peak") + " " + cost_color_tag(peak_cpu, COST_EFFECT_CPU_WARN, COST_EFFECT_CPU_HEAVY) + format_ms(peak_cpu) + "[/color]\n";
	bbcode += vformat(TTR("Particles ~%d / %d, overdraw %sx (peak %sx)"), int(Math::round(now_live)), max_particles, String::num(now_overdraw, now_overdraw < 10.0 ? 1 : 0), String::num(peak_overdraw, peak_overdraw < 10.0 ? 1 : 0));
	if (preview_viewport) {
		const int draw_calls = preview_viewport->get_render_info(Viewport::RENDER_INFO_TYPE_VISIBLE, Viewport::RENDER_INFO_DRAW_CALLS_IN_FRAME);
		const int primitives = preview_viewport->get_render_info(Viewport::RENDER_INFO_TYPE_VISIBLE, Viewport::RENDER_INFO_PRIMITIVES_IN_FRAME);
		bbcode += vformat(TTR(", %d draws, %s tris"), draw_calls, primitives >= 1000 ? String::num(primitives / 1000.0, 1) + "k" : itos(primitives));
	}
	if (!measured.is_empty()) {
		bbcode += "\n[color=#ffffff88]" + measured + "[/color]";
	}
	if (heavy) {
		bbcode += "\n[color=#ff6b6b]" + TTR("Heavy: several of these on screen at once will cost a lot of frame time.") + "[/color]";
	} else if (warn) {
		bbcode += "\n[color=#ffcc55]" + TTR("Moderate: fine as a one-off, watch out if it plays often (impacts, muzzle flashes).") + "[/color]";
	}
	for (int i = 0; i < MIN(issues.size(), 5); i++) {
		bbcode += "\n" + issues[i];
	}
	if (issues.size() > 5) {
		bbcode += "\n" + vformat(TTR("... and %d more"), issues.size() - 5);
	}
	_update_preview_perf(bbcode);
}

void FPSStudioParticleEditorPlugin::_update_preview_perf(const String &p_bbcode) {
	if (!preview_perf_panel || !preview_perf_label) {
		return;
	}
	const bool show = preview_show_perf && !p_bbcode.is_empty();
	preview_perf_panel->set_visible(show);
	if (show && preview_perf_label->get_text() != p_bbcode) {
		preview_perf_label->set_text(p_bbcode);
		preview_perf_panel->reset_size();
	}
}

void FPSStudioParticleEditorPlugin::_layout_preview_overlays() {
	if (!preview_container) {
		return;
	}
	const Size2 size = preview_container->get_size();
	if (preview_compare) {
		preview_compare->set_position(Point2());
		preview_compare->set_size(size);
	}
	if (preview_perf_panel) {
		preview_perf_panel->set_position(Point2(8, 8) * EDSCALE);
		preview_perf_label->set_custom_minimum_size(Size2(MIN(real_t(460) * EDSCALE, MAX(real_t(120), size.x - 16 * EDSCALE)), 0));
		preview_perf_panel->reset_size();
	}
}

// Markers live in the effect (root metadata), so they are part of the
// document: undoable and saved with the file.
void FPSStudioParticleEditorPlugin::_on_timeline_markers_committed(const Array &p_old, const Array &p_new) {
	Node *effect = _get_effect_root();
	if (!effect) {
		return;
	}
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(TTR("Edit Timeline Markers"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(this, "_apply_timeline_markers", effect, p_new);
	undo_redo->add_undo_method(this, "_apply_timeline_markers", effect, p_old);
	undo_redo->commit_action();
}

void FPSStudioParticleEditorPlugin::_apply_timeline_markers(Object *p_effect, const Array &p_markers) {
	Node *effect = Object::cast_to<Node>(p_effect);
	if (!effect) {
		return;
	}
	if (p_markers.is_empty()) {
		effect->remove_meta(SNAME("_fps_timeline_markers"));
	} else {
		effect->set_meta(SNAME("_fps_timeline_markers"), p_markers.duplicate(true));
	}
	if (effect == _get_effect_root()) {
		_mark_document_dirty();
		if (timeline_view) {
			timeline_view->refresh();
		}
	}
}

// New random look for the next run, unless Lock Seed keeps the current one.
void FPSStudioParticleEditorPlugin::_reseed_preview_for_next_run() {
	if (!preview_lock_seed) {
		reseed_preview_emitters(preview_effect);
	}
}

void FPSStudioParticleEditorPlugin::_on_lock_seed_toggled(bool p_locked) {
	preview_lock_seed = p_locked;
	if (preview_lock_seed_toggle) {
		preview_lock_seed_toggle->set_pressed_no_signal(p_locked);
	}
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_particle", "lock_seed", p_locked);
}

// Another random look at the same moment (e.g. to browse variants of a
// paused frame); with Lock Seed on it becomes the kept look.
void FPSStudioParticleEditorPlugin::_on_new_seed_pressed() {
	if (!preview_effect) {
		return;
	}
	reseed_preview_emitters(preview_effect);
	_restart_preview_at(preview_time);
}

void FPSStudioParticleEditorPlugin::_take_compare_snapshot() {
	if (!preview_viewport || !preview_compare) {
		return;
	}
	const Ref<Image> image = preview_viewport->get_texture()->get_image();
	if (image.is_null() || image->is_empty()) {
		return;
	}
	preview_compare->set_snapshot(ImageTexture::create_from_image(image), vformat(TTR("snapshot at %s s"), String::num(preview_time, 2)));
	preview_compare->set_visible(true);
	_layout_preview_overlays();
	if (preview_compare_menu) {
		PopupMenu *popup = preview_compare_menu->get_popup();
		popup->set_item_disabled(popup->get_item_index(1), false);
		popup->set_item_checked(popup->get_item_index(1), true);
		popup->set_item_disabled(popup->get_item_index(2), false);
	}
}

void FPSStudioParticleEditorPlugin::_on_compare_menu_id(int p_id) {
	if (!preview_compare || !preview_compare_menu) {
		return;
	}
	PopupMenu *popup = preview_compare_menu->get_popup();
	switch (p_id) {
		case 0:
			_take_compare_snapshot();
			break;
		case 1: {
			const bool show = !popup->is_item_checked(popup->get_item_index(1)) && preview_compare->has_snapshot();
			popup->set_item_checked(popup->get_item_index(1), show);
			preview_compare->set_visible(show);
		} break;
		case 2:
			preview_compare->clear_snapshot();
			preview_compare->set_visible(false);
			popup->set_item_checked(popup->get_item_index(1), false);
			popup->set_item_disabled(popup->get_item_index(1), true);
			popup->set_item_disabled(popup->get_item_index(2), true);
			break;
	}
}

void FPSStudioParticleEditorPlugin::_on_convert_shader_pressed() {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	if (!gpu) {
		return;
	}
	Ref<ParticleProcessMaterial> process = gpu->get_process_material();
	if (process.is_null()) {
		return;
	}
	Ref<ParticleProcessMaterialConversionPlugin> converter;
	converter.instantiate();
	Ref<ShaderMaterial> shader_material = converter->convert(process);
	// Never replace working modules with an empty shader (e.g. when the
	// renderer cannot provide the generated code).
	if (shader_material.is_null() || shader_material->get_shader().is_null() || shader_material->get_shader()->get_code().strip_edges().is_empty()) {
		EditorNode::get_singleton()->show_warning(TTR("Could not convert the process material to shader code. The emitter was left unchanged."));
		return;
	}
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(TTR("Convert Particle Modules to Shader Code"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_property(gpu, "process_material", shader_material);
	undo_redo->add_undo_property(gpu, "process_material", process);
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", gpu, effect_document_serial);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", gpu, effect_document_serial);
	undo_redo->commit_action(); // The history callback rebuilds the module stack.
	_show_bottom_tab(PARTICLE_BOTTOM_TAB_LOGIC);
}

void FPSStudioParticleEditorPlugin::_on_convert_graph_pressed() {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	if (!gpu) {
		return;
	}
	Ref<ParticleProcessMaterial> process = gpu->get_process_material();
	if (process.is_null()) {
		return;
	}
	String warnings;
	Ref<VisualShader> visual_shader = FPSParticleVisualShaderConverter::convert(process, warnings);
	if (visual_shader.is_null()) {
		EditorNode::get_singleton()->show_warning(warnings.is_empty() ? TTR("Could not convert the modules to a node graph. The emitter was left unchanged.") : warnings);
		return;
	}
	Ref<ShaderMaterial> shader_material;
	shader_material.instantiate();
	shader_material->set_shader(visual_shader);
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(TTR("Convert Particle Modules to Node Graph"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_property(gpu, "process_material", shader_material);
	undo_redo->add_undo_property(gpu, "process_material", process);
	undo_redo->add_do_reference(shader_material.ptr());
	undo_redo->add_undo_reference(process.ptr());
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", gpu, effect_document_serial);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", gpu, effect_document_serial);
	undo_redo->commit_action(); // The history callback shows the graph editor.
	_show_bottom_tab(PARTICLE_BOTTOM_TAB_LOGIC);
	if (particle_graph_editor) {
		particle_graph_editor->fit_graph_to_content();
	}
	if (!warnings.is_empty()) {
		EditorToaster::get_singleton()->popup_str(warnings, EditorToaster::SEVERITY_WARNING);
	}
}

void FPSStudioParticleEditorPlugin::_update_toolbar_icons() {
	if (!timeline_play_button || !timeline_play_button->is_inside_tree()) {
		return;
	}
	Node *toolbar = timeline_play_button->get_parent();
	for (int i = 0; i < toolbar->get_child_count(); i++) {
		Button *button = Object::cast_to<Button>(toolbar->get_child(i));
		if (button && button->has_meta("icon_name")) {
			button->set_button_icon(button->get_editor_theme_icon(StringName(String(button->get_meta("icon_name")))));
		}
	}
	timeline_play_button->set_button_icon(timeline_play_button->get_editor_theme_icon(preview_playing ? SNAME("Pause") : SNAME("Play")));
	for (Button *button : layout_icon_buttons) {
		button->set_button_icon(button->get_editor_theme_icon(StringName(String(button->get_meta("icon_name")))));
	}
	_apply_compact_layout();
	_apply_workspace_theme();
}

void FPSStudioParticleEditorPlugin::_apply_workspace_theme() {
	// Setting an override on the workspace root emits its theme_changed signal,
	// which calls back into here; ignore that re-entry.
	if (applying_workspace_theme || !screen || !screen->is_inside_tree()) {
		return;
	}
	applying_workspace_theme = true;
	// Colors come from the active editor theme so the workspace matches the
	// rest of Godot in any theme: panels use the base (dock) color on top of
	// the darker editor background, separated by the split gaps.
	const Color background = screen->get_theme_color(SNAME("background"), EditorStringName(Editor));
	const Color surface = screen->get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	const Color accent = screen->get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color mono = screen->get_theme_color(SNAME("mono_color"), EditorStringName(Editor));
	const Color font = screen->get_theme_color(SceneStringName(font_color), EditorStringName(Editor));
	const real_t radius = 4 * EDSCALE;
	const real_t margin = 4 * EDSCALE;
	auto make_surface = [&](const Color &p_color, real_t p_radius, real_t p_margin_h, real_t p_margin_v) {
		Ref<StyleBoxFlat> style;
		style.instantiate();
		style->set_bg_color(p_color);
		style->set_corner_radius_all(p_radius);
		style->set_content_margin_individual(p_margin_h, p_margin_v, p_margin_h, p_margin_v);
		style->set_border_width_all(Math::round(EDSCALE));
		style->set_border_color(Color(mono, 0.06));
		return style;
	};

	Ref<StyleBoxFlat> root_style = make_surface(background, 0, margin, margin);
	root_style->set_border_width_all(0);
	workspace_background->add_theme_style_override(SceneStringName(panel), root_style);
	if (Control *workspace = Object::cast_to<Control>(workspace_background->get_child(0))) {
		workspace->add_theme_constant_override("separation", Math::round(margin));
	}
	document_toolbar_surface->add_theme_style_override(SceneStringName(panel), make_surface(surface, radius, 8 * EDSCALE, 2 * EDSCALE));
	const Ref<StyleBoxFlat> panel_style = make_surface(surface, radius, margin, margin);
	left_surface->add_theme_style_override(SceneStringName(panel), panel_style);
	preview_surface->add_theme_style_override(SceneStringName(panel), panel_style);
	right_surface->add_theme_style_override(SceneStringName(panel), panel_style);
	if (bottom_tabs) {
		Ref<StyleBoxFlat> tabs_panel = make_surface(surface, 0, margin, margin);
		tabs_panel->set_corner_radius(CORNER_BOTTOM_LEFT, radius);
		tabs_panel->set_corner_radius(CORNER_BOTTOM_RIGHT, radius);
		bottom_tabs->add_theme_style_override(SceneStringName(panel), tabs_panel);
		Ref<StyleBoxFlat> tabbar = make_surface(surface.lerp(background, 0.45), 0, 0, 0);
		tabbar->set_corner_radius(CORNER_TOP_LEFT, radius);
		tabbar->set_corner_radius(CORNER_TOP_RIGHT, radius);
		bottom_tabs->add_theme_style_override(SNAME("tabbar_background"), tabbar);
		Ref<StyleBoxFlat> tab_selected = make_surface(surface, 0, 12 * EDSCALE, 4 * EDSCALE);
		tab_selected->set_border_width_all(0);
		tab_selected->set_border_width(SIDE_TOP, Math::round(2 * EDSCALE));
		tab_selected->set_border_color(accent);
		tab_selected->set_corner_radius(CORNER_TOP_LEFT, radius);
		tab_selected->set_corner_radius(CORNER_TOP_RIGHT, radius);
		bottom_tabs->add_theme_style_override(SNAME("tab_selected"), tab_selected);
		bottom_tabs->add_theme_color_override(SNAME("font_selected_color"), font);
	}
	if (emitter_tree) {
		// Same selection look as the Model workspace: accent strip + tint.
		Ref<StyleBoxFlat> selected;
		selected.instantiate();
		selected->set_bg_color(Color(accent, 0.2));
		selected->set_border_width(SIDE_LEFT, Math::round(2 * EDSCALE));
		selected->set_border_color(accent);
		emitter_tree->add_theme_style_override(SNAME("selected"), selected);
		emitter_tree->add_theme_style_override(SNAME("selected_focus"), selected);
		emitter_tree->add_theme_color_override(SNAME("font_selected_color"), font);
		Ref<StyleBoxEmpty> transparent;
		transparent.instantiate();
		emitter_tree->add_theme_style_override(SceneStringName(panel), transparent);
		if (effects_tree) {
			effects_tree->add_theme_style_override(SNAME("selected"), selected);
			effects_tree->add_theme_style_override(SNAME("selected_focus"), selected);
			effects_tree->add_theme_color_override(SNAME("font_selected_color"), font);
			effects_tree->add_theme_style_override(SceneStringName(panel), transparent);
		}
	}
	applying_workspace_theme = false;
}

void FPSStudioParticleEditorPlugin::_apply_preview_solo() {
	Node *source = _get_effect_root();
	Node *solo = Object::cast_to<Node>(ObjectDB::get_instance(solo_emitter_id));
	if (!source || !preview_effect) {
		return;
	}
	if (solo && !source->is_ancestor_of(solo)) {
		solo_emitter_id = ObjectID();
		solo = nullptr;
	}
	if (!solo) {
		return;
	}
	// Solo is preview only: hide every other track in the preview copy.
	Vector<Node *> emitters;
	collect_tracks(source, emitters);
	for (Node *emitter : emitters) {
		Node *preview_node = preview_effect->get_node_or_null(source->get_path_to(emitter));
		if (Node3D *preview_3d = Object::cast_to<Node3D>(preview_node)) {
			if (emitter != solo) {
				preview_3d->set_visible(false);
			}
		}
	}
}

static void apply_preview_event_toggles(Node *p_node, bool p_sound, bool p_shake) {
	if (!p_node) {
		return;
	}
	if (!p_sound) {
		if (AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(p_node)) {
			player->stop();
			player->set_volume_db(-80.0);
		}
	}
	if (!p_shake) {
		if (CameraShake3D *shake = Object::cast_to<CameraShake3D>(p_node)) {
			shake->stop();
			shake->set_strength(0.0);
		}
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		apply_preview_event_toggles(p_node->get_child(i), p_sound, p_shake);
	}
}

void FPSStudioParticleEditorPlugin::_apply_preview_event_toggles() {
	// Preview only: the edited effect keeps its sound volume and shake strength.
	apply_preview_event_toggles(preview_effect, preview_play_sound, preview_play_shake);
}

void FPSStudioParticleEditorPlugin::_set_emitter_visible(Object *p_emitter, bool p_visible) {
	Node3D *emitter = Object::cast_to<Node3D>(p_emitter);
	if (!emitter) {
		return;
	}
	emitter->set_visible(p_visible);
	_mark_document_dirty();
	_sync_preview_properties();
	_restart_preview_at(preview_time);
	if (timeline_view) {
		timeline_view->refresh();
	}
}

// Gives a copied track (and its child tracks) its own process material,
// meshes and draw material so editing it does not change the original.
// External textures stay shared.
static void duplicate_track_resources(Node *p_copy) {
	Node *copy = p_copy;
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(copy)) {
		if (gpu->get_process_material().is_valid()) {
			gpu->set_process_material(gpu->get_process_material()->duplicate_deep(RESOURCE_DEEP_DUPLICATE_INTERNAL));
		}
		for (int pass = 0; pass < gpu->get_draw_passes(); pass++) {
			if (gpu->get_draw_pass_mesh(pass).is_valid()) {
				gpu->set_draw_pass_mesh(pass, gpu->get_draw_pass_mesh(pass)->duplicate_deep(RESOURCE_DEEP_DUPLICATE_INTERNAL));
			}
		}
	} else if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(copy)) {
		if (cpu->get_mesh().is_valid()) {
			cpu->set_mesh(cpu->get_mesh()->duplicate_deep(RESOURCE_DEEP_DUPLICATE_INTERNAL));
		}
	}
	if (GeometryInstance3D *geometry = Object::cast_to<GeometryInstance3D>(copy)) {
		if (geometry->get_material_override().is_valid()) {
			geometry->set_material_override(geometry->get_material_override()->duplicate_deep(RESOURCE_DEEP_DUPLICATE_INTERNAL));
		}
	}
	for (int i = 0; i < copy->get_child_count(); i++) {
		duplicate_track_resources(copy->get_child(i));
	}
}

static Node *make_track_copy(Node *p_source) {
	Node *copy = p_source ? p_source->duplicate(Node::DUPLICATE_SIGNALS | Node::DUPLICATE_SCRIPTS) : nullptr;
	if (copy) {
		duplicate_track_resources(copy);
	}
	return copy;
}

void FPSStudioParticleEditorPlugin::_duplicate_emitter(Node *p_emitter) {
	Node *effect = _get_effect_root();
	if (!effect || !p_emitter || p_emitter == effect) {
		return;
	}
	Node *parent = p_emitter->get_parent();
	Node *copy = make_track_copy(p_emitter);
	if (!copy) {
		return;
	}
	copy->set_name(parent->prevalidate_child_name(copy, String(p_emitter->get_name()) + "Copy"));
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(vformat(TTR("Duplicate %s"), p_emitter->get_name()), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(parent, "add_child", copy, true);
	undo_redo->add_do_method(parent, "move_child", copy, p_emitter->get_index() + 1);
	undo_redo->add_do_method(this, "_assign_level_owners", copy, effect);
	undo_redo->add_do_method(copy, "set_owner", effect);
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", copy, effect_document_serial);
	undo_redo->add_do_reference(copy);
	undo_redo->add_undo_method(parent, "remove_child", copy);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", p_emitter, effect_document_serial);
	undo_redo->commit_action();
}

void FPSStudioParticleEditorPlugin::_on_duplicate_emitter_pressed() {
	_duplicate_emitter(_get_edited_emitter());
}

void FPSStudioParticleEditorPlugin::_copy_track(Node *p_track) {
	Node *effect = _get_effect_root();
	if (!effect || !p_track || p_track == effect || !effect->is_ancestor_of(p_track)) {
		return;
	}
	// The clipboard holds its own copy, so it survives editing, closing or
	// switching the effect and pastes into any effect.
	Node *copy = make_track_copy(p_track);
	if (!copy) {
		return;
	}
	if (track_clipboard) {
		memdelete(track_clipboard);
	}
	track_clipboard = copy;
	if (timeline_view) {
		timeline_view->set_can_paste(true);
	}
}

void FPSStudioParticleEditorPlugin::_cut_track(Node *p_track) {
	Node *effect = _get_effect_root();
	if (!effect || !p_track || p_track == effect) {
		return;
	}
	_copy_track(p_track);
	_set_edited_emitter(p_track);
	_remove_selected_emitter();
}

void FPSStudioParticleEditorPlugin::_paste_tracks(Node *p_after) {
	Node *effect = _get_effect_root();
	if (!effect || !track_clipboard) {
		return;
	}
	Node *copy = make_track_copy(track_clipboard);
	if (!copy) {
		return;
	}
	copy->set_name(effect->prevalidate_child_name(copy, track_clipboard->get_name()));
	// Keep sub-emitter links that still resolve (inside the pasted subtree, or
	// a same-named sibling in this effect); clear the rest.
	Vector<Node *> stack;
	stack.push_back(copy);
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		for (int i = 0; i < node->get_child_count(); i++) {
			stack.push_back(node->get_child(i));
		}
		GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(node);
		if (!gpu || gpu->get_sub_emitter().is_empty()) {
			continue;
		}
		const NodePath path = gpu->get_sub_emitter();
		Node *target = gpu->get_node_or_null(path);
		if (!target && gpu == copy && String(path).begins_with("../")) {
			target = effect->get_node_or_null(NodePath(String(path).substr(3)));
		}
		if (!Object::cast_to<GPUParticles3D>(target) || target == gpu) {
			gpu->set_sub_emitter(NodePath());
		}
	}
	const int index = (p_after && p_after->get_parent() == effect) ? p_after->get_index() + 1 : effect->get_child_count();
	Node *previous_emitter = _get_edited_emitter();
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action_for_history(vformat(TTR("Paste %s"), copy->get_name()), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(effect, "add_child", copy, true);
	undo_redo->add_do_method(effect, "move_child", copy, index);
	undo_redo->add_do_method(this, "_assign_level_owners", copy, effect);
	undo_redo->add_do_method(copy, "set_owner", effect);
	undo_redo->add_do_method(this, "_on_emitter_hierarchy_history_changed", copy, effect_document_serial);
	undo_redo->add_do_reference(copy);
	undo_redo->add_undo_method(effect, "remove_child", copy);
	undo_redo->add_undo_method(this, "_on_emitter_hierarchy_history_changed", previous_emitter, effect_document_serial);
	undo_redo->commit_action();
}

bool FPSStudioParticleEditorPlugin::_handle_track_clipboard_key(const Ref<InputEventKey> &p_key) {
	if (p_key.is_null() || !p_key->is_pressed() || p_key->is_echo() || !p_key->is_command_or_control_pressed() || p_key->is_alt_pressed()) {
		return false;
	}
	Node *track = _get_edited_emitter();
	switch (p_key->get_keycode()) {
		case Key::C:
			_copy_track(track);
			return track != nullptr;
		case Key::X:
			_cut_track(track);
			return track != nullptr;
		case Key::V:
			_paste_tracks(track);
			return track_clipboard != nullptr;
		default:
			return false;
	}
}

void FPSStudioParticleEditorPlugin::_on_emitter_tree_gui_input(const Ref<InputEvent> &p_event) {
	// Only while the list itself has focus (renaming uses its own line edit).
	if (_handle_track_clipboard_key(p_event)) {
		emitter_tree->accept_event();
	}
}

void FPSStudioParticleEditorPlugin::_on_timeline_track_action(Object *p_track, int p_action) {
	Node *track = Object::cast_to<Node>(p_track);
	if (p_action == FPSParticleTimeline::ACTION_PASTE) {
		_paste_tracks(track); // Pasted after the clicked track, or at the end.
		return;
	}
	if (!track) {
		return;
	}
	switch (p_action) {
		case FPSParticleTimeline::ACTION_DUPLICATE:
			_duplicate_emitter(track);
			break;
		case FPSParticleTimeline::ACTION_COPY:
			_copy_track(track);
			break;
		case FPSParticleTimeline::ACTION_CUT:
			_cut_track(track);
			break;
		case FPSParticleTimeline::ACTION_RENAME: {
			_set_edited_emitter(track);
			if (emitter_tree && emitter_tree->get_selected()) {
				emitter_tree->grab_focus();
				emitter_tree->edit_selected(true);
			}
		} break;
		case FPSParticleTimeline::ACTION_DELETE:
			_set_edited_emitter(track);
			_remove_selected_emitter();
			break;
		case FPSParticleTimeline::ACTION_SOLO:
			solo_emitter_id = solo_emitter_id == track->get_instance_id() ? ObjectID() : track->get_instance_id();
			if (timeline_view) {
				timeline_view->set_solo_track(Object::cast_to<Node>(ObjectDB::get_instance(solo_emitter_id)));
			}
			_sync_preview_properties();
			_restart_preview_at(preview_time);
			break;
		case FPSParticleTimeline::ACTION_TOGGLE_VISIBLE: {
			Node3D *track_3d = Object::cast_to<Node3D>(track);
			if (!track_3d) {
				break;
			}
			EditorUndoRedoManager *undo_redo = get_undo_redo();
			undo_redo->create_action_for_history(track_3d->is_visible() ? TTR("Mute Track") : TTR("Unmute Track"), EditorUndoRedoManager::GLOBAL_HISTORY);
			undo_redo->force_fixed_history();
			undo_redo->add_do_method(this, "_set_emitter_visible", track, !track_3d->is_visible());
			undo_redo->add_undo_method(this, "_set_emitter_visible", track, track_3d->is_visible());
			undo_redo->commit_action();
		} break;
	}
}

void FPSStudioParticleEditorPlugin::_on_timeline_play_toggle() {
	if (!_get_effect_root()) {
		return;
	}
	if (!preview_playing) {
		_rewind_preview_if_finished();
	}
	_set_preview_playing(!preview_playing);
}

void FPSStudioParticleEditorPlugin::_update_time_label() {
	if (timeline_time_label) {
		timeline_time_label->set_text(vformat("%s / %s s", String::num(preview_time, 2), String::num(preview_duration, 2)));
	}
	if (time_label) {
		time_label->set_text(vformat("%s / %s s", String::num(preview_time, 2), String::num(preview_duration, 2)));
	}
}

void FPSStudioParticleEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS && texture_generator_dialog && texture_generator_dialog->is_visible() && texture_generator_animate && texture_generator_animate->is_pressed()) {
		const double delta = get_process_delta_time();
		const int frame_columns = texture_generator_output ? MAX(1, texture_generator_output->get_selected_id()) : 1;
		const int playback_fps = texture_generator_fps ? MAX(1, int(texture_generator_fps->get_value())) : 12;
		const bool is_sheet = frame_columns > 1;
		const double duration = is_sheet && texture_generator_duration ? texture_generator_duration->get_value() : (is_sheet ? double(frame_columns * frame_columns) / playback_fps : 1.0);
		texture_generator_time_value += delta;
		if (texture_generator_time_value >= duration) {
			texture_generator_time_value = Math::fposmod(texture_generator_time_value, duration);
			texture_generator_preview_frame = -1;
		}
		_update_texture_time_controls();
		if (texture_generator_show_sheet && texture_generator_show_sheet->is_pressed()) {
			// The whole sheet is shown; nothing to animate.
		} else if (is_sheet) {
			const int current_frame = CLAMP(int(Math::floor((texture_generator_time_value / MAX(0.001, duration)) * (frame_columns * frame_columns))), 0, frame_columns * frame_columns - 1);
			if (current_frame != texture_generator_preview_frame) {
				_regenerate_texture(true);
			}
		} else if (texture_preview_task < 0 || !texture_preview_has_pending) {
			// Request the next frame as soon as the previous one is under way, so
			// the preview runs as fast as the renderer allows. Playback changes only
			// the live preview; an exported PNG stays valid for Apply to Emitter.
			_regenerate_texture(true);
		}
	}
	if (p_what == NOTIFICATION_PROCESS && screen && screen->is_visible_in_tree()) {
		preview_stats_accumulator += get_process_delta_time();
		if (preview_stats_accumulator >= 0.25) {
			preview_stats_accumulator = 0.0;
			_update_preview_stats();
		}
	}
	if (p_what == NOTIFICATION_PROCESS && texture_preview_task >= 0) {
		_poll_texture_preview_task();
	}
	if (p_what == NOTIFICATION_PROCESS && (thumbnail_frames >= 0 || !thumbnail_queue.is_empty() || (screen && screen->is_visible_in_tree()))) {
		_process_thumbnails();
	}
	if (p_what == NOTIFICATION_PROCESS && (!hover_key.is_empty() || (screen && screen->is_visible_in_tree()))) {
		_process_library_hover(get_process_delta_time());
	}
	if (p_what == NOTIFICATION_PROCESS && screen && screen->is_visible_in_tree() && preview_playing && preview_effect) {
		const double delta = get_process_delta_time();
		preview_time += delta * preview_speed;
		// A loop region on the timeline always loops; otherwise the Loop toggle
		// decides whether the whole effect repeats.
		const bool has_region = preview_loop_end > preview_loop_start;
		const double loop_from = has_region ? preview_loop_start : 0.0;
		const double loop_to = has_region ? MIN(preview_loop_end, MAX(preview_duration, preview_loop_end)) : preview_duration;
		if (preview_time >= loop_to) {
			if (preview_loop) {
				const double span = MAX(0.01, loop_to - loop_from);
				preview_time = loop_from + Math::fmod(preview_time - loop_to, span);
				_reseed_preview_for_next_run();
				_restart_preview_at(preview_time);
			} else {
				// Loop off: stop on the end (flag) frame and hold it.
				preview_time = loop_to;
				_set_preview_playing(false);
				if (has_region) {
					_restart_preview_at(preview_time);
				}
			}
		}
		if (timeline) {
			timeline->set_value_no_signal(preview_time);
		}
		if (timeline_view) {
			timeline_view->set_current_time(preview_time);
		}
		// Keep the slider in sync every frame, but avoid invalidating the transport
		// row and its parent split containers whenever the formatted time changes.
		preview_time_label_update_accumulator += delta;
		if (preview_time_label_update_accumulator >= 0.1 || !preview_playing) {
			preview_time_label_update_accumulator = Math::fmod(preview_time_label_update_accumulator, 0.1);
			_update_time_label();
		}
	}
	if (p_what == NOTIFICATION_ENTER_TREE) {
		if (FileSystemDock::get_singleton() && asset_tooltip_plugin.is_null()) {
			Ref<FPSAssetTooltipPlugin> tooltip_plugin;
			tooltip_plugin.instantiate();
			tooltip_plugin->plugin_id = get_instance_id();
			asset_tooltip_plugin = tooltip_plugin;
			FileSystemDock::get_singleton()->add_resource_tooltip_plugin(asset_tooltip_plugin);
		}
		if (EditorResourcePreview::get_singleton() && shader_preview_generator.is_null()) {
			shader_preview_generator = Ref<FPSCachedShaderPreviewGenerator>(memnew(FPSCachedShaderPreviewGenerator)); // Not instantiate(): that makes the base class.
			EditorResourcePreview::get_singleton()->add_preview_generator(shader_preview_generator);
		}
		if (EditorFileSystem::get_singleton()) {
			EditorFileSystem::get_singleton()->connect("filesystem_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_queue_asset_thumbnail_scan), CONNECT_REFERENCE_COUNTED);
		}
		_queue_asset_thumbnail_scan();
	}
	if (p_what == NOTIFICATION_EXIT_TREE) {
		if (asset_tooltip_plugin.is_valid() && FileSystemDock::get_singleton()) {
			FileSystemDock::get_singleton()->remove_resource_tooltip_plugin(asset_tooltip_plugin);
		}
		asset_tooltip_plugin.unref();
		if (shader_preview_generator.is_valid() && EditorResourcePreview::get_singleton()) {
			EditorResourcePreview::get_singleton()->remove_preview_generator(shader_preview_generator);
		}
		shader_preview_generator.unref();
		if (EditorFileSystem::get_singleton() && EditorFileSystem::get_singleton()->is_connected("filesystem_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_queue_asset_thumbnail_scan))) {
			EditorFileSystem::get_singleton()->disconnect("filesystem_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_queue_asset_thumbnail_scan));
		}
	}
	if (p_what == NOTIFICATION_PREDELETE && texture_preview_task >= 0) {
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(texture_preview_task);
		texture_preview_task = -1;
	}
	if (p_what == NOTIFICATION_PREDELETE && preview_effect) {
		preview_effect->queue_free();
		preview_effect = nullptr;
	}
	if (p_what == NOTIFICATION_PREDELETE && track_clipboard) {
		memdelete(track_clipboard); // Never in the tree; would leak on exit.
		track_clipboard = nullptr;
	}
}

void FPSStudioParticleEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_emitter_name_changed"), &FPSStudioParticleEditorPlugin::_on_emitter_name_changed);
	ClassDB::bind_method(D_METHOD("bake_preset_scene", "preset", "path"), &FPSStudioParticleEditorPlugin::bake_preset_scene);
	// Opens an effect scene in the workspace (asks first when there are unsaved changes); for tools and tests.
	ClassDB::bind_method(D_METHOD("open_effect", "path"), &FPSStudioParticleEditorPlugin::_request_load_particle_scene);
	ClassDB::bind_method(D_METHOD("_on_effect_name_changed"), &FPSStudioParticleEditorPlugin::_on_effect_name_changed);
	ClassDB::bind_method(D_METHOD("_on_emitter_hierarchy_history_changed", "preferred_emitter", "document_serial"), &FPSStudioParticleEditorPlugin::_on_emitter_hierarchy_history_changed);
	ClassDB::bind_method(D_METHOD("_apply_emitter_timing", "emitter", "delay", "duration"), &FPSStudioParticleEditorPlugin::_apply_emitter_timing);
	ClassDB::bind_method(D_METHOD("_apply_timeline_markers", "effect", "markers"), &FPSStudioParticleEditorPlugin::_apply_timeline_markers);
	ClassDB::bind_method(D_METHOD("_assign_level_owners", "instance", "owner"), &FPSStudioParticleEditorPlugin::_assign_level_owners);
	ClassDB::bind_method(D_METHOD("_set_emitter_visible", "emitter", "visible"), &FPSStudioParticleEditorPlugin::_set_emitter_visible);
}

void FPSStudioParticleEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		callable_mp(this, &FPSStudioParticleEditorPlugin::_update_toolbar_icons).call_deferred();
		if (screen && screen->is_inside_tree()) {
			const Callable layout_callable = callable_mp(this, &FPSStudioParticleEditorPlugin::_update_document_toolbar_layout);
			if (!screen->get_viewport()->is_connected(SNAME("size_changed"), layout_callable)) {
				screen->get_viewport()->connect(SNAME("size_changed"), layout_callable, CONNECT_DEFERRED);
			}
			layout_callable.call_deferred();
		}
	}
	// A detached Timeline & Logic window belongs to this workspace: dock it
	// while another workspace is active and detach it again on return.
	if (!p_visible && _is_bottom_floating()) {
		bottom_float_on_return = true;
		bottom_window_rect = bottom_window_wrapper->get_window_rect();
		bottom_window_screen = bottom_window_wrapper->get_window_screen();
		bottom_window_wrapper->set_window_enabled(false);
	} else if (p_visible && bottom_float_on_return) {
		bottom_float_on_return = false;
		callable_mp(this, &FPSStudioParticleEditorPlugin::_toggle_bottom_floating).call_deferred();
	}
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
	// Without a saved layout (first start) the workspace keeps its defaults;
	// afterwards every panel, split, tab and window comes back as the user
	// left it.
	if (p_layout.is_null() || !p_layout->has_section("ParticleWorkspace")) {
		return;
	}
	const String section = "ParticleWorkspace";
	show_left_panel = p_layout->get_value(section, "show_emitters", true);
	show_right_panel = p_layout->get_value(section, "show_properties", true);
	show_library_panel = p_layout->get_value(section, "show_library", false);
	show_preview_panel = p_layout->get_value(section, "show_preview", true);
	show_bottom_panel = p_layout->get_value(section, "show_timeline", true);
	if (!show_preview_panel && !show_bottom_panel) {
		show_preview_panel = true; // The center column always shows something.
	}
	if (bottom_tabs) {
		const int tab = p_layout->get_value(section, "bottom_tab", int(PARTICLE_BOTTOM_TAB_TIMELINE));
		bottom_tabs->set_current_tab(CLAMP(tab, 0, bottom_tabs->get_tab_count() - 1));
	}
	bottom_window_rect = p_layout->get_value(section, "timeline_window_rect", Rect2i());
	bottom_window_screen = p_layout->get_value(section, "timeline_window_screen", -1);
	texture_generator_size = p_layout->get_value(section, "texture_generator_size", Size2i());
	// Preview View menu: environment, overlays and preview events.
	if (preview_view_menu) {
		PopupMenu *view_popup = preview_view_menu->get_popup();
		if (p_layout->has_section_key(section, "view_environment")) {
			_set_preview_view_option(CLAMP(int(p_layout->get_value(section, "view_environment", 0)), 0, 3), true);
		}
		for (int id : PREVIEW_VIEW_OPTIONS) {
			const String key = "view_option_" + itos(id);
			if (!p_layout->has_section_key(section, key)) {
				continue;
			}
			const bool checked = p_layout->get_value(section, key, false);
			if (checked != view_popup->is_item_checked(view_popup->get_item_index(id))) {
				_set_preview_view_option(id, checked);
			}
		}
	}
	// A detached Timeline & Logic window is detached again when the
	// workspace is shown (it docks while another workspace is active).
	if (p_layout->get_value(section, "timeline_floating", false) && show_bottom_panel) {
		if (screen && screen->is_visible_in_tree()) {
			callable_mp(this, &FPSStudioParticleEditorPlugin::_toggle_bottom_floating).call_deferred();
		} else {
			bottom_float_on_return = true;
		}
	}
	_apply_panel_visibility();
	if (!p_layout->has_section_key(section, "main_split_ratio") ||
			!p_layout->has_section_key(section, "properties_split_ratio") ||
			!p_layout->has_section_key(section, "logic_split_ratio")) {
		return;
	}
	panel_layout_main_ratio = p_layout->get_value(section, "main_split_ratio", 0.0);
	panel_layout_properties_ratio = p_layout->get_value(section, "properties_split_ratio", 0.0);
	panel_layout_logic_ratio = p_layout->get_value(section, "logic_split_ratio", 0.0);
	panel_layout_pending = true;
	// Applied once the workspace has a size: the editor often starts on
	// another workspace, so this also runs when the splits are resized.
	callable_mp(this, &FPSStudioParticleEditorPlugin::_restore_panel_layout).call_deferred();
}

void FPSStudioParticleEditorPlugin::get_window_layout(Ref<ConfigFile> p_layout) {
	if (p_layout.is_null() || !main_panel_split || !preview_properties_split || !preview_logic_split) {
		return;
	}
	const String section = "ParticleWorkspace";
	p_layout->set_value(section, "show_emitters", show_left_panel);
	p_layout->set_value(section, "show_properties", show_right_panel);
	p_layout->set_value(section, "show_library", show_library_panel);
	p_layout->set_value(section, "show_preview", show_preview_panel);
	p_layout->set_value(section, "show_timeline", show_bottom_panel);
	if (bottom_tabs) {
		p_layout->set_value(section, "bottom_tab", bottom_tabs->get_current_tab());
	}
	const bool floating = _is_bottom_floating() || bottom_float_on_return;
	p_layout->set_value(section, "timeline_floating", floating);
	if (_is_bottom_floating()) {
		bottom_window_rect = bottom_window_wrapper->get_window_rect();
		bottom_window_screen = bottom_window_wrapper->get_window_screen();
	}
	if (bottom_window_rect.has_area()) {
		p_layout->set_value(section, "timeline_window_rect", bottom_window_rect);
		p_layout->set_value(section, "timeline_window_screen", bottom_window_screen);
	}
	if (texture_generator_dialog && texture_generator_dialog->is_visible()) {
		texture_generator_size = texture_generator_dialog->get_size();
	}
	if (texture_generator_size.x > 0 && texture_generator_size.y > 0) {
		p_layout->set_value(section, "texture_generator_size", texture_generator_size);
	}
	if (preview_view_menu) {
		PopupMenu *view_popup = preview_view_menu->get_popup();
		p_layout->set_value(section, "view_environment", preview_environment_preset);
		for (int id : PREVIEW_VIEW_OPTIONS) {
			p_layout->set_value(section, "view_option_" + itos(id), view_popup->is_item_checked(view_popup->get_item_index(id)));
		}
	}
	if (panel_layout_pending) {
		// Not applied yet (the workspace was never shown): keep the saved split.
		p_layout->set_value(section, "main_split_ratio", panel_layout_main_ratio);
		p_layout->set_value(section, "properties_split_ratio", panel_layout_properties_ratio);
		p_layout->set_value(section, "logic_split_ratio", panel_layout_logic_ratio);
		return;
	}
	const real_t main_extent = main_panel_split->get_size().width;
	const real_t properties_extent = preview_properties_split->get_size().width;
	const real_t logic_extent = preview_logic_split->get_size().height;
	if (main_extent <= 0.0 || properties_extent <= 0.0 || logic_extent <= 0.0) {
		return; // Hidden: the config keeps the previously saved ratios.
	}
	p_layout->set_value(section, "main_split_ratio", main_panel_split->get_split_offset() / main_extent);
	p_layout->set_value(section, "properties_split_ratio", preview_properties_split->get_split_offset() / properties_extent);
	p_layout->set_value(section, "logic_split_ratio", preview_logic_split->get_split_offset() / logic_extent);
}

void FPSStudioParticleEditorPlugin::_restore_panel_layout() {
	if (!panel_layout_pending || !main_panel_split || !preview_properties_split || !preview_logic_split) {
		return;
	}
	const real_t main_extent = main_panel_split->get_size().width;
	const real_t properties_extent = preview_properties_split->get_size().width;
	const real_t logic_extent = preview_logic_split->get_size().height;
	if (!main_panel_split->is_visible_in_tree() || main_extent <= 0.0 || properties_extent <= 0.0 || logic_extent <= 0.0) {
		return;
	}
	// The workspace settles over a few resizes after it is first shown, and a
	// split offset does not scale with its container. Keep re-applying the
	// saved proportions on every resize until the user drags a split.
	main_panel_split->set_split_offset(Math::round(panel_layout_main_ratio * main_extent));
	preview_properties_split->set_split_offset(Math::round(panel_layout_properties_ratio * properties_extent));
	preview_logic_split->set_split_offset(Math::round(panel_layout_logic_ratio * logic_extent));
}

void FPSStudioParticleEditorPlugin::_on_panel_split_drag_ended() {
	panel_layout_pending = false; // The user's split now wins over the saved one.
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
}

bool FPSStudioParticleEditorPlugin::_is_bottom_floating() const {
	return bottom_window_wrapper && bottom_window_wrapper->get_window_enabled();
}

void FPSStudioParticleEditorPlugin::_set_panel_visible(int p_panel, bool p_visible) {
	switch (p_panel) {
		case PARTICLE_PANEL_LEFT: {
			show_left_panel = p_visible;
		} break;
		case PARTICLE_PANEL_RIGHT: {
			show_right_panel = p_visible;
		} break;
		case PARTICLE_PANEL_LIBRARY: {
			show_library_panel = p_visible;
			if (p_visible) {
				show_left_panel = true;
			}
		} break;
		case PARTICLE_PANEL_PREVIEW: {
			show_preview_panel = p_visible;
			if (!p_visible) {
				// The center column always shows something: hiding the preview
				// docks and shows Timeline & Logic in its place.
				if (_is_bottom_floating()) {
					bottom_window_wrapper->set_window_enabled(false);
				}
				show_bottom_panel = true;
			}
		} break;
		case PARTICLE_PANEL_BOTTOM: {
			if (!p_visible && _is_bottom_floating()) {
				bottom_window_wrapper->set_window_enabled(false);
			}
			show_bottom_panel = p_visible;
			if (!p_visible) {
				show_preview_panel = true;
			}
		} break;
		default:
			return;
	}
	_apply_panel_visibility();
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
}

void FPSStudioParticleEditorPlugin::_apply_panel_visibility() {
	if (!left_panel_root || !right_panel_root || !preview_panel_root || !bottom_window_wrapper) {
		return;
	}
	const bool floating = _is_bottom_floating();
	left_panel_root->set_visible(show_left_panel);
	if (library_panel_root) {
		library_panel_root->set_visible(show_library_panel);
	}
	right_panel_root->set_visible(show_right_panel);
	preview_panel_root->set_visible(show_preview_panel);
	bottom_window_wrapper->set_visible(show_bottom_panel && !floating);
	if (bottom_maximize_button) {
		bottom_maximize_button->set_pressed_no_signal(!show_preview_panel);
		bottom_maximize_button->set_visible(!floating);
	}
	if (bottom_float_button) {
		bottom_float_button->set_visible(bottom_window_wrapper->is_window_available());
		bottom_float_button->set_tooltip_text(floating ? TTR("Dock Timeline & Logic back into the workspace.") : TTR("Move Timeline & Logic into a separate window. Closing the window docks it back."));
	}
}

void FPSStudioParticleEditorPlugin::_update_layout_menu() {
	if (!layout_menu) {
		return;
	}
	PopupMenu *popup = layout_menu->get_popup();
	const bool floating = _is_bottom_floating();
	popup->set_item_checked(popup->get_item_index(PARTICLE_LAYOUT_LEFT), show_left_panel);
	popup->set_item_checked(popup->get_item_index(PARTICLE_LAYOUT_LIBRARY), show_left_panel && show_library_panel);
	popup->set_item_checked(popup->get_item_index(PARTICLE_LAYOUT_PREVIEW), show_preview_panel);
	popup->set_item_checked(popup->get_item_index(PARTICLE_LAYOUT_BOTTOM), show_bottom_panel);
	popup->set_item_checked(popup->get_item_index(PARTICLE_LAYOUT_RIGHT), show_right_panel);
	popup->set_item_checked(popup->get_item_index(PARTICLE_LAYOUT_MAXIMIZE_BOTTOM), show_bottom_panel && !show_preview_panel);
	popup->set_item_disabled(popup->get_item_index(PARTICLE_LAYOUT_MAXIMIZE_BOTTOM), floating);
	const int float_index = popup->get_item_index(PARTICLE_LAYOUT_FLOAT_BOTTOM);
	popup->set_item_checked(float_index, floating);
	popup->set_item_disabled(float_index, !bottom_window_wrapper || !bottom_window_wrapper->is_window_available());
}

void FPSStudioParticleEditorPlugin::_on_layout_menu_id(int p_id) {
	switch (p_id) {
		case PARTICLE_LAYOUT_LEFT: {
			_set_panel_visible(PARTICLE_PANEL_LEFT, !show_left_panel);
		} break;
		case PARTICLE_LAYOUT_LIBRARY: {
			_set_panel_visible(PARTICLE_PANEL_LIBRARY, !(show_left_panel && show_library_panel));
		} break;
		case PARTICLE_LAYOUT_PREVIEW: {
			_set_panel_visible(PARTICLE_PANEL_PREVIEW, !show_preview_panel);
		} break;
		case PARTICLE_LAYOUT_BOTTOM: {
			_set_panel_visible(PARTICLE_PANEL_BOTTOM, !show_bottom_panel);
		} break;
		case PARTICLE_LAYOUT_RIGHT: {
			_set_panel_visible(PARTICLE_PANEL_RIGHT, !show_right_panel);
		} break;
		case PARTICLE_LAYOUT_SHOW_TIMELINE: {
			_show_bottom_tab(PARTICLE_BOTTOM_TAB_TIMELINE);
		} break;
		case PARTICLE_LAYOUT_SHOW_LOGIC: {
			_show_bottom_tab(PARTICLE_BOTTOM_TAB_LOGIC);
		} break;
		case PARTICLE_LAYOUT_MAXIMIZE_BOTTOM: {
			_toggle_bottom_maximized();
		} break;
		case PARTICLE_LAYOUT_FLOAT_BOTTOM: {
			_toggle_bottom_floating();
		} break;
		case PARTICLE_LAYOUT_RESET: {
			_reset_panel_layout();
		} break;
	}
	_update_layout_menu();
}

void FPSStudioParticleEditorPlugin::_reset_panel_layout() {
	if (_is_bottom_floating()) {
		bottom_window_wrapper->set_window_enabled(false);
	}
	show_left_panel = true;
	show_right_panel = true;
	show_preview_panel = true;
	show_bottom_panel = true;
	show_library_panel = false;
	bottom_window_rect = Rect2i();
	bottom_window_screen = -1;
	texture_generator_size = Size2i();
	panel_layout_pending = false;
	if (bottom_tabs) {
		bottom_tabs->set_current_tab(PARTICLE_BOTTOM_TAB_TIMELINE);
	}
	_apply_panel_visibility();
	for (SplitContainer *split : { main_panel_split, preview_properties_split, preview_logic_split }) {
		if (split) {
			split->set_split_offset(0);
		}
	}
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
}

void FPSStudioParticleEditorPlugin::_on_bottom_close_pressed() {
	// In the separate window the close button returns the panel to its
	// place in the workspace; docked, it hides the panel.
	if (_is_bottom_floating()) {
		bottom_window_wrapper->set_window_enabled(false);
		_apply_panel_visibility();
		return;
	}
	_set_panel_visible(PARTICLE_PANEL_BOTTOM, false);
}

void FPSStudioParticleEditorPlugin::_toggle_bottom_maximized() {
	if (_is_bottom_floating()) {
		return;
	}
	if (show_preview_panel) {
		_set_panel_visible(PARTICLE_PANEL_PREVIEW, false);
	} else {
		_set_panel_visible(PARTICLE_PANEL_PREVIEW, true);
	}
}

void FPSStudioParticleEditorPlugin::_toggle_bottom_floating() {
	if (!bottom_window_wrapper || !bottom_window_wrapper->is_window_available()) {
		return;
	}
	const bool make_floating = !_is_bottom_floating();
	if (make_floating) {
		// The detached panel leaves the whole center column to the preview.
		show_bottom_panel = true;
		show_preview_panel = true;
	}
	if (make_floating && bottom_window_rect.has_area()) {
		bottom_window_wrapper->restore_window(bottom_window_rect, bottom_window_screen);
	} else {
		if (!make_floating) {
			bottom_window_rect = bottom_window_wrapper->get_window_rect();
			bottom_window_screen = bottom_window_wrapper->get_window_screen();
		}
		bottom_window_wrapper->set_window_enabled(make_floating);
	}
	_apply_panel_visibility();
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
}

void FPSStudioParticleEditorPlugin::_on_bottom_window_visibility_changed(bool p_visible) {
	if (!p_visible) {
		// Closing the separate window docks the panel back into the workspace.
		show_bottom_panel = true;
	}
	_apply_panel_visibility();
	if (EditorNode::get_singleton() && !bottom_float_on_return) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
}

void FPSStudioParticleEditorPlugin::_show_bottom_tab(int p_tab) {
	if (!bottom_tabs) {
		return;
	}
	if (!show_bottom_panel) {
		_set_panel_visible(PARTICLE_PANEL_BOTTOM, true);
	}
	bottom_tabs->set_current_tab(p_tab);
	if (_is_bottom_floating()) {
		bottom_window_wrapper->grab_window_focus();
	}
}

void FPSStudioParticleEditorPlugin::_on_bottom_tab_changed(int p_tab) {
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
	// A node graph needs room: give Particle Logic about half of the column
	// when it is shown, but never shrink a panel the user made larger.
	const bool has_graph = particle_graph_editor && particle_graph_editor->is_visible();
	const bool has_code = particle_code_editor && particle_code_editor->is_visible();
	if (p_tab == PARTICLE_BOTTOM_TAB_LOGIC && (has_graph || has_code)) {
		callable_mp(this, &FPSStudioParticleEditorPlugin::_ensure_bottom_panel_share).call_deferred(0.5);
	}
}

void FPSStudioParticleEditorPlugin::_ensure_bottom_panel_share(real_t p_share) {
	if (!preview_logic_split || !bottom_window_wrapper || _is_bottom_floating() || !show_preview_panel || !bottom_window_wrapper->is_visible_in_tree()) {
		return;
	}
	const real_t total = preview_logic_split->get_size().y;
	const real_t bottom_height = bottom_window_wrapper->get_size().y;
	if (total <= 0.0) {
		return;
	}
	const real_t desired = total * p_share;
	if (bottom_height >= desired - 1.0) {
		return;
	}
	if (panel_layout_pending) {
		_on_panel_split_drag_ended(); // Keep the restored splits; this one changes now.
	}
	preview_logic_split->set_split_offset(preview_logic_split->get_split_offset() - int(Math::round(desired - bottom_height)));
	if (EditorNode::get_singleton()) {
		EditorNode::get_singleton()->save_editor_layout_delayed();
	}
}

const Ref<Texture2D> FPSStudioParticleEditorPlugin::get_plugin_icon() const {
	return EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("GPUParticles3D"), EditorStringName(EditorIcons));
}

// Space plays / pauses and R restarts anywhere in the Particle workspace,
// before buttons and checkboxes take Space for themselves. Text fields keep
// both keys for typing.
void FPSStudioParticleEditorPlugin::input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || key->is_echo() || !screen || !screen->is_visible_in_tree() || !_get_effect_root()) {
		return;
	}
	const Key code = key->get_keycode();
	if ((code != Key::SPACE && code != Key::R) || key->is_command_or_control_pressed() || key->is_alt_pressed() || key->is_shift_pressed()) {
		return;
	}
	Viewport *viewport = screen->get_viewport();
	Control *focus = viewport ? viewport->gui_get_focus_owner() : nullptr;
	if (focus && (!screen->is_ancestor_of(focus) || Object::cast_to<LineEdit>(focus) || Object::cast_to<TextEdit>(focus))) {
		return; // Typing, or the keys belong to another part of the editor.
	}
	if (code == Key::SPACE) {
		_on_timeline_play_toggle();
	} else {
		_on_restart_pressed();
	}
	if (viewport) {
		viewport->set_input_as_handled();
	}
}

FPSStudioParticleEditorPlugin::FPSStudioParticleEditorPlugin() {
	set_process_input(true);
	set_process(true);
	// Seconds the mouse rests on a Library item before its bigger, playing
	// preview opens (0 turns it off).
	EDITOR_DEF("fps_studio/particles/library_hover_preview_delay", 1.0);
	EditorSettings::get_singleton()->add_property_hint(PropertyInfo(Variant::FLOAT, "fps_studio/particles/library_hover_preview_delay", PROPERTY_HINT_RANGE, "0,5,0.1,suffix:s"));
	preview_quad = create_particle_quad();
	_load_recent_effects();

	// The workspace sits on the editor background color; every panel is its
	// own surface on top of it (see _apply_workspace_theme), like Godot's docks.
	workspace_background = memnew(PanelContainer);
	workspace_background->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	workspace_background->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	screen = workspace_background;
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(workspace_background);
	workspace_background->hide();
	workspace_background->connect(SceneStringName(theme_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_update_toolbar_icons));
	workspace_background->connect(SceneStringName(resized), callable_mp(this, &FPSStudioParticleEditorPlugin::_update_document_toolbar_layout));
	VBoxContainer *workspace = memnew(VBoxContainer);
	workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	workspace_background->add_child(workspace);
	// Level editor gizmos showing the reach of damage and camera shake events.
	add_node_3d_gizmo_plugin(memnew(FPSParticleEventGizmoPlugin));

	HBoxContainer *document_toolbar = memnew(HBoxContainer);
	document_toolbar->set_custom_minimum_size(Size2(0, 34) * EDSCALE);
	document_toolbar->connect("resized", callable_mp(this, &FPSStudioParticleEditorPlugin::_update_document_toolbar_layout));
	document_toolbar_surface = memnew(PanelContainer);
	workspace->add_child(document_toolbar_surface);
	document_toolbar_surface->add_child(document_toolbar);
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
	document_status_label->set_modulate(Color(1, 1, 1, 0.65));
	document_toolbar->add_child(document_status_label);
	new_effect_menu = memnew(MenuButton);
	new_effect_menu->set_text(TTR("New"));
	new_effect_menu->set_flat(false);
	new_effect_menu->set_tooltip_text(TTR("Create a new particle effect document."));
	new_effect_menu->get_popup()->add_item(TTR("Empty GPU Effect"), PARTICLE_TEMPLATE_GPU);
	new_effect_menu->get_popup()->add_item(TTR("Empty CPU Effect"), PARTICLE_TEMPLATE_CPU);
	new_effect_menu->get_popup()->add_separator(TTR("Templates"));
	new_effect_menu->get_popup()->add_item(TTR("Burst"), PARTICLE_TEMPLATE_BURST);
	new_effect_menu->get_popup()->add_item(TTR("Fountain"), PARTICLE_TEMPLATE_FOUNTAIN);
	new_effect_menu->get_popup()->add_item(TTR("Smoke"), PARTICLE_TEMPLATE_SMOKE);
	{
		// FPS presets in a submenu grouped by category, so the menu stays short.
		PopupMenu *presets_menu = memnew(PopupMenu);
		presets_menu->set_name("FPSPresets");
		String current_category;
		for (int preset : FPSParticlePresets::get_presets_in_display_order()) {
			const String category = FPSParticlePresets::get_preset_category(preset);
			if (category != current_category) {
				current_category = category;
				presets_menu->add_separator(TTRGET(category));
			}
			presets_menu->add_item(FPSParticlePresets::get_preset_name(preset), PARTICLE_TEMPLATE_PRESET_BASE + preset);
			presets_menu->set_item_tooltip(-1, FPSParticlePresets::get_preset_description(preset));
		}
		presets_menu->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_new_effect_menu_pressed));
		new_effect_menu->get_popup()->add_separator();
		new_effect_menu->get_popup()->add_submenu_node_item(TTR("FPS Presets"), presets_menu);
	}
	new_effect_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_new_effect_menu_pressed));
	document_toolbar->add_child(new_effect_menu);
	open_effect_button = memnew(Button);
	open_effect_button->set_text(TTR("Open..."));
	document_toolbar->add_child(open_effect_button);
	library_menu = memnew(MenuButton);
	library_menu->set_text(TTR("Library"));
	library_menu->set_flat(false);
	library_menu->set_tooltip_text(TTR("Create an effect from an FPS preset or reopen a recent effect."));
	library_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_library_menu_id));
	library_menu->get_popup()->connect("about_to_popup", callable_mp(this, &FPSStudioParticleEditorPlugin::_update_library_menu));
	document_toolbar->add_child(library_menu);
	save_effect_button = memnew(Button);
	save_effect_button->set_text(TTR("Save"));
	save_effect_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_save_scene_pressed));
	document_toolbar->add_child(save_effect_button);
	add_to_level_button = memnew(Button);
	add_to_level_button->set_text(TTR("Add to Level"));
	add_to_level_button->set_tooltip_text(TTR("Create an instance of the complete particle effect in the open Level scene."));
	add_to_level_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_add_effect_to_level));
	document_toolbar->add_child(add_to_level_button);
	layout_menu = memnew(MenuButton);
	layout_menu->set_text(TTR("Layout"));
	layout_menu->set_flat(false);
	layout_menu->set_tooltip_text(TTR("Show, hide, maximize or detach the workspace panels."));
	{
		PopupMenu *layout_popup = layout_menu->get_popup();
		// Editor shortcuts: listed and rebindable in Editor Settings > Shortcuts
		// under "Particle Workspace". They work while the Particle workspace is
		// shown (the Layout menu forwards them). Physical keys keep them working
		// on layouts where the number row types other characters (e.g. Czech).
		const KeyModifierMask alt = KeyModifierMask::ALT;
		layout_popup->add_separator(TTR("Panels"));
		layout_popup->add_check_shortcut(ED_SHORTCUT("particle_workspace/toggle_emitters", TTRC("Toggle Emitters Panel"), alt | Key::KEY_1, true), PARTICLE_LAYOUT_LEFT);
		layout_popup->set_item_text(-1, TTR("Emitters"));
		layout_popup->add_check_shortcut(ED_SHORTCUT("particle_workspace/toggle_library", TTRC("Toggle Effect Library Panel"), alt | Key::KEY_2, true), PARTICLE_LAYOUT_LIBRARY);
		layout_popup->set_item_text(-1, TTR("Effect Library"));
		layout_popup->add_check_shortcut(ED_SHORTCUT("particle_workspace/toggle_preview", TTRC("Toggle 3D Preview"), alt | Key::KEY_3, true), PARTICLE_LAYOUT_PREVIEW);
		layout_popup->set_item_text(-1, TTR("Preview"));
		layout_popup->add_check_shortcut(ED_SHORTCUT("particle_workspace/toggle_timeline_logic", TTRC("Toggle Timeline & Logic Panel"), alt | Key::KEY_4, true), PARTICLE_LAYOUT_BOTTOM);
		layout_popup->set_item_text(-1, TTR("Timeline & Logic"));
		layout_popup->add_check_shortcut(ED_SHORTCUT("particle_workspace/toggle_properties", TTRC("Toggle Properties Panel"), alt | Key::KEY_5, true), PARTICLE_LAYOUT_RIGHT);
		layout_popup->set_item_text(-1, TTR("Properties"));
		layout_popup->add_separator(TTR("Timeline & Logic"));
		layout_popup->add_shortcut(ED_SHORTCUT("particle_workspace/open_timeline", TTRC("Open Timeline"), alt | Key::KEY_6, true), PARTICLE_LAYOUT_SHOW_TIMELINE);
		layout_popup->add_shortcut(ED_SHORTCUT("particle_workspace/open_particle_logic", TTRC("Open Particle Logic"), alt | Key::KEY_7, true), PARTICLE_LAYOUT_SHOW_LOGIC);
		layout_popup->add_check_shortcut(ED_SHORTCUT("particle_workspace/maximize_timeline_logic", TTRC("Maximize Timeline & Logic"), alt | Key::KEY_8, true), PARTICLE_LAYOUT_MAXIMIZE_BOTTOM);
		layout_popup->set_item_text(-1, TTR("Maximize (Hide Preview)"));
		layout_popup->add_check_shortcut(ED_SHORTCUT("particle_workspace/separate_window", TTRC("Timeline & Logic in Separate Window"), alt | Key::KEY_9, true), PARTICLE_LAYOUT_FLOAT_BOTTOM);
		layout_popup->set_item_text(-1, TTR("Separate Window"));
		layout_popup->add_separator();
		layout_popup->add_shortcut(ED_SHORTCUT("particle_workspace/reset_layout", TTRC("Reset Particle Layout"), alt | Key::KEY_0, true), PARTICLE_LAYOUT_RESET);
		layout_popup->set_item_text(-1, TTR("Reset Layout"));
		layout_popup->set_hide_on_checkable_item_selection(false);
		layout_popup->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_layout_menu_id));
		layout_popup->connect("about_to_popup", callable_mp(this, &FPSStudioParticleEditorPlugin::_update_layout_menu));
	}
	document_toolbar->add_child(layout_menu);

	HSplitContainer *main_split = memnew(HSplitContainer);
	main_panel_split = main_split;
	main_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_split->connect("drag_ended", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_panel_split_drag_ended));
	// A saved split waits until the workspace first gets a size.
	main_split->connect(SceneStringName(resized), callable_mp(this, &FPSStudioParticleEditorPlugin::_restore_panel_layout), CONNECT_DEFERRED);
	workspace->add_child(main_split);

	VBoxContainer *left_panel = memnew(VBoxContainer);
	left_surface = memnew(PanelContainer);
	left_panel_root = left_surface;
	left_surface->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
	main_split->add_child(left_surface);
	left_surface->add_child(left_panel);
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
	add_emitter_menu->get_popup()->add_separator(TTR("Events"));
	add_emitter_menu->get_popup()->add_item(TTR("Light Flash"), PARTICLE_EVENT_LIGHT);
	add_emitter_menu->get_popup()->add_item(TTR("Sound"), PARTICLE_EVENT_SOUND);
	add_emitter_menu->get_popup()->add_item(TTR("Camera Shake"), PARTICLE_EVENT_SHAKE);
	add_emitter_menu->get_popup()->add_item(TTR("Decal"), PARTICLE_EVENT_DECAL);
	add_emitter_menu->get_popup()->add_item(TTR("Damage"), PARTICLE_EVENT_DAMAGE);
	add_emitter_menu->get_popup()->set_item_tooltip(-1, TTR("Area damage with falloff and cover checks; calls take_damage() on what it hits."));
	add_emitter_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_add_emitter_menu_pressed));
	emitter_header->add_child(add_emitter_menu);
	duplicate_emitter_button = memnew(Button);
	duplicate_emitter_button->set_text(TTR("Duplicate"));
	duplicate_emitter_button->set_tooltip_text(TTR("Duplicate the selected emitter with its own materials (Ctrl/Cmd+D in the timeline)."));
	duplicate_emitter_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_duplicate_emitter_pressed));
	emitter_header->add_child(duplicate_emitter_button);
	remove_emitter_button = memnew(Button);
	remove_emitter_button->set_text(TTR("Remove"));
	remove_emitter_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_remove_selected_emitter));
	emitter_header->add_child(remove_emitter_button);
	{
		Button *close_left = memnew(Button);
		close_left->set_flat(true);
		close_left->set_tooltip_text(TTR("Hide the Emitters panel (reopen it from Layout)."));
		close_left->set_meta("icon_name", "Close");
		close_left->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_set_panel_visible).bind(int(PARTICLE_PANEL_LEFT), false));
		emitter_header->add_child(close_left);
		layout_icon_buttons.push_back(close_left);
	}
	emitter_tree = memnew(Tree);
	emitter_tree->set_hide_root(true);
	emitter_tree->set_columns(2);
	emitter_tree->set_column_expand(0, true);
	emitter_tree->set_column_expand(1, false);
	emitter_tree->set_column_clip_content(0, true);
	emitter_tree->set_column_custom_minimum_width(1, 72 * EDSCALE);
	emitter_tree->set_custom_minimum_size(Size2(0, 140) * EDSCALE);
	emitter_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_tree->set_stretch_ratio(1.4);
	emitter_tree->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_selected));
	emitter_tree->connect("item_edited", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_renamed));
	emitter_tree->connect(SceneStringName(gui_input), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_tree_gui_input));
	emitter_tree->set_drag_forwarding(Callable(), callable_mp(this, &FPSStudioParticleEditorPlugin::_can_drop_on_emitter_tree), callable_mp(this, &FPSStudioParticleEditorPlugin::_drop_on_emitter_tree));
	emitter_tree->set_tooltip_text(TTR("Drop audio files here to add Sound events (or onto a Sound track to replace its sound)."));
	left_panel->add_child(emitter_tree);

	// Library of effect files below the emitter list; folded away when not needed.
	FoldableContainer *library_section = memnew(FoldableContainer);
	library_panel_root = library_section;
	library_section->set_visible(show_library_panel);
	library_section->set_title(TTR("Effect Library"));
	library_section->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	left_panel->add_child(library_section);
	VBoxContainer *library_content = memnew(VBoxContainer);
	library_content->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	library_section->add_child(library_content);
	effect_filter = memnew(LineEdit);
	effect_filter->set_placeholder(TTR("Search effects..."));
	effect_filter->set_clear_button_enabled(true);
	effect_filter->connect(SceneStringName(text_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_effect_filter_changed));
	library_content->add_child(effect_filter);
	effects_tree = memnew(Tree);
	effects_tree->set_hide_root(true);
	effects_tree->set_custom_minimum_size(Size2(0, 120) * EDSCALE);
	effects_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	effects_tree->set_tooltip_text(TTR("Double-click an effect to open it."));
	effects_tree->connect(SNAME("item_activated"), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_effect_activated));
	library_content->add_child(effects_tree);

	HSplitContainer *center_right_split = memnew(HSplitContainer);
	preview_properties_split = center_right_split;
	center_right_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	center_right_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_right_split->connect("drag_ended", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_panel_split_drag_ended));
	center_right_split->connect(SceneStringName(resized), callable_mp(this, &FPSStudioParticleEditorPlugin::_restore_panel_layout), CONNECT_DEFERRED);
	main_split->add_child(center_right_split);

	VSplitContainer *center_split = memnew(VSplitContainer);
	preview_logic_split = center_split;
	center_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	center_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_split->connect("drag_ended", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_panel_split_drag_ended));
	center_split->connect(SceneStringName(resized), callable_mp(this, &FPSStudioParticleEditorPlugin::_restore_panel_layout), CONNECT_DEFERRED);
	center_right_split->add_child(center_split);

	VBoxContainer *preview_panel = memnew(VBoxContainer);
	preview_surface = memnew(PanelContainer);
	preview_panel_root = preview_surface;
	preview_surface->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_surface->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	preview_surface->set_stretch_ratio(2.4);
	center_split->add_child(preview_surface);
	preview_surface->add_child(preview_panel);
	HBoxContainer *preview_toolbar = memnew(HBoxContainer);
	preview_panel->add_child(preview_toolbar);
	selection_label = memnew(Label);
	selection_label->set_text(TTR("Create or open a particle effect to start."));
	selection_label->set_clip_text(true);
	selection_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_toolbar->add_child(selection_label);
	reset_view_button = memnew(Button);
	reset_view_button->set_text(TTR("Reset View"));
	reset_view_button->set_tooltip_text(TTR("Frame the full particle effect in the preview."));
	reset_view_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_reset_view_pressed));
	preview_toolbar->add_child(reset_view_button);
	preview_view_menu = memnew(MenuButton);
	preview_view_menu->set_text(TTR("View"));
	preview_view_menu->set_flat(false);
	preview_view_menu->set_tooltip_text(TTR("Preview environment and debug overlays."));
	{
		PopupMenu *view_popup = preview_view_menu->get_popup();
		view_popup->add_separator(TTR("Environment"));
		view_popup->add_radio_check_item(TTR("Studio (dark)"), 0);
		view_popup->add_radio_check_item(TTR("Daylight"), 1);
		view_popup->add_radio_check_item(TTR("Night"), 2);
		view_popup->add_radio_check_item(TTR("Interior"), 3);
		view_popup->set_item_checked(view_popup->get_item_index(0), true);
		view_popup->add_separator(TTR("Overlays"));
		view_popup->add_check_item(TTR("Grid"), 10);
		view_popup->set_item_checked(view_popup->get_item_index(10), true);
		view_popup->add_check_item(TTR("Glow (Bloom)"), 11);
		view_popup->add_check_item(TTR("Emitter Bounds"), 12);
		view_popup->add_check_item(TTR("Floor"), 15);
		view_popup->set_item_tooltip(view_popup->get_item_index(15), TTR("Show a ground plane to judge height, lighting and decals."));
		view_popup->add_check_item(TTR("Performance Info"), 18);
		view_popup->set_item_checked(view_popup->get_item_index(18), preview_show_perf);
		view_popup->set_item_tooltip(view_popup->get_item_index(18), TTR("Estimated GPU and CPU cost of the effect over the preview, with what makes it expensive (red = heavy)."));
		view_popup->add_check_item(TTR("Damage & Shake Radius"), 14);
		view_popup->set_item_checked(view_popup->get_item_index(14), preview_show_radii);
		view_popup->set_item_tooltip(view_popup->get_item_index(14), TTR("Rings around Damage (red) and Camera Shake (blue) events: inner = full strength, outer = fade out."));
		view_popup->add_separator(TTR("Events"));
		view_popup->add_check_item(TTR("Sound"), 16);
		view_popup->set_item_checked(view_popup->get_item_index(16), preview_play_sound);
		view_popup->set_item_tooltip(view_popup->get_item_index(16), TTR("Play Sound events in the preview. Preview only; the effect itself is unchanged."));
		view_popup->add_check_item(TTR("Camera Shake"), 17);
		view_popup->set_item_checked(view_popup->get_item_index(17), preview_play_shake);
		view_popup->set_item_tooltip(view_popup->get_item_index(17), TTR("Shake the preview camera on Camera Shake events. Preview only; the effect itself is unchanged."));
		view_popup->add_separator();
		view_popup->add_check_item(TTR("Collision Test Walls"), 13);
		view_popup->set_item_tooltip(view_popup->get_item_index(13), TTR("Add a collision floor and three see-through walls to the preview to tune the Collision module (bounce, friction, particle size)."));
		view_popup->set_hide_on_checkable_item_selection(false);
		view_popup->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_preview_view_menu_id));
	}
	preview_toolbar->add_child(preview_view_menu);
	preview_compare_menu = memnew(MenuButton);
	preview_compare_menu->set_text(TTR("A/B"));
	preview_compare_menu->set_flat(false);
	preview_compare_menu->set_tooltip_text(TTR("Compare before / after: take a snapshot of the preview (A), change the effect, then drag the divider over the live preview (B)."));
	{
		PopupMenu *compare_popup = preview_compare_menu->get_popup();
		compare_popup->add_item(TTR("Take Snapshot (A)"), 0);
		compare_popup->add_check_item(TTR("Show A/B Wipe"), 1);
		compare_popup->set_item_disabled(-1, true);
		compare_popup->add_item(TTR("Clear Snapshot"), 2);
		compare_popup->set_item_disabled(-1, true);
		compare_popup->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_compare_menu_id));
	}
	preview_toolbar->add_child(preview_compare_menu);
	texture_generator_button = memnew(Button);
	texture_generator_button->set_text(TTR("Texture Generator..."));
	texture_generator_button->set_tooltip_text(TTR("Create and export a procedural VFX texture for a particle emitter."));
	texture_generator_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_pressed));
	preview_toolbar->add_child(texture_generator_button);
	{
		Button *close_preview = memnew(Button);
		close_preview->set_flat(true);
		close_preview->set_tooltip_text(TTR("Hide the preview and give its space to Timeline & Logic (reopen it from Layout)."));
		close_preview->set_meta("icon_name", "Close");
		close_preview->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_set_panel_visible).bind(int(PARTICLE_PANEL_PREVIEW), false));
		preview_toolbar->add_child(close_preview);
		layout_icon_buttons.push_back(close_preview);
	}

	preview_container = memnew(SubViewportContainer);
	preview_container->set_stretch(true);
	preview_container->set_custom_minimum_size(Size2(0, 140) * EDSCALE);
	preview_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	preview_container->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	preview_container->set_focus_mode(Control::FOCUS_ALL);
	preview_container->set_tooltip_text(TTR("Drag with the left or right mouse button to orbit, middle mouse button to pan, and use the wheel to zoom."));
	preview_container->connect(SceneStringName(gui_input), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_preview_gui_input));
	preview_panel->add_child(preview_container);
	preview_viewport = memnew(SubViewport);
	preview_viewport->set_size(Vector2i(1280, 720));
	preview_viewport->set_update_mode(SubViewport::UPDATE_WHEN_VISIBLE);
	preview_viewport->set_use_own_world_3d(true);
	// Without a 3D listener AudioStreamPlayer3D nodes in the preview stay silent.
	preview_viewport->set_as_audio_listener_3d(true);
	preview_container->add_child(preview_viewport);
	// Real render time of the preview, shown next to the estimate.
	RS::get_singleton()->viewport_set_measure_render_time(preview_viewport->get_viewport_rid(), true);
	preview_compare = memnew(FPSPreviewCompareOverlay);
	preview_compare->set_visible(false);
	preview_container->add_child(preview_compare);
	preview_perf_panel = memnew(PanelContainer);
	{
		Ref<StyleBoxFlat> perf_style;
		perf_style.instantiate();
		perf_style->set_bg_color(Color(0.0, 0.0, 0.0, 0.55));
		perf_style->set_corner_radius_all(int(4 * EDSCALE));
		perf_style->set_content_margin_all(6 * EDSCALE);
		preview_perf_panel->add_theme_style_override(SceneStringName(panel), perf_style);
	}
	preview_perf_panel->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	preview_perf_panel->set_visible(false);
	preview_container->add_child(preview_perf_panel);
	preview_perf_label = memnew(RichTextLabel);
	preview_perf_label->set_use_bbcode(true);
	preview_perf_label->set_fit_content(true);
	preview_perf_label->set_scroll_active(false);
	preview_perf_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	preview_perf_label->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	preview_perf_label->add_theme_font_size_override(SNAME("normal_font_size"), 12 * EDSCALE);
	preview_perf_label->add_theme_font_size_override(SNAME("bold_font_size"), 12 * EDSCALE);
	preview_perf_panel->add_child(preview_perf_label);
	// The container only sizes its SubViewport; the overlays follow by hand.
	preview_container->connect(SceneStringName(resized), callable_mp(this, &FPSStudioParticleEditorPlugin::_layout_preview_overlays));
	preview_root = memnew(Node3D);
	preview_root->set_name("ParticlePreviewWorld");
	preview_viewport->add_child(preview_root);
	document_host = memnew(Node3D);
	document_host->set_name("ParticleDocumentHost");
	document_host->set_visible(false);
	document_host->set_process_mode(Node::PROCESS_MODE_DISABLED);
	preview_root->add_child(document_host);

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
	preview_key_light = key_light;
	key_light->set_rotation(Vector3(Math::deg_to_rad(-38.0), Math::deg_to_rad(-26.0), 0.0));
	key_light->set_param(Light3D::PARAM_ENERGY, 1.45);
	key_light->set_color(Color(0.9, 0.94, 1.0));
	preview_root->add_child(key_light);
	DirectionalLight3D *fill_light = memnew(DirectionalLight3D);
	preview_fill_light = fill_light;
	fill_light->set_rotation(Vector3(Math::deg_to_rad(-18.0), Math::deg_to_rad(148.0), Math::deg_to_rad(12.0)));
	fill_light->set_param(Light3D::PARAM_ENERGY, 0.48);
	fill_light->set_color(Color(0.34, 0.56, 1.0));
	preview_root->add_child(fill_light);
	DirectionalLight3D *rim_light = memnew(DirectionalLight3D);
	preview_rim_light = rim_light;
	rim_light->set_rotation(Vector3(Math::deg_to_rad(-16.0), Math::deg_to_rad(-145.0), Math::deg_to_rad(-18.0)));
	rim_light->set_param(Light3D::PARAM_ENERGY, 0.65);
	rim_light->set_color(Color(1.0, 0.66, 0.42));
	preview_root->add_child(rim_light);
	preview_grid = create_particle_preview_grid();
	preview_root->add_child(preview_grid);
	{
		// Collision test room: a floor and three walls the particles can hit,
		// with see-through walls so bouncing can be tuned in the preview.
		Node3D *room = memnew(Node3D);
		room->set_name("PreviewCollisionRoom");
		room->set_visible(false);
		preview_root->add_child(room);
		preview_floor_collision = room;
		GPUParticlesCollisionBox3D *floor = memnew(GPUParticlesCollisionBox3D);
		floor->set_name("FloorCollision");
		floor->set_size(Vector3(60.0, 0.2, 60.0));
		floor->set_position(Vector3(0.0, -0.1, 0.0));
		room->add_child(floor);
		Ref<StandardMaterial3D> wall_material;
		wall_material.instantiate();
		wall_material->set_albedo(Color(0.55, 0.62, 0.75, 0.18));
		wall_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		wall_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		wall_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		const struct {
			Vector3 position;
			Vector3 size;
		} walls[] = {
			{ Vector3(-3.0, 2.0, 0.0), Vector3(0.2, 4.0, 6.0) },
			{ Vector3(3.0, 2.0, 0.0), Vector3(0.2, 4.0, 6.0) },
			{ Vector3(0.0, 2.0, -3.0), Vector3(6.0, 4.0, 0.2) },
		};
		for (const auto &wall : walls) {
			GPUParticlesCollisionBox3D *collision = memnew(GPUParticlesCollisionBox3D);
			collision->set_size(wall.size);
			collision->set_position(wall.position);
			room->add_child(collision);
			Ref<BoxMesh> box;
			box.instantiate();
			box->set_size(wall.size);
			box->set_material(wall_material);
			MeshInstance3D *visual = memnew(MeshInstance3D);
			visual->set_mesh(box);
			visual->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			visual->set_position(wall.position);
			room->add_child(visual);
		}
	}
	{
		// Visible ground plane, off by default (View > Floor).
		Ref<PlaneMesh> plane;
		plane.instantiate();
		plane->set_size(Vector2(80.0, 80.0));
		Ref<StandardMaterial3D> floor_material;
		floor_material.instantiate();
		floor_material->set_albedo(Color(0.22, 0.23, 0.25));
		floor_material->set_roughness(0.95);
		plane->set_material(floor_material);
		preview_floor = memnew(MeshInstance3D);
		preview_floor->set_name("PreviewFloor");
		preview_floor->set_mesh(plane);
		preview_floor->set_position(Vector3(0.0, -0.002, 0.0));
		preview_floor->set_visible(false);
		preview_root->add_child(preview_floor);
		preview_radii_root = memnew(Node3D);
		preview_radii_root->set_name("PreviewEventRadii");
		preview_root->add_child(preview_radii_root);
	}
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
	timeline->set_custom_minimum_size(Size2(120, 0) * EDSCALE);
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
	preview_stats_label = memnew(Label);
	preview_stats_label->set_modulate(Color(1, 1, 1, 0.6));
	preview_stats_label->set_tooltip_text(TTR("Visible tracks, maximum particles of visible emitters, draw calls and primitives rendered in the preview."));
	preview_stats_label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	preview_stats_label->set_clip_text(true);
	preview_stats_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	preview_stats_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_stats_label->set_stretch_ratio(0.5);
	transport->add_child(preview_stats_label);
	speed_label = memnew(Label);
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
	preview_loop_toggle = memnew(CheckButton);
	preview_loop_toggle->set_text(TTR("Loop"));
	preview_loop_toggle->set_pressed(preview_loop);
	preview_loop_toggle->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_loop_toggled));
	transport->add_child(preview_loop_toggle);
	preview_lock_seed = EditorSettings::get_singleton() ? bool(EditorSettings::get_singleton()->get_project_metadata("fps_studio_particle", "lock_seed", false)) : false;
	preview_lock_seed_toggle = memnew(CheckButton);
	preview_lock_seed_toggle->set_text(TTR("Lock Seed"));
	preview_lock_seed_toggle->set_pressed(preview_lock_seed);
	preview_lock_seed_toggle->set_tooltip_text(TTR("Keep the same random look on Restart and Loop, so a change you make is the only difference you see. Off: every run looks a little different, as in the game. Effects saved with a fixed seed always look the same."));
	preview_lock_seed_toggle->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_lock_seed_toggled));
	transport->add_child(preview_lock_seed_toggle);
	preview_new_seed_button = memnew(Button);
	preview_new_seed_button->set_flat(true);
	preview_new_seed_button->set_meta("icon_name", "RandomNumberGenerator");
	preview_new_seed_button->set_tooltip_text(TTR("New random look at the current moment (with Lock Seed on, this look is kept)."));
	preview_new_seed_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_new_seed_pressed));
	transport->add_child(preview_new_seed_button);
	layout_icon_buttons.push_back(preview_new_seed_button);

	// Timeline & Logic live in a panel that can be closed, maximized (hiding
	// the preview) or detached into its own window through WindowWrapper.
	bottom_window_wrapper = memnew(WindowWrapper);
	bottom_window_wrapper->set_window_title(TTR("Particle Timeline & Logic"));
	bottom_window_wrapper->set_margins_enabled(true);
	bottom_window_wrapper->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_window_wrapper->connect("window_visibility_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_bottom_window_visibility_changed));
	center_split->add_child(bottom_window_wrapper);
	Control *bottom_panel = memnew(Control);
	bottom_panel_root = bottom_panel;
	bottom_panel->set_custom_minimum_size(Size2(0, 150) * EDSCALE);
	bottom_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_window_wrapper->set_wrapped_control(bottom_panel);
	bottom_tabs = memnew(TabContainer);
	bottom_tabs->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	bottom_tabs->connect("tab_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_bottom_tab_changed));
	bottom_panel->add_child(bottom_tabs);
	{
		HBoxContainer *bottom_actions = memnew(HBoxContainer);
		bottom_actions->set_anchors_and_offsets_preset(Control::PRESET_TOP_RIGHT);
		bottom_actions->set_h_grow_direction(Control::GROW_DIRECTION_BEGIN);
		bottom_actions->add_theme_constant_override("separation", 0);
		bottom_panel->add_child(bottom_actions);
		bottom_maximize_button = memnew(Button);
		bottom_maximize_button->set_flat(true);
		bottom_maximize_button->set_toggle_mode(true);
		bottom_maximize_button->set_tooltip_text(TTR("Maximize Timeline & Logic (hides the preview). Click again to restore."));
		bottom_maximize_button->set_meta("icon_name", "ExpandBottomDock");
		bottom_maximize_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_toggle_bottom_maximized));
		bottom_actions->add_child(bottom_maximize_button);
		layout_icon_buttons.push_back(bottom_maximize_button);
		bottom_float_button = memnew(Button);
		bottom_float_button->set_flat(true);
		bottom_float_button->set_tooltip_text(TTR("Move Timeline & Logic into a separate window. Closing the window docks it back."));
		bottom_float_button->set_meta("icon_name", "MakeFloating");
		bottom_float_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_toggle_bottom_floating));
		bottom_actions->add_child(bottom_float_button);
		layout_icon_buttons.push_back(bottom_float_button);
		Button *close_bottom = memnew(Button);
		close_bottom->set_flat(true);
		close_bottom->set_tooltip_text(TTR("Hide Timeline & Logic (reopen it from Layout)."));
		close_bottom->set_meta("icon_name", "Close");
		close_bottom->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_bottom_close_pressed));
		bottom_actions->add_child(close_bottom);
		layout_icon_buttons.push_back(close_bottom);
	}
	VBoxContainer *timeline_page = memnew(VBoxContainer);
	timeline_page->set_name(TTR("Timeline"));
	bottom_tabs->add_child(timeline_page);
	HBoxContainer *timeline_toolbar = memnew(HBoxContainer);
	timeline_page->add_child(timeline_toolbar);
	timeline_play_button = memnew(Button);
	timeline_play_button->set_flat(true);
	timeline_play_button->set_tooltip_text(TTR("Play / Pause (Space)"));
	timeline_play_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_play_toggle));
	timeline_toolbar->add_child(timeline_play_button);
	Button *timeline_restart = memnew(Button);
	timeline_restart->set_flat(true);
	timeline_restart->set_tooltip_text(TTR("Restart from the beginning (Home)"));
	timeline_restart->set_meta("icon_name", "PlayStart");
	timeline_restart->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_restart_pressed));
	timeline_toolbar->add_child(timeline_restart);
	timeline_loop_button = memnew(Button);
	timeline_loop_button->set_flat(true);
	timeline_loop_button->set_toggle_mode(true);
	timeline_loop_button->set_pressed(preview_loop);
	timeline_loop_button->set_tooltip_text(TTR("Loop the preview over the whole effect"));
	timeline_loop_button->set_meta("icon_name", "Loop");
	timeline_loop_button->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_loop_toggled));
	timeline_toolbar->add_child(timeline_loop_button);
	timeline_time_label = memnew(Label);
	timeline_time_label->set_custom_minimum_size(Size2(130, 0) * EDSCALE);
	timeline_time_label->set_text("0.00 / 0.00 s");
	timeline_toolbar->add_child(timeline_time_label);
	Control *timeline_spacer = memnew(Control);
	timeline_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	timeline_toolbar->add_child(timeline_spacer);
	Label *timeline_hint = memnew(Label);
	timeline_hint->set_text(TTR("Drag bars to time tracks, right-click for options"));
	timeline_hint->set_modulate(Color(1, 1, 1, 0.5));
	timeline_hint->set_clip_text(true);
	timeline_hint->set_h_size_flags(Control::SIZE_SHRINK_END);
	timeline_toolbar->add_child(timeline_hint);
	Button *timeline_snap = memnew(Button);
	timeline_snap->set_flat(true);
	timeline_snap->set_toggle_mode(true);
	timeline_snap->set_pressed(true);
	timeline_snap->set_tooltip_text(TTR("Snap timing to 0.05 s (hold Shift to bypass)"));
	timeline_snap->set_meta("icon_name", "Snap");
	timeline_toolbar->add_child(timeline_snap);
	Button *timeline_zoom_out = memnew(Button);
	timeline_zoom_out->set_flat(true);
	timeline_zoom_out->set_tooltip_text(TTR("Zoom out (Ctrl/Cmd + wheel)"));
	timeline_zoom_out->set_meta("icon_name", "ZoomLess");
	timeline_toolbar->add_child(timeline_zoom_out);
	Button *timeline_zoom_in = memnew(Button);
	timeline_zoom_in->set_flat(true);
	timeline_zoom_in->set_tooltip_text(TTR("Zoom in (Ctrl/Cmd + wheel)"));
	timeline_zoom_in->set_meta("icon_name", "ZoomMore");
	timeline_toolbar->add_child(timeline_zoom_in);
	Button *timeline_fit = memnew(Button);
	timeline_fit->set_text(TTR("Fit"));
	timeline_fit->set_flat(true);
	timeline_fit->set_tooltip_text(TTR("Show the whole effect (F)"));
	timeline_toolbar->add_child(timeline_fit);
	ScrollContainer *timeline_scroll = memnew(ScrollContainer);
	timeline_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	timeline_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	timeline_page->add_child(timeline_scroll);
	timeline_view = memnew(FPSParticleTimeline);
	timeline_view->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	timeline_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	timeline_view->connect("scrub_requested", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_scrub));
	timeline_view->connect("track_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_emitter_selected));
	timeline_view->connect("timing_committed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_timing_committed));
	timeline_view->connect("track_action", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_track_action));
	timeline_view->connect("play_toggle_requested", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_play_toggle));
	timeline_view->connect("restart_requested", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_restart_pressed));
	timeline_view->connect("loop_region_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_loop_region_changed));
	timeline_view->connect("markers_committed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_timeline_markers_committed));
	timeline_scroll->add_child(timeline_view);
	// The timeline draws its ruler at the visible top; redraw while scrolling.
	timeline_scroll->get_v_scroll_bar()->connect(SceneStringName(value_changed), callable_mp((CanvasItem *)timeline_view, &CanvasItem::queue_redraw).unbind(1));
	timeline_snap->connect(SceneStringName(toggled), callable_mp(timeline_view, &FPSParticleTimeline::set_snap_enabled));
	timeline_zoom_out->connect(SceneStringName(pressed), callable_mp(timeline_view, &FPSParticleTimeline::zoom_out));
	timeline_zoom_in->connect(SceneStringName(pressed), callable_mp(timeline_view, &FPSParticleTimeline::zoom_in));
	timeline_fit->connect(SceneStringName(pressed), callable_mp(timeline_view, &FPSParticleTimeline::fit_view));
	PanelContainer *logic_panel = memnew(PanelContainer);
	logic_panel->set_name(TTR("Particle Logic"));
	logic_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	bottom_tabs->add_child(logic_panel);
	VBoxContainer *logic_content = memnew(VBoxContainer);
	particle_logic_content = logic_content;
	logic_panel->add_child(logic_content);
	// Wraps onto a second row in narrow layouts instead of widening the workspace.
	HFlowContainer *logic_header = memnew(HFlowContainer);
	logic_content->add_child(logic_header);
	Label *logic_title = memnew(Label);
	logic_title->set_text(TTR("Particle Logic"));
	logic_title->add_theme_font_size_override(SceneStringName(font_size), 15 * EDSCALE);
	logic_header->add_child(logic_title);
	particle_logic_status = memnew(Label);
	particle_logic_status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	particle_logic_status->set_clip_text(true);
	particle_logic_status->set_custom_minimum_size(Size2(160, 0) * EDSCALE);
	particle_logic_status->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	logic_header->add_child(particle_logic_status);
	convert_shader_button = memnew(Button);
	convert_shader_button->set_text(TTR("Convert to Shader Code"));
	convert_shader_button->set_tooltip_text(TTR("Replace the module settings with the equivalent particle shader code (same look), for custom behavior written in code. Undoable."));
	convert_shader_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_convert_shader_pressed));
	logic_header->add_child(convert_shader_button);
	convert_graph_button = memnew(Button);
	convert_graph_button->set_text(TTR("Convert to Node Graph"));
	convert_graph_button->set_tooltip_text(TTR("Replace the module settings with an equivalent particle VisualShader graph, to extend the behavior with nodes. Undoable."));
	convert_graph_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_convert_graph_pressed));
	logic_header->add_child(convert_graph_button);
	particle_graph_button = memnew(Button);
	particle_graph_button->set_text(TTR("Create Node Graph"));
	particle_graph_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_particle_graph_button_pressed));
	logic_header->add_child(particle_graph_button);
	particle_logic_hint = memnew(Label);
	particle_logic_hint->set_text(TTR("Edit this emitter in Process, or create a VisualShader for custom particle logic."));
	particle_logic_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	logic_content->add_child(particle_logic_hint);

	VBoxContainer *right_panel = memnew(VBoxContainer);
	right_surface = memnew(PanelContainer);
	right_panel_root = right_surface;
	right_surface->set_custom_minimum_size(Size2(360, 0) * EDSCALE);
	right_surface->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_surface->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_right_split->add_child(right_surface);
	right_surface->add_child(right_panel);
	HBoxContainer *properties_mode_bar = memnew(HBoxContainer);
	right_panel->add_child(properties_mode_bar);
	Ref<ButtonGroup> properties_mode_group;
	properties_mode_group.instantiate();
	modules_mode_button = memnew(Button);
	modules_mode_button->set_text(TTR("Modules"));
	modules_mode_button->set_toggle_mode(true);
	modules_mode_button->set_pressed(true);
	modules_mode_button->set_button_group(properties_mode_group);
	modules_mode_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	modules_mode_button->set_tooltip_text(TTR("Edit the emitter as a stack of modules (spawn, motion, color, render)."));
	modules_mode_button->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_properties_mode_toggled).bind(false));
	properties_mode_bar->add_child(modules_mode_button);
	Button *advanced_mode_button = memnew(Button);
	advanced_mode_button->set_text(TTR("All Properties"));
	advanced_mode_button->set_toggle_mode(true);
	advanced_mode_button->set_button_group(properties_mode_group);
	advanced_mode_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	advanced_mode_button->set_tooltip_text(TTR("Show every native property of the emitter, its process material and its mesh."));
	advanced_mode_button->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_properties_mode_toggled).bind(true));
	properties_mode_bar->add_child(advanced_mode_button);
	{
		Button *close_right = memnew(Button);
		close_right->set_flat(true);
		close_right->set_tooltip_text(TTR("Hide the Properties panel (reopen it from Layout)."));
		close_right->set_meta("icon_name", "Close");
		close_right->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_set_panel_visible).bind(int(PARTICLE_PANEL_RIGHT), false));
		properties_mode_bar->add_child(close_right);
		layout_icon_buttons.push_back(close_right);
	}
	ScrollContainer *module_scroll = memnew(ScrollContainer);
	module_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	module_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_panel->add_child(module_scroll);
	module_stack_scroll = module_scroll;
	module_stack = memnew(FPSParticleModuleStack);
	module_stack->connect("property_edited", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_module_stack_property_edited));
	module_stack->connect("generate_texture_requested", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_module_stack_generate_texture));
	module_stack->connect("level_collision_requested", callable_mp(this, &FPSStudioParticleEditorPlugin::_bake_level_particle_collision));
	module_scroll->add_child(module_stack);
	properties_tabs = memnew(TabContainer);
	properties_tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	properties_tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	properties_tabs->hide();
	right_panel->add_child(properties_tabs);

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
	// The curve cards are shown inside the Color and Size modules.
	curve_panel_vbox->remove_child(alpha_curve_card);
	curve_panel_vbox->remove_child(scale_curve_card);
	module_stack->set_module_custom_control(FPSParticleModuleStack::MODULE_COLOR, alpha_curve_card);
	module_stack->set_module_custom_control(FPSParticleModuleStack::MODULE_SIZE, scale_curve_card);

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
	texture_generator_dialog->connect(SceneStringName(visibility_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_visibility_changed));
	texture_generator_dialog->set_title(TTR("VFX Texture Generator"));
	texture_generator_dialog->set_min_size(Size2(1080, 700) * EDSCALE);
	texture_generator_dialog->get_ok_button()->hide();
	add_child(texture_generator_dialog);
	VBoxContainer *texture_generator_root = memnew(VBoxContainer);
	texture_generator_root->set_custom_minimum_size(Size2(1040, 640) * EDSCALE);
	texture_generator_dialog->add_child(texture_generator_root);

	HSplitContainer *texture_generator_split = memnew(HSplitContainer);
	texture_generator_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_root->add_child(texture_generator_split);

	// Left: effect gallery grouped by category.
	VBoxContainer *texture_type_column = memnew(VBoxContainer);
	texture_type_column->set_custom_minimum_size(Size2(210, 0) * EDSCALE);
	texture_generator_split->add_child(texture_type_column);
	Label *texture_type_header = memnew(Label);
	texture_type_header->set_text(TTR("Effect type"));
	texture_type_header->set_theme_type_variation("HeaderSmall");
	texture_type_column->add_child(texture_type_header);
	texture_generator_type_tree = memnew(Tree);
	texture_generator_type_tree->set_hide_root(true);
	texture_generator_type_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_type_tree->set_hide_folding(true);
	texture_generator_type_tree->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_type_selected));
	texture_type_column->add_child(texture_generator_type_tree);

	HSplitContainer *texture_generator_inner_split = memnew(HSplitContainer);
	texture_generator_inner_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_split->add_child(texture_generator_inner_split);

	// Center: preview stage with background choice and transport.
	VBoxContainer *texture_preview_column = memnew(VBoxContainer);
	texture_preview_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_preview_column->set_custom_minimum_size(Size2(420, 0) * EDSCALE);
	texture_generator_inner_split->add_child(texture_preview_column);
	HBoxContainer *texture_preview_header = memnew(HBoxContainer);
	texture_preview_column->add_child(texture_preview_header);
	texture_generator_type_title = memnew(Label);
	texture_generator_type_title->set_theme_type_variation("HeaderMedium");
	texture_generator_type_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_type_title->set_clip_text(true);
	texture_preview_header->add_child(texture_generator_type_title);
	texture_generator_show_sheet = memnew(Button);
	texture_generator_show_sheet->set_text(TTR("Show Sheet"));
	texture_generator_show_sheet->set_toggle_mode(true);
	texture_generator_show_sheet->set_flat(true);
	texture_generator_show_sheet->set_tooltip_text(TTR("Show the whole sprite sheet instead of the animated frame."));
	texture_generator_show_sheet->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_show_sheet_toggled));
	texture_preview_header->add_child(texture_generator_show_sheet);
	texture_generator_background = memnew(OptionButton);
	texture_generator_background->add_item(TTR("Checker"));
	texture_generator_background->add_item(TTR("Black"));
	texture_generator_background->add_item(TTR("Dark"));
	texture_generator_background->add_item(TTR("Gray"));
	texture_generator_background->add_item(TTR("White"));
	texture_generator_background->select(1);
	texture_generator_background->set_tooltip_text(TTR("Preview background. Emissive effects are usually judged on black, transparency on the checker."));
	texture_generator_background->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_background_selected));
	texture_preview_header->add_child(texture_generator_background);
	texture_generator_type_description = memnew(Label);
	texture_generator_type_description->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	texture_generator_type_description->set_modulate(Color(1, 1, 1, 0.65));
	texture_preview_column->add_child(texture_generator_type_description);
	AspectRatioContainer *texture_preview_aspect = memnew(AspectRatioContainer);
	texture_preview_aspect->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	texture_preview_aspect->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_preview_column->add_child(texture_preview_aspect);
	texture_generator_stage = memnew(PanelContainer);
	texture_preview_aspect->add_child(texture_generator_stage);
	texture_generator_checker = memnew(TextureRect);
	{
		Ref<Image> checker = Image::create_empty(16, 16, false, Image::FORMAT_RGB8);
		for (int y = 0; y < 16; y++) {
			for (int x = 0; x < 16; x++) {
				const bool odd = ((x / 8) + (y / 8)) & 1;
				checker->set_pixel(x, y, odd ? Color(0.42, 0.42, 0.42) : Color(0.3, 0.3, 0.3));
			}
		}
		texture_generator_checker->set_texture(ImageTexture::create_from_image(checker));
	}
	texture_generator_checker->set_stretch_mode(TextureRect::STRETCH_TILE);
	texture_generator_checker->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	texture_generator_checker->set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
	texture_generator_stage->add_child(texture_generator_checker);
	texture_generator_preview = memnew(TextureRect);
	texture_generator_preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	texture_generator_preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	// The preview is usually smaller than the stage; filter it smoothly.
	texture_generator_preview->set_texture_filter(CanvasItem::TEXTURE_FILTER_LINEAR);
	texture_generator_stage->add_child(texture_generator_preview);
	HBoxContainer *texture_preview_transport = memnew(HBoxContainer);
	texture_preview_column->add_child(texture_preview_transport);
	texture_generator_animate = memnew(Button);
	texture_generator_animate->set_toggle_mode(true);
	texture_generator_animate->set_pressed(true);
	texture_generator_animate->set_text(TTR("Pause"));
	texture_generator_animate->set_tooltip_text(TTR("Play or pause the preview animation."));
	texture_preview_transport->add_child(texture_generator_animate);
	texture_generator_time = memnew(HSlider);
	texture_generator_time->set_min(0.0);
	texture_generator_time->set_max(1.0);
	texture_generator_time->set_step(0.01);
	texture_generator_time->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_time->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	texture_generator_time->set_tooltip_text(TTR("Scrub the animation. For a single texture this also drives the life cycle."));
	texture_preview_transport->add_child(texture_generator_time);
	texture_generator_time_label = memnew(Label);
	texture_generator_time_label->set_custom_minimum_size(Size2(96, 0) * EDSCALE);
	texture_generator_time_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	texture_preview_transport->add_child(texture_generator_time_label);
	texture_generator_frame_label = memnew(Label);
	texture_generator_frame_label->set_custom_minimum_size(Size2(96, 0) * EDSCALE);
	texture_generator_frame_label->set_modulate(Color(1, 1, 1, 0.65));
	texture_preview_transport->add_child(texture_generator_frame_label);

	// Right: compact inspector.
	ScrollContainer *texture_controls_scroll = memnew(ScrollContainer);
	texture_controls_scroll->set_custom_minimum_size(Size2(340, 0) * EDSCALE);
	texture_controls_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	texture_generator_inner_split->add_child(texture_controls_scroll);
	VBoxContainer *texture_controls = memnew(VBoxContainer);
	texture_controls->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_controls_scroll->add_child(texture_controls);

	auto add_section = [&](const String &p_title, bool p_folded) -> VBoxContainer * {
		FoldableContainer *section = memnew(FoldableContainer);
		section->set_title(p_title);
		section->set_folded(p_folded);
		texture_controls->add_child(section);
		VBoxContainer *content = memnew(VBoxContainer);
		content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		section->add_child(content);
		return content;
	};
	auto add_row = [&](VBoxContainer *p_parent, const String &p_label, Control *p_control, const String &p_tooltip = String()) -> HBoxContainer * {
		HBoxContainer *row = memnew(HBoxContainer);
		p_parent->add_child(row);
		Label *label = memnew(Label);
		label->set_text(p_label);
		label->set_custom_minimum_size(Size2(118, 0) * EDSCALE);
		label->set_tooltip_text(p_tooltip);
		label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		row->add_child(label);
		p_control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		if (!p_tooltip.is_empty()) {
			p_control->set_tooltip_text(p_tooltip);
		}
		row->add_child(p_control);
		return row;
	};
	auto make_slider = [&](real_t p_min, real_t p_max, real_t p_step, real_t p_value) -> EditorSpinSlider * {
		EditorSpinSlider *slider = memnew(EditorSpinSlider);
		slider->set_min(p_min);
		slider->set_max(p_max);
		slider->set_step(p_step);
		slider->set_value_no_signal(p_value);
		slider->set_editing_integer(p_step >= 1.0);
		slider->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_processing_value_changed));
		return slider;
	};
	auto make_toggle = [&](const String &p_text, const String &p_tooltip) -> CheckButton * {
		CheckButton *toggle = memnew(CheckButton);
		toggle->set_text(p_text);
		toggle->set_tooltip_text(p_tooltip);
		toggle->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_toggle_changed));
		return toggle;
	};

	VBoxContainer *effect_section = add_section(TTR("Effect"), false);
	texture_generator_param_grid = memnew(GridContainer);
	texture_generator_param_grid->set_columns(2);
	texture_generator_param_grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	effect_section->add_child(texture_generator_param_grid);
	HBoxContainer *seed_box = memnew(HBoxContainer);
	texture_generator_seed = memnew(SpinBox);
	texture_generator_seed->set_min(0);
	texture_generator_seed->set_max(99999);
	texture_generator_seed->set_step(1);
	texture_generator_seed->set_value(7);
	texture_generator_seed->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_seed->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_seed_changed));
	seed_box->add_child(texture_generator_seed);
	Button *randomize_seed = memnew(Button);
	randomize_seed->set_text(TTR("Random"));
	randomize_seed->set_tooltip_text(TTR("Pick a random seed for a new variation."));
	randomize_seed->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_randomize_seed_pressed));
	seed_box->add_child(randomize_seed);
	add_row(effect_section, TTR("Seed"), seed_box, TTR("The seed recreates the same procedural shape."));
	Button *reset_params = memnew(Button);
	reset_params->set_text(TTR("Reset to Defaults"));
	reset_params->set_tooltip_text(TTR("Restore the default parameters and colors of this effect type."));
	reset_params->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_reset_params_pressed));
	effect_section->add_child(reset_params);

	VBoxContainer *color_section = add_section(TTR("Color & Tone"), false);
	texture_generator_color_a = memnew(ColorPickerButton(TTR("Color A")));
	texture_generator_color_a->set_edit_alpha(false);
	texture_generator_color_a->set_custom_minimum_size(Size2(0, 24) * EDSCALE);
	texture_generator_color_a->connect("color_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_color_changed));
	HBoxContainer *color_a_row = add_row(color_section, TTR("Color A"), texture_generator_color_a);
	texture_generator_color_a_label = Object::cast_to<Label>(color_a_row->get_child(0));
	texture_generator_color_b = memnew(ColorPickerButton(TTR("Color B")));
	texture_generator_color_b->set_edit_alpha(false);
	texture_generator_color_b->set_custom_minimum_size(Size2(0, 24) * EDSCALE);
	texture_generator_color_b->connect("color_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_color_changed));
	HBoxContainer *color_b_row = add_row(color_section, TTR("Color B"), texture_generator_color_b);
	texture_generator_color_b_label = Object::cast_to<Label>(color_b_row->get_child(0));
	texture_generator_exposure = make_slider(0.1, 4.0, 0.01, 1.0);
	add_row(color_section, TTR("Exposure"), texture_generator_exposure, TTR("Brightness of the HDR result before tone mapping."));
	texture_generator_opacity = make_slider(0.0, 1.0, 0.01, 1.0);
	add_row(color_section, TTR("Opacity"), texture_generator_opacity, TTR("Multiplies the alpha channel."));

	VBoxContainer *motion_section = add_section(TTR("Motion & Mapping"), false);
	texture_generator_speed = make_slider(0.0, 5.0, 0.01, 1.0);
	add_row(motion_section, TTR("Speed"), texture_generator_speed, TTR("Animation speed of the noise evolution."));
	texture_generator_rotation = make_slider(-180.0, 180.0, 1.0, 0.0);
	texture_generator_rotation->set_suffix(U"°");
	add_row(motion_section, TTR("Rotation"), texture_generator_rotation, TTR("Rotates the effect inside the frame."));
	texture_generator_polar = make_toggle(TTR("Polar Coordinates"), TTR("Wrap the effect around the center: angle becomes X and radius becomes Y."));
	motion_section->add_child(texture_generator_polar);
	texture_generator_tiling = make_toggle(TTR("Seamless Tiling"), TTR("Generate a texture that repeats seamlessly."));
	motion_section->add_child(texture_generator_tiling);

	VBoxContainer *output_section = add_section(TTR("Output"), false);
	texture_generator_resolution = memnew(OptionButton);
	texture_generator_resolution->add_item("128 x 128");
	texture_generator_resolution->add_item("256 x 256");
	texture_generator_resolution->add_item("512 x 512");
	texture_generator_resolution->add_item("1024 x 1024");
	texture_generator_resolution->add_item("2048 x 2048");
	texture_generator_resolution->select(2);
	texture_generator_resolution->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_resolution_selected));
	add_row(output_section, TTR("Resolution"), texture_generator_resolution, TTR("Size of the exported PNG. Sprite sheets split it into frames."));
	texture_generator_output = memnew(OptionButton);
	texture_generator_output->add_item(TTR("Single texture"), VFX_TEXTURE_OUTPUT_STILL);
	texture_generator_output->add_item(TTR("Sprite sheet 4 x 4"), VFX_TEXTURE_OUTPUT_SHEET_4X4);
	texture_generator_output->add_item(TTR("Sprite sheet 8 x 8"), VFX_TEXTURE_OUTPUT_SHEET_8X8);
	texture_generator_output->select(0);
	texture_generator_output->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_output_selected));
	add_row(output_section, TTR("Layout"), texture_generator_output);
	texture_generator_fps = memnew(EditorSpinSlider);
	texture_generator_fps->set_min(1);
	texture_generator_fps->set_max(60);
	texture_generator_fps->set_step(1);
	texture_generator_fps->set_value_no_signal(12);
	texture_generator_fps->set_editing_integer(true);
	texture_generator_fps->set_suffix(TTR("fps"));
	texture_generator_fps->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_fps_changed));
	texture_generator_fps_row = add_row(output_section, TTR("Frame rate"), texture_generator_fps, TTR("Playback rate written to the emitter's animation speed."));
	texture_generator_duration = memnew(EditorSpinSlider);
	texture_generator_duration->set_min(0.1);
	texture_generator_duration->set_max(30.0);
	texture_generator_duration->set_step(0.1);
	texture_generator_duration->set_value_no_signal(2.0);
	texture_generator_duration->set_suffix("s");
	texture_generator_duration->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_duration_changed));
	texture_generator_duration_row = add_row(output_section, TTR("Duration"), texture_generator_duration, TTR("Time span sampled by the sprite sheet frames. The life cycle runs over this span."));

	VBoxContainer *post_section = add_section(TTR("Post-processing"), true);
	texture_generator_toon = make_toggle(TTR("Toon Shading"), TTR("Quantize luminance and alpha into three cel-shaded bands."));
	post_section->add_child(texture_generator_toon);
	texture_generator_toon_dark = make_slider(0.0, 1.0, 0.01, 0.5);
	add_row(post_section, TTR("Toon dark"), texture_generator_toon_dark);
	texture_generator_toon_light = make_slider(0.0, 1.0, 0.01, 0.85);
	add_row(post_section, TTR("Toon light"), texture_generator_toon_light);
	texture_generator_alpha_threshold = make_slider(0.0, 0.95, 0.01, 0.0);
	add_row(post_section, TTR("Alpha threshold"), texture_generator_alpha_threshold, TTR("Remove alpha values below this threshold and remap the remaining range."));
	texture_generator_alpha_blur = make_slider(0.0, 1.0, 0.01, 0.0);
	add_row(post_section, TTR("Alpha blur"), texture_generator_alpha_blur, TTR("Blend the alpha channel with a soft 3 x 3 blur."));
	texture_generator_alpha_preview = memnew(CheckButton);
	texture_generator_alpha_preview->set_text(TTR("Preview Alpha Mask"));
	texture_generator_alpha_preview->set_tooltip_text(TTR("Show the processed alpha as grayscale. This changes only the preview."));
	texture_generator_alpha_preview->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_alpha_preview_toggled));
	post_section->add_child(texture_generator_alpha_preview);
	texture_generator_normal_map = make_toggle(TTR("Export as Normal Map"), TTR("Derive tangent-space normals from the processed alpha shape."));
	post_section->add_child(texture_generator_normal_map);
	texture_generator_normal_height_scale = make_slider(0.5, 10.0, 0.1, 3.0);
	add_row(post_section, TTR("Normal strength"), texture_generator_normal_height_scale);
	auto make_tint = [&](const String &p_label) -> ColorPickerButton * {
		ColorPickerButton *picker = memnew(ColorPickerButton(p_label));
		picker->set_edit_alpha(false);
		picker->set_pick_color(Color(1.0, 1.0, 1.0, 1.0));
		picker->set_custom_minimum_size(Size2(0, 24) * EDSCALE);
		picker->connect("color_changed", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_color_changed));
		add_row(post_section, p_label, picker);
		return picker;
	};
	texture_generator_shadows_tint = make_tint(TTR("Shadows tint"));
	texture_generator_midtones_tint = make_tint(TTR("Midtones tint"));
	texture_generator_highlights_tint = make_tint(TTR("Highlights tint"));
	Button *reset_balance = memnew(Button);
	reset_balance->set_text(TTR("Reset Color Balance"));
	reset_balance->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_reset_color_balance_pressed));
	post_section->add_child(reset_balance);

	// Footer: recipe actions on the left, export flow on the right.
	texture_generator_root->add_child(memnew(HSeparator));
	HBoxContainer *texture_footer = memnew(HBoxContainer);
	texture_generator_root->add_child(texture_footer);
	Button *texture_recipe_load_button = memnew(Button);
	texture_recipe_load_button->set_text(TTR("Load Recipe..."));
	texture_recipe_load_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_recipe_load_pressed));
	texture_footer->add_child(texture_recipe_load_button);
	Button *texture_recipe_save_button = memnew(Button);
	texture_recipe_save_button->set_text(TTR("Save Recipe..."));
	texture_recipe_save_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_recipe_save_pressed));
	texture_footer->add_child(texture_recipe_save_button);
	texture_generator_status = memnew(Label);
	texture_generator_status->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	texture_generator_status->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	texture_generator_status->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	texture_generator_status->set_modulate(Color(1, 1, 1, 0.75));
	texture_footer->add_child(texture_generator_status);
	Button *texture_close_button = memnew(Button);
	texture_close_button->set_text(TTR("Close"));
	texture_close_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_close_pressed));
	texture_footer->add_child(texture_close_button);
	Button *texture_export_button = memnew(Button);
	texture_export_button->set_text(TTR("Export PNG..."));
	texture_export_button->set_tooltip_text(TTR("Render at full resolution and save the texture into the project."));
	texture_export_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_export_pressed));
	texture_footer->add_child(texture_export_button);
	texture_generator_apply_button = memnew(Button);
	texture_generator_apply_button->set_text(TTR("Apply to Emitter"));
	texture_generator_apply_button->set_disabled(true);
	texture_generator_apply_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_apply_pressed));
	texture_footer->add_child(texture_generator_apply_button);

	texture_generator_animate->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_animate_toggled));
	texture_generator_time->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_generator_time_changed));
	_select_texture_type(texture_generator_type, true);
	_on_texture_generator_background_selected(texture_generator_background->get_selected());
	_update_texture_time_controls();
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

	texture_recipe_save_dialog = memnew(EditorFileDialog);
	texture_recipe_save_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	texture_recipe_save_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	texture_recipe_save_dialog->set_title(TTR("Save VFX Texture Recipe"));
	texture_recipe_save_dialog->add_filter("*.cfg", TTR("Texture Recipes"));
	texture_recipe_save_dialog->set_current_dir("res://");
	add_child(texture_recipe_save_dialog);
	texture_recipe_save_dialog->connect("file_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_recipe_save_path_selected));
	texture_recipe_save_dialog->connect(SceneStringName(visibility_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_recipe_dialog_visibility_changed));

	texture_recipe_load_dialog = memnew(EditorFileDialog);
	texture_recipe_load_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	texture_recipe_load_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	texture_recipe_load_dialog->set_title(TTR("Load VFX Texture Recipe"));
	texture_recipe_load_dialog->add_filter("*.cfg", TTR("Texture Recipes"));
	texture_recipe_load_dialog->set_current_dir("res://");
	add_child(texture_recipe_load_dialog);
	texture_recipe_load_dialog->connect("file_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_recipe_load_path_selected));
	texture_recipe_load_dialog->connect(SceneStringName(visibility_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_texture_recipe_dialog_visibility_changed));

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
	load_scene_dialog->connect("file_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_request_load_particle_scene));
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
