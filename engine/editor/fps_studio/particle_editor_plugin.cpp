/**************************************************************************/
/*  particle_editor_plugin.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_editor_plugin.h"

#include "core/input/input_event.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "editor/editor_data.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/editor_string_names.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/themes/editor_scale.h"
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
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/environment.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"
#include "scene/resources/packed_scene.h"

enum ParticleFileMenuItem {
	PARTICLE_FILE_NEW_GPU = 0,
	PARTICLE_FILE_NEW_CPU = 1,
	PARTICLE_FILE_BURST = 2,
	PARTICLE_FILE_FOUNTAIN = 3,
	PARTICLE_FILE_SMOKE = 4,
	PARTICLE_FILE_LOAD_SCENE = 5,
	PARTICLE_FILE_SAVE_SCENE = 6,
};

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
	constexpr real_t grid_extent = 30.0;
	constexpr int grid_divisions = 60;
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
		const Color grid_color = i == 0 ? Color(0.76, 0.23, 0.19, 0.85) : Color(0.48, 0.56, 0.7, 0.4);
		add_grid_line(Vector3(-grid_extent, -0.1, coordinate), Vector3(grid_extent, -0.1, coordinate), grid_color);

		const Color side_color = i == 0 ? Color(0.23, 0.42, 0.78, 0.85) : Color(0.48, 0.56, 0.7, 0.4);
		add_grid_line(Vector3(coordinate, -0.1, -grid_extent), Vector3(coordinate, -0.1, grid_extent), side_color);
	}
	grid_mesh->surface_end();

	MeshInstance3D *grid_instance = memnew(MeshInstance3D);
	grid_instance->set_name("ParticlePreviewGrid");
	grid_instance->set_mesh(grid_mesh);
	grid_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	return grid_instance;
}

static Ref<QuadMesh> create_particle_quad() {
	Ref<QuadMesh> particle_mesh;
	particle_mesh.instantiate();
	particle_mesh->set_size(Vector2(0.12, 0.12));

	Ref<StandardMaterial3D> particle_material;
	particle_material.instantiate();
	particle_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	particle_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	particle_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
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

Node *FPSStudioParticleEditorPlugin::_get_active_scene_root() const {
	EditorData &editor_data = EditorNode::get_editor_data();
	const int scene_index = editor_data.get_edited_scene();
	if (scene_index < 0 || scene_index >= editor_data.get_edited_scene_count()) {
		return nullptr;
	}
	return editor_data.get_edited_scene_root(scene_index);
}

Node *FPSStudioParticleEditorPlugin::_get_edited_emitter() const {
	return Object::cast_to<Node>(ObjectDB::get_instance(edited_emitter_id));
}

void FPSStudioParticleEditorPlugin::_collect_emitters(Node *p_node, Vector<Node *> &r_emitters) const {
	if (!p_node) {
		return;
	}
	if (Object::cast_to<GPUParticles3D>(p_node) || Object::cast_to<CPUParticles3D>(p_node)) {
		r_emitters.push_back(p_node);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_emitters(p_node->get_child(i), r_emitters);
	}
}

void FPSStudioParticleEditorPlugin::_update_particle_file_menu_state() {
	if (!particle_file_menu) {
		return;
	}
	PopupMenu *menu = particle_file_menu->get_popup();
	const bool scene_is_open = _get_active_scene_root() != nullptr;
	for (int id = PARTICLE_FILE_NEW_GPU; id <= PARTICLE_FILE_SMOKE; id++) {
		menu->set_item_disabled(menu->get_item_index(id), !scene_is_open);
	}
	menu->set_item_disabled(menu->get_item_index(PARTICLE_FILE_LOAD_SCENE), !scene_is_open);
	menu->set_item_disabled(menu->get_item_index(PARTICLE_FILE_SAVE_SCENE), _get_edited_emitter() == nullptr);
}

void FPSStudioParticleEditorPlugin::_update_emitter_tree() {
	if (!emitter_tree) {
		return;
	}
	emitter_tree->clear();
	TreeItem *root_item = emitter_tree->create_item();
	Node *scene_root = _get_active_scene_root();
	Vector<Node *> emitters;
	_collect_emitters(scene_root, emitters);
	Node *edited_emitter = _get_edited_emitter();
	TreeItem *selected_item = nullptr;
	for (Node *emitter : emitters) {
		TreeItem *item = emitter_tree->create_item(root_item);
		item->set_text(0, emitter->get_name());
		item->set_tooltip_text(0, String(emitter->get_class()) + "\n" + String(emitter->get_path()));
		item->set_metadata(0, emitter->get_path());
		if (emitter == edited_emitter) {
			selected_item = item;
		}
	}
	if (selected_item) {
		selected_item->select(0);
	}
	_update_particle_file_menu_state();
	if (!scene_root) {
		root_item->set_text(0, TTR("No scene is open"));
	} else if (emitters.is_empty()) {
		root_item->set_text(0, TTR("No 3D particle emitters in this scene"));
	}
}

void FPSStudioParticleEditorPlugin::_set_edited_emitter(Node *p_node, bool p_update_tree) {
	if (p_node && !Object::cast_to<GPUParticles3D>(p_node) && !Object::cast_to<CPUParticles3D>(p_node)) {
		p_node = nullptr;
	}
	if (p_node && _get_edited_emitter() == p_node) {
		return;
	}
	edited_emitter_id = p_node ? p_node->get_instance_id() : ObjectID();
	preview_camera_target = Vector3(0.0, 0.5, 0.0);
	_update_preview_camera();
	_update_particle_file_menu_state();
	if (emitter_inspector) {
		emitter_inspector->edit(p_node);
	}

	Ref<Material> process_material;
	if (GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(p_node)) {
		process_material = gpu_emitter->get_process_material();
	}
	if (process_material_inspector) {
		process_material_inspector->edit(process_material.ptr());
		process_material_inspector->set_visible(process_material.is_valid());
	}
	if (process_material_page) {
		process_material_page->set_visible(Object::cast_to<GPUParticles3D>(p_node) != nullptr);
	}
	_update_particle_graph_controls();

	if (selection_label) {
		if (p_node) {
			selection_label->set_text(vformat(TTR("Previewing %s"), p_node->get_name()));
		} else {
			selection_label->set_text(TTR("Select a GPUParticles3D or CPUParticles3D node to preview it here."));
		}
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
	if (preview_emitter) {
		preview_root->remove_child(preview_emitter);
		preview_emitter->queue_free();
		preview_emitter = nullptr;
	}
	Node *source = _get_edited_emitter();
	if (!source) {
		rebuilding_preview = false;
		return;
	}

	// Copy the native particle settings but keep cloned scene scripts and signal
	// connections out of the isolated preview.
	preview_emitter = source->duplicate(Node::DUPLICATE_SCRIPTS);
	if (!preview_emitter) {
		rebuilding_preview = false;
		return;
	}
	preview_emitter->set_name("ParticlePreviewEmitter");
	_clear_preview_scripts(preview_emitter);

	if (Node3D *preview_node_3d = Object::cast_to<Node3D>(preview_emitter)) {
		Transform3D preview_transform = preview_node_3d->get_transform();
		preview_transform.origin = Vector3();
		preview_node_3d->set_transform(preview_transform);
	}

	_apply_preview_defaults();

	preview_root->add_child(preview_emitter);
	preview_emitter->set("emitting", true);
	preview_emitter->call("restart", true);
	preview_duration = 5.0;
	if (source->has_method("get_lifetime")) {
		preview_duration = MAX(0.1, double(source->call("get_lifetime")));
	}
	preview_time = 0.0;
	if (timeline) {
		timeline->set_max(preview_duration);
		timeline->set_value(0.0);
	}
	_set_preview_playing(preview_playing);
	_update_time_label();
	rebuilding_preview = false;
}

void FPSStudioParticleEditorPlugin::_clear_preview_scripts(Node *p_node) {
	if (!p_node) {
		return;
	}
	p_node->set_script(Ref<Script>());
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_clear_preview_scripts(p_node->get_child(i));
	}
}

void FPSStudioParticleEditorPlugin::_apply_preview_defaults() {
	if (!preview_emitter) {
		return;
	}
	if (preview_quad.is_null()) {
		preview_quad = create_particle_quad();
	}
	if (GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(preview_emitter)) {
		if (gpu_emitter->get_process_material().is_null()) {
			Ref<ParticleProcessMaterial> process_material;
			process_material.instantiate();
			gpu_emitter->set_process_material(process_material);
		}
		if (gpu_emitter->get_draw_passes() == 0) {
			gpu_emitter->set_draw_passes(1);
		}
		if (gpu_emitter->get_draw_pass_mesh(0).is_null()) {
			gpu_emitter->set_draw_pass_mesh(0, preview_quad);
		}
	} else if (CPUParticles3D *cpu_emitter = Object::cast_to<CPUParticles3D>(preview_emitter)) {
		if (cpu_emitter->get_mesh().is_null()) {
			cpu_emitter->set_mesh(preview_quad);
		}
	}
}

void FPSStudioParticleEditorPlugin::_sync_preview_properties() {
	Node *source = _get_edited_emitter();
	if (!source || !preview_emitter) {
		return;
	}
	List<PropertyInfo> properties;
	source->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!(property.usage & PROPERTY_USAGE_STORAGE) || (property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE) || property.name == "name" || property.name == "owner" || property.name == "script" ||
				property.name == "scene_file_path" || property.name == "emitting" || property.name == "visible" || property.name == "transform") {
			continue;
		}
		bool valid = false;
		Variant value = source->get(property.name, &valid);
		if (valid) {
			preview_emitter->set(property.name, value);
		}
	}
	_apply_preview_defaults();
	if (Node3D *source_node_3d = Object::cast_to<Node3D>(source)) {
		if (Node3D *preview_node_3d = Object::cast_to<Node3D>(preview_emitter)) {
			Transform3D preview_transform = source_node_3d->get_transform();
			preview_transform.origin = Vector3();
			preview_node_3d->set_transform(preview_transform);
		}
	}
	preview_emitter->set("visible", true);
	_set_preview_playing(preview_playing);
}

void FPSStudioParticleEditorPlugin::_set_preview_playing(bool p_playing) {
	preview_playing = p_playing;
	if (play_button) {
		play_button->set_pressed_no_signal(preview_playing);
		play_button->set_text(preview_playing ? TTR("Pause") : TTR("Play"));
	}
	if (preview_emitter && preview_emitter->has_method("set_speed_scale")) {
		preview_emitter->call("set_speed_scale", preview_playing ? preview_speed : 0.0);
	}
}

void FPSStudioParticleEditorPlugin::_on_play_toggled(bool p_pressed) {
	if (!_get_edited_emitter()) {
		return;
	}
	_set_preview_playing(p_pressed);
}

void FPSStudioParticleEditorPlugin::_on_restart_pressed() {
	if (!preview_emitter) {
		return;
	}
	preview_time = 0.0;
	if (timeline) {
		timeline->set_value_no_signal(0.0);
	}
	preview_emitter->call("restart", true);
	preview_emitter->set("emitting", true);
	_set_preview_playing(true);
	_update_time_label();
}

void FPSStudioParticleEditorPlugin::_on_timeline_changed(double p_value) {
	if (!preview_emitter) {
		return;
	}
	_set_preview_playing(false);
	preview_time = p_value;
	preview_emitter->call("restart", true);
	preview_emitter->set("emitting", true);
	preview_emitter->call("request_particles_process", preview_time, 0.0);
	_update_time_label();
}

void FPSStudioParticleEditorPlugin::_on_emitter_selected() {
	TreeItem *item = emitter_tree ? emitter_tree->get_selected() : nullptr;
	Node *scene_root = _get_active_scene_root();
	if (!item || !scene_root) {
		return;
	}
	_set_edited_emitter(scene_root->get_node_or_null(item->get_metadata(0)), false);
}

void FPSStudioParticleEditorPlugin::_on_emitter_property_edited(const String &p_property) {
	Node *source = _get_edited_emitter();
	if (GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(source)) {
		Ref<Material> process_material = gpu_emitter->get_process_material();
		if (process_material_inspector->get_edited_object() != process_material.ptr()) {
			process_material_inspector->edit(process_material.ptr());
		}
		process_material_inspector->set_visible(process_material.is_valid());
	}
	_update_particle_graph_controls();
	_sync_preview_properties();
	if (p_property == "lifetime") {
		if (source && source->has_method("get_lifetime")) {
			preview_duration = MAX(0.1, double(source->call("get_lifetime")));
			if (timeline) {
				timeline->set_max(preview_duration);
			}
		}
		_update_emitter_tree();
	}
}

Ref<VisualShader> FPSStudioParticleEditorPlugin::_get_particle_visual_shader(Node *p_emitter) const {
	GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(p_emitter);
	if (!gpu_emitter) {
		return Ref<VisualShader>();
	}
	Ref<Material> process_material = gpu_emitter->get_process_material();
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
	if (!particle_graph_button) {
		return;
	}
	Node *emitter = _get_edited_emitter();
	GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(emitter);
	Ref<VisualShader> visual_shader = _get_particle_visual_shader(emitter);
	particle_graph_button->set_disabled(!gpu_emitter);
	particle_graph_button->set_text(visual_shader.is_valid() ? TTR("Edit Node Graph") : TTR("Create Node Graph"));
	if (visual_shader.is_valid()) {
		particle_graph_button->set_tooltip_text(TTR("Open the native Godot VisualShader graph for this particle emitter."));
	} else {
		particle_graph_button->set_tooltip_text(TTR("Create a native Godot particle VisualShader. The current Process Material will be replaced and can be restored with Undo."));
	}
}

void FPSStudioParticleEditorPlugin::_on_particle_graph_button_pressed() {
	Node *emitter = _get_edited_emitter();
	GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(emitter);
	if (!gpu_emitter) {
		return;
	}

	Ref<VisualShader> visual_shader = _get_particle_visual_shader(gpu_emitter);
	if (visual_shader.is_valid()) {
		_open_particle_graph(visual_shader);
		return;
	}

	if (gpu_emitter->get_process_material().is_valid()) {
		particle_graph_confirmation->set_text(TTR("Creating a node graph replaces this emitter's current Process Material. You can restore the previous material with Undo."));
		particle_graph_confirmation->popup_centered();
		return;
	}
	_create_particle_graph();
}

void FPSStudioParticleEditorPlugin::_create_particle_graph() {
	GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(_get_edited_emitter());
	if (!gpu_emitter) {
		return;
	}

	Ref<VisualShader> visual_shader;
	visual_shader.instantiate();
	visual_shader->set_mode(Shader::MODE_PARTICLES);

	Ref<ShaderMaterial> shader_material;
	shader_material.instantiate();
	shader_material->set_shader(visual_shader);

	Ref<Material> previous_material = gpu_emitter->get_process_material();
	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Create Particle Node Graph"));
	undo_redo->add_do_method(gpu_emitter, "set_process_material", shader_material);
	undo_redo->add_undo_method(gpu_emitter, "set_process_material", previous_material);
	undo_redo->add_do_reference(shader_material.ptr());
	if (previous_material.is_valid()) {
		undo_redo->add_undo_reference(previous_material.ptr());
	}
	undo_redo->commit_action();

	process_material_inspector->edit(shader_material.ptr());
	process_material_inspector->set_visible(true);
	_sync_preview_properties();
	_update_particle_graph_controls();
	_open_particle_graph(visual_shader);
}

void FPSStudioParticleEditorPlugin::_open_particle_graph(const Ref<VisualShader> &p_visual_shader) {
	if (p_visual_shader.is_valid()) {
		EditorNode::get_singleton()->edit_item(p_visual_shader.ptr(), this);
	}
}

void FPSStudioParticleEditorPlugin::_on_speed_changed(double p_value) {
	preview_speed = p_value;
	if (preview_playing && preview_emitter) {
		preview_emitter->call("set_speed_scale", preview_speed);
	}
}

void FPSStudioParticleEditorPlugin::_on_loop_toggled(bool p_enabled) {
	preview_loop = p_enabled;
}

void FPSStudioParticleEditorPlugin::_on_reset_view_pressed() {
	preview_camera_yaw = 0.0;
	preview_camera_pitch = 0.245;
	preview_camera_distance = 6.18;
	preview_camera_target = Vector3(0.0, 0.5, 0.0);
	_update_preview_camera();
}

void FPSStudioParticleEditorPlugin::_on_grid_toggled(bool p_enabled) {
	if (preview_grid) {
		preview_grid->set_visible(p_enabled);
	}
}

void FPSStudioParticleEditorPlugin::_on_particle_file_menu_pressed(int p_id) {
	if (p_id == PARTICLE_FILE_LOAD_SCENE) {
		if (load_scene_dialog) {
			load_scene_dialog->popup_file_dialog();
		}
		return;
	}
	if (p_id == PARTICLE_FILE_SAVE_SCENE) {
		_on_save_scene_pressed();
		return;
	}

	Node *scene_root = _get_active_scene_root();
	if (!scene_root || p_id < PARTICLE_FILE_NEW_GPU || p_id > PARTICLE_FILE_SMOKE) {
		return;
	}

	Node3D *emitter = nullptr;
	Ref<QuadMesh> particle_mesh = create_particle_quad();
	if (p_id == PARTICLE_FILE_NEW_CPU) {
		CPUParticles3D *cpu_emitter = memnew(CPUParticles3D);
		cpu_emitter->set_name("CPUParticles3D");
		cpu_emitter->set_amount(32);
		cpu_emitter->set_lifetime(2.0);
		cpu_emitter->set_mesh(particle_mesh);
		emitter = cpu_emitter;
	} else {
		GPUParticles3D *gpu_emitter = memnew(GPUParticles3D);
		String emitter_name = "GPUParticles3D";
		int emitter_amount = 32;
		double emitter_lifetime = 2.0;
		if (p_id == PARTICLE_FILE_BURST) {
			emitter_name = "BurstParticles";
			emitter_amount = 64;
			emitter_lifetime = 1.25;
		} else if (p_id == PARTICLE_FILE_FOUNTAIN) {
			emitter_name = "FountainParticles";
		} else if (p_id == 4) {
			emitter_name = "SmokeParticles";
			emitter_amount = 48;
			emitter_lifetime = 4.0;
		}
		gpu_emitter->set_name(emitter_name);
		gpu_emitter->set_amount(emitter_amount);
		gpu_emitter->set_lifetime(emitter_lifetime);

		Ref<ParticleProcessMaterial> process_material;
		process_material.instantiate();
		if (p_id >= 2) {
			process_material->set_direction(Vector3(0.0, 1.0, 0.0));
			process_material->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.5);
			process_material->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.0);
			if (p_id == 2) {
				gpu_emitter->set_one_shot(true);
				gpu_emitter->set_explosiveness_ratio(1.0);
				process_material->set_spread(180.0);
				process_material->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 3.0);
				process_material->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 7.0);
				process_material->set_gravity(Vector3(0.0, -3.0, 0.0));
				process_material->set_color(Color(1.0, 0.38, 0.08, 1.0));
			} else if (p_id == 3) {
				process_material->set_spread(18.0);
				process_material->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 2.8);
				process_material->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 4.8);
				process_material->set_gravity(Vector3(0.0, -3.0, 0.0));
				process_material->set_color(Color(0.2, 0.7, 1.0, 1.0));
			} else {
				process_material->set_spread(12.0);
				process_material->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.4);
				process_material->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.9);
				process_material->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 1.0);
				process_material->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 2.5);
				process_material->set_gravity(Vector3(0.0, 0.6, 0.0));
				process_material->set_color(Color(0.72, 0.76, 0.82, 0.5));
			}
		}
		gpu_emitter->set_process_material(process_material);
		gpu_emitter->set_draw_pass_mesh(0, particle_mesh);
		gpu_emitter->set_emitting(true);
		emitter = gpu_emitter;
	}

	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(vformat(TTR("Create %s"), emitter->get_class()));
	undo_redo->add_do_method(scene_root, "add_child", emitter, true);
	undo_redo->add_do_method(emitter, "set_owner", scene_root);
	undo_redo->add_do_reference(emitter);
	undo_redo->add_undo_method(scene_root, "remove_child", emitter);
	undo_redo->commit_action();

	EditorSelection *editor_selection = EditorNode::get_singleton()->get_editor_selection();
	editor_selection->clear();
	editor_selection->add_node(emitter);
	_set_edited_emitter(emitter);
}

void FPSStudioParticleEditorPlugin::_on_save_scene_pressed() {
	Node *emitter = _get_edited_emitter();
	if (!emitter || !save_scene_dialog) {
		return;
	}
	save_scene_dialog->set_current_dir("res://");
	save_scene_dialog->set_current_file(String(emitter->get_name()).validate_filename() + ".tscn");
	save_scene_dialog->popup_file_dialog();
}

void FPSStudioParticleEditorPlugin::_save_emitter_scene(const String &p_path) {
	Node *emitter = _get_edited_emitter();
	if (!emitter) {
		return;
	}

	Node *emitter_copy = emitter->duplicate();
	if (!emitter_copy) {
		EditorNode::get_singleton()->show_warning(TTR("Could not duplicate the selected emitter for saving."));
		return;
	}
	emitter_copy->set_name(emitter->get_name());
	set_scene_subtree_owners(emitter_copy, emitter_copy);

	Ref<PackedScene> packed_scene;
	packed_scene.instantiate();
	const Error pack_error = packed_scene->pack(emitter_copy);
	memdelete(emitter_copy);
	if (pack_error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not pack the selected emitter scene (error %d)."), pack_error));
		return;
	}

	String save_path = p_path;
	if (save_path.get_extension().to_lower() != "tscn") {
		save_path += ".tscn";
	}
	const Error save_error = ResourceSaver::save(packed_scene, save_path);
	if (save_error != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not save particle scene to %s (error %d)."), save_path, save_error));
		return;
	}
	EditorFileSystem::get_singleton()->scan_changes();
}

void FPSStudioParticleEditorPlugin::_load_particle_scene(const String &p_path) {
	Node *scene_root = _get_active_scene_root();
	if (!scene_root) {
		EditorNode::get_singleton()->show_warning(TTR("Open a scene before loading a particle scene."));
		return;
	}

	Ref<PackedScene> packed_scene = ResourceLoader::load(p_path, "PackedScene");
	if (packed_scene.is_null()) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not load particle scene from %s."), p_path));
		return;
	}

	Node *particle_scene = packed_scene->instantiate();
	if (!particle_scene) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Could not instantiate particle scene from %s."), p_path));
		return;
	}
	Vector<Node *> emitters;
	_collect_emitters(particle_scene, emitters);
	if (emitters.is_empty()) {
		memdelete(particle_scene);
		EditorNode::get_singleton()->show_warning(TTR("The selected scene does not contain a 3D particle emitter."));
		return;
	}

	EditorUndoRedoManager *undo_redo = get_undo_redo();
	undo_redo->create_action(TTR("Load Particle Scene"));
	undo_redo->add_do_method(scene_root, "add_child", particle_scene, true);
	undo_redo->add_do_method(particle_scene, "set_owner", scene_root);
	undo_redo->add_do_reference(particle_scene);
	undo_redo->add_undo_method(scene_root, "remove_child", particle_scene);
	undo_redo->commit_action();

	EditorSelection *editor_selection = EditorNode::get_singleton()->get_editor_selection();
	editor_selection->clear();
	editor_selection->add_node(emitters[0]);
	_set_edited_emitter(emitters[0]);
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
			preview_camera_distance = CLAMP(preview_camera_distance * (button_index == MouseButton::WHEEL_UP ? 0.85 : 1.0 / 0.85), 1.0, 50.0);
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

void FPSStudioParticleEditorPlugin::_update_time_label() {
	if (time_label) {
		time_label->set_text(vformat("%s / %s s", String::num(preview_time, 2), String::num(preview_duration, 2)));
	}
}

void FPSStudioParticleEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS && screen && screen->is_visible_in_tree()) {
		Node *edited_emitter = _get_edited_emitter();
		Node *scene_root = _get_active_scene_root();
		if (edited_emitter && (!scene_root || (scene_root != edited_emitter && !scene_root->is_ancestor_of(edited_emitter)))) {
			_set_edited_emitter(nullptr);
		} else if (GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(edited_emitter)) {
			Ref<Material> process_material = gpu_emitter->get_process_material();
			if (process_material_inspector && process_material_inspector->get_edited_object() != process_material.ptr()) {
				process_material_inspector->edit(process_material.ptr());
				process_material_inspector->set_visible(process_material.is_valid());
				_update_particle_graph_controls();
				_sync_preview_properties();
			}
		}
	}
	if (p_what == NOTIFICATION_PROCESS && preview_playing && preview_emitter && screen->is_visible_in_tree()) {
		preview_time += get_process_delta_time() * preview_speed;
		if (preview_time >= preview_duration) {
			if (preview_loop) {
				preview_time = Math::fmod(preview_time, preview_duration);
				preview_emitter->call("restart", true);
			} else {
				preview_time = preview_duration;
				_set_preview_playing(false);
			}
		}
		if (timeline) {
			timeline->set_value_no_signal(preview_time);
		}
		_update_time_label();
	} else if (p_what == NOTIFICATION_PREDELETE) {
		if (preview_emitter) {
			preview_emitter->queue_free();
			preview_emitter = nullptr;
		}
	}
}

void FPSStudioParticleEditorPlugin::make_visible(bool p_visible) {
	if (screen) {
		screen->set_visible(p_visible);
	}
	if (p_visible) {
		_update_emitter_tree();
		if (_get_edited_emitter()) {
			_rebuild_preview();
		}
	}
}

void FPSStudioParticleEditorPlugin::edit(Object *p_object) {
	_set_edited_emitter(Object::cast_to<Node>(p_object));
}

bool FPSStudioParticleEditorPlugin::handles(Object *p_object) const {
	return p_object && (p_object->is_class("GPUParticles3D") || p_object->is_class("CPUParticles3D"));
}

void FPSStudioParticleEditorPlugin::edited_scene_changed() {
	_update_emitter_tree();
	Node *scene_root = _get_active_scene_root();
	Node *edited_emitter = _get_edited_emitter();
	if (scene_root && edited_emitter && (scene_root == edited_emitter || scene_root->is_ancestor_of(edited_emitter))) {
		_rebuild_preview();
	} else {
		_set_edited_emitter(nullptr);
	}
}

const Ref<Texture2D> FPSStudioParticleEditorPlugin::get_plugin_icon() const {
	return EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("GPUParticles3D"), EditorStringName(EditorIcons));
}

FPSStudioParticleEditorPlugin::FPSStudioParticleEditorPlugin() {
	set_process(true);

	VBoxContainer *workspace = memnew(VBoxContainer);
	workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	screen = workspace;
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(workspace);
	workspace->hide();

	HSplitContainer *main_split = memnew(HSplitContainer);
	main_split->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	main_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	workspace->add_child(main_split);

	VBoxContainer *emitters_panel = memnew(VBoxContainer);
	emitters_panel->set_custom_minimum_size(Size2(210, 0) * EDSCALE);
	main_split->add_child(emitters_panel);
	HBoxContainer *emitters_header = memnew(HBoxContainer);
	emitters_panel->add_child(emitters_header);
	Label *emitters_title = memnew(Label);
	emitters_title->set_text(TTR("Emitters"));
	emitters_title->add_theme_font_size_override(SceneStringName(font_size), 16 * EDSCALE);
	emitters_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	emitters_header->add_child(emitters_title);
	particle_file_menu = memnew(MenuButton);
	particle_file_menu->set_text(TTR("File"));
	particle_file_menu->set_tooltip_text(TTR("Create, load, or save a particle emitter scene."));
	PopupMenu *particle_menu = particle_file_menu->get_popup();
	particle_menu->add_item(TTR("New GPU Emitter"), PARTICLE_FILE_NEW_GPU);
	particle_menu->add_item(TTR("New CPU Emitter"), PARTICLE_FILE_NEW_CPU);
	particle_menu->add_separator(TTR("GPU Presets"));
	particle_menu->add_item(TTR("Burst"), PARTICLE_FILE_BURST);
	particle_menu->add_item(TTR("Fountain"), PARTICLE_FILE_FOUNTAIN);
	particle_menu->add_item(TTR("Smoke"), PARTICLE_FILE_SMOKE);
	particle_menu->add_separator(TTR("Particle Scene"));
	particle_menu->add_item(TTR("Load Particle Scene..."), PARTICLE_FILE_LOAD_SCENE);
	particle_menu->add_item(TTR("Save Particle Scene As..."), PARTICLE_FILE_SAVE_SCENE);
	particle_menu->connect(SceneStringName(id_pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_particle_file_menu_pressed));
	emitters_header->add_child(particle_file_menu);
	emitter_tree = memnew(Tree);
	emitter_tree->set_hide_root(true);
	emitter_tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_tree->connect(SceneStringName(item_selected), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_selected));
	emitters_panel->add_child(emitter_tree);

	VBoxContainer *preview_panel = memnew(VBoxContainer);
	preview_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	HSplitContainer *preview_settings_split = memnew(HSplitContainer);
	preview_settings_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_settings_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_split->add_child(preview_settings_split);
	preview_settings_split->add_child(preview_panel);

	HBoxContainer *preview_toolbar = memnew(HBoxContainer);
	preview_panel->add_child(preview_toolbar);
	selection_label = memnew(Label);
	selection_label->set_text(TTR("Select a GPUParticles3D or CPUParticles3D node to preview it here."));
	selection_label->set_clip_text(true);
	selection_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_toolbar->add_child(selection_label);
	Button *reset_view_button = memnew(Button);
	reset_view_button->set_text(TTR("Reset View"));
	reset_view_button->set_tooltip_text(TTR("Reset the particle preview camera."));
	reset_view_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_reset_view_pressed));
	preview_toolbar->add_child(reset_view_button);
	CheckButton *grid_toggle = memnew(CheckButton);
	grid_toggle->set_text(TTR("Grid"));
	grid_toggle->set_pressed(true);
	grid_toggle->set_tooltip_text(TTR("Show or hide the preview grid."));
	grid_toggle->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_grid_toggled));
	preview_toolbar->add_child(grid_toggle);
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
	preview_environment->set_bg_color(Color(0.055, 0.065, 0.085));
	preview_environment->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
	preview_environment->set_ambient_light_color(Color(0.42, 0.46, 0.54));
	preview_environment->set_ambient_light_energy(0.65);
	WorldEnvironment *world_environment = memnew(WorldEnvironment);
	world_environment->set_environment(preview_environment);
	preview_root->add_child(world_environment);

	DirectionalLight3D *key_light = memnew(DirectionalLight3D);
	key_light->set_rotation(Vector3(Math::deg_to_rad(-40.0), Math::deg_to_rad(-25.0), 0.0));
	key_light->set_param(Light3D::PARAM_ENERGY, 1.4);
	preview_root->add_child(key_light);
	preview_grid = create_particle_preview_grid();
	preview_root->add_child(preview_grid);

	preview_camera = memnew(Camera3D);
	preview_camera->set_perspective(55.0, 0.05, 4000.0);
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
	Button *restart_button = memnew(Button);
	this->restart_button = restart_button;
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
	time_label = memnew(Label);
	time_label->set_text("0.00 / 5.00 s");
	transport->add_child(time_label);
	Label *speed_label = memnew(Label);
	speed_label->set_text(TTR("Speed"));
	transport->add_child(speed_label);
	speed_spin = memnew(SpinBox);
	speed_spin->set_min(0.05);
	speed_spin->set_max(4.0);
	speed_spin->set_step(0.05);
	speed_spin->set_value(1.0);
	speed_spin->set_suffix("x");
	speed_spin->set_custom_minimum_size(Size2(88, 0) * EDSCALE);
	speed_spin->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_speed_changed));
	transport->add_child(speed_spin);
	CheckButton *loop_check = memnew(CheckButton);
	loop_check->set_text(TTR("Loop"));
	loop_check->set_pressed(preview_loop);
	loop_check->connect(SceneStringName(toggled), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_loop_toggled));
	transport->add_child(loop_check);

	TabContainer *settings_tabs = memnew(TabContainer);
	settings_tabs->set_custom_minimum_size(Size2(340, 0) * EDSCALE);
	settings_tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	preview_settings_split->add_child(settings_tabs);

	emitter_inspector = EditorInspector::create_default_inspector();
	emitter_inspector->set_name(TTR("Emitter"));
	emitter_inspector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	emitter_inspector->connect("property_edited", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_property_edited));
	settings_tabs->add_child(emitter_inspector);

	process_material_inspector = EditorInspector::create_default_inspector();
	process_material_inspector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	process_material_inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	process_material_inspector->connect("property_edited", callable_mp(this, &FPSStudioParticleEditorPlugin::_on_emitter_property_edited));
	process_material_page = memnew(VBoxContainer);
	process_material_page->set_name(TTR("Process Material"));
	process_material_page->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	process_material_page->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->add_child(process_material_page);
	particle_graph_button = memnew(Button);
	particle_graph_button->connect(SceneStringName(pressed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_particle_graph_button_pressed));
	process_material_page->add_child(particle_graph_button);
	process_material_page->add_child(process_material_inspector);
	process_material_page->hide();
	_update_particle_graph_controls();

	particle_graph_confirmation = memnew(ConfirmationDialog);
	particle_graph_confirmation->set_title(TTR("Create Particle Node Graph"));
	particle_graph_confirmation->set_ok_button_text(TTR("Replace Material"));
	particle_graph_confirmation->set_cancel_button_text(TTR("Cancel"));
	particle_graph_confirmation->connect(SceneStringName(confirmed), callable_mp(this, &FPSStudioParticleEditorPlugin::_create_particle_graph));
	add_child(particle_graph_confirmation);

	load_scene_dialog = memnew(EditorFileDialog);
	load_scene_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	load_scene_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	load_scene_dialog->set_title(TTR("Load Particle Scene"));
	load_scene_dialog->add_filter("*.tscn,*.scn", TTR("Godot Scenes"));
	add_child(load_scene_dialog);
	load_scene_dialog->connect("file_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_load_particle_scene));

	save_scene_dialog = memnew(EditorFileDialog);
	save_scene_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	save_scene_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	save_scene_dialog->set_title(TTR("Save Particle Scene"));
	save_scene_dialog->add_filter("*.tscn", TTR("Godot Scene"));
	add_child(save_scene_dialog);
	save_scene_dialog->connect("file_selected", callable_mp(this, &FPSStudioParticleEditorPlugin::_save_emitter_scene));

	preview_quad.instantiate();
	preview_quad->set_size(Vector2(0.12, 0.12));

	_update_emitter_tree();
}
