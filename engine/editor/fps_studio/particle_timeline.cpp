/**************************************************************************/
/*  particle_timeline.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_timeline.h"

#include "core/input/input_event.h"
#include "core/object/class_db.h"
#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/resources/font.h"

static const double TIMELINE_SNAP = 0.05;

real_t FPSParticleTimeline::_name_width() const {
	return 150 * EDSCALE;
}

real_t FPSParticleTimeline::_ruler_height() const {
	return 22 * EDSCALE;
}

real_t FPSParticleTimeline::_row_height() const {
	return 26 * EDSCALE;
}

double FPSParticleTimeline::_x_to_time(real_t p_x) const {
	const real_t width = MAX(real_t(1.0), get_size().x - _name_width() - 8 * EDSCALE);
	return MAX(0.0, double(p_x - _name_width()) / double(width) * view_length);
}

real_t FPSParticleTimeline::_time_to_x(double p_time) const {
	const real_t width = MAX(real_t(1.0), get_size().x - _name_width() - 8 * EDSCALE);
	return _name_width() + real_t(p_time / view_length) * width;
}

int FPSParticleTimeline::_row_at(real_t p_y) const {
	if (p_y < _ruler_height()) {
		return -1;
	}
	const int row = int((p_y - _ruler_height()) / _row_height());
	return row < emitter_ids.size() ? row : -1;
}

Node *FPSParticleTimeline::_emitter_at_row(int p_row) const {
	if (p_row < 0 || p_row >= emitter_ids.size()) {
		return nullptr;
	}
	return Object::cast_to<Node>(ObjectDB::get_instance(emitter_ids[p_row]));
}

void FPSParticleTimeline::_collect(Node *p_node) {
	if (ParticleEffect3D::is_emitter(p_node)) {
		emitter_ids.push_back(p_node->get_instance_id());
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect(p_node->get_child(i));
	}
}

void FPSParticleTimeline::_update_view_length() {
	double total = 0.0;
	double max_start = 0.0;
	double max_lifetime = 0.0;
	bool infinite = false;
	for (const ObjectID &id : emitter_ids) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(id));
		if (!emitter) {
			continue;
		}
		double start, emission_end, visual_end;
		ParticleEffect3D::get_emitter_window(emitter, start, emission_end, visual_end);
		max_start = MAX(max_start, start);
		max_lifetime = MAX(max_lifetime, double(emitter->get("lifetime")));
		if (visual_end < 0.0) {
			infinite = true;
		} else {
			total = MAX(total, visual_end);
		}
	}
	if (infinite) {
		total = MAX(total, max_start + max_lifetime * 2.0 + 1.0);
	}
	view_length = MAX(2.0, total * 1.15);
}

void FPSParticleTimeline::set_effect(Node *p_effect) {
	effect_id = p_effect ? p_effect->get_instance_id() : ObjectID();
	refresh();
}

void FPSParticleTimeline::set_selected_emitter(Node *p_emitter) {
	selected_id = p_emitter ? p_emitter->get_instance_id() : ObjectID();
	queue_redraw();
}

void FPSParticleTimeline::set_current_time(double p_time) {
	if (Math::is_equal_approx(current_time, p_time)) {
		return;
	}
	current_time = p_time;
	queue_redraw();
}

void FPSParticleTimeline::refresh() {
	emitter_ids.clear();
	Node *effect = Object::cast_to<Node>(ObjectDB::get_instance(effect_id));
	if (effect) {
		_collect(effect);
	}
	if (drag_mode == DRAG_NONE) {
		_update_view_length();
	}
	update_minimum_size();
	queue_redraw();
}

Size2 FPSParticleTimeline::get_minimum_size() const {
	return Size2(320 * EDSCALE, _ruler_height() + MAX(1, emitter_ids.size()) * _row_height() + 4 * EDSCALE);
}

String FPSParticleTimeline::get_tooltip(const Point2 &p_pos) const {
	if (p_pos.y < _ruler_height()) {
		return TTR("Click or drag to scrub the preview.");
	}
	Node *emitter = _emitter_at_row(_row_at(p_pos.y));
	if (!emitter) {
		return String();
	}
	double start, emission_end, visual_end;
	ParticleEffect3D::get_emitter_window(emitter, start, emission_end, visual_end);
	const String emission = emission_end < 0.0 ? TTR("continuous") : vformat(TTR("%s s"), String::num(emission_end - start, 2));
	return vformat(TTR("%s\nStart delay: %s s\nEmission: %s\nLifetime: %s s\n\nDrag the bar to change the delay, drag its right edge to change the emission length (Shift disables snapping). Double-click resets the length."), emitter->get_name(), String::num(start, 2), emission, String::num(double(emitter->get("lifetime")), 2));
}

void FPSParticleTimeline::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		const Point2 pos = mb->get_position();
		if (mb->is_pressed()) {
			if (pos.y < _ruler_height() && pos.x >= _name_width()) {
				drag_mode = DRAG_SCRUB;
				emit_signal(SNAME("scrub_requested"), _x_to_time(pos.x));
				accept_event();
				return;
			}
			Node *emitter = _emitter_at_row(_row_at(pos.y));
			if (!emitter) {
				return;
			}
			emit_signal(SNAME("emitter_selected"), emitter);
			if (pos.x < _name_width()) {
				accept_event();
				return;
			}
			double start, emission_end, visual_end;
			ParticleEffect3D::get_emitter_window(emitter, start, emission_end, visual_end);
			const real_t x_start = _time_to_x(start);
			const real_t x_end = emission_end < 0.0 ? get_size().x : _time_to_x(emission_end);
			if (mb->is_double_click()) {
				if (ParticleEffect3D::get_emitter_duration(emitter) > 0.0) {
					const double old_duration = ParticleEffect3D::get_emitter_duration(emitter);
					ParticleEffect3D::set_emitter_duration(emitter, 0.0);
					emit_signal(SNAME("timing_committed"), emitter, ParticleEffect3D::get_emitter_delay(emitter), old_duration);
					refresh();
				}
				accept_event();
				return;
			}
			drag_emitter = emitter->get_instance_id();
			drag_start_time = _x_to_time(pos.x);
			drag_old_delay = ParticleEffect3D::get_emitter_delay(emitter);
			drag_old_duration = ParticleEffect3D::get_emitter_duration(emitter);
			if (emission_end >= 0.0 && Math::abs(pos.x - x_end) <= 6 * EDSCALE) {
				drag_mode = DRAG_RESIZE;
			} else if (pos.x >= x_start - 4 * EDSCALE && pos.x <= x_end + 4 * EDSCALE) {
				drag_mode = DRAG_MOVE;
			} else {
				drag_mode = DRAG_SCRUB;
				emit_signal(SNAME("scrub_requested"), _x_to_time(pos.x));
			}
			accept_event();
		} else {
			const DragMode finished = drag_mode;
			drag_mode = DRAG_NONE;
			Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(drag_emitter));
			drag_emitter = ObjectID();
			if ((finished == DRAG_MOVE || finished == DRAG_RESIZE) && emitter) {
				const bool changed = !Math::is_equal_approx(drag_old_delay, ParticleEffect3D::get_emitter_delay(emitter)) || !Math::is_equal_approx(drag_old_duration, ParticleEffect3D::get_emitter_duration(emitter));
				if (changed) {
					emit_signal(SNAME("timing_committed"), emitter, drag_old_delay, drag_old_duration);
				}
			}
			refresh();
		}
		return;
	}

	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid()) {
		const Point2 pos = mm->get_position();
		const double time = _x_to_time(pos.x);
		const bool snap = !mm->is_shift_pressed();
		auto snapped = [&](double p_value) {
			return snap ? Math::snapped(p_value, TIMELINE_SNAP) : p_value;
		};
		if (drag_mode == DRAG_SCRUB) {
			emit_signal(SNAME("scrub_requested"), MIN(time, view_length));
			accept_event();
			return;
		}
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(drag_emitter));
		if (drag_mode == DRAG_MOVE && emitter) {
			ParticleEffect3D::set_emitter_delay(emitter, MAX(0.0, snapped(drag_old_delay + time - drag_start_time)));
			queue_redraw();
			accept_event();
			return;
		}
		if (drag_mode == DRAG_RESIZE && emitter) {
			const double delay = ParticleEffect3D::get_emitter_delay(emitter);
			ParticleEffect3D::set_emitter_duration(emitter, MAX(TIMELINE_SNAP, snapped(time - delay)));
			queue_redraw();
			accept_event();
			return;
		}
		// Hover feedback: resize cursor on the emission end handle.
		const int row = _row_at(pos.y);
		CursorShape shape = CURSOR_ARROW;
		Node *hover = _emitter_at_row(row);
		if (hover && pos.x >= _name_width()) {
			double start, emission_end, visual_end;
			ParticleEffect3D::get_emitter_window(hover, start, emission_end, visual_end);
			if (emission_end >= 0.0 && Math::abs(pos.x - _time_to_x(emission_end)) <= 6 * EDSCALE) {
				shape = CURSOR_HSIZE;
			} else if (pos.x >= _time_to_x(start) && (emission_end < 0.0 || pos.x <= _time_to_x(emission_end))) {
				shape = CURSOR_DRAG;
			}
		}
		set_default_cursor_shape(shape);
		if (row != hover_row) {
			hover_row = row;
			queue_redraw();
		}
	}
}

void FPSParticleTimeline::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_MOUSE_EXIT: {
			hover_row = -1;
			queue_redraw();
		} break;
		case NOTIFICATION_DRAW: {
			const Size2 size = get_size();
			const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
			const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
			const Color font_color = get_theme_color(SceneStringName(font_color), SNAME("Label"));
			const Color dim = Color(font_color, 0.45);
			const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
			const Color base = get_theme_color(SNAME("dark_color_2"), EditorStringName(Editor));
			draw_rect(Rect2(Point2(), size), base);

			// Ruler with a tick spacing of at least ~60 px.
			static const double steps[] = { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0 };
			const real_t px_per_second = (size.x - _name_width() - 8 * EDSCALE) / real_t(view_length);
			double step = steps[8];
			for (double candidate : steps) {
				if (candidate * px_per_second >= 60 * EDSCALE) {
					step = candidate;
					break;
				}
			}
			draw_rect(Rect2(0, 0, size.x, _ruler_height()), Color(0, 0, 0, 0.2));
			for (double t = 0.0; t <= view_length + 0.0001; t += step) {
				const real_t x = _time_to_x(t);
				draw_line(Point2(x, _ruler_height() * 0.55), Point2(x, size.y), Color(font_color, 0.08));
				draw_string(font, Point2(x + 3 * EDSCALE, _ruler_height() * 0.72), String::num(t, step < 0.1 ? 2 : (step < 1.0 ? 1 : 0)) + "s", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.85, dim);
			}

			if (emitter_ids.is_empty()) {
				draw_string(font, Point2(_name_width(), _ruler_height() + _row_height() * 0.68), TTR("Open or create an effect to see its emitters here."), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, dim);
			}
			for (int row = 0; row < emitter_ids.size(); row++) {
				Node *emitter = _emitter_at_row(row);
				if (!emitter) {
					continue;
				}
				const real_t y = _ruler_height() + row * _row_height();
				const bool selected = emitter->get_instance_id() == selected_id;
				if (selected) {
					draw_rect(Rect2(0, y, size.x, _row_height()), Color(accent, 0.16));
				} else if (row == hover_row) {
					draw_rect(Rect2(0, y, size.x, _row_height()), Color(font_color, 0.05));
				}
				draw_line(Point2(0, y + _row_height()), Point2(size.x, y + _row_height()), Color(font_color, 0.06));
				const Color row_color = Color::from_hsv(Math::fposmod(0.58 + row * 0.13, 1.0), 0.55, 0.9);
				draw_rect(Rect2(6 * EDSCALE, y + _row_height() * 0.3, 4 * EDSCALE, _row_height() * 0.4), row_color);
				draw_string(font, Point2(16 * EDSCALE, y + _row_height() * 0.68), emitter->get_name(), HORIZONTAL_ALIGNMENT_LEFT, _name_width() - 20 * EDSCALE, font_size, selected ? font_color : Color(font_color, 0.8));

				double start, emission_end, visual_end;
				ParticleEffect3D::get_emitter_window(emitter, start, emission_end, visual_end);
				const real_t bar_top = y + _row_height() * 0.2;
				const real_t bar_height = _row_height() * 0.6;
				const real_t x_start = _time_to_x(start);
				const real_t x_end = emission_end < 0.0 ? size.x - 2 * EDSCALE : _time_to_x(emission_end);
				const real_t x_visual = visual_end < 0.0 ? size.x - 2 * EDSCALE : _time_to_x(visual_end);
				// Lifetime tail: particles still alive after emission ends.
				if (x_visual > x_end) {
					draw_rect(Rect2(x_end, bar_top + bar_height * 0.25, x_visual - x_end, bar_height * 0.5), Color(row_color, 0.25));
				}
				draw_rect(Rect2(x_start, bar_top, MAX(real_t(3.0), x_end - x_start), bar_height), Color(row_color, selected ? 0.95 : 0.75));
				if (emission_end < 0.0) {
					draw_string(font, Point2(x_end - 18 * EDSCALE, bar_top + bar_height * 0.78), U"∞", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0, 0, 0, 0.7));
				} else {
					draw_rect(Rect2(x_end - 2 * EDSCALE, bar_top, 4 * EDSCALE, bar_height), Color(1, 1, 1, 0.55));
				}
			}

			// Playhead.
			const real_t x_play = _time_to_x(CLAMP(current_time, 0.0, view_length));
			draw_line(Point2(x_play, 0), Point2(x_play, size.y), Color(1.0, 0.35, 0.3), 2 * EDSCALE);
			draw_line(Point2(_name_width(), 0), Point2(_name_width(), size.y), Color(font_color, 0.12));
		} break;
	}
}

void FPSParticleTimeline::_bind_methods() {
	ADD_SIGNAL(MethodInfo("scrub_requested", PropertyInfo(Variant::FLOAT, "time")));
	ADD_SIGNAL(MethodInfo("emitter_selected", PropertyInfo(Variant::OBJECT, "emitter")));
	ADD_SIGNAL(MethodInfo("timing_committed", PropertyInfo(Variant::OBJECT, "emitter"), PropertyInfo(Variant::FLOAT, "old_delay"), PropertyInfo(Variant::FLOAT, "old_duration")));
}

FPSParticleTimeline::FPSParticleTimeline() {
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_clip_contents(true);
}
