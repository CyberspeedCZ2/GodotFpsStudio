/**************************************************************************/
/*  path_3d_editor_plugin.h                                               */
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
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "editor/plugins/editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_gizmos.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/path_3d.h"

class HBoxContainer;
class MenuButton;
class ConfirmationDialog;
class EditorUndoRedoManager;
class OptionButton;
class SpinBox;
class LineEdit;
class Label;
class CheckButton;
class PhysicsDirectSpaceState3D;
class HFlowContainer;

class Path3DGizmo : public EditorNode3DGizmo {
	GDCLASS(Path3DGizmo, EditorNode3DGizmo);

	// Map handle id to control point id and handle type.
	enum HandleType {
		HANDLE_TYPE_IN,
		HANDLE_TYPE_OUT,
		HANDLE_TYPE_TILT,
	};

	struct HandleInfo {
		int point_idx; // Index of control point.
		HandleType type; // Type of this handle.
	};

	Path3D *path = nullptr;
	int curve_point_count = 0;
	Ref<StandardMaterial3D> debug_material;
	mutable Vector3 original;
	mutable float orig_in_length;
	mutable float orig_out_length;

	// Index that should have swapped control points for achieving an outwards curve.
	int swapped_control_points_idx = -1;
	bool control_points_overlapped = false;

	// Cache information of secondary handles.
	Vector<HandleInfo> _secondary_handles_info;

	void _update_transform_gizmo();

public:
	virtual String get_handle_name(int p_id, bool p_secondary) const override;
	virtual Variant get_handle_value(int p_id, bool p_secondary) const override;
	virtual void set_handle(int p_id, bool p_secondary, Camera3D *p_camera, const Point2 &p_point) override;
	virtual void commit_handle(int p_id, bool p_secondary, const Variant &p_restore, bool p_cancel = false) override;

	virtual void redraw() override;
	Path3DGizmo(Path3D *p_path = nullptr);
};

class Path3DGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(Path3DGizmoPlugin, EditorNode3DGizmoPlugin);

	// Locking basis is meant to ensure a predictable behavior during translation of the curve points in "local space transform mode".
	// Without the locking, the gizmo/point, in "local space transform mode", wouldn't follow a straight path and would curve and twitch in an unpredictable way.
	HashMap<int, Basis> transformation_locked_basis;
	HashMap<ObjectID, Vector<int>> route_reachability;

protected:
	Ref<EditorNode3DGizmo> create_gizmo(Node3D *p_spatial) override;

public:
	virtual bool has_gizmo(Node3D *p_spatial) override;
	String get_gizmo_name() const override;
	void set_route_reachability(ObjectID p_path_id, const Vector<int> &p_states);
	void clear_route_reachability(ObjectID p_path_id);
	const Vector<int> *get_route_reachability(ObjectID p_path_id) const;

	virtual void redraw(EditorNode3DGizmo *p_gizmo) override;

	virtual int subgizmos_intersect_ray(const EditorNode3DGizmo *p_gizmo, Camera3D *p_camera, const Vector2 &p_point) const override;
	virtual Vector<int> subgizmos_intersect_frustum(const EditorNode3DGizmo *p_gizmo, const Camera3D *p_camera, const Vector<Plane> &p_frustum) const override;
	virtual Transform3D get_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id) const override;
	virtual void set_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id, Transform3D p_transform) override;
	virtual void commit_subgizmos(const EditorNode3DGizmo *p_gizmo, const Vector<int> &p_ids, const Vector<Transform3D> &p_restore, bool p_cancel = false) override;

	int get_priority() const override;
	Path3DGizmoPlugin();
};

class Path3DEditorPlugin : public EditorPlugin {
	GDCLASS(Path3DEditorPlugin, EditorPlugin);

	friend class Path3DGizmo;
	friend class Path3DGizmoPlugin;

	Ref<Path3DGizmoPlugin> path_3d_gizmo_plugin;

	HBoxContainer *topmenu_bar = nullptr;

	HFlowContainer *toolbar = nullptr;
	Vector<Control *> npc_route_controls;
	Button *curve_create = nullptr;
	Button *curve_edit = nullptr;
	Button *curve_edit_curve = nullptr;
	Button *curve_edit_tilt = nullptr;
	Button *curve_del = nullptr;
	Button *curve_closed = nullptr;
	Button *curve_clear_points = nullptr;
	MenuButton *handle_menu = nullptr;

	Button *create_curve_button = nullptr;
	ConfirmationDialog *clear_points_dialog = nullptr;
	OptionButton *npc_point_action = nullptr;
	SpinBox *npc_point_duration = nullptr;
	SpinBox *npc_corner_radius = nullptr;
	Button *npc_round_corner = nullptr;
	CheckButton *npc_route_navmesh_snap = nullptr;
	Button *npc_snap_route_to_navmesh = nullptr;
	LineEdit *npc_point_target = nullptr;
	Label *npc_route_selection_label = nullptr;
	Label *npc_route_reachability_label = nullptr;
	Button *npc_apply_point_action = nullptr;
	Button *npc_check_reachability = nullptr;

	enum Mode {
		MODE_CREATE,
		MODE_EDIT,
		MODE_EDIT_CURVE,
		MODE_EDIT_TILT,
		MODE_DELETE,
		ACTION_CLOSE
	};

	Path3D *path = nullptr;
	Vector<int> route_current_selection;

public:
	enum NPCPointAction {
		NPC_POINT_ACTION_MOVE,
		NPC_POINT_ACTION_RUN,
		NPC_POINT_ACTION_WAIT,
		NPC_POINT_ACTION_SIT,
		NPC_POINT_ACTION_LOOK_AT,
		NPC_POINT_ACTION_PLAY_ANIMATION,
		NPC_POINT_ACTION_INTERACT,
		NPC_POINT_ACTION_ATTACK,
		NPC_POINT_ACTION_TAKE_COVER,
		NPC_POINT_ACTION_MAX,
	};

private:

	void _update_theme();
	void _update_toolbar();
	void _update_route_controls();
	void _update_route_action_fields(int p_action_id);
	void _refresh_route_action_display();
	void _sync_route_point_actions();
	void _apply_route_point_action();
	void _round_route_corner();
	void _set_route_navmesh_snap_enabled(bool p_enabled);
	void _set_route_navmesh_snap_state(bool p_enabled);
	int _snap_route_to_navmesh();
	bool _snap_npc_route_point_to_navmesh(const Vector3 &p_world_position, Vector3 &r_snapped_position) const;
	bool _npc_route_segment_hits_static_collision(const Ref<Curve3D> &p_curve, int p_segment_index, const Transform3D &p_path_transform, PhysicsDirectSpaceState3D *p_space, int p_override_point = -1, const Vector3 &p_override_position = Vector3()) const;
	int _npc_route_adjacent_collision_count(const Ref<Curve3D> &p_curve, int p_point_index, const Transform3D &p_path_transform, PhysicsDirectSpaceState3D *p_space, int p_override_point = -1, const Vector3 &p_override_position = Vector3()) const;
	void _set_route_status(const String &p_text);
	void _queue_route_reachability_check(bool p_immediate = false);
	void _request_route_reachability_check();
	void _check_route_reachability();
	void _process_pending_route_waypoint_drag();
	void _route_curve_changed();
	Array _get_route_point_actions() const;
	Array _insert_route_point_action(const Array &p_actions, int p_index) const;
	Array _remove_route_point_action(const Array &p_actions, int p_index) const;
	void _add_route_point_action_undo(EditorUndoRedoManager *p_undo_redo, const Array &p_before, const Array &p_after);
	Vector<int> _get_selected_route_point_ids() const;
	bool _is_fps_npc_route() const;

	void _mode_changed(int p_mode);
	void _toggle_closed_curve();
	void _handle_option_pressed(int p_option);
	bool handle_clicked = false;
	bool mirror_handle_angle = true;
	bool mirror_handle_length = true;
	bool snap_to_collider = true;
	bool npc_route_snap_to_navmesh = true;
	bool pending_route_waypoint_drag = false;
	Vector<int> pending_route_waypoint_ids;
	Vector<Vector3> pending_route_waypoint_positions;
	uint64_t route_reachability_check_deadline_msec = 0;
	bool route_reachability_check_pending = false;

	void _create_curve();
	void _confirm_clear_points();
	void _clear_points();
	void _clear_curve_points();
	void _restore_curve_points(const PackedVector3Array &p_points, bool p_curve_was_closed);

	enum HandleOption {
		HANDLE_OPTION_ANGLE,
		HANDLE_OPTION_LENGTH,
		HANDLE_OPTION_SNAP_COLLIDER,
	};

protected:
	virtual void _notification(int p_what);
	static void _bind_methods();

public:
	Path3D *get_edited_path() { return path; }

	void update_handles();

	inline static Path3DEditorPlugin *singleton = nullptr;
	virtual EditorPlugin::AfterGUIInput forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) override;

	virtual String get_plugin_name() const override { return "Path3D"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;

	bool mirror_angle_enabled() { return mirror_handle_angle; }
	bool mirror_length_enabled() { return mirror_handle_length; }
	bool is_handle_clicked() { return handle_clicked; }
	void set_handle_clicked(bool clicked) { handle_clicked = clicked; }

	Path3DEditorPlugin();

private:
	struct EditData {
		Vector3 click_ray_dir;
		Vector3 click_ray_pos;
		Vector3 origin;
		Point2 mouse_pos;
		bool show_rotation_line = false;
		Ref<Path3DGizmo> gizmo;
		int gizmo_handle = 0;
		bool gizmo_handle_secondary = false;
		Camera3D *gizmo_camera;
		bool waiting_point_physics = false;
		bool waiting_handle_physics = false;
		bool in_physics_frame = false;
	} _edit;
};
