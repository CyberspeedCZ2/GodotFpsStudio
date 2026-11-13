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
class Camera3D;
class CheckButton;
class ColorPickerButton;
class ConfigFile;
class AcceptDialog;
class ConfirmationDialog;
class CurveEditor;
class EditorFileDialog;
class EditorInspector;
class HSlider;
class HBoxContainer;
class InputEvent;
class Label;
class LineEdit;
class MenuButton;
class MeshInstance3D;
class Node;
class Node3D;
class OptionButton;
class PanelContainer;
class QuadMesh;
class SubViewport;
class SubViewportContainer;
class SpinBox;
class SplitContainer;
class Tree;
class TreeItem;
class TextureRect;
class VisualShader;
class VisualShaderEditor;
class VBoxContainer;
class Image;
class Texture2D;
struct Color;

class FPSStudioParticleEditorPlugin : public EditorPlugin {
	GDCLASS(FPSStudioParticleEditorPlugin, EditorPlugin);

	Control *screen = nullptr;
	SplitContainer *main_panel_split = nullptr;
	SplitContainer *preview_properties_split = nullptr;
	SplitContainer *preview_logic_split = nullptr;
	Tree *effects_tree = nullptr;
	Tree *emitter_tree = nullptr;
	LineEdit *effect_filter = nullptr;
	MenuButton *new_effect_menu = nullptr;
	MenuButton *add_emitter_menu = nullptr;
	Button *open_effect_button = nullptr;
	Button *save_effect_button = nullptr;
	Button *add_to_level_button = nullptr;
	Label *document_title = nullptr;
	Label *document_path_label = nullptr;
	Label *document_status_label = nullptr;
	AcceptDialog *rename_effect_dialog = nullptr;
	LineEdit *rename_effect_line_edit = nullptr;
	EditorFileDialog *save_scene_dialog = nullptr;
	EditorFileDialog *load_scene_dialog = nullptr;
	EditorFileDialog *texture_save_dialog = nullptr;
	AcceptDialog *texture_generator_dialog = nullptr;
	TextureRect *texture_generator_preview = nullptr;
	CheckButton *texture_generator_animate = nullptr;
	HSlider *texture_generator_time = nullptr;
	Label *texture_generator_time_label = nullptr;
	Label *texture_generator_status = nullptr;
	OptionButton *texture_generator_preset = nullptr;
	OptionButton *texture_generator_resolution = nullptr;
	OptionButton *texture_generator_output = nullptr;
	SpinBox *texture_generator_seed = nullptr;
	SpinBox *texture_generator_fps = nullptr;
	Label *texture_generator_fps_label = nullptr;
	ColorPickerButton *texture_generator_color_a = nullptr;
	ColorPickerButton *texture_generator_color_b = nullptr;
	Button *texture_generator_apply_button = nullptr;
	Ref<Image> generated_texture_image;
	Ref<Texture2D> generated_texture;
	String generated_texture_path;
	int generated_texture_frame_columns = 1;
	int generated_texture_fps = 12;
	int texture_generator_preview_frame = -1;
	double texture_generator_preview_refresh = 0.0;
	bool texture_generator_return_after_export = false;
	ConfirmationDialog *particle_graph_confirmation = nullptr;
	ConfirmationDialog *replace_document_confirmation = nullptr;
	SubViewport *preview_viewport = nullptr;
	SubViewportContainer *preview_container = nullptr;
	Node3D *preview_root = nullptr;
	Camera3D *preview_camera = nullptr;
	MeshInstance3D *preview_grid = nullptr;
	Node *preview_effect = nullptr;
	Ref<QuadMesh> preview_quad;
	EditorInspector *emitter_inspector = nullptr;
	EditorInspector *process_material_inspector = nullptr;
	EditorInspector *renderer_inspector = nullptr;
	Control *process_material_page = nullptr;
	Control *particle_logic_content = nullptr;
	Control *curve_panel = nullptr;
	VBoxContainer *alpha_curve_card = nullptr;
	VBoxContainer *scale_curve_card = nullptr;
	CurveEditor *alpha_curve_editor = nullptr;
	CurveEditor *scale_curve_editor = nullptr;
	Button *alpha_curve_add_button = nullptr;
	Button *scale_curve_add_button = nullptr;
	MenuButton *alpha_curve_presets_button = nullptr;
	MenuButton *scale_curve_presets_button = nullptr;
	Label *alpha_curve_info = nullptr;
	Label *scale_curve_info = nullptr;
	Button *particle_graph_button = nullptr;
	Label *particle_logic_status = nullptr;
	Label *particle_logic_hint = nullptr;
	VisualShaderEditor *particle_graph_editor = nullptr;
	Button *play_button = nullptr;
	Button *restart_button = nullptr;
	HSlider *timeline = nullptr;
	SpinBox *speed_spin = nullptr;
	Label *time_label = nullptr;
	Label *selection_label = nullptr;

	ObjectID effect_root_id;
	ObjectID edited_emitter_id;
	String effect_path;
	String pending_load_path;
	int pending_new_template = -1;
	Vector<String> recent_effect_paths;
	bool effect_dirty = false;
	bool preview_playing = true;
	bool preview_loop = true;
	bool preview_orbit_dragging = false;
	bool preview_pan_dragging = false;
	bool rebuilding_preview = false;
	double preview_time = 0.0;
	double preview_duration = 5.0;
	double preview_time_label_update_accumulator = 0.0;
	double preview_speed = 1.0;
	double texture_generator_time_value = 0.0;
	double texture_generator_playback_accumulator = 0.0;
	double preview_camera_yaw = 0.0;
	double preview_camera_pitch = 0.245;
	double preview_camera_distance = 6.18;
	Vector3 preview_camera_target = Vector3(0.0, 0.5, 0.0);
	real_t panel_layout_main_ratio = 0.0;
	real_t panel_layout_properties_ratio = 0.0;
	real_t panel_layout_logic_ratio = 0.0;
	bool panel_layout_pending = false;

	Node *_get_active_scene_root() const;
	Node *_get_effect_root() const;
	Node *_get_edited_emitter() const;
	void _collect_emitters(Node *p_node, Vector<Node *> &r_emitters) const;
	void _load_recent_effects();
	void _remember_effect(const String &p_path);
	void _update_effects_tree();
	void _update_emitter_tree();
	void _update_document_ui();
	void _update_document_toolbar_layout();
	void _set_effect_document(Node *p_root, const String &p_path, bool p_dirty = false);
	void _set_edited_emitter(Node *p_node, bool p_update_tree = true);
	void _create_effect_document(int p_template);
	void _add_emitter(int p_type);
	void _remove_selected_emitter();
	void _on_emitter_renamed();
	void _on_emitter_name_changed();
	void _rebuild_preview();
	void _clear_preview_scripts(Node *p_node);
	void _apply_preview_defaults(Node *p_node);
	void _sync_preview_properties();
	void _set_preview_playing(bool p_playing);
	void _on_preview_gui_input(const Ref<InputEvent> &p_event);
	void _update_preview_camera();
	void _frame_preview();
	void _on_reset_view_pressed();
	void _on_grid_toggled(bool p_enabled);
	void _on_texture_generator_pressed();
	void _on_texture_generator_preset_selected(int p_index);
	void _on_texture_generator_resolution_selected(int p_index);
	void _on_texture_generator_output_selected(int p_index);
	void _on_texture_generator_seed_changed(double p_value);
	void _on_texture_generator_fps_changed(double p_value);
	void _on_texture_generator_animate_toggled(bool p_enabled);
	void _on_texture_generator_time_changed(double p_value);
	void _on_texture_generator_color_changed(const Color &p_color);
	void _on_texture_generator_action(const String &p_action);
	void _on_texture_save_dialog_visibility_changed();
	void _on_texture_save_dialog_cancelled();
	void _show_texture_generator_after_export();
	void _on_texture_save_path_selected(const String &p_path);
	void _regenerate_texture(bool p_preview_only = false);
	void _update_texture_preview();
	void _update_texture_time_controls();
	void _apply_generated_texture();
	void _on_play_toggled(bool p_pressed);
	void _on_restart_pressed();
	void _on_timeline_changed(double p_value);
	void _on_speed_changed(double p_value);
	void _on_loop_toggled(bool p_enabled);
	void _on_new_effect_menu_pressed(int p_id);
	void _on_add_emitter_menu_pressed(int p_id);
	void _on_effect_filter_changed(const String &p_text);
	void _on_open_effect_pressed();
	void _on_effect_activated();
	void _on_document_title_gui_input(const Ref<InputEvent> &p_event);
	void _on_rename_effect_requested();
	void _on_rename_effect_confirmed();
	void _on_effect_name_changed();
	void _on_emitter_selected();
	void _on_emitter_hierarchy_history_changed(Node *p_preferred_emitter);
	void _on_save_scene_pressed();
	void _save_emitter_scene(const String &p_path);
	void _load_particle_scene(const String &p_path);
	void _confirm_replace_document();
	void _add_effect_to_level();
	void _on_emitter_property_edited(const String &p_property);
	void _on_curve_changed();
	void _on_add_alpha_curve_pressed();
	void _on_add_scale_curve_pressed();
	void _on_alpha_curve_preset_selected(int p_preset_id);
	void _on_scale_curve_preset_selected(int p_preset_id);
	void _apply_curve_preset(bool p_alpha, int p_preset_id);
	void _refresh_curve_editors();
	void _update_particle_graph_controls();
	void _restore_panel_layout();
	void _on_panel_split_drag_ended();
	void _on_particle_graph_button_pressed();
	void _create_particle_graph();
	void _open_particle_graph(const Ref<VisualShader> &p_visual_shader);
	Ref<VisualShader> _get_particle_visual_shader(Node *p_emitter) const;
	void _update_time_label();
	void _mark_document_dirty();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	bool has_main_screen() const override { return true; }
	void make_visible(bool p_visible) override;
	void edit(Object *p_object) override;
	bool handles(Object *p_object) const override;
	void edited_scene_changed() override;
	void set_window_layout(Ref<ConfigFile> p_layout) override;
	void get_window_layout(Ref<ConfigFile> p_layout) override;
	String get_plugin_name() const override { return TTRC("Particle"); }
	const Ref<Texture2D> get_plugin_icon() const override;

	FPSStudioParticleEditorPlugin();
};
