/**************************************************************************/
/*  particle_module_stack.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_module_stack.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/resources/curve.h"
#include "scene/gui/button.h"
#include "scene/gui/foldable_container.h"
#include "scene/gui/label.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"

static const char *MODULE_META = "_fps_particle_modules";

String FPSParticleModuleStack::get_module_name(int p_module) {
	switch (p_module) {
		case MODULE_EMITTER:
			return TTR("Emitter");
		case MODULE_SHAPE:
			return TTR("Spawn Shape");
		case MODULE_VELOCITY:
			return TTR("Initial Velocity");
		case MODULE_FORCES:
			return TTR("Forces");
		case MODULE_COLOR:
			return TTR("Color over Life");
		case MODULE_SIZE:
			return TTR("Size over Life");
		case MODULE_ROTATION:
			return TTR("Rotation");
		case MODULE_SUB_UV:
			return TTR("Sprite Sheet Animation");
		case MODULE_TURBULENCE:
			return TTR("Turbulence");
		case MODULE_COLLISION:
			return TTR("Collision");
		case MODULE_SUB_EMITTER:
			return TTR("Sub Emitter");
		case MODULE_RENDER:
			return TTR("Render");
		case MODULE_EVENT:
			return TTR("Event");
	}
	return String();
}

Ref<Mesh> FPSParticleModuleStack::get_draw_mesh(Node *p_emitter) {
	if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_emitter)) {
		return gpu->get_draw_passes() > 0 ? gpu->get_draw_pass_mesh(0) : Ref<Mesh>();
	}
	if (CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(p_emitter)) {
		return cpu->get_mesh();
	}
	return Ref<Mesh>();
}

Ref<Material> FPSParticleModuleStack::get_draw_material(Node *p_emitter) {
	GeometryInstance3D *geometry = Object::cast_to<GeometryInstance3D>(p_emitter);
	if (geometry && geometry->get_material_override().is_valid()) {
		return geometry->get_material_override();
	}
	Ref<Mesh> mesh = get_draw_mesh(p_emitter);
	if (mesh.is_valid() && mesh->get_surface_count() > 0) {
		return mesh->surface_get_material(0);
	}
	return Ref<Material>();
}

Node *FPSParticleModuleStack::get_edited_emitter() const {
	return Object::cast_to<Node>(ObjectDB::get_instance(emitter_id));
}

bool FPSParticleModuleStack::_is_gpu() const {
	return Object::cast_to<GPUParticles3D>(get_edited_emitter()) != nullptr;
}

bool FPSParticleModuleStack::_has_process_material() const {
	return _get_target(TARGET_PROCESS) != nullptr;
}

Object *FPSParticleModuleStack::_get_target(Target p_target) const {
	Node *emitter = get_edited_emitter();
	if (!emitter) {
		return nullptr;
	}
	switch (p_target) {
		case TARGET_NODE:
			return emitter;
		case TARGET_PROCESS: {
			if (GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(emitter)) {
				Ref<Material> material = gpu->get_process_material();
				return Object::cast_to<ParticleProcessMaterial>(material.ptr());
			}
			return Object::cast_to<CPUParticles3D>(emitter);
		}
		case TARGET_MATERIAL: {
			Ref<Material> material = get_draw_material(emitter);
			return Object::cast_to<BaseMaterial3D>(material.ptr());
		}
		case TARGET_MESH: {
			Ref<Mesh> mesh = get_draw_mesh(emitter);
			return mesh.ptr();
		}
	}
	return nullptr;
}

bool FPSParticleModuleStack::_is_module_optional(int p_module) const {
	return p_module == MODULE_ROTATION || p_module == MODULE_SUB_UV || p_module == MODULE_TURBULENCE || p_module == MODULE_COLLISION || p_module == MODULE_SUB_EMITTER;
}

static bool vector2_nonzero(const Variant &p_value) {
	if (p_value.get_type() == Variant::VECTOR2) {
		const Vector2 v = p_value;
		return !Math::is_zero_approx(v.x) || !Math::is_zero_approx(v.y);
	}
	return !Math::is_zero_approx(double(p_value));
}

bool FPSParticleModuleStack::_is_module_active(int p_module) const {
	if (!_is_module_optional(p_module)) {
		return true;
	}
	Node *emitter = get_edited_emitter();
	Object *process = _get_target(TARGET_PROCESS);
	if (!emitter || !process) {
		return false;
	}
	const Array manual = emitter->get_meta(MODULE_META, Array());
	if (manual.has(p_module)) {
		return true;
	}
	const bool gpu = _is_gpu();
	switch (p_module) {
		case MODULE_TURBULENCE:
			return gpu && bool(process->get("turbulence_enabled"));
		case MODULE_COLLISION:
			return gpu && int(process->get("collision_mode")) != 0;
		case MODULE_SUB_EMITTER:
			return gpu && int(process->get("sub_emitter_mode")) != 0;
		case MODULE_ROTATION:
			if (gpu) {
				return vector2_nonzero(process->get("angle")) || vector2_nonzero(process->get("angular_velocity"));
			}
			return vector2_nonzero(process->get("angle_min")) || vector2_nonzero(process->get("angle_max")) || vector2_nonzero(process->get("angular_velocity_min")) || vector2_nonzero(process->get("angular_velocity_max"));
		case MODULE_SUB_UV: {
			Object *material = _get_target(TARGET_MATERIAL);
			if (material && int(material->get("particles_anim_h_frames")) * int(material->get("particles_anim_v_frames")) > 1) {
				return true;
			}
			return gpu ? vector2_nonzero(process->get("anim_speed")) : vector2_nonzero(process->get("anim_speed_max"));
		}
	}
	return false;
}

void FPSParticleModuleStack::_set_module_active(int p_module, bool p_active) {
	Node *emitter = get_edited_emitter();
	Object *process = _get_target(TARGET_PROCESS);
	if (!emitter || !process) {
		return;
	}
	const bool gpu = _is_gpu();
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(vformat(p_active ? TTR("Add Particle Module: %s") : TTR("Remove Particle Module: %s"), get_module_name(p_module)));
	auto set_prop = [&](Object *p_object, const StringName &p_property, const Variant &p_value) {
		if (!p_object) {
			return;
		}
		undo_redo->add_do_property(p_object, p_property, p_value);
		undo_redo->add_undo_property(p_object, p_property, p_object->get(p_property));
	};
	Array manual = emitter->get_meta(MODULE_META, Array());
	Array new_manual = manual.duplicate();
	new_manual.erase(p_module);
	switch (p_module) {
		case MODULE_TURBULENCE:
			set_prop(process, "turbulence_enabled", p_active);
			break;
		case MODULE_COLLISION:
			set_prop(process, "collision_mode", p_active ? int(ParticleProcessMaterial::COLLISION_RIGID) : int(ParticleProcessMaterial::COLLISION_DISABLED));
			break;
		case MODULE_SUB_EMITTER:
			set_prop(process, "sub_emitter_mode", p_active ? int(ParticleProcessMaterial::SUB_EMITTER_AT_END) : int(ParticleProcessMaterial::SUB_EMITTER_DISABLED));
			break;
		case MODULE_ROTATION:
			if (p_active) {
				new_manual.push_back(p_module);
			} else if (gpu) {
				set_prop(process, "angle", Vector2());
				set_prop(process, "angular_velocity", Vector2());
			} else {
				set_prop(process, "angle_min", 0.0);
				set_prop(process, "angle_max", 0.0);
				set_prop(process, "angular_velocity_min", 0.0);
				set_prop(process, "angular_velocity_max", 0.0);
			}
			break;
		case MODULE_SUB_UV:
			if (p_active) {
				new_manual.push_back(p_module);
			} else {
				if (gpu) {
					set_prop(process, "anim_speed", Vector2());
					set_prop(process, "anim_offset", Vector2());
				} else {
					set_prop(process, "anim_speed_min", 0.0);
					set_prop(process, "anim_speed_max", 0.0);
				}
				Object *material = _get_target(TARGET_MATERIAL);
				set_prop(material, "particles_anim_h_frames", 1);
				set_prop(material, "particles_anim_v_frames", 1);
			}
			break;
	}
	undo_redo->add_do_method(emitter, "set_meta", MODULE_META, new_manual);
	undo_redo->add_undo_method(emitter, "set_meta", MODULE_META, manual);
	undo_redo->add_do_method(this, "_history_changed", StringName("modules"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("modules"));
	undo_redo->commit_action();
}

void FPSParticleModuleStack::_add_row(int p_module, Target p_target, const StringName &p_gpu_property, const StringName &p_cpu_property, const String &p_label, const StringName &p_condition, const Vector<int> &p_condition_values) {
	const bool gpu = _is_gpu();
	// The node and draw resources are shared between GPU and CPU emitters; only
	// process properties differ in naming.
	const StringName property = (p_target == TARGET_PROCESS && !gpu) ? p_cpu_property : p_gpu_property;
	if (property == StringName() || !module_contents[p_module]) {
		return;
	}
	if (p_target == TARGET_NODE && !gpu && p_cpu_property == StringName()) {
		return;
	}
	Object *object = _get_target(p_target);
	if (!object) {
		return;
	}
	List<PropertyInfo> properties;
	object->get_property_list(&properties);
	const PropertyInfo *info = nullptr;
	for (const PropertyInfo &candidate : properties) {
		if (candidate.name == property) {
			info = &candidate;
			break;
		}
	}
	if (!info) {
		return;
	}
	EditorProperty *editor = EditorInspector::instantiate_property_editor(object, info->type, info->name, info->hint, info->hint_string, info->usage | PROPERTY_USAGE_EDITOR);
	if (!editor) {
		return;
	}
	editor->set_object_and_property(object, info->name);
	editor->set_label(p_label);
	editor->set_tooltip_text(String(info->name));
	editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	if (info->type == Variant::OBJECT) {
		editor->set_custom_minimum_size(Size2(160, 0) * EDSCALE);
	}
	editor->update_property();
	editor->connect(SNAME("property_changed"), callable_mp(this, &FPSParticleModuleStack::_on_property_changed).bind(editor));
	module_contents[p_module]->add_child(editor);
	PropertyRow row;
	row.editor = editor;
	row.target = p_target;
	row.property = info->name;
	row.condition_property = p_condition;
	row.condition_values = p_condition_values;
	row.module = p_module;
	rows.push_back(row);
}

void FPSParticleModuleStack::_add_meta_row(int p_module, const String &p_key, const String &p_label, Variant::Type p_type, PropertyHint p_hint, const String &p_hint_string, const Variant &p_default) {
	Node *node = get_edited_emitter();
	if (!node || !module_contents[p_module]) {
		return;
	}
	// Metadata is edited through its "metadata/<key>" property path.
	const String path = "metadata/" + p_key;
	if (!node->has_meta(p_key)) {
		node->set_meta(p_key, p_default);
	}
	EditorProperty *editor = EditorInspector::instantiate_property_editor(node, p_type, path, p_hint, p_hint_string, PROPERTY_USAGE_DEFAULT);
	if (!editor) {
		return;
	}
	editor->set_object_and_property(node, path);
	editor->set_label(p_label);
	editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	if (p_type == Variant::OBJECT) {
		editor->set_custom_minimum_size(Size2(160, 0) * EDSCALE);
	}
	editor->update_property();
	editor->connect(SNAME("property_changed"), callable_mp(this, &FPSParticleModuleStack::_on_property_changed).bind(editor));
	module_contents[p_module]->add_child(editor);
	PropertyRow row;
	row.editor = editor;
	row.target = TARGET_NODE;
	row.property = path;
	row.module = p_module;
	rows.push_back(row);
}

void FPSParticleModuleStack::_build_event() {
	Node *node = get_edited_emitter();
	const ParticleEffect3D::EventKind kind = ParticleEffect3D::get_event_kind(node);
	String title;
	String subtitle;
	switch (kind) {
		case ParticleEffect3D::EVENT_LIGHT:
			title = TTR("Light Flash");
			subtitle = TTR("Light event: intensity follows the curve over its duration.");
			break;
		case ParticleEffect3D::EVENT_SOUND:
			title = TTR("Sound");
			subtitle = TTR("Sound event: plays once at its start time.");
			break;
		case ParticleEffect3D::EVENT_CAMERA_SHAKE:
			title = TTR("Camera Shake");
			subtitle = TTR("Shakes the active camera when it fires.");
			break;
		case ParticleEffect3D::EVENT_DECAL:
			title = TTR("Decal");
			subtitle = TTR("Decal event: appears at its start and fades out.");
			break;
		default:
			break;
	}
	header_subtitle->set_text(subtitle);
	add_module_button->set_disabled(true);
	FoldableContainer *card = memnew(FoldableContainer);
	card->set_title(title);
	cards->add_child(card);
	VBoxContainer *content = memnew(VBoxContainer);
	content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	card->add_child(content);
	module_cards[MODULE_EVENT] = card;
	module_contents[MODULE_EVENT] = content;

	_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_DELAY, TTR("Start Time"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,30,0.01,or_greater,suffix:s", 0.0);
	switch (kind) {
		case ParticleEffect3D::EVENT_LIGHT: {
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_DURATION, TTR("Duration (0 = always on)"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s", 0.3);
			_add_row(MODULE_EVENT, TARGET_NODE, "light_color", "light_color", TTR("Color"));
			_add_row(MODULE_EVENT, TARGET_NODE, "light_energy", "light_energy", TTR("Peak Energy"));
			_add_row(MODULE_EVENT, TARGET_NODE, "omni_range", "omni_range", TTR("Range"));
			_add_row(MODULE_EVENT, TARGET_NODE, "spot_range", "spot_range", TTR("Range"));
			_add_row(MODULE_EVENT, TARGET_NODE, "spot_angle", "spot_angle", TTR("Cone Angle"));
			_add_row(MODULE_EVENT, TARGET_NODE, "shadow_enabled", "shadow_enabled", TTR("Shadows"));
			Ref<Curve> curve;
			curve.instantiate();
			curve->add_point(Vector2(0.0, 0.0));
			curve->add_point(Vector2(0.1, 1.0));
			curve->add_point(Vector2(1.0, 0.0));
			if (!node->has_meta(ParticleEffect3D::META_CURVE)) {
				node->set_meta(ParticleEffect3D::META_CURVE, curve);
			}
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_CURVE, TTR("Intensity over Time"), Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Curve", curve);
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_FLICKER, TTR("Flicker"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01", 0.0);
		} break;
		case ParticleEffect3D::EVENT_SOUND:
			_add_row(MODULE_EVENT, TARGET_NODE, "stream", "stream", TTR("Sound"));
			_add_row(MODULE_EVENT, TARGET_NODE, "volume_db", "volume_db", TTR("Volume"));
			_add_row(MODULE_EVENT, TARGET_NODE, "pitch_scale", "pitch_scale", TTR("Pitch"));
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_PITCH_RANDOM, TTR("Pitch Random"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01", 0.1);
			_add_row(MODULE_EVENT, TARGET_NODE, "unit_size", "unit_size", TTR("Loudness Distance"));
			_add_row(MODULE_EVENT, TARGET_NODE, "max_distance", "max_distance", TTR("Max Distance"));
			_add_row(MODULE_EVENT, TARGET_NODE, "bus", "bus", TTR("Bus"));
			break;
		case ParticleEffect3D::EVENT_CAMERA_SHAKE:
			_add_row(MODULE_EVENT, TARGET_NODE, "strength", "strength", TTR("Strength"));
			_add_row(MODULE_EVENT, TARGET_NODE, "frequency", "frequency", TTR("Frequency"));
			_add_row(MODULE_EVENT, TARGET_NODE, "duration", "duration", TTR("Duration"));
			_add_row(MODULE_EVENT, TARGET_NODE, "radius", "radius", TTR("Radius"));
			break;
		case ParticleEffect3D::EVENT_DECAL:
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_DURATION, TTR("Duration (0 = stays)"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,120,0.1,or_greater,suffix:s", 6.0);
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_FADE, TTR("Fade Out Part"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,1,0.01", 0.5);
			_add_row(MODULE_EVENT, TARGET_NODE, "texture_albedo", "texture_albedo", TTR("Texture"));
			_add_row(MODULE_EVENT, TARGET_NODE, "size", "size", TTR("Size"));
			_add_row(MODULE_EVENT, TARGET_NODE, "modulate", "modulate", TTR("Tint"));
			_add_row(MODULE_EVENT, TARGET_NODE, "albedo_mix", "albedo_mix", TTR("Albedo Mix"));
			_add_row(MODULE_EVENT, TARGET_NODE, "upper_fade", "upper_fade", TTR("Upper Fade"));
			_add_row(MODULE_EVENT, TARGET_NODE, "lower_fade", "lower_fade", TTR("Lower Fade"));
			break;
		default:
			break;
	}
}

void FPSParticleModuleStack::_clear() {
	rows.clear();
	for (int i = 0; i < MODULE_MAX; i++) {
		if (module_custom_controls[i] && module_custom_controls[i]->get_parent()) {
			// Plugin-owned controls survive rebuilds.
			module_custom_controls[i]->get_parent()->remove_child(module_custom_controls[i]);
		}
		module_cards[i] = nullptr;
		module_contents[i] = nullptr;
	}
	texture_preview = nullptr;
	texture_label = nullptr;
	create_material_button = nullptr;
	generate_texture_button = nullptr;
	custom_shader_note = nullptr;
	for (int i = cards->get_child_count() - 1; i >= 0; i--) {
		Node *child = cards->get_child(i);
		cards->remove_child(child);
		child->queue_free();
	}
}

void FPSParticleModuleStack::_build() {
	_clear();
	Node *emitter = get_edited_emitter();
	if (!emitter) {
		header_title->set_text(TTR("No emitter selected"));
		header_subtitle->set_text(TTR("Select an emitter on the left or create a new effect."));
		add_module_button->set_disabled(true);
		return;
	}
	header_title->set_text(emitter->get_name());
	if (!ParticleEffect3D::is_emitter(emitter)) {
		_build_event();
		return;
	}
	const bool gpu = _is_gpu();
	const bool has_process = _has_process_material();
	header_subtitle->set_text(gpu ? (has_process ? TTR("GPU emitter") : TTR("GPU emitter with custom particle shader")) : TTR("CPU emitter"));

	for (int module = 0; module < MODULE_EVENT; module++) {
		const bool process_module = module != MODULE_EMITTER && module != MODULE_RENDER;
		if (process_module && !has_process) {
			continue;
		}
		if (!gpu && (module == MODULE_TURBULENCE || module == MODULE_COLLISION || module == MODULE_SUB_EMITTER)) {
			continue;
		}
		FoldableContainer *card = memnew(FoldableContainer);
		card->set_title(get_module_name(module));
		card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		cards->add_child(card);
		if (_is_module_optional(module)) {
			Button *remove = memnew(Button);
			remove->set_flat(true);
			remove->set_text(U"✕");
			remove->set_tooltip_text(TTR("Remove this module from the emitter."));
			remove->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_remove_module_pressed).bind(module));
			card->add_title_bar_control(remove);
		}
		VBoxContainer *content = memnew(VBoxContainer);
		content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		card->add_child(content);
		module_cards[module] = card;
		module_contents[module] = content;
		if (module_custom_controls[module]) {
			content->add_child(module_custom_controls[module]);
		}
	}
	if (!has_process && gpu) {
		custom_shader_note = memnew(Label);
		custom_shader_note->set_text(TTR("This emitter uses a custom particle shader. Edit its logic in the Particle Logic graph below; spawn, motion and color modules are unavailable."));
		custom_shader_note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		cards->add_child(custom_shader_note);
		cards->move_child(custom_shader_note, 1);
	}

	const Vector<int> shape_sphere = { ParticleProcessMaterial::EMISSION_SHAPE_SPHERE, ParticleProcessMaterial::EMISSION_SHAPE_SPHERE_SURFACE };
	const Vector<int> shape_box = { ParticleProcessMaterial::EMISSION_SHAPE_BOX };
	const Vector<int> shape_ring = { ParticleProcessMaterial::EMISSION_SHAPE_RING };
	const Vector<int> enabled = { 1 };
	auto add_range = [&](int p_module, const StringName &p_base, const String &p_label) {
		if (gpu) {
			_add_row(p_module, TARGET_PROCESS, p_base, StringName(), p_label);
		} else {
			_add_row(p_module, TARGET_PROCESS, StringName(), String(p_base) + "_min", p_label + " " + TTR("Min"));
			_add_row(p_module, TARGET_PROCESS, StringName(), String(p_base) + "_max", p_label + " " + TTR("Max"));
		}
	};

	// Emitter: how many particles, how long and how they are released.
	_add_row(MODULE_EMITTER, TARGET_NODE, "emitting", "emitting", TTR("Emitting"));
	_add_meta_row(MODULE_EMITTER, ParticleEffect3D::META_DELAY, TTR("Start Delay"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,30,0.01,or_greater,suffix:s", 0.0);
	_add_meta_row(MODULE_EMITTER, ParticleEffect3D::META_DURATION, TTR("Emission Length (0 = auto)"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s", 0.0);
	_add_row(MODULE_EMITTER, TARGET_NODE, "amount", "amount", TTR("Max Particles"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "lifetime", "lifetime", TTR("Lifetime"));
	_add_row(MODULE_EMITTER, TARGET_PROCESS, "lifetime_randomness", "lifetime_randomness", TTR("Lifetime Random"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "one_shot", "one_shot", TTR("One Shot"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "explosiveness", "explosiveness", TTR("Burst"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "randomness", "randomness", TTR("Randomness"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "preprocess", "preprocess", TTR("Prewarm"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "speed_scale", "speed_scale", TTR("Speed Scale"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "local_coords", "local_coords", TTR("Local Space"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "fixed_fps", "fixed_fps", TTR("Fixed FPS"));

	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_shape", "emission_shape", TTR("Shape"));
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_sphere_radius", "emission_sphere_radius", TTR("Radius"), "emission_shape", shape_sphere);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_box_extents", "emission_box_extents", TTR("Box Extents"), "emission_shape", shape_box);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_radius", "emission_ring_radius", TTR("Ring Radius"), "emission_shape", shape_ring);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_inner_radius", "emission_ring_inner_radius", TTR("Inner Radius"), "emission_shape", shape_ring);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_height", "emission_ring_height", TTR("Ring Height"), "emission_shape", shape_ring);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_axis", "emission_ring_axis", TTR("Ring Axis"), "emission_shape", shape_ring);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_shape_offset", StringName(), TTR("Offset"));
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_shape_scale", StringName(), TTR("Scale"));

	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "direction", "direction", TTR("Direction"));
	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "spread", "spread", TTR("Spread"));
	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "flatness", "flatness", TTR("Flatness"));
	add_range(MODULE_VELOCITY, "initial_velocity", TTR("Speed"));
	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "inherit_velocity_ratio", StringName(), TTR("Inherit Velocity"));

	_add_row(MODULE_FORCES, TARGET_PROCESS, "gravity", "gravity", TTR("Gravity"));
	add_range(MODULE_FORCES, "linear_accel", TTR("Linear Accel"));
	add_range(MODULE_FORCES, "radial_accel", TTR("Radial Accel"));
	add_range(MODULE_FORCES, "tangential_accel", TTR("Tangential Accel"));
	add_range(MODULE_FORCES, "damping", TTR("Drag"));

	_add_row(MODULE_COLOR, TARGET_PROCESS, "color", "color", TTR("Base Color"));
	_add_row(MODULE_COLOR, TARGET_PROCESS, "color_ramp", "color_ramp", TTR("Color over Life"));
	_add_row(MODULE_COLOR, TARGET_PROCESS, "color_initial_ramp", "color_initial_ramp", TTR("Random Color"));
	_add_row(MODULE_COLOR, TARGET_PROCESS, "emission_curve", StringName(), TTR("Glow over Life"));
	add_range(MODULE_COLOR, "hue_variation", TTR("Hue Variation"));

	add_range(MODULE_SIZE, gpu ? StringName("scale") : StringName("scale_amount"), TTR("Size"));

	add_range(MODULE_ROTATION, "angle", TTR("Start Angle"));
	add_range(MODULE_ROTATION, "angular_velocity", TTR("Spin Speed"));

	add_range(MODULE_SUB_UV, "anim_speed", TTR("Frames per Life"));
	add_range(MODULE_SUB_UV, "anim_offset", TTR("Start Frame"));
	_add_row(MODULE_SUB_UV, TARGET_MATERIAL, "particles_anim_h_frames", "particles_anim_h_frames", TTR("Columns"));
	_add_row(MODULE_SUB_UV, TARGET_MATERIAL, "particles_anim_v_frames", "particles_anim_v_frames", TTR("Rows"));
	_add_row(MODULE_SUB_UV, TARGET_MATERIAL, "particles_anim_loop", "particles_anim_loop", TTR("Loop"));

	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_noise_strength", StringName(), TTR("Strength"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_noise_scale", StringName(), TTR("Noise Scale"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_noise_speed", StringName(), TTR("Noise Speed"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_noise_speed_random", StringName(), TTR("Speed Random"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_influence", StringName(), TTR("Influence"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_initial_displacement", StringName(), TTR("Displacement"));

	_add_row(MODULE_COLLISION, TARGET_PROCESS, "collision_mode", StringName(), TTR("Mode"));
	_add_row(MODULE_COLLISION, TARGET_PROCESS, "collision_friction", StringName(), TTR("Friction"));
	_add_row(MODULE_COLLISION, TARGET_PROCESS, "collision_bounce", StringName(), TTR("Bounce"));
	_add_row(MODULE_COLLISION, TARGET_PROCESS, "collision_use_scale", StringName(), TTR("Use Scale"));
	_add_row(MODULE_COLLISION, TARGET_NODE, "collision_base_size", StringName(), TTR("Particle Size"));

	_add_row(MODULE_SUB_EMITTER, TARGET_NODE, "sub_emitter", StringName(), TTR("Emitter"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_mode", StringName(), TTR("Trigger"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_frequency", StringName(), TTR("Frequency"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_end", StringName(), TTR("Amount at End"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_collision", StringName(), TTR("Amount at Hit"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_start", StringName(), TTR("Amount at Start"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_keep_velocity", StringName(), TTR("Keep Velocity"));

	// Render: texture first, then how it is blended and oriented.
	{
		VBoxContainer *render = module_contents[MODULE_RENDER];
		HBoxContainer *texture_row = memnew(HBoxContainer);
		render->add_child(texture_row);
		texture_preview = memnew(TextureRect);
		texture_preview->set_custom_minimum_size(Size2(64, 64) * EDSCALE);
		texture_preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		texture_preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		texture_row->add_child(texture_preview);
		VBoxContainer *texture_actions = memnew(VBoxContainer);
		texture_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		texture_row->add_child(texture_actions);
		texture_label = memnew(Label);
		texture_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		texture_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		texture_actions->add_child(texture_label);
		generate_texture_button = memnew(Button);
		generate_texture_button->set_text(TTR("Generate Texture..."));
		generate_texture_button->set_tooltip_text(TTR("Open the VFX Texture Generator. The result is saved next to the effect and assigned to this emitter."));
		generate_texture_button->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_generate_texture_pressed));
		texture_actions->add_child(generate_texture_button);
		create_material_button = memnew(Button);
		create_material_button->set_text(TTR("Create Particle Material"));
		create_material_button->set_tooltip_text(TTR("Give this emitter an unshaded, billboarded particle material that uses the particle color."));
		create_material_button->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_create_material_pressed));
		texture_actions->add_child(create_material_button);
	}
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "albedo_texture", "albedo_texture", TTR("Texture"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "albedo_color", "albedo_color", TTR("Tint"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "blend_mode", "blend_mode", TTR("Blend"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "shading_mode", "shading_mode", TTR("Shading"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "billboard_mode", "billboard_mode", TTR("Facing"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "vertex_color_use_as_albedo", "vertex_color_use_as_albedo", TTR("Use Particle Color"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "proximity_fade_enabled", "proximity_fade_enabled", TTR("Soft Particles"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "proximity_fade_distance", "proximity_fade_distance", TTR("Soft Distance"), "proximity_fade_enabled", enabled);
	_add_row(MODULE_RENDER, TARGET_MESH, "size", "size", TTR("Quad Size"));
	_add_row(MODULE_RENDER, TARGET_NODE, "draw_order", "draw_order", TTR("Sorting"));
	_add_row(MODULE_RENDER, TARGET_NODE, "transform_align", StringName(), TTR("Align"));

	_update_render_card();
	_update_visibility();
	_update_add_menu();
}

void FPSParticleModuleStack::_update_visibility() {
	for (int module = 0; module < MODULE_EVENT; module++) {
		if (module_cards[module]) {
			module_cards[module]->set_visible(_is_module_active(module));
		}
	}
	for (const PropertyRow &row : rows) {
		bool visible = true;
		if (row.condition_property != StringName()) {
			Object *object = _get_target(row.target);
			visible = object && row.condition_values.has(int(object->get(row.condition_property)));
		}
		row.editor->set_visible(visible);
	}
}

void FPSParticleModuleStack::_update_add_menu() {
	PopupMenu *popup = add_module_button->get_popup();
	popup->clear();
	Node *emitter = get_edited_emitter();
	const bool gpu = _is_gpu();
	int available = 0;
	if (emitter && ParticleEffect3D::is_emitter(emitter) && _has_process_material()) {
		for (int module = 0; module < MODULE_EVENT; module++) {
			if (!_is_module_optional(module) || _is_module_active(module)) {
				continue;
			}
			if (!gpu && (module == MODULE_TURBULENCE || module == MODULE_COLLISION || module == MODULE_SUB_EMITTER)) {
				continue;
			}
			popup->add_item(get_module_name(module), module);
			available++;
		}
	}
	add_module_button->set_disabled(available == 0);
}

void FPSParticleModuleStack::_update_render_card() {
	if (!texture_preview) {
		return;
	}
	BaseMaterial3D *material = Object::cast_to<BaseMaterial3D>(_get_target(TARGET_MATERIAL));
	create_material_button->set_visible(material == nullptr);
	generate_texture_button->set_disabled(get_edited_emitter() == nullptr);
	Ref<Texture2D> texture = material ? material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO) : Ref<Texture2D>();
	texture_preview->set_texture(texture);
	if (!material) {
		texture_label->set_text(TTR("No particle material"));
	} else if (texture.is_null()) {
		texture_label->set_text(TTR("No texture (soft color quad)"));
	} else {
		texture_label->set_text(texture->get_path().is_empty() ? TTR("Embedded texture") : texture->get_path().get_file());
		texture_label->set_tooltip_text(texture->get_path());
	}
}

void FPSParticleModuleStack::_commit_property(Object *p_object, const StringName &p_property, const Variant &p_value, bool p_merge) {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(vformat(TTR("Set Particle %s"), p_property), p_merge ? UndoRedo::MERGE_ENDS : UndoRedo::MERGE_DISABLE);
	undo_redo->add_do_property(p_object, p_property, p_value);
	undo_redo->add_undo_property(p_object, p_property, p_object->get(p_property));
	undo_redo->add_do_method(this, "_history_changed", p_property);
	undo_redo->add_undo_method(this, "_history_changed", p_property);
	undo_redo->commit_action();
}

void FPSParticleModuleStack::_on_property_changed(const StringName &p_property, const Variant &p_value, const StringName &p_field, bool p_changing, EditorProperty *p_editor) {
	if (updating || !p_editor) {
		return;
	}
	Object *object = p_editor->get_edited_object();
	if (!object) {
		return;
	}
	updating = true;
	_commit_property(object, p_property, p_value, true);
	updating = false;
}

void FPSParticleModuleStack::_on_add_module_selected(int p_id) {
	_set_module_active(p_id, true);
	if (p_id >= 0 && p_id < MODULE_MAX && module_cards[p_id]) {
		module_cards[p_id]->set_folded(false);
	}
}

void FPSParticleModuleStack::_on_remove_module_pressed(int p_module) {
	_set_module_active(p_module, false);
}

void FPSParticleModuleStack::_on_create_material_pressed() {
	Node *emitter = get_edited_emitter();
	if (!emitter) {
		return;
	}
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
	material->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
	material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	// Duplicate the mesh so emitters that share a quad don't share the material.
	Ref<Mesh> mesh = get_draw_mesh(emitter);
	Ref<Mesh> new_mesh;
	if (mesh.is_valid()) {
		new_mesh = mesh->duplicate();
	} else {
		Ref<QuadMesh> quad;
		quad.instantiate();
		quad->set_size(Vector2(0.5, 0.5));
		new_mesh = quad;
	}
	if (PrimitiveMesh *primitive = Object::cast_to<PrimitiveMesh>(new_mesh.ptr())) {
		primitive->set_material(material);
	} else {
		for (int i = 0; i < new_mesh->get_surface_count(); i++) {
			new_mesh->surface_set_material(i, material);
		}
	}
	const StringName mesh_property = _is_gpu() ? StringName("draw_pass_1") : StringName("mesh");
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Create Particle Material"));
	undo_redo->add_do_property(emitter, mesh_property, new_mesh);
	undo_redo->add_undo_property(emitter, mesh_property, emitter->get(mesh_property));
	undo_redo->add_do_method(this, "_history_changed", StringName("material"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("material"));
	undo_redo->commit_action();
}

void FPSParticleModuleStack::set_module_custom_control(int p_module, Control *p_control) {
	ERR_FAIL_INDEX(p_module, MODULE_MAX);
	module_custom_controls[p_module] = p_control;
	if (module_contents[p_module] && p_control && !p_control->get_parent()) {
		module_contents[p_module]->add_child(p_control);
		module_contents[p_module]->move_child(p_control, 0);
	}
}

void FPSParticleModuleStack::edit(Node *p_emitter) {
	emitter_id = p_emitter ? p_emitter->get_instance_id() : ObjectID();
	_build();
}

void FPSParticleModuleStack::refresh_values() {
	Node *emitter = get_edited_emitter();
	if (!emitter) {
		return;
	}
	// A different material/mesh/process resource means different rows; rebuild.
	for (const PropertyRow &row : rows) {
		if (row.editor->get_edited_object() != _get_target(row.target)) {
			_build();
			return;
		}
	}
	for (const PropertyRow &row : rows) {
		row.editor->update_property();
	}
	header_title->set_text(emitter->get_name());
	_update_render_card();
	_update_visibility();
	_update_add_menu();
}

void FPSParticleModuleStack::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_history_changed", "property"), &FPSParticleModuleStack::_history_changed);
	ADD_SIGNAL(MethodInfo("property_edited", PropertyInfo(Variant::STRING, "property")));
	ADD_SIGNAL(MethodInfo("generate_texture_requested"));
}

void FPSParticleModuleStack::_on_generate_texture_pressed() {
	emit_signal(SNAME("generate_texture_requested"));
}

void FPSParticleModuleStack::_history_changed(const StringName &p_property) {
	refresh_values();
	emit_signal(SNAME("property_edited"), String(p_property));
}

FPSParticleModuleStack::FPSParticleModuleStack() {
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	HBoxContainer *header = memnew(HBoxContainer);
	add_child(header);
	VBoxContainer *titles = memnew(VBoxContainer);
	titles->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header->add_child(titles);
	header_title = memnew(Label);
	header_title->set_theme_type_variation("HeaderMedium");
	header_title->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	titles->add_child(header_title);
	header_subtitle = memnew(Label);
	header_subtitle->set_modulate(Color(1, 1, 1, 0.6));
	header_subtitle->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	titles->add_child(header_subtitle);
	add_module_button = memnew(MenuButton);
	add_module_button->set_text(TTR("+ Module"));
	add_module_button->set_flat(false);
	add_module_button->set_tooltip_text(TTR("Add an optional module (rotation, sprite sheet animation, turbulence, collision, sub emitter)."));
	add_module_button->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSParticleModuleStack::_on_add_module_selected));
	header->add_child(add_module_button);
	cards = memnew(VBoxContainer);
	cards->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(cards);
	_build();
}
