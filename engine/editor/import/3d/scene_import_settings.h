/**************************************************************************/
/*  scene_import_settings.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,          */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                  */
/**************************************************************************/

#pragma once

#include "editor/import/3d/resource_importer_scene.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/gui/control.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/slider.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sky_material.h"

class EditorFileDialog;
class EditorInspector;
class SceneImportSettingsData;
class Timer;

// Full-size workspace screen (the "Model" main editor tab) hosting the
// advanced scene import settings. Formerly a modal ConfirmationDialog
// (SceneImportSettingsDialog); now embedded in the editor main screen so
// the model preview viewport and inspector get the entire editor area.
class SceneImportSettingsScreen : public Control {
	GDCLASS(SceneImportSettingsScreen, Control)

	static SceneImportSettingsScreen *singleton;

	enum Actions {
		ACTION_EXTRACT_MATERIALS,
		ACTION_CHOOSE_MESH_SAVE_PATHS,
		ACTION_CHOOSE_ANIMATION_SAVE_PATHS,
	};

	Node *scene = nullptr;

	HSplitContainer *tree_split = nullptr;
	HSplitContainer *property_split = nullptr;
	TabContainer *data_mode = nullptr;
	Tree *scene_tree = nullptr;
	Tree *mesh_tree = nullptr;
	Tree *material_tree = nullptr;

	EditorInspector *inspector = nullptr;

	SubViewport *base_viewport = nullptr;

	Camera3D *camera = nullptr;
	Ref<CameraAttributesPractical> camera_attributes;
	Ref<Environment> environment;
	Ref<Sky> sky;
	Ref<ProceduralSkyMaterial> procedural_sky_material;
	bool first_aabb = false;
	AABB contents_aabb;

	// Skeleton gizmo extents are kept separate from `contents_aabb` so debug
	// bone lines never shrink the framed model; used only as a fallback when
	// the file has no visible mesh content (animation-only imports).
	bool has_skeleton_aabb = false;
	AABB skeleton_aabb;

	Button *light_1_switch = nullptr;
	Button *light_2_switch = nullptr;
	Button *light_rotate_switch = nullptr;
	Button *frame_button = nullptr;
	Button *skeleton_visibility_toggle = nullptr;

	Label *file_label = nullptr;
	PanelContainer *empty_hint_panel = nullptr;

	// Mesh statistics overlay (bottom-right of the preview), shown while a
	// model is loaded. Reports the original polygon count of the whole model
	// (or of the mesh selected in the Mesh tab) and the total triangle count
	// of every generated LOD level (LOD 0 = full detail).
	PanelContainer *mesh_stats_panel = nullptr;
	Label *mesh_stats_label = nullptr;
	bool mesh_stats_dirty = true;
	uint64_t stats_base_faces = 0;
	uint64_t stats_base_indices = 0;
	// Aggregated original polygon (quad/n-gon) counts from the source files
	// ("source_face_count" mesh metadata). stats_has_source_faces is false
	// when at least one mesh has no source polygon data available.
	bool stats_has_source_faces = true;
	uint64_t stats_source_faces = 0;
	uint64_t stats_active_faces = 0;
	uint64_t stats_active_indices = 0;
	int stats_lod_min = 0;
	int stats_lod_max = 0;
	int stats_lod_count = 0;
	bool stats_mixed_lod = false;
	bool stats_has_any_lods = false;
	// Aggregated triangle count of every generated LOD level (index 0 = full
	// detail) across the currently previewed meshes, and the unique vertex
	// count of the active level; for the statistics overlay.
	Vector<uint64_t> stats_level_faces;
	uint64_t stats_active_vertices = 0;

	// LOD preview state (toolbar controls): automatic renderer-driven
	// switching with a user-adjustable bias on the screen-space threshold,
	// or a manual level picker that swaps the preview meshes for the actual
	// decimated LOD geometry so any level can be inspected up close.
	enum LodPreviewMode {
		LOD_MODE_AUTO,
		LOD_MODE_MANUAL,
	};
	
	int lod_mode = LOD_MODE_AUTO;
	int lod_manual_level = 0;
	float lod_bias = 1.0;
	// Highest LOD level currently offered by the level picker (-1 = needs a
	// rebuild); guards against repopulating the list on every camera move.
	int lod_picker_levels = -1;

	// LOD picker in the preview viewport toolbar (UE5-style): an "Auto"
	// toggle with the switching bias, plus one flat button per level.
	Button *lod_auto_toggle = nullptr;
	HBoxContainer *lod_buttons_container = nullptr;
	Vector<Button *> lod_level_buttons;
	HSlider *lod_bias_slider = nullptr;
	
	// LOD Settings Panel (per-level configuration)
	VBoxContainer *lod_settings_panel = nullptr;
	struct LODLevelSettings {
		int level = 0;
		float screen_size = 1.0f;        // 0.0 - 1.0 (threshold)
		float distance = 0.0f;           // Estimated distance in meters
		bool enabled = true;
		uint64_t triangle_count = 0;
		uint64_t vertex_count = 0;
		float reduction_percent = 0.0f;  // % reduction vs LOD 0
	};
	Vector<LODLevelSettings> lod_level_settings;
	// Live widgets of the LOD settings cards so slider edits can update the
	// distance labels without rebuilding the whole panel.
	Vector<Label *> lod_distance_labels;
	Vector<Button *> lod_card_select_buttons;
	// Nodes whose mesh was swapped for a manual LOD preview mesh, mapped to
	// their original mesh so switching back to Auto can restore them.
	HashMap<MeshInstance3D *, Ref<Mesh>> lod_swapped_meshes;
	// Manual LOD preview meshes per source mesh (cleared whenever the pinned
	// level changes).
	HashMap<Ref<Mesh>, Ref<Mesh>> lod_preview_mesh_cache;

	// Cached per-mesh surface geometry so the active LOD can be re-evaluated
	// on every camera/viewport change without touching the RenderingServer.
	struct MeshSurfaceStats {
		Mesh::PrimitiveType primitive = Mesh::PRIMITIVE_TRIANGLES;
		uint64_t vertex_count = 0;
		uint64_t index_count = 0;
		// LOD levels in storage order (ascending edge length), mirroring the
		// renderer's LOD selection loop semantics. Edge lengths are kept as
		// doubles to match the exact Variant dictionary keys from the mesh.
		Vector<uint64_t> lod_index_counts;
		Vector<double> lod_edge_lengths;
		// Unique source vertices referenced by each LOD level (level 0 uses
		// the full vertex array).
		Vector<uint64_t> lod_vertex_counts;
	};
	struct MeshStats {
		Vector<MeshSurfaceStats> surfaces;
		// Aggregated full-detail totals of this mesh.
		uint64_t base_faces = 0;
		uint64_t base_indices = 0;
		// Original polygon (quad/n-gon) count from the source file, when the
		// importer stored it as "source_face_count" mesh metadata.
		bool has_source_faces = false;
		uint64_t source_faces = 0;
		int max_lod_count = 0;
		bool has_any_lods = false;
	};
	HashMap<Ref<Mesh>, MeshStats> mesh_stats_map;

	// Statistics of the mesh currently selected in the Mesh tab (per-mesh
	// preview mode), cached so camera moves don't re-fetch mesh surface
	// arrays on every frame. Kept separate from mesh_stats_map (which only
	// holds meshes of visible scene nodes) so hidden meshes never leak into
	// the scene-wide LOD evaluation.
	Ref<Mesh> stats_selected_mesh;
	MeshStats stats_selected;

	Button *reimport_button = nullptr;
	Button *close_button = nullptr;

	struct ThemeCache {
		Ref<Texture2D> light_1_icon;
		Ref<Texture2D> light_2_icon;
		Ref<Texture2D> rotate_icon;
	} theme_cache;

	DirectionalLight3D *light1 = nullptr;
	DirectionalLight3D *light2 = nullptr;
	Ref<ArrayMesh> selection_mesh;
	MeshInstance3D *node_selected = nullptr;

	MeshInstance3D *mesh_preview = nullptr;
	Ref<SphereMesh> material_preview;

	AnimationPlayer *animation_player = nullptr;
	List<Skeleton3D *> skeletons;
	PanelContainer *animation_preview = nullptr;
	HSlider *animation_slider = nullptr;
	Button *animation_play_button = nullptr;
	Button *animation_stop_button = nullptr;
	Button *animation_toggle_skeleton_visibility = nullptr;
	Animation::LoopMode animation_loop_mode = Animation::LOOP_NONE;
	bool animation_pingpong = false;
	bool previous_import_as_skeleton = false;
	bool previous_rest_as_reset = false;
	MeshInstance3D *bones_mesh_preview = nullptr;

	Ref<StandardMaterial3D> collider_mat;

	float cam_rot_x = 0.0f;
	float cam_rot_y = 0.0f;
	float cam_zoom = 0.0f;
	Vector2 cam_pan;
	// Size of the currently framed content, used to scale pan/zoom navigation.
	float frame_size = 1.0f;

	void _update_scene();

	struct MaterialData {
		bool has_import_id;
		Ref<Material> material;
		TreeItem *scene_node = nullptr;
		TreeItem *mesh_node = nullptr;
		TreeItem *material_node = nullptr;

		float cam_rot_x = -Math::PI / 4;
		float cam_rot_y = -Math::PI / 4;
		float cam_zoom = 1;
		Vector2 cam_pan;

		HashMap<StringName, Variant> settings;
	};
	HashMap<String, MaterialData> material_map;
	HashMap<Ref<Material>, String> unnamed_material_name_map;

	struct MeshData {
		bool has_import_id;
		Ref<Mesh> mesh;
		TreeItem *scene_node = nullptr;
		TreeItem *mesh_node = nullptr;

		float cam_rot_x = -Math::PI / 4;
		float cam_rot_y = -Math::PI / 4;
		float cam_zoom = 1;
		Vector2 cam_pan;
		HashMap<StringName, Variant> settings;
	};
	HashMap<String, MeshData> mesh_map;

	struct AnimationData {
		Ref<Animation> animation;
		TreeItem *scene_node = nullptr;
		HashMap<StringName, Variant> settings;
	};
	HashMap<String, AnimationData> animation_map;

	struct NodeData {
		Node *node = nullptr;
		TreeItem *scene_node = nullptr;
		HashMap<StringName, Variant> settings;
	};
	HashMap<String, NodeData> node_map;

	bool _get_current(const StringName &p_name, Variant &r_ret) const;
	void _set_default(const StringName &p_name, const Variant &p_value);
	void _fill_material(Tree *p_tree, const Ref<Material> &p_material, TreeItem *p_parent);
	void _fill_mesh(Tree *p_tree, const Ref<Mesh> &p_mesh, TreeItem *p_parent);
	void _fill_animation(Tree *p_tree, const Ref<Animation> &p_anim, const String &p_name, TreeItem *p_parent);
	void _fill_scene(Node *p_node, TreeItem *p_parent_item);

	HashSet<Ref<Mesh>> mesh_set;

	String selected_type;
	String selected_id;

	bool selecting = false;

	void _update_view_gizmos();
	void _update_camera();
	void _gather_mesh_stats();
	bool _gather_mesh_stats_for_mesh(const Ref<Mesh> &p_mesh, MeshStats &r_stats);
	void _update_mesh_stats();
	void _apply_lod_preview();
	Ref<Mesh> _get_lod_preview_mesh(const Ref<Mesh> &p_mesh, int p_level);
	void _lod_bias_changed(double p_value);
	void _generate_preview_lods(Node *p_node);

	// LOD picker (viewport toolbar) methods.
	void _update_lod_toolbar_buttons(int p_max_level);
	void _update_lod_button_states();
	void _on_lod_level_selected(int p_level);
	void _on_lod_auto_toggled(bool p_enabled);
	
	// LOD Inspector Panel methods
	void _build_lod_settings_panel();
	void _update_lod_settings_panel();
	void _gather_lod_level_settings();
	void _on_lod_screen_size_changed(float p_value, int p_level);
	float _calculate_distance_from_screen_size(float p_screen_size);
	void _select(Tree *p_from, const String &p_type, const String &p_id);
	void _inspector_property_edited(const String &p_name);
	void _reset_bone_transforms();
	void _play_animation();
	void _stop_current_animation();
	void _reset_animation(const String &p_animation_name = "");
	void _animation_slider_value_changed(double p_value);
	void _animation_finished(const StringName &p_name);
	void _update_skeleton_preview_visibility();
	void _animation_update_skeleton_visibility();
	void _material_tree_selected();
	void _mesh_tree_selected();
	void _scene_tree_selected();
	void _skeleton_tree_entered(Skeleton3D *p_skeleton);
	void _cleanup();
	void _on_light_1_switch_pressed();
	void _on_light_2_switch_pressed();
	void _on_light_rotate_switch_pressed();

	void _get_active_camera_ptrs(float **r_rot_x, float **r_rot_y, float **r_zoom, Vector2 **r_pan);
	void _reset_camera();
	void _viewport_input(const Ref<InputEvent> &p_input);

	void _update_empty_state();
	void _close_screen();
	void _reimport_button_pressed();

	HashMap<StringName, Variant> defaults;

	SceneImportSettingsData *scene_import_settings_data = nullptr;
	Ref<ResourceImporterScene> _resource_importer_scene;

	void _re_import();

	String base_path;
	String current_import_type = "PackedScene";

	MenuButton *action_menu = nullptr;

	ConfirmationDialog *external_paths = nullptr;
	Tree *external_path_tree = nullptr;
	EditorFileDialog *save_path = nullptr;
	OptionButton *external_extension_type = nullptr;

	EditorFileDialog *item_save_path = nullptr;

	void _menu_callback(int p_id);
	void _save_dir_callback(const String &p_path);

	int current_action = 0;

	Vector<TreeItem *> save_path_items;

	TreeItem *save_path_item = nullptr;
	void _save_path_changed(const String &p_path);
	void _browse_save_callback(Object *p_item, int p_column, int p_id, MouseButton p_button);
	void _save_dir_confirm();

	Dictionary base_subresource_settings;

	void _load_default_subresource_settings(HashMap<StringName, Variant> &settings, const String &p_type, const String &p_import_id, ResourceImporterScene::InternalImportCategory p_category);

	bool generate_collider = false;

	Timer *update_view_timer = nullptr;

protected:
	virtual void _update_theme_item_cache() override;
	void _notification(int p_what);

public:
	const Ref<ResourceImporterScene> &get_resource_importer_scene() const { return _resource_importer_scene; }
	void request_generate_collider();
	void update_view();
	void open_settings(const String &p_path, const String &p_scene_import_type = "PackedScene");
	static SceneImportSettingsScreen *get_singleton();
	Node *get_selected_node();
	SceneImportSettingsScreen();
	~SceneImportSettingsScreen();
};
