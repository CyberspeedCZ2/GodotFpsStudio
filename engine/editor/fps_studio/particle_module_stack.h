/**************************************************************************/
/*  particle_module_stack.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/object/object_id.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "scene/gui/box_container.h"

class Button;
class EditorProperty;
class FoldableContainer;
class Label;
class MenuButton;
class Material;
class Mesh;
class TextureRect;

// Niagara / Source style module stack for one particle emitter. Each module is
// a card with a curated subset of the emitter's native Godot properties
// (GPUParticles3D + ParticleProcessMaterial, or CPUParticles3D) edited with the
// standard inspector property editors and Undo/Redo.
class FPSParticleModuleStack : public VBoxContainer {
	GDCLASS(FPSParticleModuleStack, VBoxContainer);

public:
	enum ModuleID {
		MODULE_EMITTER,
		MODULE_SHAPE,
		MODULE_VELOCITY,
		MODULE_FORCES,
		MODULE_COLOR,
		MODULE_SIZE,
		MODULE_ROTATION,
		MODULE_SUB_UV,
		MODULE_TURBULENCE,
		MODULE_COLLISION,
		MODULE_SUB_EMITTER,
		MODULE_RENDER,
		MODULE_EVENT, // Timed event nodes (light, sound, camera shake, decal).
		MODULE_MAX,
	};

	enum Target {
		TARGET_NODE,
		TARGET_PROCESS, // ParticleProcessMaterial for GPU, the node itself for CPU.
		TARGET_MATERIAL, // Draw material (StandardMaterial3D).
		TARGET_MESH, // Draw mesh (QuadMesh etc.).
	};

private:
	struct PropertyRow {
		EditorProperty *editor = nullptr;
		Target target = TARGET_NODE;
		StringName property;
		StringName condition_property;
		Vector<int> condition_values;
		int module = 0;
	};

	ObjectID emitter_id;
	Label *header_title = nullptr;
	Label *header_subtitle = nullptr;
	VBoxContainer *cards = nullptr;
	MenuButton *add_module_button = nullptr;
	FoldableContainer *module_cards[MODULE_MAX] = {};
	VBoxContainer *module_contents[MODULE_MAX] = {};
	Control *module_custom_controls[MODULE_MAX] = {};
	Vector<PropertyRow> rows;
	Label *custom_shader_note = nullptr;
	Label *empty_label = nullptr;

	// Render module helpers.
	TextureRect *texture_preview = nullptr;
	Label *texture_label = nullptr;
	Button *create_material_button = nullptr;
	Button *generate_texture_button = nullptr;
	Button *edit_texture_button = nullptr;

	bool updating = false;

	Object *_get_target(Target p_target) const;
	bool _is_gpu() const;
	bool _has_process_material() const;
	bool _is_module_optional(int p_module) const;
	bool _is_module_active(int p_module) const;
	void _set_module_active(int p_module, bool p_active);
	void _add_meta_row(int p_module, const String &p_key, const String &p_label, Variant::Type p_type, PropertyHint p_hint, const String &p_hint_string, const Variant &p_default);
	void _build_event();
	void _add_row(int p_module, Target p_target, const StringName &p_gpu_property, const StringName &p_cpu_property, const String &p_label, const StringName &p_condition = StringName(), const Vector<int> &p_condition_values = Vector<int>());
	void _build();
	void _clear();
	void _update_visibility();
	void _update_add_menu();
	void _update_render_card();
	void _on_property_changed(const StringName &p_property, const Variant &p_value, const StringName &p_field, bool p_changing, EditorProperty *p_editor);
	void _on_add_module_selected(int p_id);
	void _on_remove_module_pressed(int p_module);
	void _on_create_material_pressed();
	void _commit_property(Object *p_object, const StringName &p_property, const Variant &p_value, bool p_merge);
	void _history_changed(const StringName &p_property);
	void _on_generate_texture_pressed();

protected:
	static void _bind_methods();

public:
	static String get_module_name(int p_module);
	static Ref<Material> get_draw_material(Node *p_emitter);
	static Ref<Mesh> get_draw_mesh(Node *p_emitter);

	void edit(Node *p_emitter);
	Node *get_edited_emitter() const;
	void refresh_values();
	// Places a plugin-owned control (e.g. curve cards) at the top of a module.
	void set_module_custom_control(int p_module, Control *p_control);

	FPSParticleModuleStack();
};
