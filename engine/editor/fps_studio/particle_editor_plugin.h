/**************************************************************************/
/*  particle_editor_plugin.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/object/object_id.h"
#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "editor/plugins/editor_plugin.h"

class Button;
class CheckButton;
class EditorInspector;
class HSlider;
class Label;
class Node;
class Node3D;
class QuadMesh;
class SubViewport;
class SpinBox;
class Tree;
class TreeItem;

class FPSStudioParticleEditorPlugin : public EditorPlugin {
	GDCLASS(FPSStudioParticleEditorPlugin, EditorPlugin);

	Control *screen = nullptr;
	Tree *emitter_tree = nullptr;
	SubViewport *preview_viewport = nullptr;
	Node3D *preview_root = nullptr;
	Node *preview_emitter = nullptr;
	Ref<QuadMesh> preview_quad;
	EditorInspector *emitter_inspector = nullptr;
	EditorInspector *process_material_inspector = nullptr;
	Button *play_button = nullptr;
	Button *restart_button = nullptr;
	HSlider *timeline = nullptr;
	SpinBox *speed_spin = nullptr;
	Label *time_label = nullptr;
	Label *selection_label = nullptr;

	ObjectID edited_emitter_id;
	bool preview_playing = true;
	bool preview_loop = true;
	bool rebuilding_preview = false;
	double preview_time = 0.0;
	double preview_duration = 5.0;
	double preview_speed = 1.0;

	Node *_get_active_scene_root() const;
	Node *_get_edited_emitter() const;
	void _collect_emitters(Node *p_node, Vector<Node *> &r_emitters) const;
	void _update_emitter_tree();
	void _set_edited_emitter(Node *p_node, bool p_update_tree = true);
	void _rebuild_preview();
	void _clear_preview_scripts(Node *p_node);
	void _apply_preview_defaults();
	void _sync_preview_properties();
	void _set_preview_playing(bool p_playing);
	void _on_play_toggled(bool p_pressed);
	void _on_restart_pressed();
	void _on_timeline_changed(double p_value);
	void _on_speed_changed(double p_value);
	void _on_loop_toggled(bool p_enabled);
	void _on_emitter_selected();
	void _on_emitter_property_edited(const String &p_property);
	void _update_time_label();

protected:
	void _notification(int p_what);

public:
	bool has_main_screen() const override { return true; }
	void make_visible(bool p_visible) override;
	void edit(Object *p_object) override;
	bool handles(Object *p_object) const override;
	void edited_scene_changed() override;
	String get_plugin_name() const override { return TTRC("Particle"); }
	const Ref<Texture2D> get_plugin_icon() const override;

	FPSStudioParticleEditorPlugin();
};
