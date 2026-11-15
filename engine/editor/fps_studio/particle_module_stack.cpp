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
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "particle_presets.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/gradient_texture.h"
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
		case MODULE_TRAILS:
			return TTR("Trails");
		case MODULE_ATTRACTORS:
			return TTR("Attractors");
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
	return p_module == MODULE_ROTATION || p_module == MODULE_SUB_UV || p_module == MODULE_TURBULENCE || p_module == MODULE_COLLISION || p_module == MODULE_SUB_EMITTER || p_module == MODULE_TRAILS || p_module == MODULE_ATTRACTORS;
}

static bool is_gpu_only_module(int p_module) {
	return p_module == FPSParticleModuleStack::MODULE_TURBULENCE || p_module == FPSParticleModuleStack::MODULE_COLLISION || p_module == FPSParticleModuleStack::MODULE_SUB_EMITTER || p_module == FPSParticleModuleStack::MODULE_TRAILS || p_module == FPSParticleModuleStack::MODULE_ATTRACTORS;
}

static bool has_attractor_child(const Node *p_node) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (Object::cast_to<GPUParticlesAttractor3D>(p_node->get_child(i))) {
			return true;
		}
	}
	return false;
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
		case MODULE_TRAILS:
			return gpu && bool(emitter->get("trail_enabled"));
		case MODULE_ATTRACTORS:
			return gpu && has_attractor_child(emitter);
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
	undo_redo->create_action_for_history(vformat(p_active ? TTR("Add Particle Module: %s") : TTR("Remove Particle Module: %s"), get_module_name(p_module)), EditorUndoRedoManager::GLOBAL_HISTORY, UndoRedo::MERGE_DISABLE);
	undo_redo->force_fixed_history();
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
			if (p_active && gpu && double(emitter->get("collision_base_size")) < 0.05) {
				// The default 0.01 m makes particles sink into surfaces; start
				// with a size close to a typical smoke or debris particle.
				set_prop(emitter, "collision_base_size", 0.25);
			}
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
		case MODULE_TRAILS: {
			set_prop(emitter, "trail_enabled", p_active);
			if (p_active && double(emitter->get("trail_lifetime")) <= 0.0) {
				set_prop(emitter, "trail_lifetime", 0.3);
			}
			// Trails need a trail mesh and a material in trail mode.
			Ref<Material> material = get_draw_material(emitter);
			if (material.is_valid()) {
				set_prop(material.ptr(), "use_particle_trails", p_active);
			}
			Ref<Mesh> old_mesh = get_draw_mesh(emitter);
			Ref<PrimitiveMesh> new_mesh;
			if (p_active) {
				Ref<RibbonTrailMesh> ribbon;
				ribbon.instantiate();
				ribbon->set_size(0.15);
				ribbon->set_sections(4);
				new_mesh = ribbon;
			} else {
				Ref<QuadMesh> quad;
				quad.instantiate();
				quad->set_size(Vector2(0.5, 0.5));
				new_mesh = quad;
			}
			if (material.is_valid()) {
				new_mesh->set_material(material);
			}
			set_prop(emitter, "draw_pass_1", new_mesh);
		} break;
		case MODULE_ATTRACTORS:
			if (p_active) {
				// Adding the module creates a first attractor next to the emitter.
				GPUParticlesAttractorSphere3D *attractor = memnew(GPUParticlesAttractorSphere3D);
				attractor->set_name("Attractor");
				attractor->set_radius(1.5);
				attractor->set_strength(4.0);
				attractor->set_position(Vector3(0, 1.5, 0));
				undo_redo->add_do_method(emitter, "add_child", attractor, true);
				undo_redo->add_do_method(attractor, "set_owner", emitter->get_owner() ? emitter->get_owner() : emitter);
				undo_redo->add_do_reference(attractor);
				undo_redo->add_undo_method(emitter, "remove_child", attractor);
				set_prop(process, "attractor_interaction_enabled", true);
			} else {
				for (int i = 0; i < emitter->get_child_count(); i++) {
					Node *child = emitter->get_child(i);
					if (Object::cast_to<GPUParticlesAttractor3D>(child)) {
						undo_redo->add_do_method(emitter, "remove_child", child);
						undo_redo->add_undo_method(emitter, "add_child", child, true);
						undo_redo->add_undo_method(child, "set_owner", child->get_owner());
						undo_redo->add_undo_reference(child);
					}
				}
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
	const StringName property = ((p_target == TARGET_PROCESS || p_target == TARGET_NODE) && !gpu && ParticleEffect3D::is_emitter(get_edited_emitter())) ? p_cpu_property : p_gpu_property;
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
		case ParticleEffect3D::EVENT_DAMAGE:
			title = TTR("Damage");
			subtitle = TTR("Hurts everything with take_damage() in range: full damage inside the inner radius, fading to zero at the outer radius. Targets fully behind cover take nothing, partly covered targets take the visible share.");
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
			if (AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(get_edited_emitter())) {
				if (player->get_stream().is_null()) {
					Label *hint = memnew(Label);
					hint->set_text(TTR("No sound yet: pick an audio file below, or drag one from the FileSystem onto this track in the emitter list."));
					hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
					hint->set_modulate(Color(1.0, 0.85, 0.5));
					module_contents[MODULE_EVENT]->add_child(hint);
				}
			}
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
			_add_row(MODULE_EVENT, TARGET_NODE, "duration", "duration", TTR("Duration"));
			_add_row(MODULE_EVENT, TARGET_NODE, "frequency", "frequency", TTR("Frequency"));
			_add_row(MODULE_EVENT, TARGET_NODE, "max_rotation_degrees", "max_rotation_degrees", TTR("Max Rotation (Pitch, Yaw, Roll)"));
			_add_row(MODULE_EVENT, TARGET_NODE, "max_offset", "max_offset", TTR("Max Offset"));
			_add_row(MODULE_EVENT, TARGET_NODE, "inner_radius", "inner_radius", TTR("Full Strength Radius"));
			_add_row(MODULE_EVENT, TARGET_NODE, "radius", "radius", TTR("Fade Out Radius"));
			break;
		case ParticleEffect3D::EVENT_DAMAGE:
			_add_row(MODULE_EVENT, TARGET_NODE, "damage", "damage", TTR("Damage (Center)"));
			_add_row(MODULE_EVENT, TARGET_NODE, "inner_radius", "inner_radius", TTR("Full Damage Radius"));
			_add_row(MODULE_EVENT, TARGET_NODE, "radius", "radius", TTR("Fade Out Radius"));
			_add_row(MODULE_EVENT, TARGET_NODE, "use_cover", "use_cover", TTR("Cover Blocks Damage"));
			_add_row(MODULE_EVENT, TARGET_NODE, "impulse", "impulse", TTR("Physics Push"));
			_add_row(MODULE_EVENT, TARGET_NODE, "collision_mask", "collision_mask", TTR("Target Layers"));
			_add_row(MODULE_EVENT, TARGET_NODE, "obstacle_mask", "obstacle_mask", TTR("Cover Layers"));
			_add_row(MODULE_EVENT, TARGET_NODE, "damage_method", "damage_method", TTR("Damage Method"));
			_add_row(MODULE_EVENT, TARGET_NODE, "target_group", "target_group", TTR("Only Group (optional)"));
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
		if (!gpu && is_gpu_only_module(module)) {
			continue;
		}
		FoldableContainer *card = memnew(FoldableContainer);
		card->set_title(get_module_name(module));
		card->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		cards->add_child(card);
		{
			// Module menu: presets, copy / paste, reset.
			MenuButton *menu = memnew(MenuButton);
			menu->set_text(U"⋮");
			menu->set_flat(true);
			menu->set_tooltip_text(TTR("Module presets, copy, paste and reset."));
			menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &FPSParticleModuleStack::_on_module_menu_id).bind(module));
			menu->get_popup()->connect("about_to_popup", callable_mp(this, &FPSParticleModuleStack::_on_module_menu_id).bind(-1, module));
			card->add_title_bar_control(menu);
		}
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
		custom_shader_note->set_text(TTR("This emitter uses a custom particle shader. Edit its logic in the Particle Logic tab (node graph or shader code); spawn, motion and color modules are unavailable."));
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
	add_range(MODULE_VELOCITY, "radial_velocity", TTR("Radial Speed"));
	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "radial_velocity_curve", StringName(), TTR("Radial Speed over Life"));
	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "velocity_limit_curve", StringName(), TTR("Speed Limit over Life"));

	_add_row(MODULE_FORCES, TARGET_PROCESS, "gravity", "gravity", TTR("Gravity"));
	add_range(MODULE_FORCES, "linear_accel", TTR("Linear Accel"));
	add_range(MODULE_FORCES, "radial_accel", TTR("Radial Accel"));
	add_range(MODULE_FORCES, "tangential_accel", TTR("Tangential Accel"));
	add_range(MODULE_FORCES, "damping", TTR("Drag"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "linear_accel_curve", "linear_accel_curve", TTR("Linear Accel over Life"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "radial_accel_curve", "radial_accel_curve", TTR("Radial Accel over Life"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "tangential_accel_curve", "tangential_accel_curve", TTR("Tangential over Life"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "damping_curve", "damping_curve", TTR("Drag over Life"));

	_add_row(MODULE_COLOR, TARGET_PROCESS, "color", "color", TTR("Base Color"));
	_add_row(MODULE_COLOR, TARGET_PROCESS, "color_ramp", "color_ramp", TTR("Color over Life"));
	_add_row(MODULE_COLOR, TARGET_PROCESS, "color_initial_ramp", "color_initial_ramp", TTR("Random Color"));
	_add_row(MODULE_COLOR, TARGET_PROCESS, "emission_curve", StringName(), TTR("Glow over Life"));
	add_range(MODULE_COLOR, "hue_variation", TTR("Hue Variation"));
	_add_row(MODULE_COLOR, TARGET_PROCESS, "hue_variation_curve", "hue_variation_curve", TTR("Hue over Life"));

	add_range(MODULE_SIZE, gpu ? StringName("scale") : StringName("scale_amount"), TTR("Size"));
	_add_row(MODULE_SIZE, TARGET_PROCESS, StringName(), "scale_amount_curve", TTR("Size over Life"));
	add_range(MODULE_SIZE, "scale_over_velocity", TTR("Size by Speed Range"));
	_add_row(MODULE_SIZE, TARGET_PROCESS, "scale_over_velocity_curve", StringName(), TTR("Size by Speed"));

	add_range(MODULE_ROTATION, "angle", TTR("Start Angle"));
	add_range(MODULE_ROTATION, "angular_velocity", TTR("Spin Speed"));
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "angle_curve", "angle_curve", TTR("Angle over Life"));
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "angular_velocity_curve", "angular_velocity_curve", TTR("Spin over Life"));

	add_range(MODULE_SUB_UV, "anim_speed", TTR("Frames per Life"));
	add_range(MODULE_SUB_UV, "anim_offset", TTR("Start Frame"));
	_add_row(MODULE_SUB_UV, TARGET_PROCESS, "anim_speed_curve", "anim_speed_curve", TTR("Speed over Life"));
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
	{
		VBoxContainer *collision = module_contents[MODULE_COLLISION];
		Label *collision_note = memnew(Label);
		collision_note->set_text(TTR("GPU particles do not collide with physics bodies, only with particle collision nodes. Add Collision to Level bakes the level's walls, floors and props into one collision field (players and moving bodies are left out). Test bouncing here with View > Collision Test Walls."));
		collision_note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		collision_note->set_modulate(Color(1, 1, 1, 0.7));
		collision->add_child(collision_note);
		Button *level_collision_button = memnew(Button);
		level_collision_button->set_text(TTR("Add Collision to Level"));
		level_collision_button->set_tooltip_text(TTR("Create (or re-bake) a GPUParticlesCollisionSDF3D covering the open Level scene, so particles bounce off its walls and floors. Re-bake after changing the level geometry."));
		level_collision_button->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_level_collision_pressed));
		collision->add_child(level_collision_button);
	}

	_add_row(MODULE_SUB_EMITTER, TARGET_NODE, "sub_emitter", StringName(), TTR("Emitter"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_mode", StringName(), TTR("Trigger"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_frequency", StringName(), TTR("Frequency"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_end", StringName(), TTR("Amount at End"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_collision", StringName(), TTR("Amount at Hit"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_start", StringName(), TTR("Amount at Start"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_keep_velocity", StringName(), TTR("Keep Velocity"));

	_add_row(MODULE_TRAILS, TARGET_NODE, "trail_lifetime", StringName(), TTR("Trail Length"));
	_add_row(MODULE_TRAILS, TARGET_MESH, "size", StringName(), TTR("Trail Width"));
	_add_row(MODULE_TRAILS, TARGET_MESH, "sections", StringName(), TTR("Segments"));
	_add_row(MODULE_TRAILS, TARGET_MESH, "curve", StringName(), TTR("Width over Trail"));

	if (module_contents[MODULE_ATTRACTORS]) {
		Label *info = memnew(Label);
		info->set_text(TTR("Attractors pull or push particles. Move them in the Level or preview hierarchy; each one is a GPUParticlesAttractor node."));
		info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		info->set_modulate(Color(1, 1, 1, 0.7));
		module_contents[MODULE_ATTRACTORS]->add_child(info);
		Button *add_attractor = memnew(Button);
		add_attractor->set_text(TTR("Add Attractor"));
		add_attractor->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_add_attractor_pressed));
		module_contents[MODULE_ATTRACTORS]->add_child(add_attractor);
	}
	_add_row(MODULE_ATTRACTORS, TARGET_PROCESS, "attractor_interaction_enabled", StringName(), TTR("React to Attractors"));

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
	_add_row(MODULE_RENDER, TARGET_NODE, "draw_pass_1", "mesh", TTR("Mesh"));
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
			if (!gpu && is_gpu_only_module(module)) {
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
	undo_redo->create_action_for_history(vformat(TTR("Set Particle %s"), p_property), EditorUndoRedoManager::GLOBAL_HISTORY, p_merge ? UndoRedo::MERGE_ENDS : UndoRedo::MERGE_DISABLE);
	undo_redo->force_fixed_history();
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
	undo_redo->create_action_for_history(TTR("Create Particle Material"), EditorUndoRedoManager::GLOBAL_HISTORY, UndoRedo::MERGE_DISABLE);
	undo_redo->force_fixed_history();
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
	ADD_SIGNAL(MethodInfo("level_collision_requested"));
}

/* Module presets ---------------------------------------------------------- */

namespace {

struct PresetValue {
	FPSParticleModuleStack::Target target;
	StringName gpu;
	StringName cpu;
	Variant value;
};

struct ModulePreset {
	String name;
	Vector<PresetValue> values;
};

PresetValue pv(FPSParticleModuleStack::Target p_target, const StringName &p_gpu, const StringName &p_cpu, const Variant &p_value) {
	return { p_target, p_gpu, p_cpu, p_value };
}

// A min/max range: GPU uses the Vector2 range property, CPU the _min/_max pair.
void add_range_values(Vector<PresetValue> &r_values, const String &p_gpu_base, const String &p_cpu_base, float p_min, float p_max) {
	r_values.push_back(pv(FPSParticleModuleStack::TARGET_PROCESS, p_gpu_base, StringName(), Vector2(p_min, p_max)));
	r_values.push_back(pv(FPSParticleModuleStack::TARGET_PROCESS, StringName(), p_cpu_base + "_min", p_min));
	r_values.push_back(pv(FPSParticleModuleStack::TARGET_PROCESS, StringName(), p_cpu_base + "_max", p_max));
}

Vector<ModulePreset> get_module_presets(int p_module) {
	using T = FPSParticleModuleStack::Target;
	Vector<ModulePreset> presets;
	auto add = [&](const String &p_name) -> ModulePreset & {
		presets.push_back(ModulePreset{ p_name, Vector<PresetValue>() });
		return presets.write[presets.size() - 1];
	};
	auto ramp = [](std::initializer_list<float> p_offsets, std::initializer_list<Color> p_colors) {
		Vector<float> offsets;
		Vector<Color> colors;
		for (float o : p_offsets) {
			offsets.push_back(o);
		}
		for (const Color &c : p_colors) {
			colors.push_back(c);
		}
		return Variant(FPSParticlePresets::make_color_ramp(offsets, colors));
	};
	auto curve = [](std::initializer_list<Vector2> p_points) {
		Vector<Vector2> points;
		for (const Vector2 &p : p_points) {
			points.push_back(p);
		}
		return Variant(FPSParticlePresets::make_curve_texture(points));
	};
	switch (p_module) {
		case FPSParticleModuleStack::MODULE_EMITTER: {
			ModulePreset &continuous = add(TTR("Continuous Stream"));
			continuous.values.push_back(pv(T::TARGET_NODE, "one_shot", "one_shot", false));
			continuous.values.push_back(pv(T::TARGET_NODE, "explosiveness", "explosiveness", 0.0));
			ModulePreset &burst = add(TTR("Single Burst"));
			burst.values.push_back(pv(T::TARGET_NODE, "one_shot", "one_shot", true));
			burst.values.push_back(pv(T::TARGET_NODE, "explosiveness", "explosiveness", 1.0));
			ModulePreset &pulses = add(TTR("Repeating Pulses"));
			pulses.values.push_back(pv(T::TARGET_NODE, "one_shot", "one_shot", false));
			pulses.values.push_back(pv(T::TARGET_NODE, "explosiveness", "explosiveness", 0.85));
		} break;
		case FPSParticleModuleStack::MODULE_SHAPE: {
			add(TTR("Point")).values.push_back(pv(T::TARGET_PROCESS, "emission_shape", "emission_shape", 0));
			ModulePreset &small = add(TTR("Small Sphere"));
			small.values.push_back(pv(T::TARGET_PROCESS, "emission_shape", "emission_shape", 1));
			small.values.push_back(pv(T::TARGET_PROCESS, "emission_sphere_radius", "emission_sphere_radius", 0.2));
			ModulePreset &large = add(TTR("Large Sphere"));
			large.values.push_back(pv(T::TARGET_PROCESS, "emission_shape", "emission_shape", 1));
			large.values.push_back(pv(T::TARGET_PROCESS, "emission_sphere_radius", "emission_sphere_radius", 1.0));
			ModulePreset &shell = add(TTR("Sphere Shell"));
			shell.values.push_back(pv(T::TARGET_PROCESS, "emission_shape", "emission_shape", 2));
			shell.values.push_back(pv(T::TARGET_PROCESS, "emission_sphere_radius", "emission_sphere_radius", 0.5));
			ModulePreset &ground = add(TTR("Ground Area"));
			ground.values.push_back(pv(T::TARGET_PROCESS, "emission_shape", "emission_shape", 3));
			ground.values.push_back(pv(T::TARGET_PROCESS, "emission_box_extents", "emission_box_extents", Vector3(1.5, 0.05, 1.5)));
			ModulePreset &ring = add(TTR("Ring"));
			ring.values.push_back(pv(T::TARGET_PROCESS, "emission_shape", "emission_shape", 6));
			ring.values.push_back(pv(T::TARGET_PROCESS, "emission_ring_radius", "emission_ring_radius", 1.0));
			ring.values.push_back(pv(T::TARGET_PROCESS, "emission_ring_inner_radius", "emission_ring_inner_radius", 0.85));
			ring.values.push_back(pv(T::TARGET_PROCESS, "emission_ring_height", "emission_ring_height", 0.05));
			ring.values.push_back(pv(T::TARGET_PROCESS, "emission_ring_axis", "emission_ring_axis", Vector3(0, 1, 0)));
		} break;
		case FPSParticleModuleStack::MODULE_VELOCITY: {
			ModulePreset &fountain = add(TTR("Fountain"));
			fountain.values.push_back(pv(T::TARGET_PROCESS, "direction", "direction", Vector3(0, 1, 0)));
			fountain.values.push_back(pv(T::TARGET_PROCESS, "spread", "spread", 15.0));
			add_range_values(fountain.values, "initial_velocity", "initial_velocity", 4.0, 6.0);
			ModulePreset &radial = add(TTR("Explosion (Radial)"));
			radial.values.push_back(pv(T::TARGET_PROCESS, "spread", "spread", 180.0));
			add_range_values(radial.values, "initial_velocity", "initial_velocity", 4.0, 10.0);
			ModulePreset &rise = add(TTR("Gentle Rise"));
			rise.values.push_back(pv(T::TARGET_PROCESS, "direction", "direction", Vector3(0, 1, 0)));
			rise.values.push_back(pv(T::TARGET_PROCESS, "spread", "spread", 10.0));
			add_range_values(rise.values, "initial_velocity", "initial_velocity", 0.3, 0.8);
			ModulePreset &cone = add(TTR("Forward Cone (-Z)"));
			cone.values.push_back(pv(T::TARGET_PROCESS, "direction", "direction", Vector3(0, 0, -1)));
			cone.values.push_back(pv(T::TARGET_PROCESS, "spread", "spread", 20.0));
			add_range_values(cone.values, "initial_velocity", "initial_velocity", 5.0, 8.0);
		} break;
		case FPSParticleModuleStack::MODULE_FORCES: {
			ModulePreset &earth = add(TTR("Earth Gravity"));
			earth.values.push_back(pv(T::TARGET_PROCESS, "gravity", "gravity", Vector3(0, -9.8, 0)));
			add_range_values(earth.values, "damping", "damping", 0.0, 0.0);
			ModulePreset &light = add(TTR("Light Gravity"));
			light.values.push_back(pv(T::TARGET_PROCESS, "gravity", "gravity", Vector3(0, -2.0, 0)));
			ModulePreset &buoyant = add(TTR("Buoyant Smoke"));
			buoyant.values.push_back(pv(T::TARGET_PROCESS, "gravity", "gravity", Vector3(0, 0.4, 0)));
			add_range_values(buoyant.values, "damping", "damping", 0.4, 0.8);
			ModulePreset &zero = add(TTR("Zero Gravity"));
			zero.values.push_back(pv(T::TARGET_PROCESS, "gravity", "gravity", Vector3()));
			add_range_values(zero.values, "damping", "damping", 0.0, 0.0);
			ModulePreset &drag = add(TTR("Heavy Drag"));
			add_range_values(drag.values, "damping", "damping", 2.5, 3.5);
		} break;
		case FPSParticleModuleStack::MODULE_COLOR: {
			ModulePreset &fire = add(TTR("Fire"));
			fire.values.push_back(pv(T::TARGET_PROCESS, "color", "color", Color(1, 1, 1, 1)));
			fire.values.push_back(pv(T::TARGET_PROCESS, "color_ramp", "color_ramp", ramp({ 0.0f, 0.25f, 0.7f, 1.0f }, { Color(1.0, 0.95, 0.7, 1.0), Color(1.0, 0.6, 0.15, 1.0), Color(0.7, 0.15, 0.03, 0.7), Color(0.2, 0.05, 0.02, 0.0) })));
			ModulePreset &smoke = add(TTR("Smoke"));
			smoke.values.push_back(pv(T::TARGET_PROCESS, "color", "color", Color(1, 1, 1, 1)));
			smoke.values.push_back(pv(T::TARGET_PROCESS, "color_ramp", "color_ramp", ramp({ 0.0f, 0.15f, 1.0f }, { Color(0.35, 0.33, 0.32, 0.0), Color(0.4, 0.38, 0.36, 0.75), Color(0.6, 0.6, 0.62, 0.0) })));
			ModulePreset &sparks = add(TTR("Sparks"));
			sparks.values.push_back(pv(T::TARGET_PROCESS, "color", "color", Color(1, 1, 1, 1)));
			sparks.values.push_back(pv(T::TARGET_PROCESS, "color_ramp", "color_ramp", ramp({ 0.0f, 0.5f, 1.0f }, { Color(1.0, 0.97, 0.8, 1.0), Color(1.0, 0.6, 0.15, 1.0), Color(0.8, 0.2, 0.05, 0.0) })));
			ModulePreset &magic = add(TTR("Magic"));
			magic.values.push_back(pv(T::TARGET_PROCESS, "color", "color", Color(1, 1, 1, 1)));
			magic.values.push_back(pv(T::TARGET_PROCESS, "color_ramp", "color_ramp", ramp({ 0.0f, 0.5f, 1.0f }, { Color(0.6, 0.95, 1.0, 1.0), Color(0.45, 0.35, 1.0, 0.9), Color(0.6, 0.1, 0.8, 0.0) })));
			ModulePreset &fade = add(TTR("White Fade Out"));
			fade.values.push_back(pv(T::TARGET_PROCESS, "color", "color", Color(1, 1, 1, 1)));
			fade.values.push_back(pv(T::TARGET_PROCESS, "color_ramp", "color_ramp", ramp({ 0.0f, 1.0f }, { Color(1, 1, 1, 1), Color(1, 1, 1, 0) })));
		} break;
		case FPSParticleModuleStack::MODULE_SIZE: {
			add(TTR("Grow")).values.push_back(pv(T::TARGET_PROCESS, "scale_curve", "scale_amount_curve", curve({ Vector2(0, 0.3), Vector2(1, 1) })));
			add(TTR("Shrink")).values.push_back(pv(T::TARGET_PROCESS, "scale_curve", "scale_amount_curve", curve({ Vector2(0, 1), Vector2(1, 0) })));
			add(TTR("Pop In and Out")).values.push_back(pv(T::TARGET_PROCESS, "scale_curve", "scale_amount_curve", curve({ Vector2(0, 0), Vector2(0.2, 1), Vector2(1, 0) })));
			add(TTR("Constant")).values.push_back(pv(T::TARGET_PROCESS, "scale_curve", "scale_amount_curve", Variant()));
		} break;
		case FPSParticleModuleStack::MODULE_ROTATION: {
			ModulePreset &random = add(TTR("Random Static"));
			add_range_values(random.values, "angle", "angle", 0.0, 360.0);
			add_range_values(random.values, "angular_velocity", "angular_velocity", 0.0, 0.0);
			ModulePreset &slow = add(TTR("Slow Spin"));
			add_range_values(slow.values, "angle", "angle", 0.0, 360.0);
			add_range_values(slow.values, "angular_velocity", "angular_velocity", -30.0, 30.0);
			ModulePreset &fast = add(TTR("Fast Spin"));
			add_range_values(fast.values, "angle", "angle", 0.0, 360.0);
			add_range_values(fast.values, "angular_velocity", "angular_velocity", -360.0, 360.0);
		} break;
		case FPSParticleModuleStack::MODULE_TURBULENCE: {
			ModulePreset &gentle = add(TTR("Gentle Drift"));
			gentle.values.push_back(pv(T::TARGET_PROCESS, "turbulence_noise_strength", StringName(), 0.5));
			gentle.values.push_back(pv(T::TARGET_PROCESS, "turbulence_noise_scale", StringName(), 3.0));
			ModulePreset &chaos = add(TTR("Chaotic"));
			chaos.values.push_back(pv(T::TARGET_PROCESS, "turbulence_noise_strength", StringName(), 2.0));
			chaos.values.push_back(pv(T::TARGET_PROCESS, "turbulence_noise_scale", StringName(), 1.5));
			chaos.values.push_back(pv(T::TARGET_PROCESS, "turbulence_noise_speed", StringName(), Vector3(0.5, 0.5, 0.5)));
		} break;
		case FPSParticleModuleStack::MODULE_RENDER: {
			ModulePreset &glow = add(TTR("Additive Glow"));
			glow.values.push_back(pv(T::TARGET_MATERIAL, "blend_mode", "blend_mode", int(BaseMaterial3D::BLEND_MODE_ADD)));
			glow.values.push_back(pv(T::TARGET_MATERIAL, "shading_mode", "shading_mode", int(BaseMaterial3D::SHADING_MODE_UNSHADED)));
			glow.values.push_back(pv(T::TARGET_MATERIAL, "proximity_fade_enabled", "proximity_fade_enabled", false));
			ModulePreset &smoke = add(TTR("Soft Alpha (Smoke)"));
			smoke.values.push_back(pv(T::TARGET_MATERIAL, "blend_mode", "blend_mode", int(BaseMaterial3D::BLEND_MODE_MIX)));
			smoke.values.push_back(pv(T::TARGET_MATERIAL, "proximity_fade_enabled", "proximity_fade_enabled", true));
			smoke.values.push_back(pv(T::TARGET_MATERIAL, "proximity_fade_distance", "proximity_fade_distance", 0.35));
			ModulePreset &lit = add(TTR("Lit by Scene"));
			lit.values.push_back(pv(T::TARGET_MATERIAL, "blend_mode", "blend_mode", int(BaseMaterial3D::BLEND_MODE_MIX)));
			lit.values.push_back(pv(T::TARGET_MATERIAL, "shading_mode", "shading_mode", int(BaseMaterial3D::SHADING_MODE_PER_PIXEL)));
		} break;
		default:
			break;
	}
	return presets;
}

Dictionary module_clipboard;
int module_clipboard_module = -1;

} // namespace

void FPSParticleModuleStack::_on_module_menu_id(int p_id, int p_module) {
	if (p_module < 0 || p_module >= MODULE_MAX || !module_cards[p_module]) {
		return;
	}
	if (p_id == -1) {
		// about_to_popup: rebuild the menu for this module.
		MenuButton *menu = nullptr;
		for (int i = 0; i < module_cards[p_module]->get_child_count(true); i++) {
			menu = Object::cast_to<MenuButton>(module_cards[p_module]->get_child(i, true));
			if (menu) {
				break;
			}
		}
		if (!menu) {
			return;
		}
		PopupMenu *popup = menu->get_popup();
		popup->clear();
		const Vector<ModulePreset> presets = get_module_presets(p_module);
		if (!presets.is_empty()) {
			popup->add_separator(TTR("Presets"));
			for (int i = 0; i < presets.size(); i++) {
				popup->add_item(presets[i].name, i);
			}
			popup->add_separator();
		}
		popup->add_item(TTR("Copy Module"), 1000);
		popup->add_item(TTR("Paste Module"), 1001);
		popup->set_item_disabled(-1, module_clipboard_module != p_module);
		popup->add_item(TTR("Reset to Defaults"), 1002);
		return;
	}
	if (p_id == 1000) {
		_copy_module(p_module);
	} else if (p_id == 1001) {
		_paste_module(p_module);
	} else if (p_id == 1002) {
		_reset_module(p_module);
	} else {
		_apply_module_preset(p_module, p_id);
	}
}

void FPSParticleModuleStack::_apply_module_preset(int p_module, int p_preset) {
	const Vector<ModulePreset> presets = get_module_presets(p_module);
	ERR_FAIL_INDEX(p_preset, presets.size());
	const bool gpu = _is_gpu();
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(vformat(TTR("Apply %s Preset: %s"), get_module_name(p_module), presets[p_preset].name), EditorUndoRedoManager::GLOBAL_HISTORY, UndoRedo::MERGE_DISABLE);
	undo_redo->force_fixed_history();
	for (const PresetValue &value : presets[p_preset].values) {
		const StringName property = ((value.target == TARGET_PROCESS || value.target == TARGET_NODE) && !gpu) ? value.cpu : value.gpu;
		Object *object = _get_target(value.target);
		if (property == StringName() || !object) {
			continue;
		}
		bool valid = false;
		const Variant current = object->get(property, &valid);
		if (!valid) {
			continue;
		}
		undo_redo->add_do_property(object, property, value.value);
		undo_redo->add_undo_property(object, property, current);
	}
	undo_redo->add_do_method(this, "_history_changed", StringName("preset"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("preset"));
	undo_redo->commit_action();
}

void FPSParticleModuleStack::_copy_module(int p_module) {
	module_clipboard.clear();
	for (const PropertyRow &row : rows) {
		if (row.module != p_module) {
			continue;
		}
		Object *object = row.editor->get_edited_object();
		if (object) {
			Variant value = object->get(row.property);
			// Copy resources so pasting doesn't link two emitters together.
			Ref<Resource> resource = value;
			if (resource.is_valid() && resource->get_path().is_empty()) {
				value = resource->duplicate(true);
			}
			module_clipboard[String(row.property)] = value;
		}
	}
	module_clipboard_module = p_module;
}

void FPSParticleModuleStack::_paste_module(int p_module) {
	if (module_clipboard_module != p_module || module_clipboard.is_empty()) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(vformat(TTR("Paste Module: %s"), get_module_name(p_module)), EditorUndoRedoManager::GLOBAL_HISTORY, UndoRedo::MERGE_DISABLE);
	undo_redo->force_fixed_history();
	for (const PropertyRow &row : rows) {
		if (row.module != p_module || !module_clipboard.has(String(row.property))) {
			continue;
		}
		Object *object = row.editor->get_edited_object();
		if (!object) {
			continue;
		}
		Variant value = module_clipboard[String(row.property)];
		Ref<Resource> resource = value;
		if (resource.is_valid() && resource->get_path().is_empty()) {
			value = resource->duplicate(true);
		}
		undo_redo->add_do_property(object, row.property, value);
		undo_redo->add_undo_property(object, row.property, object->get(row.property));
	}
	undo_redo->add_do_method(this, "_history_changed", StringName("paste"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("paste"));
	undo_redo->commit_action();
}

void FPSParticleModuleStack::_reset_module(int p_module) {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(vformat(TTR("Reset Module: %s"), get_module_name(p_module)), EditorUndoRedoManager::GLOBAL_HISTORY, UndoRedo::MERGE_DISABLE);
	undo_redo->force_fixed_history();
	for (const PropertyRow &row : rows) {
		if (row.module != p_module) {
			continue;
		}
		Object *object = row.editor->get_edited_object();
		if (!object) {
			continue;
		}
		Variant default_value;
		if (String(row.property).begins_with("metadata/")) {
			default_value = 0.0;
		} else {
			bool valid = false;
			default_value = ClassDB::class_get_default_property_value(object->get_class_name(), row.property, &valid);
			if (!valid) {
				continue;
			}
		}
		undo_redo->add_do_property(object, row.property, default_value);
		undo_redo->add_undo_property(object, row.property, object->get(row.property));
	}
	undo_redo->add_do_method(this, "_history_changed", StringName("reset"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("reset"));
	undo_redo->commit_action();
}

void FPSParticleModuleStack::_on_add_attractor_pressed() {
	Node *emitter = get_edited_emitter();
	if (!emitter) {
		return;
	}
	GPUParticlesAttractorSphere3D *attractor = memnew(GPUParticlesAttractorSphere3D);
	attractor->set_name("Attractor");
	attractor->set_radius(1.5);
	attractor->set_strength(4.0);
	attractor->set_position(Vector3(0, 1.5, 0));
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(TTR("Add Particle Attractor"), EditorUndoRedoManager::GLOBAL_HISTORY, UndoRedo::MERGE_DISABLE);
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(emitter, "add_child", attractor, true);
	undo_redo->add_do_method(attractor, "set_owner", emitter->get_owner() ? emitter->get_owner() : emitter);
	undo_redo->add_do_reference(attractor);
	undo_redo->add_undo_method(emitter, "remove_child", attractor);
	undo_redo->add_do_method(this, "_history_changed", StringName("attractor"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("attractor"));
	undo_redo->commit_action();
}

void FPSParticleModuleStack::_on_generate_texture_pressed() {
	emit_signal(SNAME("generate_texture_requested"));
}

void FPSParticleModuleStack::_on_level_collision_pressed() {
	emit_signal(SNAME("level_collision_requested"));
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
