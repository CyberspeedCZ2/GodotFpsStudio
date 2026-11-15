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

// Niagara-style emitter timeline: one row per emitter of a ParticleEffect3D
// with its start delay, emission window and particle lifetime tail. Bars can
// be dragged to change the delay; the right edge changes the emission
// duration. Clicking the ruler scrubs the preview.
class FPSParticleTimeline : public Control {
	GDCLASS(FPSParticleTimeline, Control);

	enum DragMode {
		DRAG_NONE,
		DRAG_SCRUB,
		DRAG_MOVE,
		DRAG_RESIZE,
	};

	ObjectID effect_id;
	ObjectID selected_id;
	Vector<ObjectID> emitter_ids;
	double current_time = 0.0;
	double view_length = 3.0;

	DragMode drag_mode = DRAG_NONE;
	ObjectID drag_emitter;
	double drag_start_time = 0.0;
	double drag_old_delay = 0.0;
	double drag_old_duration = 0.0;
	int hover_row = -1;

	real_t _name_width() const;
	real_t _ruler_height() const;
	real_t _row_height() const;
	double _x_to_time(real_t p_x) const;
	real_t _time_to_x(double p_time) const;
	int _row_at(real_t p_y) const;
	Node *_emitter_at_row(int p_row) const;
	void _collect(Node *p_node);
	void _update_view_length();

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	static void _bind_methods();

public:
	void set_effect(Node *p_effect);
	void set_selected_emitter(Node *p_emitter);
	void set_current_time(double p_time);
	double get_view_length() const { return view_length; }
	void refresh();

	virtual Size2 get_minimum_size() const override;
	virtual String get_tooltip(const Point2 &p_pos) const override;

	FPSParticleTimeline();
};
