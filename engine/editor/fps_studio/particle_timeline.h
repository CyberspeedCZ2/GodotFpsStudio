/**************************************************************************/
/*  particle_timeline.h                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/object/object_id.h"
#include "core/templates/vector.h"
#include "scene/gui/control.h"

class LineEdit;
class PopupMenu;
class PopupPanel;

// Niagara-style effect timeline: one track per emitter (and per timed event
// node) of a ParticleEffect3D with its start delay, active window and
// particle lifetime tail. Supports zoom/pan, scrubbing, visibility and solo
// toggles, a context menu and keyboard shortcuts.
class FPSParticleTimeline : public Control {
	GDCLASS(FPSParticleTimeline, Control);

public:
	// Color of a track row; shared with the emitter list so both read alike.
	static Color get_track_color(Node *p_track, int p_row);

	enum TrackAction {
		ACTION_DUPLICATE,
		ACTION_RENAME,
		ACTION_RESET_TIMING,
		ACTION_DELETE,
		ACTION_SOLO,
		ACTION_TOGGLE_VISIBLE,
		ACTION_COPY,
		ACTION_CUT,
		ACTION_PASTE, // Track may be null (paste into the effect).
		ACTION_MOVE_UP,
		ACTION_MOVE_DOWN,
	};

private:
	enum DragMode {
		DRAG_NONE,
		DRAG_SCRUB,
		DRAG_MOVE,
		DRAG_RESIZE,
		DRAG_PAN,
		DRAG_LOOP,
		DRAG_START, // The playback start flag on the ruler.
		DRAG_END, // The playback end flag.
		DRAG_MARKER, // A marker in the ruler (moves once dragged a few pixels).
		DRAG_REORDER, // A track name dragged up / down to change the order.
	};
	void _set_playback_start(double p_time);
	bool _is_on_start_flag(const Point2 &p_pos) const;
	void _set_playback_end(double p_time);
	double _snap_to_edges(double p_time, bool p_free) const;
	bool _is_on_end_flag(const Point2 &p_pos) const;
	enum RulerAction {
		RULER_LOOP_START = 100,
		RULER_LOOP_END,
		RULER_LOOP_CLEAR,
		RULER_ADD_MARKER,
		RULER_DELETE_ALL_MARKERS,
		MARKER_RENAME = 200,
		MARKER_DELETE,
		MARKER_COLOR_FIRST = 220, // + index into the marker palette.
	};

	// Named, colored points on the ruler (like markers in a video editor),
	// saved with the effect as root metadata so they travel with the file.
	struct Marker {
		double time = 0.0;
		String name;
		int color = 0;
	};
	Vector<Marker> markers;
	int drag_marker = -1;
	bool drag_marker_moved = false;
	real_t drag_marker_press_x = 0.0;
	Array drag_marker_old;
	int context_marker = -1;
	int selected_marker = -1; // Clicked marker: Delete removes it.
	mutable Vector<Rect2> marker_label_rects; // Names in the ruler, clickable too.
	void _delete_marker(int p_index);
	real_t drag_reorder_press_y = 0.0;
	bool drag_reorder_active = false;
	int drag_reorder_gap = -1; // Insert before this row (== row count: at the end).
	PopupPanel *rename_popup = nullptr;
	LineEdit *rename_edit = nullptr;
	int rename_marker = -1;

	void _load_markers();
	Array _markers_to_array() const;
	void _store_markers(const Array &p_old);
	int _marker_at(const Point2 &p_pos) const;
	void _open_marker_rename(int p_index);
	void _on_marker_rename_submitted(const String &p_text);
	void _jump(int p_dir, bool p_markers_only);
	void _draw_life_curves(Node *p_track, double p_start, real_t p_row_y, float p_alpha);

	ObjectID effect_id;
	ObjectID selected_id;
	ObjectID solo_id;
	Vector<ObjectID> track_ids;
	double current_time = 0.0;
	double view_start = 0.0;
	double view_length = 3.0;
	double content_length = 3.0;
	bool user_zoomed = false;
	bool snap_enabled = true;

	DragMode drag_mode = DRAG_NONE;
	ObjectID drag_track;
	double drag_start_time = 0.0;
	double drag_old_delay = 0.0;
	double drag_old_duration = 0.0;
	real_t drag_pan_origin_x = 0.0;
	double drag_pan_origin_start = 0.0;
	int hover_row = -1;
	PopupMenu *context_menu = nullptr;
	ObjectID context_track;
	bool can_paste = false; // The editor has tracks on its clipboard.
	// Preview loop region (editor only, not saved in the effect); end < 0 = none.
	double loop_start = 0.0;
	double loop_end = -1.0;
	double drag_loop_anchor = 0.0;
	double context_time = 0.0;

	real_t _name_width() const;
	real_t _ruler_height() const;
	real_t _ruler_top() const;
	bool _in_ruler(real_t p_y) const;
	real_t _row_height() const;
	real_t _timeline_width() const;
	double _x_to_time(real_t p_x) const;
	real_t _time_to_x(double p_time) const;
	int _row_at(real_t p_y) const;
	Node *_track_at_row(int p_row) const;
	Rect2 _visibility_rect(int p_row) const;
	Rect2 _solo_rect(int p_row) const;
	void _collect(Node *p_node);
	void _update_content_length();
	void _zoom_at(double p_factor, real_t p_x);
	void _clamp_view();
	double _snap(double p_value, bool p_shift) const;
	void _on_context_menu_id(int p_id);
	void _set_loop_region_internal(double p_start, double p_end);

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	static bool is_track(const Node *p_node);

	void set_effect(Node *p_effect);
	void set_selected_track(Node *p_node);
	void set_solo_track(Node *p_node);
	void set_can_paste(bool p_can_paste) { can_paste = p_can_paste; }
	void set_current_time(double p_time);
	void set_snap_enabled(bool p_enabled);
	bool is_snap_enabled() const { return snap_enabled; }
	void zoom_in();
	void zoom_out();
	void fit_view();
	void refresh();
	// Loop region of the preview; p_end < 0 clears it. Emits loop_region_changed.
	void set_loop_region(double p_start, double p_end);
	bool has_loop_region() const { return loop_end > loop_start; }
	// Where Play and Restart begin: the loop region start, else 0.
	double get_playback_start() const { return has_loop_region() ? loop_start : 0.0; }
	// Where playback ends (and loops back to the start): the loop region end, else the effect's end.
	double get_playback_end() const { return has_loop_region() ? loop_end : content_length; }
	double get_loop_start() const { return loop_start; }
	double get_loop_end() const { return loop_end; }
	// Markers: M adds one at the playhead. Up / Down jump to the previous /
	// next edge (track start or end, flag, marker); Shift: markers only.
	static Color get_marker_color(int p_index);
	void add_marker_at_playhead();
	double get_jump_time(int p_dir, bool p_markers_only) const;
	void jump(int p_dir, bool p_markers_only) { _jump(p_dir, p_markers_only); }
	// Markers sorted by time: [{time, name, color}]; 1-9 jump to them.
	Array get_markers() const { return _markers_to_array(); }
	void jump_to_marker(int p_index);
	void delete_all_markers() { _on_context_menu_id(RULER_DELETE_ALL_MARKERS); }

	virtual Size2 get_minimum_size() const override;
	virtual String get_tooltip(const Point2 &p_pos) const override;

	FPSParticleTimeline();
};
