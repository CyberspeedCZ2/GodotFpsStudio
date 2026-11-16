/**************************************************************************/
/*  particle_editor_plugin.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/math/color.h"
#include "core/object/object_id.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"
#include "editor/plugins/editor_plugin.h"

#include "vfx_texture_generator.h"

namespace FPSParticlePresets {
struct TextureRequest;
}

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
class DirectionalLight3D;
class Environment;
class FPSParticleModuleStack;
class FPSParticleTimeline;
class TabContainer;
class EditorSpinSlider;
class GridContainer;
class HSlider;
class HBoxContainer;
class InputEvent;
class InputEventKey;
class Label;
class EditorFileSystemDirectory;
class EditorResourceTooltipPlugin;
class EditorResourcePreviewGenerator;
class ConfigFile;
class PopupPanel;
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
class WindowWrapper;
class TextShaderEditor;
class Shader;
class VBoxContainer;
class Image;
class ImageTexture;
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
	EditorFileDialog *texture_recipe_save_dialog = nullptr;
	EditorFileDialog *texture_recipe_load_dialog = nullptr;
	AcceptDialog *texture_generator_dialog = nullptr;
	Tree *texture_generator_type_tree = nullptr;
	TextureRect *texture_generator_preview = nullptr;
	TextureRect *texture_generator_checker = nullptr;
	PanelContainer *texture_generator_stage = nullptr;
	OptionButton *texture_generator_background = nullptr;
	Button *texture_generator_show_sheet = nullptr;
	Button *texture_generator_animate = nullptr;
	HSlider *texture_generator_time = nullptr;
	Label *texture_generator_time_label = nullptr;
	Label *texture_generator_frame_label = nullptr;
	Label *texture_generator_status = nullptr;
	Label *texture_generator_type_title = nullptr;
	Label *texture_generator_type_description = nullptr;
	GridContainer *texture_generator_param_grid = nullptr;
	Vector<EditorSpinSlider *> texture_generator_param_sliders;
	OptionButton *texture_generator_resolution = nullptr;
	OptionButton *texture_generator_output = nullptr;
	SpinBox *texture_generator_seed = nullptr;
	EditorSpinSlider *texture_generator_fps = nullptr;
	EditorSpinSlider *texture_generator_duration = nullptr;
	Control *texture_generator_fps_row = nullptr;
	Control *texture_generator_duration_row = nullptr;
	Label *texture_generator_color_a_label = nullptr;
	Label *texture_generator_color_b_label = nullptr;
	ColorPickerButton *texture_generator_color_a = nullptr;
	ColorPickerButton *texture_generator_color_b = nullptr;
	EditorSpinSlider *texture_generator_exposure = nullptr;
	EditorSpinSlider *texture_generator_opacity = nullptr;
	EditorSpinSlider *texture_generator_speed = nullptr;
	EditorSpinSlider *texture_generator_rotation = nullptr;
	CheckButton *texture_generator_polar = nullptr;
	CheckButton *texture_generator_tiling = nullptr;
	CheckButton *texture_generator_toon = nullptr;
	EditorSpinSlider *texture_generator_toon_dark = nullptr;
	EditorSpinSlider *texture_generator_toon_light = nullptr;
	ColorPickerButton *texture_generator_shadows_tint = nullptr;
	ColorPickerButton *texture_generator_midtones_tint = nullptr;
	ColorPickerButton *texture_generator_highlights_tint = nullptr;
	EditorSpinSlider *texture_generator_alpha_threshold = nullptr;
	EditorSpinSlider *texture_generator_alpha_blur = nullptr;
	CheckButton *texture_generator_alpha_preview = nullptr;
	CheckButton *texture_generator_normal_map = nullptr;
	EditorSpinSlider *texture_generator_normal_height_scale = nullptr;
	Button *texture_generator_apply_button = nullptr;
	int texture_generator_type = VFXTextureGenerator::TYPE_EXPLOSION;
	float texture_generator_params[VFXTextureGenerator::MAX_PARAMS] = {};
	bool texture_generator_thumbnails_ready = false;
	bool texture_generator_syncing_ui = false;

	// Asynchronous preview rendering: one job runs on the WorkerThreadPool while
	// the newest request waits in texture_preview_pending (older ones are dropped).
	struct TexturePreviewJob {
		VFXTextureGenerator::Settings settings;
		int size = 0;
		int refine_size = 0;
		int frame_index = 0;
		bool playback = false;
		real_t alpha_threshold = 0.0;
		real_t alpha_blur = 0.0;
		bool toon_enabled = false;
		real_t toon_dark = 0.5;
		real_t toon_light = 0.85;
		Color shadows_tint = Color(1, 1, 1, 1);
		Color midtones_tint = Color(1, 1, 1, 1);
		Color highlights_tint = Color(1, 1, 1, 1);
		bool normal_map = false;
		real_t normal_height_scale = 3.0;
		bool show_alpha = false;
		Vector<uint8_t> bytes;
		uint8_t *buffer = nullptr;
		uint64_t start_usec = 0;
		uint64_t render_usec = 0;
	};
	TexturePreviewJob texture_preview_job;
	TexturePreviewJob texture_preview_pending;
	bool texture_preview_has_pending = false;
	bool texture_preview_needs_refine = false;
	int64_t texture_preview_task = -1;
	int texture_preview_playback_size = 192;
	Ref<ImageTexture> texture_preview_texture;
	Ref<Image> generated_texture_image;
	Ref<Texture2D> generated_texture;
	String generated_texture_path;
	int generated_texture_frame_columns = 1;
	int generated_texture_fps = 12;
	int texture_generator_preview_frame = -1;
	double texture_generator_preview_refresh = 0.0;
	bool texture_generator_return_after_export = false;
	bool texture_generator_return_after_recipe = false;
	bool texture_generator_loading_recipe = false;
	ConfirmationDialog *particle_graph_confirmation = nullptr;
	ConfirmationDialog *replace_document_confirmation = nullptr;
	SubViewport *preview_viewport = nullptr;
	SubViewportContainer *preview_container = nullptr;
	Node3D *preview_root = nullptr;
	Node3D *document_host = nullptr;
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
	TextShaderEditor *particle_code_editor = nullptr;
	Button *play_button = nullptr;
	Button *restart_button = nullptr;
	HSlider *timeline = nullptr;
	SpinBox *speed_spin = nullptr;
	Label *time_label = nullptr;
	Label *selection_label = nullptr;
	FPSParticleModuleStack *module_stack = nullptr;
	FPSParticleTimeline *timeline_view = nullptr;
	Button *timeline_play_button = nullptr;
	Button *timeline_loop_button = nullptr;
	Label *timeline_time_label = nullptr;
	CheckButton *preview_loop_toggle = nullptr;
	ObjectID solo_emitter_id;
	Ref<Environment> preview_environment;
	DirectionalLight3D *preview_key_light = nullptr;
	DirectionalLight3D *preview_fill_light = nullptr;
	DirectionalLight3D *preview_rim_light = nullptr;
	Node3D *preview_floor_collision = nullptr;
	MenuButton *preview_view_menu = nullptr;
	Label *preview_stats_label = nullptr;
	Button *convert_shader_button = nullptr;
	Button *convert_graph_button = nullptr;
	Node *track_clipboard = nullptr; // Copied/cut track subtree, owned here.
	double preview_stats_accumulator = 0.0;
	int preview_environment_preset = 0;
	bool preview_show_bounds = false;
	bool preview_show_radii = true;
	bool preview_play_sound = false; // Off by default; the View menu choice is remembered.
	bool preview_play_shake = false;
	Node3D *preview_radii_root = nullptr;
	MeshInstance3D *preview_floor = nullptr;
	Control *module_stack_scroll = nullptr;
	TabContainer *properties_tabs = nullptr;
	TabContainer *bottom_tabs = nullptr;
	Control *left_panel_root = nullptr;
	PanelContainer *workspace_background = nullptr;
	PanelContainer *document_toolbar_surface = nullptr;
	PanelContainer *left_surface = nullptr;
	PanelContainer *preview_surface = nullptr;
	PanelContainer *right_surface = nullptr;
	Control *right_panel_root = nullptr;
	Control *preview_panel_root = nullptr;
	Control *bottom_panel_root = nullptr;
	WindowWrapper *bottom_window_wrapper = nullptr;
	MenuButton *layout_menu = nullptr;
	MenuButton *library_menu = nullptr;
	Control *library_panel_root = nullptr;
	Ref<Texture2D> track_color_icon;
	Button *bottom_maximize_button = nullptr;
	Button *bottom_float_button = nullptr;
	LocalVector<Button *> layout_icon_buttons;
	bool show_left_panel = true;
	bool show_right_panel = true;
	bool show_library_panel = false;
	bool show_preview_panel = true;
	bool show_bottom_panel = true;
	bool bottom_float_on_return = false;
	Rect2i bottom_window_rect; // Last rect of the detached Timeline & Logic window.
	int bottom_window_screen = -1;
	Size2i texture_generator_size; // Last size of the Texture Generator dialog.
	bool compact_layout = false;
	bool applying_workspace_theme = false;
	real_t wide_layout_min_width = 0.0;
	Button *duplicate_emitter_button = nullptr;
	Button *remove_emitter_button = nullptr;
	Button *reset_view_button = nullptr;
	Button *texture_generator_button = nullptr;
	Label *speed_label = nullptr;
	Button *modules_mode_button = nullptr;

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
	ObjectID preview_source_id; // Effect root the preview copy was made from.
	ObjectID particle_graph_shader_id;
	ObjectID particle_code_shader_id;
	int64_t effect_document_serial = 0; // Bumped per document; tags undo steps.
	bool document_refresh_queued = false;
	bool preview_sync_queued = false;
	bool module_stack_editing = false;
	double preview_loop_start = 0.0; // Timeline loop region (end <= start = none).
	double preview_loop_end = -1.0;
	bool preview_restart_queued = false; // Event metadata changed: replay at the current time.
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
	void _set_edited_emitter(Node *p_node, bool p_update_tree = true, bool p_force = false);
	void _create_effect_document(int p_template);
	void _add_emitter(int p_type);
	void _remove_selected_emitter();
	void _on_emitter_renamed();
	void _on_emitter_name_changed();
	void _rebuild_preview();
	void _clear_preview_scripts(Node *p_node);
	void _apply_preview_defaults(Node *p_node);
	void _sync_preview_properties();
	void _queue_preview_sync();
	void _flush_preview_sync();
	void _flush_document_refresh();
	void _update_document_header();
	void _set_preview_playing(bool p_playing);
	void _on_preview_gui_input(const Ref<InputEvent> &p_event);
	void _update_preview_camera();
	void _frame_preview();
	void _on_reset_view_pressed();
	void _on_grid_toggled(bool p_enabled);
	void _on_texture_generator_pressed();
	void _build_texture_type_list();
	void _on_texture_generator_type_selected();
	void _select_texture_type(int p_type, bool p_reset_params);
	void _rebuild_texture_param_controls();
	bool _uses_generator_normal_map() const;
	VFXTextureGenerator::Settings _make_texture_settings(double p_time, double p_life, int p_quality) const;
	void _on_texture_generator_resolution_selected(int p_index);
	void _on_texture_generator_output_selected(int p_index);
	void _on_texture_generator_seed_changed(double p_value);
	void _on_texture_generator_randomize_seed_pressed();
	void _on_texture_generator_fps_changed(double p_value);
	void _on_texture_generator_duration_changed(double p_value);
	void _on_texture_generator_animate_toggled(bool p_enabled);
	void _on_texture_generator_time_changed(double p_value);
	void _on_texture_generator_color_changed(const Color &p_color);
	void _on_texture_generator_processing_value_changed(double p_value);
	void _on_texture_generator_param_changed(double p_value, int p_index);
	void _on_texture_generator_reset_params_pressed();
	void _on_texture_generator_toggle_changed(bool p_enabled);
	void _on_texture_generator_reset_color_balance_pressed();
	void _on_texture_generator_alpha_preview_toggled(bool p_enabled);
	void _on_texture_generator_background_selected(int p_index);
	void _on_texture_generator_show_sheet_toggled(bool p_enabled);
	void _on_texture_generator_export_pressed();
	void _on_texture_generator_apply_pressed();
	void _on_texture_generator_close_pressed();
	void _show_texture_generator();
	void _on_properties_mode_toggled(bool p_pressed, bool p_advanced);
	void _restart_preview_at(double p_time);
	void _on_timeline_scrub(double p_time);
	void _on_timeline_emitter_selected(Object *p_emitter);
	void _on_timeline_track_action(Object *p_track, int p_action);
	void _on_timeline_play_toggle();
	void _apply_preview_solo();
	void _apply_preview_event_toggles();
	void _update_toolbar_icons();
	void _on_preview_view_menu_id(int p_id);
	void _set_preview_view_option(int p_id, bool p_checked);
	void _apply_preview_environment(int p_preset);
	void _update_preview_bounds();
	void _update_preview_stats();
	void _on_convert_shader_pressed();
	void _on_convert_graph_pressed();
	void _duplicate_emitter(Node *p_emitter);
	void _on_duplicate_emitter_pressed();
	void _copy_track(Node *p_track);
	void _cut_track(Node *p_track);
	void _paste_tracks(Node *p_after);
	bool _handle_track_clipboard_key(const Ref<InputEventKey> &p_key);
	void _on_emitter_tree_gui_input(const Ref<InputEvent> &p_event);
	void _set_emitter_visible(Object *p_emitter, bool p_visible);
	void _on_timeline_timing_committed(Object *p_emitter, double p_old_delay, double p_old_duration);
	void _apply_emitter_timing(Object *p_emitter, double p_delay, double p_duration);
	Node *_convert_to_effect_root(Node *p_root);
	void _assign_level_owners(Node *p_instance, Node *p_owner);
	void _create_preset_document(int p_preset);
	void _apply_preset_textures(const Vector<FPSParticlePresets::TextureRequest> &p_textures, bool p_bake);
	void _apply_preset_sounds(Node *p_node, bool p_bake);

	// Library / Content Browser thumbnails, rendered one at a time offscreen.
	struct ThumbnailJob {
		String key; // "preset:<id>" or the effect path (= the Library item metadata).
		String path;
		int preset = -1;
		bool write_cache = false; // Also write the editor's scene thumbnail (resthumb).
		int kind = 0; // AssetPreviewKind of a Content Browser file.
	};
	SubViewport *thumbnail_viewport = nullptr;
	Node3D *thumbnail_root = nullptr;
	Camera3D *thumbnail_camera = nullptr;
	ObjectID thumbnail_effect_id;
	Vector<ThumbnailJob> thumbnail_queue;
	HashSet<String> thumbnail_queued;
	HashMap<String, Ref<Texture2D>> thumbnails;
	Ref<Texture2D> _get_preset_thumbnail(int p_preset);
	Node *_instantiate_library_effect(const String &p_key);

	// Content Browser thumbnails and tooltip for particle effect scenes.
	Ref<EditorResourceTooltipPlugin> asset_tooltip_plugin;
	Ref<EditorResourcePreviewGenerator> shader_preview_generator;
	Ref<ConfigFile> asset_thumbnail_manifest; // Path -> file stamp of the rendered thumbnail.
	HashMap<String, uint64_t> asset_scan_mtimes;
	HashMap<String, int> asset_scan_kinds;
	bool asset_thumbnail_scan_queued = false;
	Node *_instantiate_preview_subject(const String &p_path, int p_kind);
	String _get_asset_thumbnail_stamp(const String &p_path) const;
	Ref<ConfigFile> _get_asset_thumbnail_manifest();
	void _record_asset_thumbnail(const String &p_path);
	void _scan_asset_thumbnails_dir(EditorFileSystemDirectory *p_dir);
	void _scan_asset_thumbnails();
	void _queue_asset_thumbnail_scan();

	// Hover preview of a Library item (menu or panel).
	static constexpr int HOVER_PREVIEW_SIZE = 280;
	PopupPanel *hover_preview_popup = nullptr;
	SubViewport *hover_preview_viewport = nullptr;
	Node3D *hover_preview_root = nullptr;
	Camera3D *hover_preview_camera = nullptr;
	Label *hover_preview_label = nullptr;
	Label *hover_preview_description = nullptr;
	ObjectID hover_preview_effect_id;
	String hover_key; // Library item under the mouse.
	String hover_shown_key; // Item the preview shows.
	Rect2 hover_anchor;
	double hover_elapsed = 0.0;
	double hover_play_time = 0.0;
	double hover_play_length = 1.0;
	void _ensure_hover_preview();
	String _get_library_hover(Rect2 &r_anchor) const;
	void _process_library_hover(double p_delta);
	void _show_hover_preview();
	void _hide_hover_preview();
	HashMap<String, uint64_t> thumbnail_mtimes;
	ThumbnailJob thumbnail_current;
	int thumbnail_frames = -1; // -1 = idle.
	bool library_thumbnails_visible = false;
	bool library_thumbnails_prepared = false;
	void _ensure_thumbnail_viewport();
	void _queue_thumbnail(const String &p_key, const String &p_path, int p_preset, bool p_write_cache, int p_kind = 0);
	void _queue_library_thumbnails();
	void _process_thumbnails();
	bool _start_thumbnail_job(const ThumbnailJob &p_job);
	void _finish_thumbnail_job();
	void _set_library_icon(const String &p_key, const Ref<Texture2D> &p_texture);
	Ref<Texture2D> _get_cached_effect_thumbnail(const String &p_path);
	Ref<Texture2D> _bake_preset_texture(const VFXTextureGenerator::Settings &p_settings, const String &p_file_name, int p_resolution, int p_columns, double p_duration);
	void _on_module_stack_property_edited(const String &p_property);
	void _on_module_stack_generate_texture();
	void _bake_level_particle_collision();
	void _add_track_node(Node3D *p_track);
	bool _can_drop_on_emitter_tree(const Point2 &p_point, const Variant &p_data) const;
	void _drop_on_emitter_tree(const Point2 &p_point, const Variant &p_data);
	String _get_auto_texture_path() const;
	bool _save_texture_recipe(const String &p_path);
	bool _load_texture_recipe(const String &p_path, bool p_report_errors);
	bool _export_generated_texture(const String &p_path);
	static void _texture_preview_row_func(void *p_userdata, uint32_t p_row);
	void _start_texture_preview_task();
	void _poll_texture_preview_task(bool p_wait = false);
	void _on_texture_recipe_save_pressed();
	void _on_texture_recipe_load_pressed();
	void _on_texture_recipe_save_path_selected(const String &p_path);
	void _on_texture_recipe_load_path_selected(const String &p_path);
	void _on_texture_recipe_dialog_visibility_changed();
	void _show_texture_generator_after_recipe();
	void _invalidate_texture_export();
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
	void _on_emitter_hierarchy_history_changed(Node *p_preferred_emitter, int64_t p_document_serial);
	void _on_save_scene_pressed();
	void _save_emitter_scene(const String &p_path);
	void _load_particle_scene(const String &p_path);
	void _on_texture_generator_visibility_changed();
	void _on_timeline_loop_region_changed(double p_start, double p_end);
	void _request_load_particle_scene(const String &p_path);
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
	void _on_layout_menu_id(int p_id);
	void _on_library_menu_id(int p_id);
	void _update_library_menu();
	void _open_library_entry(const String &p_entry);
	void _set_panel_visible(int p_panel, bool p_visible);
	void _apply_panel_visibility();
	void _update_layout_menu();
	void _reset_panel_layout();
	void _toggle_bottom_maximized();
	void _on_bottom_close_pressed();
	void _apply_compact_layout();
	void _apply_workspace_theme();
	void _update_event_radii();
	void _toggle_bottom_floating();
	void _on_bottom_window_visibility_changed(bool p_visible);
	void _on_bottom_tab_changed(int p_tab);
	void _show_bottom_tab(int p_tab);
	void _ensure_bottom_panel_share(real_t p_share);
	bool _is_bottom_floating() const;
	void _on_particle_graph_button_pressed();
	void _create_particle_graph();
	Ref<VisualShader> _get_particle_visual_shader(Node *p_emitter) const;
	Ref<Shader> _get_particle_code_shader(Node *p_emitter) const;
	void _hide_code_editor_files_toggle();
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

	// A Library entry or effect path ready to preview (Content Browser tooltip).
	Node *instantiate_preview_subject(const String &p_path, int p_kind) { return _instantiate_preview_subject(p_path, p_kind); }

	FPSStudioParticleEditorPlugin();
};
