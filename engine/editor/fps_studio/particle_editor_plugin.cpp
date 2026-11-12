/**************************************************************************/
/*  particle_editor_plugin.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_editor_plugin.h"

#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"

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

void FPSStudioParticleEditorPlugin::_update_emitter_tree() {
	if (!emitter_tree) {
		return;
	}
	emitter_tree->clear();
	TreeItem *root_item = emitter_tree->create_item();
	Node *scene_root = EditorNode::get_singleton()->get_edited_scene();
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
	if (scene_root && emitters.is_empty()) {
		root_item->set_text(0, TTR("No 3D particle emitters in this scene"));
	}
}

void FPSStudioParticleEditorPlugin::_set_edited_emitter(Node *p_node) {
	if (p_node && !Object::cast_to<GPUParticles3D>(p_node) && !Object::cast_to<CPUParticles3D>(p_node)) {
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
	if (GPUParticles3D *gpu_emitter = Object::cast_to<GPUParticles3D>(p_node)) {
		process_material = gpu_emitter->get_process_material();
	}
	if (process_material_inspector) {
		process_material_inspector->edit(process_material.ptr());
		process_material_inspector->set_visible(process_material.is_valid());
	}

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
	_update_emitter_tree();
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
		preview_quad.instantiate();
		preview_quad->set_size(Vector2(0.12, 0.12));
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
	Node *scene_root = EditorNode::get_singleton()->get_edited_scene();
	if (!item || !scene_root) {
		return;
	}
	_set_edited_emitter(scene_root->get_node_or_null(item->get_metadata(0)));
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

void FPSStudioParticleEditorPlugin::_on_speed_changed(double p_value) {
	preview_speed = p_value;
	if (preview_playing && preview_emitter) {
		preview_emitter->call("set_speed_scale", preview_speed);
	}
}

void FPSStudioParticleEditorPlugin::_update_time_label() {
	if (time_label) {
		time_label->set_text(vformat("%s / %s s", String::num(preview_time, 2), String::num(preview_duration, 2)));
	}
}

void FPSStudioParticleEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS && preview_playing && preview_emitter && screen->is_visible_in_tree()) {
		preview_time += get_process_delta_time() * preview_speed;
		if (preview_time >= preview_duration) {
			preview_time = preview_duration;
			_set_preview_playing(false);
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
	Node *scene_root = EditorNode::get_singleton()->get_edited_scene();
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
	Label *emitters_title = memnew(Label);
	emitters_title->set_text(TTR("Emitters"));
	emitters_title->add_theme_font_size_override(SceneStringName(font_size), 16 * EDSCALE);
	emitters_panel->add_child(emitters_title);
	emitter_tree = memnew(Tree);
	emitter_tree->set_columns(1);
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

	selection_label = memnew(Label);
	selection_label->set_text(TTR("Select a GPUParticles3D or CPUParticles3D node to preview it here."));
	selection_label->set_clip_text(true);
	preview_panel->add_child(selection_label);

	SubViewportContainer *preview_container = memnew(SubViewportContainer);
	preview_container->set_stretch(true);
	preview_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preview_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
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

	Camera3D *camera = memnew(Camera3D);
	camera->set_position(Vector3(0.0, 2.0, 6.0));
	camera->look_at(Vector3(0.0, 0.5, 0.0));
	camera->set_current(true);
	preview_root->add_child(camera);

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
	speed_spin->set_suffix("×");
	speed_spin->set_custom_minimum_size(Size2(88, 0) * EDSCALE);
	speed_spin->connect(SceneStringName(value_changed), callable_mp(this, &FPSStudioParticleEditorPlugin::_on_speed_changed));
	transport->add_child(speed_spin);

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
	process_material_inspector->set_name(TTR("Process Material"));
	process_material_inspector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	process_material_inspector->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_tabs->add_child(process_material_inspector);
	process_material_inspector->hide();

	preview_quad.instantiate();
	preview_quad->set_size(Vector2(0.12, 0.12));

	_update_emitter_tree();
}
