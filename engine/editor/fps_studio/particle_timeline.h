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

class PopupMenu;

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
	};

private:
	enum DragMode {
		DRAG_NONE,
		DRAG_SCRUB,
		DRAG_MOVE,
		DRAG_RESIZE,
		DRAG_PAN,
	};

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

	real_t _name_width() const;
	real_t _ruler_height() const;
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

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	static bool is_track(const Node *p_node);

	void set_effect(Node *p_effect);
	void set_selected_track(Node *p_node);
	void set_solo_track(Node *p_node);
	void set_current_time(double p_time);
	void set_snap_enabled(bool p_enabled);
	bool is_snap_enabled() const { return snap_enabled; }
	void zoom_in();
	void zoom_out();
	void fit_view();
	void refresh();

	virtual Size2 get_minimum_size() const override;
	virtual String get_tooltip(const Point2 &p_pos) const override;

	FPSParticleTimeline();
};
