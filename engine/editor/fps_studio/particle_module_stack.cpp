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
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"
#include "scene/resources/shader.h"

static const char *MODULE_META = "_fps_particle_modules";
// Draw mesh the emitter had before the Trails module replaced it.
static const char *PRE_TRAIL_MESH_META = "_pre_trail_mesh";

// Copy / Paste Module clipboard, shared by all stacks.
static Dictionary module_clipboard;
static int module_clipboard_module = -1;

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
		case TARGET_COUNT:
			break;
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
				return vector2_nonzero(process->get("angle")) || vector2_nonzero(process->get("angular_velocity")) || bool(process->get("use_rotation_3d")) || bool(process->get("use_rotation_velocity_3d"));
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
			if (p_active && gpu) {
				// collision_base_size is the collision radius: too small and
				// particles sink into surfaces, too big and they stop in the
				// air. Match it to the drawn particle.
				set_prop(emitter, "collision_base_size", ParticleEffect3D::get_suggested_collision_size(Object::cast_to<GPUParticles3D>(emitter)));
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
				set_prop(process, "use_rotation_3d", false);
				set_prop(process, "use_rotation_velocity_3d", false);
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
			const bool had_saved = emitter->has_meta(PRE_TRAIL_MESH_META);
			const Variant saved = had_saved ? emitter->get_meta(PRE_TRAIL_MESH_META) : Variant(); // get_meta() with a nil default reports a missing key as an error.
			Ref<Mesh> new_mesh;
			if (p_active) {
				Ref<RibbonTrailMesh> ribbon;
				ribbon.instantiate();
				ribbon->set_size(0.15);
				ribbon->set_sections(4);
				if (material.is_valid()) {
					ribbon->set_material(material);
				}
				new_mesh = ribbon;
				// Remember the original mesh so removing Trails brings it back.
				if (old_mesh.is_valid() && !Object::cast_to<RibbonTrailMesh>(old_mesh.ptr()) && !Object::cast_to<TubeTrailMesh>(old_mesh.ptr())) {
					undo_redo->add_do_method(emitter, "set_meta", PRE_TRAIL_MESH_META, old_mesh);
				}
			} else {
				Ref<Mesh> restored = saved;
				if (restored.is_valid()) {
					new_mesh = restored;
					if (material.is_valid() && Object::cast_to<PrimitiveMesh>(restored.ptr())) {
						// The material may have changed while trails were on.
						set_prop(restored.ptr(), "material", material);
					}
				} else {
					Ref<QuadMesh> quad;
					quad.instantiate();
					quad->set_size(Vector2(0.5, 0.5));
					if (material.is_valid()) {
						quad->set_material(material);
					}
					new_mesh = quad;
				}
				undo_redo->add_do_method(emitter, "remove_meta", PRE_TRAIL_MESH_META);
			}
			if (had_saved) {
				undo_redo->add_undo_method(emitter, "set_meta", PRE_TRAIL_MESH_META, saved);
			} else {
				undo_redo->add_undo_method(emitter, "remove_meta", PRE_TRAIL_MESH_META);
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
	_add_object_row(p_module, _get_target(p_target), property, p_label, p_target, false, p_condition, p_condition_values);
}

void FPSParticleModuleStack::_add_object_row(int p_module, Object *p_object, const StringName &p_property, const String &p_label, Target p_target, bool p_custom, const StringName &p_condition, const Vector<int> &p_condition_values) {
	Object *object = p_object;
	if (!object || !module_contents[p_module]) {
		return;
	}
	List<PropertyInfo> properties;
	object->get_property_list(&properties);
	const PropertyInfo *info = nullptr;
	for (const PropertyInfo &candidate : properties) {
		if (candidate.name == p_property) {
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
	if (p_custom) {
		row.custom_object = object->get_instance_id();
	}
	rows.push_back(row);
}

void FPSParticleModuleStack::_add_meta_row(int p_module, const String &p_key, const String &p_label, Variant::Type p_type, PropertyHint p_hint, const String &p_hint_string) {
	Node *node = get_edited_emitter();
	if (!node || !module_contents[p_module]) {
		return;
	}
	// Metadata is edited through its "metadata/<key>" property path.
	// A missing key shows its runtime default and is written only when the
	// user edits it; building the card must not change the node.
	const String path = "metadata/" + p_key;
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
		case ParticleEffect3D::EVENT_BEAM:
			title = TTR("Beam");
			subtitle = TTR("A camera-facing strip from this track to its end point: electric arc, laser, tracer. Fades over its duration; the game aims it with ParticleEffect3D.set_target_position().");
			break;
		case ParticleEffect3D::EVENT_SPLATTER:
			title = TTR("Splatter Decals");
			subtitle = TTR("Leaves decals where flying drops land (blood behind a hit): rays in a cone, a decal on what each one hits.");
			break;
		case ParticleEffect3D::EVENT_SCREEN_FLASH:
			title = TTR("Screen Flash");
			subtitle = TTR("Flashes the whole view with a color, weaker with the distance from the camera.");
			break;
		case ParticleEffect3D::EVENT_SPAWN:
			title = TTR("Spawn Effect");
			subtitle = TTR("Plays another effect or creates a scene (gibs, burning debris, a second explosion) at this track's position when it fires.");
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

	_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_DELAY, TTR("Start Time"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,30,0.01,or_greater,suffix:s");
	switch (kind) {
		case ParticleEffect3D::EVENT_LIGHT: {
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_DURATION, TTR("Duration (0 = always on)"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s");
			_add_row(MODULE_EVENT, TARGET_NODE, "light_color", "light_color", TTR("Color"));
			_add_row(MODULE_EVENT, TARGET_NODE, "light_energy", "light_energy", TTR("Peak Energy"));
			_add_row(MODULE_EVENT, TARGET_NODE, "omni_range", "omni_range", TTR("Range"));
			_add_row(MODULE_EVENT, TARGET_NODE, "spot_range", "spot_range", TTR("Range"));
			_add_row(MODULE_EVENT, TARGET_NODE, "spot_angle", "spot_angle", TTR("Cone Angle"));
			_add_row(MODULE_EVENT, TARGET_NODE, "shadow_enabled", "shadow_enabled", TTR("Shadows"));
			// No curve = the built-in flash (quick attack, smooth decay).
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_CURVE, TTR("Intensity over Time"), Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Curve");
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_FLICKER, TTR("Flicker"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
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
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_PITCH_RANDOM, TTR("Pitch Random"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
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
		case ParticleEffect3D::EVENT_BEAM: {
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_DURATION, TTR("Duration (0 = always on)"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s");
			_add_row(MODULE_EVENT, TARGET_NODE, "position", "position", TTR("Start"));
			_add_row(MODULE_EVENT, TARGET_NODE, "end_position", "end_position", TTR("End (from Start)"));
			_add_row(MODULE_EVENT, TARGET_NODE, "width", "width", TTR("Width"));
			_add_row(MODULE_EVENT, TARGET_NODE, "taper", "taper", TTR("Taper to End"));
			_add_row(MODULE_EVENT, TARGET_NODE, "color", "color", TTR("Color"));
			_add_row(MODULE_EVENT, TARGET_NODE, "brightness", "brightness", TTR("Brightness"));
			_add_row(MODULE_EVENT, TARGET_NODE, "additive", "additive", TTR("Additive (Glow)"));
			_add_row(MODULE_EVENT, TARGET_NODE, "texture", "texture", TTR("Texture (along the beam)"));
			_add_row(MODULE_EVENT, TARGET_NODE, "texture_tiling", "texture_tiling", TTR("Texture Repeats per Meter"));
			_add_row(MODULE_EVENT, TARGET_NODE, "texture_scroll", "texture_scroll", TTR("Texture Scroll"));
			_add_row(MODULE_EVENT, TARGET_NODE, "noise_amplitude", "noise_amplitude", TTR("Wobble"));
			_add_row(MODULE_EVENT, TARGET_NODE, "noise_frequency", "noise_frequency", TTR("Wobble Waves per Meter"));
			_add_row(MODULE_EVENT, TARGET_NODE, "noise_speed", "noise_speed", TTR("Wobble Speed"));
			_add_row(MODULE_EVENT, TARGET_NODE, "segments", "segments", TTR("Segments"));
			// No curve = on at once, fading out over the duration.
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_CURVE, TTR("Intensity over Time"), Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Curve");
		} break;
		case ParticleEffect3D::EVENT_SPLATTER: {
			_add_row(MODULE_EVENT, TARGET_NODE, "texture", "texture", TTR("Decal Texture"));
			_add_row(MODULE_EVENT, TARGET_NODE, "normal_texture", "normal_texture", TTR("Normal Map"));
			_add_row(MODULE_EVENT, TARGET_NODE, "modulate", "modulate", TTR("Tint"));
			_add_row(MODULE_EVENT, TARGET_NODE, "count", "count", TTR("Drops (Rays)"));
			_add_row(MODULE_EVENT, TARGET_NODE, "direction", "direction", TTR("Direction"));
			_add_row(MODULE_EVENT, TARGET_NODE, "spread", "spread", TTR("Spread"));
			_add_row(MODULE_EVENT, TARGET_NODE, "max_distance", "max_distance", TTR("Reach"));
			_add_row(MODULE_EVENT, TARGET_NODE, "size", "size", TTR("Size Min / Max"));
			_add_row(MODULE_EVENT, TARGET_NODE, "drop_speed", "drop_speed", TTR("Drop Speed"));
			_add_row(MODULE_EVENT, TARGET_NODE, "lifetime", "lifetime", TTR("Stays"));
			_add_row(MODULE_EVENT, TARGET_NODE, "fade_time", "fade_time", TTR("Fade Out"));
			_add_row(MODULE_EVENT, TARGET_NODE, "collision_mask", "collision_mask", TTR("Surface Layers"));
			_add_row(MODULE_EVENT, TARGET_NODE, "position", "position", TTR("Position"));
			Label *note = memnew(Label);
			note->set_text(TTR("Works in the running game only (the rays need the level's physics). Each decal appears when a drop flying at Drop Speed would arrive. At most 64 splatter decals exist at once; older ones make room."));
			note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			note->set_modulate(Color(1, 1, 1, 0.7));
			module_contents[MODULE_EVENT]->add_child(note);
		} break;
		case ParticleEffect3D::EVENT_SCREEN_FLASH:
			_add_row(MODULE_EVENT, TARGET_NODE, "color", "color", TTR("Color"));
			_add_row(MODULE_EVENT, TARGET_NODE, "strength", "strength", TTR("Strength"));
			_add_row(MODULE_EVENT, TARGET_NODE, "duration", "duration", TTR("Duration"));
			_add_row(MODULE_EVENT, TARGET_NODE, "inner_radius", "inner_radius", TTR("Full Strength Radius"));
			_add_row(MODULE_EVENT, TARGET_NODE, "radius", "radius", TTR("Fade Out Radius"));
			_add_row(MODULE_EVENT, TARGET_NODE, "position", "position", TTR("Position"));
			break;
		case ParticleEffect3D::EVENT_SPAWN: {
			_add_row(MODULE_EVENT, TARGET_NODE, "scene", "scene", TTR("Effect or Scene"));
			_add_row(MODULE_EVENT, TARGET_NODE, "count", "count", TTR("Count"));
			_add_row(MODULE_EVENT, TARGET_NODE, "scatter_radius", "scatter_radius", TTR("Scatter Radius"));
			_add_row(MODULE_EVENT, TARGET_NODE, "launch_speed", "launch_speed", TTR("Launch Speed (Scenes)"));
			_add_row(MODULE_EVENT, TARGET_NODE, "launch_spread", "launch_spread", TTR("Launch Spread"));
			_add_row(MODULE_EVENT, TARGET_NODE, "inherit_look", "inherit_look", TTR("Use This Effect's Tint and Scale"));
			_add_row(MODULE_EVENT, TARGET_NODE, "position", "position", TTR("Position"));
			_add_row(MODULE_EVENT, TARGET_NODE, "rotation_degrees", "rotation_degrees", TTR("Rotation"));
			Label *note = memnew(Label);
			note->set_text(TTR("A particle effect is played from the effect pool and is also shown in this preview from the moment it fires (not when scrubbing past it). Any other scene (a RigidBody3D gib, a prop) is created only in the running game and thrown along this track's +Y at Launch Speed."));
			note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			note->set_modulate(Color(1, 1, 1, 0.7));
			module_contents[MODULE_EVENT]->add_child(note);
		} break;
		case ParticleEffect3D::EVENT_DECAL:
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_DURATION, TTR("Duration (0 = stays)"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,120,0.1,or_greater,suffix:s");
			_add_meta_row(MODULE_EVENT, ParticleEffect3D::META_FADE, TTR("Fade Out Part"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,1,0.01");
			_add_row(MODULE_EVENT, TARGET_NODE, "texture_albedo", "texture_albedo", TTR("Texture"));
			{
				Button *generate = memnew(Button);
				generate->set_text(TTR("Generate Decal Texture..."));
				generate->set_tooltip_text(TTR("Open the Texture Generator on the Decals category (bullet hole, scorch / burn mark, glass crack, splatter). Apply sets this decal's texture; with Normal Map enabled it sets the decal normal map."));
				generate->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_generate_texture_pressed));
				module_contents[MODULE_EVENT]->add_child(generate);
			}
			_add_row(MODULE_EVENT, TARGET_NODE, "texture_normal", "texture_normal", TTR("Normal Map"));
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
	collision_size_hint = nullptr;
	collision_fit_button = nullptr;
	sub_emitter_picker = nullptr;
	sub_emitter_warning = nullptr;
	rotation_note = nullptr;
	sub_uv_note = nullptr;
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
	const Vector<int> shape_points = { ParticleProcessMaterial::EMISSION_SHAPE_POINTS, ParticleProcessMaterial::EMISSION_SHAPE_DIRECTED_POINTS };
	const Vector<int> shape_directed = { ParticleProcessMaterial::EMISSION_SHAPE_DIRECTED_POINTS };
	const Vector<int> enabled = { 1 };
	auto add_note = [&](int p_module, const String &p_text) -> Label * {
		if (!module_contents[p_module]) {
			return nullptr;
		}
		Label *note = memnew(Label);
		note->set_text(p_text);
		note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		note->add_theme_color_override(SceneStringName(font_color), Color(1.0, 0.72, 0.3));
		module_contents[p_module]->add_child(note);
		return note;
	};
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
	_add_meta_row(MODULE_EMITTER, ParticleEffect3D::META_DELAY, TTR("Start Delay"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,30,0.01,or_greater,suffix:s");
	_add_meta_row(MODULE_EMITTER, ParticleEffect3D::META_DURATION, TTR("Emission Length (0 = auto)"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s");
	_add_row(MODULE_EMITTER, TARGET_NODE, "amount", "amount", TTR("Max Particles"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "amount_ratio", StringName(), TTR("Amount Ratio"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "lifetime", "lifetime", TTR("Lifetime"));
	_add_row(MODULE_EMITTER, TARGET_PROCESS, "lifetime_randomness", "lifetime_randomness", TTR("Lifetime Random"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "one_shot", "one_shot", TTR("One Shot"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "explosiveness", "explosiveness", TTR("Burst"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "randomness", "randomness", TTR("Randomness"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "preprocess", "preprocess", TTR("Prewarm"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "speed_scale", "speed_scale", TTR("Speed Scale"));
	_add_row(MODULE_EMITTER, TARGET_NODE, "local_coords", "local_coords", TTR("Local Space"));
	// For effects attached to a moving node (spawn_attached, Local Space on
	// the effect): this emitter's particles stay behind in the world.
	_add_meta_row(MODULE_EMITTER, ParticleEffect3D::META_KEEP_WORLD, TTR("Stay in World when Attached"), Variant::BOOL, PROPERTY_HINT_NONE, String());
	// Effect parameters the game sets per play (effect.set_parameter("power",
	// 0.5)): property -> parameter name, the property is multiplied by it.
	_add_meta_row(MODULE_EMITTER, ParticleEffect3D::META_PARAMETER_LINKS, TTR("Parameter Links"), Variant::DICTIONARY, PROPERTY_HINT_DICTIONARY_TYPE, "String;String");
	_add_row(MODULE_EMITTER, TARGET_NODE, "fixed_fps", "fixed_fps", TTR("Fixed FPS"));

	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_shape", "emission_shape", TTR("Shape"));
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_sphere_radius", "emission_sphere_radius", TTR("Radius"), "emission_shape", shape_sphere);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_box_extents", "emission_box_extents", TTR("Box Extents"), "emission_shape", shape_box);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_radius", "emission_ring_radius", TTR("Ring Radius"), "emission_shape", shape_ring);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_inner_radius", "emission_ring_inner_radius", TTR("Inner Radius"), "emission_shape", shape_ring);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_height", "emission_ring_height", TTR("Ring Height"), "emission_shape", shape_ring);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_axis", "emission_ring_axis", TTR("Ring Axis"), "emission_shape", shape_ring);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_ring_cone_angle", "emission_ring_cone_angle", TTR("Ring Cone Angle"), "emission_shape", shape_ring);
	// Points / Directed Points read the spawn positions from a texture
	// (GPUParticles3D > Create Emission Points From Node in the 3D editor).
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_point_texture", StringName(), TTR("Point Texture"), "emission_shape", shape_points);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_point_count", StringName(), TTR("Point Count"), "emission_shape", shape_points);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_normal_texture", StringName(), TTR("Normal Texture"), "emission_shape", shape_directed);
	_add_row(MODULE_SHAPE, TARGET_PROCESS, "emission_color_texture", StringName(), TTR("Color Texture"), "emission_shape", shape_points);
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
	// Circling around the emitter (vortex, energy ball), in turns per second.
	add_range(MODULE_VELOCITY, "orbit_velocity", TTR("Orbit Speed"));
	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "orbit_velocity_curve", "orbit_velocity_curve", TTR("Orbit over Life"));
	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "velocity_pivot", StringName(), TTR("Orbit Center"));
	// Streaks: the particle's Y axis follows its flight (sparks, rain, tracers).
	_add_row(MODULE_VELOCITY, TARGET_PROCESS, "particle_flag_align_y", "particle_flag_align_y", TTR("Align to Flight Direction"));

	_add_row(MODULE_FORCES, TARGET_PROCESS, "gravity", "gravity", TTR("Gravity"));
	add_range(MODULE_FORCES, "linear_accel", TTR("Linear Accel"));
	add_range(MODULE_FORCES, "radial_accel", TTR("Radial Accel"));
	add_range(MODULE_FORCES, "tangential_accel", TTR("Tangential Accel"));
	add_range(MODULE_FORCES, "damping", TTR("Drag"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "linear_accel_curve", "linear_accel_curve", TTR("Linear Accel over Life"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "radial_accel_curve", "radial_accel_curve", TTR("Radial Accel over Life"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "tangential_accel_curve", "tangential_accel_curve", TTR("Tangential over Life"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "damping_curve", "damping_curve", TTR("Drag over Life"));
	_add_row(MODULE_FORCES, TARGET_PROCESS, "particle_flag_damping_as_friction", StringName(), TTR("Drag as Friction"));
	// Share of the level's wind (ParticleLevelSetup > Wind) that pushes this
	// emitter in the game: smoke 1, sparks 0.2, heavy debris 0.
	_add_meta_row(MODULE_FORCES, ParticleEffect3D::META_WIND, TTR("Wind Influence (game)"), Variant::FLOAT, PROPERTY_HINT_RANGE, "0,3,0.01,or_greater");

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
	rotation_note = add_note(MODULE_ROTATION, TTR("Start Angle and Spin Speed turn sprites only with Facing = Particle Billboard (Render module). This emitter draws a mesh or faces another way: use 3D Rotation and 3D Spin below (GPU emitters), or Rotate around Y."));
	// Mesh particles (debris, casings, shards): tumble around all three axes.
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "use_rotation_3d", StringName(), TTR("3D Rotation"));
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "rotation_3d_min", StringName(), TTR("Start Rotation Min"), "use_rotation_3d", enabled);
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "rotation_3d_max", StringName(), TTR("Start Rotation Max"), "use_rotation_3d", enabled);
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "use_rotation_velocity_3d", StringName(), TTR("3D Spin"));
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "rotation_velocity_3d_min", StringName(), TTR("Spin Min"), "use_rotation_velocity_3d", enabled);
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "rotation_velocity_3d_max", StringName(), TTR("Spin Max"), "use_rotation_velocity_3d", enabled);
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "rotation_velocity_3d_curve", StringName(), TTR("3D Spin over Life"), "use_rotation_velocity_3d", enabled);
	_add_row(MODULE_ROTATION, TARGET_PROCESS, "particle_flag_rotate_y", "particle_flag_rotate_y", TTR("Rotate around Y (flat particles)"));

	sub_uv_note = add_note(MODULE_SUB_UV, TTR("Sprite sheet animation needs Facing = Particle Billboard in the Render module. With another facing or a mesh, every particle shows the whole sheet."));
	// anim_speed 1 plays the whole sheet once over the particle's life;
	// anim_offset is where in the sheet it starts (0 = first frame, 1 = last).
	add_range(MODULE_SUB_UV, "anim_speed", TTR("Sheet Plays per Life"));
	add_range(MODULE_SUB_UV, "anim_offset", TTR("Start Position (0-1)"));
	_add_row(MODULE_SUB_UV, TARGET_PROCESS, "anim_speed_curve", "anim_speed_curve", TTR("Speed over Life"));
	_add_row(MODULE_SUB_UV, TARGET_PROCESS, "anim_offset_curve", "anim_offset_curve", TTR("Position over Life"));
	_add_row(MODULE_SUB_UV, TARGET_MATERIAL, "particles_anim_h_frames", "particles_anim_h_frames", TTR("Columns"));
	_add_row(MODULE_SUB_UV, TARGET_MATERIAL, "particles_anim_v_frames", "particles_anim_v_frames", TTR("Rows"));
	_add_row(MODULE_SUB_UV, TARGET_MATERIAL, "particles_anim_loop", "particles_anim_loop", TTR("Loop"));
	_add_row(MODULE_SUB_UV, TARGET_MATERIAL, "particles_anim_blend", "particles_anim_blend", TTR("Blend Frames (smooth)"));

	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_noise_strength", StringName(), TTR("Strength"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_noise_scale", StringName(), TTR("Noise Scale"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_noise_speed", StringName(), TTR("Noise Speed"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_noise_speed_random", StringName(), TTR("Speed Random"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_influence", StringName(), TTR("Influence"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_initial_displacement", StringName(), TTR("Displacement"));
	_add_row(MODULE_TURBULENCE, TARGET_PROCESS, "turbulence_influence_over_life", StringName(), TTR("Influence over Life"));

	_add_row(MODULE_COLLISION, TARGET_PROCESS, "collision_mode", StringName(), TTR("Mode"));
	_add_row(MODULE_COLLISION, TARGET_PROCESS, "collision_friction", StringName(), TTR("Friction"));
	_add_row(MODULE_COLLISION, TARGET_PROCESS, "collision_bounce", StringName(), TTR("Bounce"));
	_add_row(MODULE_COLLISION, TARGET_PROCESS, "collision_use_scale", StringName(), TTR("Use Scale"));
	_add_row(MODULE_COLLISION, TARGET_NODE, "collision_base_size", StringName(), TTR("Particle Size"));
	// No Collision card for custom particle shaders or CPU emitters.
	if (VBoxContainer *collision = module_contents[MODULE_COLLISION]) {
		Label *collision_note = memnew(Label);
		collision_note->set_text(TTR("In the game, the effect bounces particles off walls and floors that have physics collision (StaticBody3D) automatically: when it plays, it bakes the collision shapes around it in the background (ParticleEffect3D > Level Collision). Enable this module on every emitter that should bounce, e.g. Smoke or Sparks; fast particles get extra simulation steps so they do not pass through thin walls. Test bouncing here with View > Collision Test Walls. Add Collision to Level is optional: it also shows the collision in the Level editor viewport."));
		collision_note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		collision_note->set_modulate(Color(1, 1, 1, 0.7));
		collision->add_child(collision_note);
		collision_size_hint = memnew(Label);
		collision_size_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		collision_size_hint->add_theme_color_override(SceneStringName(font_color), Color(1.0, 0.72, 0.3));
		collision_size_hint->hide();
		collision->add_child(collision_size_hint);
		collision_fit_button = memnew(Button);
		collision_fit_button->set_text(TTR("Fit Particle Size"));
		collision_fit_button->set_tooltip_text(TTR("Set Particle Size (the collision radius) from the particle mesh size and scale, so particles touch surfaces instead of stopping in the air."));
		collision_fit_button->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_fit_collision_size_pressed));
		collision->add_child(collision_fit_button);
		Button *level_collision_button = memnew(Button);
		level_collision_button->set_text(TTR("Add Collision to Level"));
		level_collision_button->set_tooltip_text(TTR("Create (or re-bake) a GPUParticlesCollisionSDF3D covering the open Level scene, so particles bounce off its walls and floors. Re-bake after changing the level geometry."));
		level_collision_button->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_level_collision_pressed));
		collision->add_child(level_collision_button);
	}

	// Sub Emitter: each particle of this emitter spawns particles of another
	// emitter of the effect (e.g. a spark that puffs on impact).
	if (VBoxContainer *sub = module_contents[MODULE_SUB_EMITTER]) {
		HBoxContainer *pick_row = memnew(HBoxContainer);
		Label *pick_label = memnew(Label);
		pick_label->set_text(TTR("Spawns"));
		pick_label->set_custom_minimum_size(Size2(90, 0) * EDSCALE);
		pick_row->add_child(pick_label);
		sub_emitter_picker = memnew(OptionButton);
		sub_emitter_picker->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		sub_emitter_picker->set_clip_text(true);
		sub_emitter_picker->set_tooltip_text(TTR("The emitter whose particles are spawned. It is driven only by this emitter: its own Emitting is turned off."));
		sub_emitter_picker->connect(SceneStringName(item_selected), callable_mp(this, &FPSParticleModuleStack::_on_sub_emitter_selected));
		pick_row->add_child(sub_emitter_picker);
		sub->add_child(pick_row);
	}
	const Vector<int> sub_constant = { ParticleProcessMaterial::SUB_EMITTER_CONSTANT };
	const Vector<int> sub_end = { ParticleProcessMaterial::SUB_EMITTER_AT_END };
	const Vector<int> sub_collision = { ParticleProcessMaterial::SUB_EMITTER_AT_COLLISION };
	const Vector<int> sub_start = { ParticleProcessMaterial::SUB_EMITTER_AT_START };
	const Vector<int> sub_any = { ParticleProcessMaterial::SUB_EMITTER_CONSTANT, ParticleProcessMaterial::SUB_EMITTER_AT_END, ParticleProcessMaterial::SUB_EMITTER_AT_COLLISION, ParticleProcessMaterial::SUB_EMITTER_AT_START };
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_mode", StringName(), TTR("Trigger"));
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_frequency", StringName(), TTR("Per Second"), "sub_emitter_mode", sub_constant);
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_end", StringName(), TTR("Amount at End"), "sub_emitter_mode", sub_end);
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_collision", StringName(), TTR("Amount at Hit"), "sub_emitter_mode", sub_collision);
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_amount_at_start", StringName(), TTR("Amount at Start"), "sub_emitter_mode", sub_start);
	_add_row(MODULE_SUB_EMITTER, TARGET_PROCESS, "sub_emitter_keep_velocity", StringName(), TTR("Keep Velocity"), "sub_emitter_mode", sub_any);
	if (VBoxContainer *sub = module_contents[MODULE_SUB_EMITTER]) {
		sub_emitter_warning = memnew(Label);
		sub_emitter_warning->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		sub_emitter_warning->add_theme_color_override(SceneStringName(font_color), Color(1.0, 0.72, 0.3));
		sub_emitter_warning->hide();
		sub->add_child(sub_emitter_warning);
	}

	_add_row(MODULE_TRAILS, TARGET_NODE, "trail_lifetime", StringName(), TTR("Trail Length"));
	_add_row(MODULE_TRAILS, TARGET_MESH, "size", StringName(), TTR("Trail Width"));
	_add_row(MODULE_TRAILS, TARGET_MESH, "sections", StringName(), TTR("Segments"));
	_add_row(MODULE_TRAILS, TARGET_MESH, "curve", StringName(), TTR("Width over Trail"));

	if (module_contents[MODULE_ATTRACTORS]) {
		Label *info = memnew(Label);
		info->set_text(TTR("Attractors pull or push particles. An attractor acts on every GPU emitter in its range that has React to Attractors on (the default), so also on the other emitters of this effect and on effects nearby: turn React to Attractors off on emitters that should ignore it."));
		info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		info->set_modulate(Color(1, 1, 1, 0.7));
		module_contents[MODULE_ATTRACTORS]->add_child(info);
		Button *add_attractor = memnew(Button);
		add_attractor->set_text(TTR("Add Attractor"));
		add_attractor->connect(SceneStringName(pressed), callable_mp(this, &FPSParticleModuleStack::_on_add_attractor_pressed));
		module_contents[MODULE_ATTRACTORS]->add_child(add_attractor);
	}
	_add_row(MODULE_ATTRACTORS, TARGET_PROCESS, "attractor_interaction_enabled", StringName(), TTR("React to Attractors"));
	if (module_contents[MODULE_ATTRACTORS]) {
		int attractor_index = 0;
		for (int i = 0; i < emitter->get_child_count(); i++) {
			GPUParticlesAttractor3D *attractor = Object::cast_to<GPUParticlesAttractor3D>(emitter->get_child(i));
			if (!attractor) {
				continue;
			}
			attractor_index++;
			Label *attractor_title = memnew(Label);
			attractor_title->set_text(vformat("%d. %s", attractor_index, attractor->get_name()));
			attractor_title->set_theme_type_variation("HeaderSmall");
			module_contents[MODULE_ATTRACTORS]->add_child(attractor_title);
			_add_object_row(MODULE_ATTRACTORS, attractor, "strength", TTR("Strength (negative pushes)"), TARGET_NODE, true);
			_add_object_row(MODULE_ATTRACTORS, attractor, "attenuation", TTR("Falloff"), TARGET_NODE, true);
			_add_object_row(MODULE_ATTRACTORS, attractor, "directionality", TTR("Directionality"), TARGET_NODE, true);
			_add_object_row(MODULE_ATTRACTORS, attractor, "radius", TTR("Radius"), TARGET_NODE, true);
			_add_object_row(MODULE_ATTRACTORS, attractor, "size", TTR("Size"), TARGET_NODE, true);
			_add_object_row(MODULE_ATTRACTORS, attractor, "position", TTR("Position"), TARGET_NODE, true);
		}
	}

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
	// Fade by distance from the camera: with Min below Max, particles vanish
	// as they come close (no screen-filling smoke, far less overdraw).
	const Vector<int> distance_fade_on = { BaseMaterial3D::DISTANCE_FADE_PIXEL_ALPHA, BaseMaterial3D::DISTANCE_FADE_PIXEL_DITHER, BaseMaterial3D::DISTANCE_FADE_OBJECT_DITHER };
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "distance_fade_mode", "distance_fade_mode", TTR("Camera Fade"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "distance_fade_min_distance", "distance_fade_min_distance", TTR("Invisible at"), "distance_fade_mode", distance_fade_on);
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "distance_fade_max_distance", "distance_fade_max_distance", TTR("Fully Visible from"), "distance_fade_mode", distance_fade_on);
	// Lit sprites: a normal map (Generate Texture > Export as Normal Map >
	// Apply) lets lights shade them from their side, Backlight lets light
	// shine through from behind (smoke, steam, water spray).
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "normal_enabled", "normal_enabled", TTR("Normal Map"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "normal_texture", "normal_texture", TTR("Normal Texture"), "normal_enabled", enabled);
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "normal_scale", "normal_scale", TTR("Normal Strength"), "normal_enabled", enabled);
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "backlight_enabled", "backlight_enabled", TTR("Backlight"));
	_add_row(MODULE_RENDER, TARGET_MATERIAL, "backlight", "backlight", TTR("Backlight Color"), "backlight_enabled", enabled);
	_add_row(MODULE_RENDER, TARGET_NODE, "draw_pass_1", "mesh", TTR("Mesh"));
	_add_row(MODULE_RENDER, TARGET_MESH, "size", "size", TTR("Quad Size"));
	_add_row(MODULE_RENDER, TARGET_NODE, "draw_order", "draw_order", TTR("Sorting"));
	_add_row(MODULE_RENDER, TARGET_NODE, "transform_align", StringName(), TTR("Align"));
	// A custom shader material (distortion, the water clip of the presets...):
	// its parameters are the rows.
	if (Ref<ShaderMaterial> shader_material = get_draw_material(emitter); shader_material.is_valid()) {
		List<PropertyInfo> shader_properties;
		shader_material->get_property_list(&shader_properties);
		for (const PropertyInfo &property : shader_properties) {
			if (property.name.begins_with("shader_parameter/")) {
				_add_object_row(MODULE_RENDER, shader_material.ptr(), property.name, property.name.trim_prefix("shader_parameter/").capitalize(), TARGET_MATERIAL, true);
			}
		}
		_add_object_row(MODULE_RENDER, shader_material.ptr(), "render_priority", TTR("Render Priority"), TARGET_MATERIAL, true);
	}

	_update_render_card();
	_update_facing_notes();
	_update_collision_hint();
	_update_sub_emitter_card();
	_update_visibility();
	_update_add_menu();
}

// Rotation (2D angle) and Sprite Sheet work only on particle billboards: say
// so instead of silently doing nothing.
void FPSParticleModuleStack::_update_facing_notes() {
	const BaseMaterial3D *material = Object::cast_to<BaseMaterial3D>(_get_target(TARGET_MATERIAL));
	const bool other_facing = material && material->get_billboard_mode() != BaseMaterial3D::BILLBOARD_PARTICLES;
	if (rotation_note) {
		// Rotate around Y applies the angle to any particle, also a mesh.
		Object *process = _get_target(TARGET_PROCESS);
		rotation_note->set_visible(other_facing && !(process && bool(process->get("particle_flag_rotate_y"))));
	}
	if (sub_uv_note) {
		sub_uv_note->set_visible(other_facing);
	}
}

void FPSParticleModuleStack::_get_targets(Object *r_targets[TARGET_COUNT]) const {
	for (int i = 0; i < TARGET_COUNT; i++) {
		r_targets[i] = _get_target(Target(i));
	}
}

void FPSParticleModuleStack::_update_visibility() {
	for (int module = 0; module < MODULE_EVENT; module++) {
		if (module_cards[module]) {
			module_cards[module]->set_visible(_is_module_active(module));
		}
	}
	Object *targets[TARGET_COUNT];
	_get_targets(targets);
	for (const PropertyRow &row : rows) {
		bool visible = true;
		if (row.condition_property != StringName()) {
			Object *object = row.custom_object.is_valid() ? ObjectDB::get_instance(row.custom_object) : targets[row.target];
			visible = object && row.condition_values.has(int(object->get(row.condition_property)));
		}
		row.editor->set_visible(visible);
	}
}

void FPSParticleModuleStack::_collect_addable_modules(Vector<int> &r_modules) const {
	Node *emitter = get_edited_emitter();
	if (!emitter || !ParticleEffect3D::is_emitter(emitter) || !_has_process_material()) {
		return;
	}
	const bool gpu = _is_gpu();
	for (int module = 0; module < MODULE_EVENT; module++) {
		if (!_is_module_optional(module) || _is_module_active(module)) {
			continue;
		}
		if (!gpu && is_gpu_only_module(module)) {
			continue;
		}
		r_modules.push_back(module);
	}
}

void FPSParticleModuleStack::_update_add_menu() {
	// Only the enabled state here; the popup is filled when it opens.
	Vector<int> modules;
	_collect_addable_modules(modules);
	add_module_button->set_disabled(modules.is_empty());
}

void FPSParticleModuleStack::_fill_add_menu() {
	PopupMenu *popup = add_module_button->get_popup();
	popup->clear();
	Vector<int> modules;
	_collect_addable_modules(modules);
	for (int module : modules) {
		popup->add_item(get_module_name(module), module);
	}
}

void FPSParticleModuleStack::_update_render_card() {
	if (!texture_preview) {
		return;
	}
	BaseMaterial3D *material = Object::cast_to<BaseMaterial3D>(_get_target(TARGET_MATERIAL));
	// A custom shader material is a material too: offering to create one
	// would replace it (the water clip shader of the water presets).
	const Ref<ShaderMaterial> shader_material = get_edited_emitter() ? get_draw_material(get_edited_emitter()) : Ref<Material>();
	create_material_button->set_visible(material == nullptr && shader_material.is_null());
	generate_texture_button->set_disabled(get_edited_emitter() == nullptr);
	Ref<Texture2D> texture = material ? material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO) : Ref<Texture2D>();
	if (shader_material.is_valid()) {
		texture = shader_material->get_shader_parameter("albedo_texture");
	}
	texture_preview->set_texture(texture);
	if (shader_material.is_valid()) {
		texture_label->set_text(TTR("Custom shader material (parameters below)"));
	} else if (!material) {
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
	if (!emitter || get_draw_material(emitter).is_valid()) {
		return; // Never over an existing material.
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

// Heat haze and blast waves: a billboard that bends the picture behind it.
static Ref<Shader> distortion_shader;

static Ref<Shader> get_distortion_shader() {
	if (distortion_shader.is_null()) {
		distortion_shader.instantiate();
		distortion_shader->set_code(R"(shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled;
// Bends the picture behind the particle (hot air, a blast wave). The
// particle's alpha (Color over Life, Alpha over Life) scales the bend, so the
// distortion fades in and out with the particle.
uniform float strength : hint_range(0.0, 0.2, 0.001) = 0.03;
// 0 = a soft lens over the whole sprite (heat), 1 = a thin ring near its
// edge (shock wave; grow the particle with Scale over Life).
uniform float ring : hint_range(0.0, 1.0, 0.01) = 0.0;
// Rippling of the bend over time (hot air); 0 = a still lens.
uniform float shimmer : hint_range(0.0, 1.0, 0.01) = 0.6;
uniform float shimmer_speed : hint_range(0.0, 20.0, 0.1) = 6.0;
uniform sampler2D screen_texture : hint_screen_texture, repeat_disable, filter_linear;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;
varying float phase;
void vertex() {
	// Facing the camera, keeping the particle's scale.
	mat4 facing = mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
	facing[0] *= length(MODEL_MATRIX[0].xyz);
	facing[1] *= length(MODEL_MATRIX[1].xyz);
	facing[2] *= length(MODEL_MATRIX[2].xyz);
	MODELVIEW_MATRIX = VIEW_MATRIX * facing;
	MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
	phase = float(INSTANCE_ID) * 1.7;
}
void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float r = length(p);
	vec2 dir = r > 0.0001 ? p / r : vec2(0.0);
	float lens = smoothstep(1.0, 0.0, r);
	float band = smoothstep(0.55, 0.8, r) * smoothstep(1.0, 0.85, r);
	float amount = mix(lens, band, ring) * COLOR.a;
	vec2 wave = vec2(sin(p.y * 9.0 + TIME * shimmer_speed + phase), cos(p.x * 8.0 + TIME * shimmer_speed * 1.3 + phase));
	vec2 uv = SCREEN_UV + mix(dir, wave, shimmer * (1.0 - ring)) * amount * strength;
	// Do not pull in things that stand in front of the particle.
	float d = texture(depth_texture, uv).x;
	vec4 sv = INV_PROJECTION_MATRIX * vec4(uv * 2.0 - 1.0, d, 1.0);
	if (-sv.z / sv.w < -VERTEX.z) {
		uv = SCREEN_UV;
	}
	ALBEDO = texture(screen_texture, uv).rgb;
	ALPHA = clamp(amount * 4.0, 0.0, 1.0);
}
)");
	}
	return distortion_shader;
}

void FPSParticleModuleStack::_on_create_distortion_material() {
	Node *emitter = get_edited_emitter();
	if (!emitter || !ParticleEffect3D::is_emitter(emitter)) {
		return;
	}
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(get_distortion_shader());
	// Its own mesh, so emitters that share a quad keep their material.
	Ref<Mesh> mesh = get_draw_mesh(emitter);
	Ref<Mesh> new_mesh;
	if (mesh.is_valid() && Object::cast_to<PrimitiveMesh>(mesh.ptr())) {
		new_mesh = mesh->duplicate();
	} else {
		Ref<QuadMesh> quad;
		quad.instantiate();
		quad->set_size(Vector2(1.0, 1.0));
		new_mesh = quad;
	}
	Object::cast_to<PrimitiveMesh>(new_mesh.ptr())->set_material(material);
	const StringName mesh_property = _is_gpu() ? StringName("draw_pass_1") : StringName("mesh");
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(TTR("Create Distortion Material"), EditorUndoRedoManager::GLOBAL_HISTORY, UndoRedo::MERGE_DISABLE);
	undo_redo->force_fixed_history();
	undo_redo->add_do_property(emitter, mesh_property, new_mesh);
	undo_redo->add_undo_property(emitter, mesh_property, emitter->get(mesh_property));
	if (GeometryInstance3D *geometry = Object::cast_to<GeometryInstance3D>(emitter); geometry && geometry->get_material_override().is_valid()) {
		// The override would hide the new material.
		undo_redo->add_do_property(emitter, "material_override", Ref<Material>());
		undo_redo->add_undo_property(emitter, "material_override", geometry->get_material_override());
	}
	undo_redo->add_do_method(this, "_history_changed", StringName("material"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("material"));
	undo_redo->commit_action();
}

void FPSParticleModuleStack::_notification(int p_what) {
	if (p_what != NOTIFICATION_PREDELETE) {
		return;
	}
	distortion_shader.unref();
	// The clipboard is static; release its resources before engine shutdown.
	module_clipboard.clear();
	module_clipboard_module = -1;
	// Plugin-owned module controls outlive card rebuilds. One that is not in a
	// card right now (e.g. the emitter uses a custom particle shader, so the
	// process modules were not built) would never be freed. Do it here: in
	// PREDELETE the children still exist, in the destructor they are gone.
	for (int i = 0; i < MODULE_MAX; i++) {
		Control *control = Object::cast_to<Control>(ObjectDB::get_instance(module_custom_control_ids[i]));
		module_custom_controls[i] = nullptr;
		module_custom_control_ids[i] = ObjectID();
		if (control && !control->get_parent() && !control->is_queued_for_deletion()) {
			for (int j = i + 1; j < MODULE_MAX; j++) {
				if (module_custom_control_ids[j] == control->get_instance_id()) {
					module_custom_control_ids[j] = ObjectID();
				}
			}
			memdelete(control);
		}
	}
}

void FPSParticleModuleStack::set_module_custom_control(int p_module, Control *p_control) {
	ERR_FAIL_INDEX(p_module, MODULE_MAX);
	module_custom_controls[p_module] = p_control;
	module_custom_control_ids[p_module] = p_control ? p_control->get_instance_id() : ObjectID();
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
		// The emitter was freed (e.g. its creation was undone): drop the
		// editors before they touch it.
		if (!rows.is_empty()) {
			_build();
		}
		return;
	}
	// A different material/mesh/process resource means different rows; rebuild.
	Object *targets[TARGET_COUNT];
	_get_targets(targets);
	for (const PropertyRow &row : rows) {
		// Custom rows edit their own object: rebuild when it is gone or (a
		// shader material) no longer the emitter's draw material.
		if (row.custom_object.is_valid() ? (!ObjectDB::get_instance(row.custom_object) || (row.target == TARGET_MATERIAL && get_draw_material(emitter).ptr() != ObjectDB::get_instance(row.custom_object))) : row.editor->get_edited_object() != targets[row.target]) {
			_build();
			return;
		}
	}
	for (const PropertyRow &row : rows) {
		row.editor->update_property();
	}
	header_title->set_text(emitter->get_name());
	_update_render_card();
	_update_facing_notes();
	_update_collision_hint();
	_update_sub_emitter_card();
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
			ModulePreset &near_fade = add(TTR("Fade near Camera"));
			near_fade.values.push_back(pv(T::TARGET_MATERIAL, "distance_fade_mode", "distance_fade_mode", int(BaseMaterial3D::DISTANCE_FADE_PIXEL_ALPHA)));
			near_fade.values.push_back(pv(T::TARGET_MATERIAL, "distance_fade_min_distance", "distance_fade_min_distance", 0.25));
			near_fade.values.push_back(pv(T::TARGET_MATERIAL, "distance_fade_max_distance", "distance_fade_max_distance", 1.2));
			ModulePreset &lit = add(TTR("Lit by Scene"));
			lit.values.push_back(pv(T::TARGET_MATERIAL, "blend_mode", "blend_mode", int(BaseMaterial3D::BLEND_MODE_MIX)));
			lit.values.push_back(pv(T::TARGET_MATERIAL, "shading_mode", "shading_mode", int(BaseMaterial3D::SHADING_MODE_PER_PIXEL)));
		} break;
		default:
			break;
	}
	return presets;
}

bool property_accepts(const PropertyInfo &p_info, const StringName &p_class) {
	for (const String &type : p_info.hint_string.split(",")) {
		if (ClassDB::is_parent_class(p_class, StringName(type.strip_edges()))) {
			return true;
		}
	}
	return false;
}

// GPU emitters take curves and gradients as textures (CurveTexture,
// GradientTexture1D), CPU emitters take the raw Curve / Gradient. Converts a
// preset or clipboard value to what the target property accepts.
Variant convert_for_property(Object *p_object, const StringName &p_property, const Variant &p_value) {
	Ref<Resource> resource = p_value;
	if (!p_object || resource.is_null()) {
		return p_value;
	}
	List<PropertyInfo> properties;
	p_object->get_property_list(&properties);
	for (const PropertyInfo &info : properties) {
		if (info.name != p_property) {
			continue;
		}
		if (info.type != Variant::OBJECT || info.hint != PROPERTY_HINT_RESOURCE_TYPE || property_accepts(info, resource->get_class_name())) {
			return p_value;
		}
		if (Ref<GradientTexture1D> texture = resource; texture.is_valid() && property_accepts(info, "Gradient")) {
			return texture->get_gradient();
		}
		if (Ref<CurveTexture> texture = resource; texture.is_valid() && property_accepts(info, "Curve")) {
			return texture->get_curve();
		}
		if (Ref<Gradient> gradient = resource; gradient.is_valid() && property_accepts(info, "GradientTexture1D")) {
			Ref<GradientTexture1D> texture;
			texture.instantiate();
			texture->set_gradient(gradient);
			return texture;
		}
		if (Ref<Curve> curve = resource; curve.is_valid() && property_accepts(info, "CurveTexture")) {
			Ref<CurveTexture> texture;
			texture.instantiate();
			texture->set_curve(curve);
			return texture;
		}
		return Variant(); // Incompatible; the caller skips it.
	}
	return p_value;
}

// Rows that Reset to Defaults leaves alone: the class default would hide the
// emitter (no mesh) or switch the module off (collision mode disabled).
bool is_reset_protected(int p_module, const StringName &p_property) {
	if (p_module == FPSParticleModuleStack::MODULE_RENDER) {
		return p_property == StringName("draw_pass_1") || p_property == StringName("mesh");
	}
	if (p_module == FPSParticleModuleStack::MODULE_COLLISION) {
		return p_property == StringName("collision_mode");
	}
	return false;
}

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
		if (p_module == MODULE_RENDER) {
			popup->add_separator();
			popup->add_item(TTR("Distortion Material (Heat Haze / Shock Wave)"), 1003);
			popup->set_item_tooltip(-1, TTR("Replaces this emitter's material with one that bends the picture behind the particle. Strength follows the particle's alpha."));
		}
		return;
	}
	if (p_id == 1000) {
		_copy_module(p_module);
	} else if (p_id == 1001) {
		_paste_module(p_module);
	} else if (p_id == 1002) {
		_reset_module(p_module);
	} else if (p_id == 1003) {
		_on_create_distortion_material();
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
		const Variant new_value = convert_for_property(object, property, value.value);
		if (new_value.get_type() == Variant::NIL && value.value.get_type() != Variant::NIL) {
			continue;
		}
		undo_redo->add_do_property(object, property, new_value);
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
		// Copied from a GPU emitter onto a CPU one (or back).
		const Variant converted = convert_for_property(object, row.property, value);
		if (converted.get_type() == Variant::NIL && value.get_type() != Variant::NIL) {
			continue;
		}
		value = converted;
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
		if (row.module != p_module || is_reset_protected(p_module, row.property)) {
			continue;
		}
		Object *object = row.editor->get_edited_object();
		if (!object) {
			continue;
		}
		Variant default_value;
		if (String(row.property).begins_with("metadata/")) {
			default_value = Variant(); // Removes the key: the runtime default applies.
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

void FPSParticleModuleStack::_update_collision_hint() {
	if (!collision_size_hint || !collision_fit_button) {
		return;
	}
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(get_edited_emitter());
	collision_fit_button->set_visible(gpu != nullptr);
	if (!gpu) {
		collision_size_hint->hide();
		return;
	}
	const real_t size = gpu->get_collision_base_size();
	const real_t suggested = ParticleEffect3D::get_suggested_collision_size(gpu);
	if (size > suggested * 2.0 && size - suggested > 0.05) {
		collision_size_hint->set_text(vformat(TTR("Particles stop %.2f m before walls and floors: Particle Size is the collision radius and it is larger than the particle. Suggested: %.2f m."), size, suggested));
		collision_size_hint->show();
	} else if (size < suggested * 0.25 && suggested - size > 0.02) {
		collision_size_hint->set_text(vformat(TTR("Particle Size %.2f m is much smaller than the particle; large particles will sink into surfaces. Suggested: %.2f m."), size, suggested));
		collision_size_hint->show();
	} else {
		collision_size_hint->hide();
	}
}

void FPSParticleModuleStack::_on_fit_collision_size_pressed() {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(get_edited_emitter());
	if (!gpu) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(TTR("Fit Particle Collision Size"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_property(gpu, "collision_base_size", ParticleEffect3D::get_suggested_collision_size(gpu));
	undo_redo->add_undo_property(gpu, "collision_base_size", gpu->get_collision_base_size());
	undo_redo->add_do_method(this, "_history_changed", StringName("collision_base_size"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("collision_base_size"));
	undo_redo->commit_action();
}

Node *FPSParticleModuleStack::_get_effect_root() const {
	Node *node = get_edited_emitter();
	for (Node *n = node; n; n = n->get_parent()) {
		if (Object::cast_to<ParticleEffect3D>(n)) {
			return n;
		}
	}
	return node ? node->get_parent() : nullptr;
}

static void collect_gpu_emitters(Node *p_node, Vector<Node *> &r_list) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		if (Object::cast_to<GPUParticles3D>(child)) {
			r_list.push_back(child);
		}
		collect_gpu_emitters(child, r_list);
	}
}

void FPSParticleModuleStack::_collect_sub_emitter_candidates(Vector<Node *> &r_candidates) const {
	Node *emitter = get_edited_emitter();
	Node *root = _get_effect_root();
	if (!emitter || !root) {
		return;
	}
	Vector<Node *> all;
	collect_gpu_emitters(root, all);
	for (Node *candidate : all) {
		if (candidate != emitter) {
			r_candidates.push_back(candidate);
		}
	}
}

void FPSParticleModuleStack::_update_sub_emitter_card() {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(get_edited_emitter());
	if (!sub_emitter_picker || !gpu) {
		return;
	}
	Vector<Node *> candidates;
	_collect_sub_emitter_candidates(candidates);
	const NodePath path = gpu->get_sub_emitter();
	Node *current = path.is_empty() ? nullptr : gpu->get_node_or_null(path);
	sub_emitter_picker->clear();
	sub_emitter_picker->add_item(TTR("(None)"));
	sub_emitter_picker->set_item_metadata(0, int64_t(0));
	int selected = 0;
	for (Node *candidate : candidates) {
		sub_emitter_picker->add_item(candidate->get_name());
		const int index = sub_emitter_picker->get_item_count() - 1;
		sub_emitter_picker->set_item_metadata(index, int64_t(uint64_t(candidate->get_instance_id())));
		if (candidate == current) {
			selected = index;
		}
	}
	if (current && selected == 0) {
		// Points outside the effect: keep it visible instead of hiding it.
		sub_emitter_picker->add_item(String(path));
		selected = sub_emitter_picker->get_item_count() - 1;
		sub_emitter_picker->set_item_metadata(selected, int64_t(uint64_t(current->get_instance_id())));
	}
	sub_emitter_picker->select(selected);

	if (!sub_emitter_warning) {
		return;
	}
	Object *process = _get_target(TARGET_PROCESS);
	const int mode = process ? int(process->get("sub_emitter_mode")) : 0;
	String warning;
	if (!path.is_empty() && !current) {
		warning = TTR("The sub emitter path is broken; pick the emitter again.");
	} else if (candidates.is_empty()) {
		warning = TTR("Add a second GPU emitter to the effect (e.g. a small puff) and pick it here.");
	} else if (!current) {
		warning = TTR("Pick the emitter whose particles each particle spawns.");
	} else if (GPUParticles3D *target = Object::cast_to<GPUParticles3D>(current)) {
		if (target->is_emitting()) {
			warning = TTR("The spawned emitter also emits on its own. Pick it again to turn its Emitting off.");
		} else if (target->has_meta(ParticleEffect3D::META_DELAY) || target->has_meta(ParticleEffect3D::META_DURATION)) {
			warning = TTR("The spawned emitter has its own timing on the timeline; it is ignored while it is a sub emitter.");
		}
		if (mode == ParticleProcessMaterial::SUB_EMITTER_AT_COLLISION && process && int(process->get("collision_mode")) == 0) {
			warning = TTR("Trigger On Collision needs the Collision module on this emitter.");
		}
	}
	sub_emitter_warning->set_text(warning);
	sub_emitter_warning->set_visible(!warning.is_empty());
}

void FPSParticleModuleStack::_on_sub_emitter_selected(int p_index) {
	GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(get_edited_emitter());
	if (!gpu || !sub_emitter_picker || p_index < 0 || p_index >= sub_emitter_picker->get_item_count()) {
		return;
	}
	const ObjectID target_id = ObjectID(uint64_t(int64_t(sub_emitter_picker->get_item_metadata(p_index))));
	GPUParticles3D *target = Object::cast_to<GPUParticles3D>(ObjectDB::get_instance(target_id));
	const NodePath path = target ? gpu->get_path_to(target) : NodePath();
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(TTR("Set Sub Emitter"), EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	undo_redo->add_do_property(gpu, "sub_emitter", path);
	undo_redo->add_undo_property(gpu, "sub_emitter", gpu->get_sub_emitter());
	if (target) {
		// A sub emitter is driven by its parent only; on its own it would
		// emit from the effect origin.
		undo_redo->add_do_property(target, "emitting", false);
		undo_redo->add_undo_property(target, "emitting", target->is_emitting());
		Object *process = _get_target(TARGET_PROCESS);
		if (process && int(process->get("sub_emitter_mode")) == ParticleProcessMaterial::SUB_EMITTER_DISABLED) {
			undo_redo->add_do_property(process, "sub_emitter_mode", int(ParticleProcessMaterial::SUB_EMITTER_AT_END));
			undo_redo->add_undo_property(process, "sub_emitter_mode", int(ParticleProcessMaterial::SUB_EMITTER_DISABLED));
		}
	}
	undo_redo->add_do_method(this, "_history_changed", StringName("sub_emitter"));
	undo_redo->add_undo_method(this, "_history_changed", StringName("sub_emitter"));
	undo_redo->commit_action();
}

void FPSParticleModuleStack::_on_level_collision_pressed() {
	emit_signal(SNAME("level_collision_requested"));
}

void FPSParticleModuleStack::_history_changed(const StringName &p_property) {
	if (p_property == SNAME("attractor") || p_property == SNAME("modules") || p_property == SNAME("material")) {
		_build(); // Child nodes or the draw material changed: different rows.
	} else {
		refresh_values();
	}
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
	add_module_button->get_popup()->connect("about_to_popup", callable_mp(this, &FPSParticleModuleStack::_fill_add_menu));
	header->add_child(add_module_button);
	cards = memnew(VBoxContainer);
	cards->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(cards);
	_build();
}
