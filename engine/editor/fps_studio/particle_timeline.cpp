/**************************************************************************/
/*  particle_timeline.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_timeline.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/gui/popup_menu.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box_flat.h"

static const double TIMELINE_SNAP = 0.05;
static const double TIMELINE_MIN_VIEW = 0.1;

Color FPSParticleTimeline::get_track_color(Node *p_track, int p_row) {
	if (p_track && !ParticleEffect3D::is_emitter(p_track)) {
		return Color(0.95, 0.78, 0.35);
	}
	return Color::from_hsv(Math::fposmod(0.58 + p_row * 0.13, 1.0), 0.55, 0.9);
}

bool FPSParticleTimeline::is_track(const Node *p_node) {
	return ParticleEffect3D::is_track(p_node);
}

real_t FPSParticleTimeline::_name_width() const {
	return 190 * EDSCALE;
}

real_t FPSParticleTimeline::_ruler_height() const {
	return 24 * EDSCALE;
}

real_t FPSParticleTimeline::_row_height() const {
	return 28 * EDSCALE;
}

real_t FPSParticleTimeline::_timeline_width() const {
	return MAX(real_t(1.0), get_size().x - _name_width() - 10 * EDSCALE);
}

double FPSParticleTimeline::_x_to_time(real_t p_x) const {
	return view_start + double(p_x - _name_width()) / double(_timeline_width()) * view_length;
}

real_t FPSParticleTimeline::_time_to_x(double p_time) const {
	return _name_width() + real_t((p_time - view_start) / view_length) * _timeline_width();
}

int FPSParticleTimeline::_row_at(real_t p_y) const {
	if (p_y < _ruler_height()) {
		return -1;
	}
	const int row = int((p_y - _ruler_height()) / _row_height());
	return row < track_ids.size() ? row : -1;
}

Node *FPSParticleTimeline::_track_at_row(int p_row) const {
	if (p_row < 0 || p_row >= track_ids.size()) {
		return nullptr;
	}
	return Object::cast_to<Node>(ObjectDB::get_instance(track_ids[p_row]));
}

Rect2 FPSParticleTimeline::_visibility_rect(int p_row) const {
	const real_t y = _ruler_height() + p_row * _row_height();
	const real_t size = 18 * EDSCALE;
	return Rect2(_name_width() - size * 2 - 10 * EDSCALE, y + (_row_height() - size) * 0.5, size, size);
}

Rect2 FPSParticleTimeline::_solo_rect(int p_row) const {
	const real_t y = _ruler_height() + p_row * _row_height();
	const real_t size = 18 * EDSCALE;
	return Rect2(_name_width() - size - 6 * EDSCALE, y + (_row_height() - size) * 0.5, size, size);
}

void FPSParticleTimeline::_collect(Node *p_node) {
	if (is_track(p_node)) {
		track_ids.push_back(p_node->get_instance_id());
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect(p_node->get_child(i));
	}
}

void FPSParticleTimeline::_update_content_length() {
	double total = 0.0;
	double max_start = 0.0;
	double max_tail = 0.0;
	bool infinite = false;
	for (const ObjectID &id : track_ids) {
		Node *track = Object::cast_to<Node>(ObjectDB::get_instance(id));
		if (!track) {
			continue;
		}
		double start, active_end, visual_end;
		ParticleEffect3D::get_track_window(track, start, active_end, visual_end);
		max_start = MAX(max_start, start);
		if (visual_end < 0.0) {
			infinite = true;
			max_tail = MAX(max_tail, ParticleEffect3D::is_emitter(track) ? double(track->get("lifetime")) : 1.0);
		} else {
			total = MAX(total, visual_end);
		}
	}
	if (infinite) {
		total = MAX(total, max_start + max_tail * 2.0 + 1.0);
	}
	content_length = MAX(1.0, total);
	if (!user_zoomed) {
		view_start = 0.0;
		view_length = MAX(2.0, content_length * 1.12);
	}
	_clamp_view();
}

void FPSParticleTimeline::_clamp_view() {
	view_length = CLAMP(view_length, TIMELINE_MIN_VIEW, MAX(content_length * 4.0, 10.0));
	view_start = CLAMP(view_start, -view_length * 0.05, MAX(0.0, content_length * 1.5 - view_length * 0.5));
}

void FPSParticleTimeline::_zoom_at(double p_factor, real_t p_x) {
	const double anchor = _x_to_time(p_x);
	const double ratio = (anchor - view_start) / view_length;
	view_length *= p_factor;
	_clamp_view();
	view_start = anchor - ratio * view_length;
	_clamp_view();
	user_zoomed = true;
	queue_redraw();
}

double FPSParticleTimeline::_snap(double p_value, bool p_shift) const {
	if (!snap_enabled || p_shift) {
		return p_value;
	}
	return Math::snapped(p_value, TIMELINE_SNAP);
}

void FPSParticleTimeline::zoom_in() {
	_zoom_at(0.7, _name_width() + _timeline_width() * 0.5);
}

void FPSParticleTimeline::zoom_out() {
	_zoom_at(1.0 / 0.7, _name_width() + _timeline_width() * 0.5);
}

void FPSParticleTimeline::fit_view() {
	user_zoomed = false;
	_update_content_length();
	queue_redraw();
}

void FPSParticleTimeline::set_snap_enabled(bool p_enabled) {
	snap_enabled = p_enabled;
}

void FPSParticleTimeline::set_effect(Node *p_effect) {
	const ObjectID new_id = p_effect ? p_effect->get_instance_id() : ObjectID();
	const bool changed = new_id != effect_id;
	if (changed) {
		user_zoomed = false;
	}
	effect_id = new_id;
	if (changed) {
		set_loop_region(0.0, -1.0); // The loop region belongs to the previous effect.
	}
	refresh();
}

void FPSParticleTimeline::set_selected_track(Node *p_node) {
	selected_id = p_node ? p_node->get_instance_id() : ObjectID();
	queue_redraw();
}

void FPSParticleTimeline::set_solo_track(Node *p_node) {
	solo_id = p_node ? p_node->get_instance_id() : ObjectID();
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
	track_ids.clear();
	Node *effect = Object::cast_to<Node>(ObjectDB::get_instance(effect_id));
	if (effect) {
		_collect(effect);
	}
	if (drag_mode == DRAG_NONE) {
		_update_content_length();
	}
	update_minimum_size();
	queue_redraw();
}

Size2 FPSParticleTimeline::get_minimum_size() const {
	return Size2(360 * EDSCALE, _ruler_height() + MAX(1, track_ids.size()) * _row_height() + 6 * EDSCALE);
}

String FPSParticleTimeline::get_tooltip(const Point2 &p_pos) const {
	if (p_pos.y < _ruler_height()) {
		return TTR("Click or drag to scrub. Ctrl/Cmd + wheel zooms, Shift + wheel or middle drag pans.") + "\n" + TTR("Shift + drag sets the preview loop region; I / O set its start / end at the playhead, Alt + I or right-click clears it.");
	}
	const int row = _row_at(p_pos.y);
	Node *track = _track_at_row(row);
	if (!track) {
		return String();
	}
	if (_visibility_rect(row).has_point(p_pos)) {
		return TTR("Show or hide this track in the preview and in the game.");
	}
	if (_solo_rect(row).has_point(p_pos)) {
		return TTR("Solo: preview only this track.");
	}
	double start, active_end, visual_end;
	ParticleEffect3D::get_track_window(track, start, active_end, visual_end);
	const String active = active_end < 0.0 ? TTR("continuous") : vformat(TTR("%s s"), String::num(active_end - start, 2));
	String text = vformat(TTR("%s\nStart: %s s\nActive: %s"), track->get_name(), String::num(start, 2), active);
	if (ParticleEffect3D::is_emitter(track)) {
		text += vformat(TTR("\nParticles: %d, lifetime %s s"), int(track->get("amount")), String::num(double(track->get("lifetime")), 2));
	}
	return text + "\n\n" + TTR("Drag the bar to change the start, its right edge to change the length (Shift: no snapping). Double-click resets the length. Right-click for more.");
}

void FPSParticleTimeline::_set_loop_region_internal(double p_start, double p_end) {
	if (p_end >= 0.0 && p_end - p_start < TIMELINE_SNAP) {
		p_end = -1.0; // Too short to loop; treat as cleared.
	}
	if (p_end < 0.0) {
		p_start = 0.0;
		p_end = -1.0;
	}
	if (Math::is_equal_approx(p_start, loop_start) && Math::is_equal_approx(p_end, loop_end)) {
		return;
	}
	loop_start = p_start;
	loop_end = p_end;
	queue_redraw();
	emit_signal(SNAME("loop_region_changed"), loop_start, loop_end);
}

void FPSParticleTimeline::set_loop_region(double p_start, double p_end) {
	_set_loop_region_internal(MAX(0.0, MIN(p_start, p_end < 0.0 ? p_start : p_end)), p_end < 0.0 ? -1.0 : MAX(p_start, p_end));
}

void FPSParticleTimeline::_on_context_menu_id(int p_id) {
	switch (p_id) {
		case RULER_LOOP_START: {
			const double end = has_loop_region() ? loop_end : content_length;
			set_loop_region(context_time, MAX(end, context_time + TIMELINE_SNAP));
			return;
		}
		case RULER_LOOP_END: {
			const double start = has_loop_region() ? loop_start : 0.0;
			set_loop_region(MIN(start, context_time), context_time);
			return;
		}
		case RULER_LOOP_CLEAR: {
			set_loop_region(0.0, -1.0);
			return;
		}
	}
	Node *track = Object::cast_to<Node>(ObjectDB::get_instance(context_track));
	if (!track) {
		return;
	}
	if (p_id == ACTION_RESET_TIMING) {
		const double old_delay = ParticleEffect3D::get_emitter_delay(track);
		const double old_duration = ParticleEffect3D::get_emitter_duration(track);
		ParticleEffect3D::set_emitter_delay(track, 0.0);
		ParticleEffect3D::set_emitter_duration(track, 0.0);
		emit_signal(SNAME("timing_committed"), track, old_delay, old_duration);
		refresh();
		return;
	}
	emit_signal(SNAME("track_action"), track, p_id);
}

void FPSParticleTimeline::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		Node *selected = Object::cast_to<Node>(ObjectDB::get_instance(selected_id));
		const Key code = key->get_keycode();
		const bool command = key->is_command_or_control_pressed();
		if (code == Key::SPACE) {
			emit_signal(SNAME("play_toggle_requested"));
		} else if (code == Key::HOME) {
			emit_signal(SNAME("restart_requested"));
		} else if (code == Key::F) {
			fit_view();
		} else if ((code == Key::KEY_DELETE || code == Key::BACKSPACE) && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_DELETE);
		} else if (code == Key::D && command && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_DUPLICATE);
		} else if (code == Key::H && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_TOGGLE_VISIBLE);
		} else if (code == Key::S && !command && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_SOLO);
		} else if (code == Key::I && !command) {
			// Loop in/out at the playhead (Alt+I clears).
			if (key->is_alt_pressed()) {
				set_loop_region(0.0, -1.0);
			} else {
				set_loop_region(current_time, MAX(has_loop_region() ? loop_end : content_length, current_time + TIMELINE_SNAP));
			}
		} else if (code == Key::O && !command) {
			set_loop_region(MIN(has_loop_region() ? loop_start : 0.0, current_time), current_time);
		} else {
			return;
		}
		accept_event();
		return;
	}

	Ref<InputEventMagnifyGesture> magnify = p_event;
	if (magnify.is_valid()) {
		_zoom_at(1.0 / MAX(0.01, magnify->get_factor()), magnify->get_position().x);
		accept_event();
		return;
	}
	Ref<InputEventPanGesture> pan = p_event;
	if (pan.is_valid() && Math::abs(pan->get_delta().x) > Math::abs(pan->get_delta().y)) {
		view_start += pan->get_delta().x * view_length * 0.02;
		user_zoomed = true;
		_clamp_view();
		queue_redraw();
		accept_event();
		return;
	}

	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		const Point2 pos = mb->get_position();
		if (mb->is_pressed() && (mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN)) {
			const bool up = mb->get_button_index() == MouseButton::WHEEL_UP;
			if (mb->is_command_or_control_pressed()) {
				_zoom_at(up ? 0.85 : 1.0 / 0.85, MAX(pos.x, _name_width()));
				accept_event();
			} else if (mb->is_shift_pressed()) {
				view_start += (up ? -1.0 : 1.0) * view_length * 0.08;
				user_zoomed = true;
				_clamp_view();
				queue_redraw();
				accept_event();
			}
			return;
		}
		if (mb->get_button_index() == MouseButton::MIDDLE) {
			if (mb->is_pressed()) {
				drag_mode = DRAG_PAN;
				drag_pan_origin_x = pos.x;
				drag_pan_origin_start = view_start;
			} else if (drag_mode == DRAG_PAN) {
				drag_mode = DRAG_NONE;
			}
			accept_event();
			return;
		}
		if (mb->get_button_index() == MouseButton::RIGHT && mb->is_pressed() && pos.y < _ruler_height() && pos.x >= _name_width()) {
			context_time = _snap(MAX(0.0, _x_to_time(pos.x)), mb->is_shift_pressed());
			context_track = ObjectID();
			context_menu->clear();
			context_menu->add_item(TTR("Set Loop Start Here"), RULER_LOOP_START, Key::I);
			context_menu->add_item(TTR("Set Loop End Here"), RULER_LOOP_END, Key::O);
			context_menu->add_item(TTR("Clear Loop Region"), RULER_LOOP_CLEAR, KeyModifierMask::ALT | Key::I);
			context_menu->set_item_disabled(-1, !has_loop_region());
			context_menu->set_position(get_screen_position() + pos);
			context_menu->reset_size();
			context_menu->popup();
			accept_event();
			return;
		}
		if (mb->get_button_index() == MouseButton::RIGHT && mb->is_pressed()) {
			Node *track = _track_at_row(_row_at(pos.y));
			if (track) {
				emit_signal(SNAME("track_selected"), track);
				context_track = track->get_instance_id();
				context_menu->clear();
				context_menu->add_item(TTR("Duplicate"), ACTION_DUPLICATE, KeyModifierMask::CMD_OR_CTRL | Key::D);
				context_menu->add_item(TTR("Rename"), ACTION_RENAME);
				context_menu->add_separator();
				context_menu->add_check_item(TTR("Visible"), ACTION_TOGGLE_VISIBLE, Key::H);
				context_menu->set_item_checked(-1, !Object::cast_to<Node3D>(track) || Object::cast_to<Node3D>(track)->is_visible());
				context_menu->add_check_item(TTR("Solo in Preview"), ACTION_SOLO, Key::S);
				context_menu->set_item_checked(-1, track->get_instance_id() == solo_id);
				context_menu->add_item(TTR("Reset Timing"), ACTION_RESET_TIMING);
				context_menu->add_separator();
				context_menu->add_item(TTR("Delete"), ACTION_DELETE, Key::KEY_DELETE);
				context_menu->set_position(get_screen_position() + pos);
				context_menu->reset_size();
				context_menu->popup();
			}
			accept_event();
			return;
		}
		if (mb->get_button_index() != MouseButton::LEFT) {
			return;
		}
		if (mb->is_pressed()) {
			grab_focus();
			if (pos.y < _ruler_height()) {
				if (pos.x >= _name_width()) {
					if (mb->is_shift_pressed()) {
						// Shift+drag on the ruler draws the loop region.
						drag_mode = DRAG_LOOP;
						drag_loop_anchor = _snap(MAX(0.0, _x_to_time(pos.x)), false);
					} else {
						drag_mode = DRAG_SCRUB;
						emit_signal(SNAME("scrub_requested"), MAX(0.0, _x_to_time(pos.x)));
					}
				}
				accept_event();
				return;
			}
			const int row = _row_at(pos.y);
			Node *track = _track_at_row(row);
			if (!track) {
				return;
			}
			if (_visibility_rect(row).has_point(pos)) {
				emit_signal(SNAME("track_action"), track, ACTION_TOGGLE_VISIBLE);
				accept_event();
				return;
			}
			if (_solo_rect(row).has_point(pos)) {
				emit_signal(SNAME("track_action"), track, ACTION_SOLO);
				accept_event();
				return;
			}
			emit_signal(SNAME("track_selected"), track);
			if (pos.x < _name_width()) {
				if (mb->is_double_click()) {
					emit_signal(SNAME("track_action"), track, ACTION_RENAME);
				}
				accept_event();
				return;
			}
			double start, active_end, visual_end;
			ParticleEffect3D::get_track_window(track, start, active_end, visual_end);
			const real_t x_start = _time_to_x(start);
			const real_t x_end = active_end < 0.0 ? get_size().x : _time_to_x(active_end);
			if (mb->is_double_click()) {
				if (ParticleEffect3D::get_emitter_duration(track) > 0.0) {
					const double old_duration = ParticleEffect3D::get_emitter_duration(track);
					ParticleEffect3D::set_emitter_duration(track, 0.0);
					emit_signal(SNAME("timing_committed"), track, ParticleEffect3D::get_emitter_delay(track), old_duration);
					refresh();
				}
				accept_event();
				return;
			}
			drag_track = track->get_instance_id();
			drag_start_time = _x_to_time(pos.x);
			drag_old_delay = ParticleEffect3D::get_emitter_delay(track);
			drag_old_duration = ParticleEffect3D::get_emitter_duration(track);
			if (active_end >= 0.0 && Math::abs(pos.x - x_end) <= 6 * EDSCALE) {
				drag_mode = DRAG_RESIZE;
			} else if (pos.x >= x_start - 4 * EDSCALE && pos.x <= x_end + 4 * EDSCALE) {
				drag_mode = DRAG_MOVE;
			} else {
				drag_mode = DRAG_SCRUB;
				emit_signal(SNAME("scrub_requested"), MAX(0.0, _x_to_time(pos.x)));
			}
			accept_event();
		} else {
			const DragMode finished = drag_mode;
			drag_mode = DRAG_NONE;
			Node *track = Object::cast_to<Node>(ObjectDB::get_instance(drag_track));
			drag_track = ObjectID();
			if ((finished == DRAG_MOVE || finished == DRAG_RESIZE) && track) {
				const bool changed = !Math::is_equal_approx(drag_old_delay, ParticleEffect3D::get_emitter_delay(track)) || !Math::is_equal_approx(drag_old_duration, ParticleEffect3D::get_emitter_duration(track));
				if (changed) {
					emit_signal(SNAME("timing_committed"), track, drag_old_delay, drag_old_duration);
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
		if (drag_mode == DRAG_PAN) {
			view_start = drag_pan_origin_start - double(pos.x - drag_pan_origin_x) / _timeline_width() * view_length;
			user_zoomed = true;
			_clamp_view();
			queue_redraw();
			accept_event();
			return;
		}
		if (drag_mode == DRAG_SCRUB) {
			emit_signal(SNAME("scrub_requested"), CLAMP(time, 0.0, content_length));
			accept_event();
			return;
		}
		if (drag_mode == DRAG_LOOP) {
			const double t = _snap(MAX(0.0, time), false);
			set_loop_region(MIN(drag_loop_anchor, t), MAX(drag_loop_anchor, t));
			accept_event();
			return;
		}
		Node *track = Object::cast_to<Node>(ObjectDB::get_instance(drag_track));
		if (drag_mode == DRAG_MOVE && track) {
			ParticleEffect3D::set_emitter_delay(track, MAX(0.0, _snap(drag_old_delay + time - drag_start_time, mm->is_shift_pressed())));
			queue_redraw();
			accept_event();
			return;
		}
		if (drag_mode == DRAG_RESIZE && track) {
			const double delay = ParticleEffect3D::get_emitter_delay(track);
			ParticleEffect3D::set_emitter_duration(track, MAX(TIMELINE_SNAP, _snap(time - delay, mm->is_shift_pressed())));
			queue_redraw();
			accept_event();
			return;
		}
		const int row = _row_at(pos.y);
		CursorShape shape = CURSOR_ARROW;
		Node *hover = _track_at_row(row);
		if (hover && pos.x >= _name_width()) {
			double start, active_end, visual_end;
			ParticleEffect3D::get_track_window(hover, start, active_end, visual_end);
			if (active_end >= 0.0 && Math::abs(pos.x - _time_to_x(active_end)) <= 6 * EDSCALE) {
				shape = CURSOR_HSIZE;
			} else if (pos.x >= _time_to_x(start) && (active_end < 0.0 || pos.x <= _time_to_x(active_end))) {
				shape = CURSOR_DRAG;
			}
		} else if (pos.y < _ruler_height() && pos.x >= _name_width()) {
			shape = mm->is_shift_pressed() ? CURSOR_HSPLIT : CURSOR_IBEAM;
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
		case NOTIFICATION_RESIZED: {
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
			draw_rect(Rect2(0, 0, _name_width(), size.y), Color(0, 0, 0, 0.12));

			// Ruler: pick a tick spacing of at least ~70 px.
			static const double steps[] = { 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 };
			const real_t px_per_second = _timeline_width() / real_t(view_length);
			double step = steps[11];
			for (double candidate : steps) {
				if (candidate * px_per_second >= 70 * EDSCALE) {
					step = candidate;
					break;
				}
			}
			draw_rect(Rect2(_name_width(), 0, size.x - _name_width(), _ruler_height()), Color(0, 0, 0, 0.25));
			if (has_loop_region()) {
				const real_t lx0 = MAX(_name_width(), _time_to_x(loop_start));
				const real_t lx1 = MIN(size.x, _time_to_x(loop_end));
				if (lx1 > lx0) {
					const Color loop_color(0.35, 0.75, 1.0);
					draw_rect(Rect2(lx0, 0, lx1 - lx0, _ruler_height()), Color(loop_color, 0.35));
					draw_rect(Rect2(lx0, _ruler_height(), lx1 - lx0, size.y - _ruler_height()), Color(loop_color, 0.06));
					for (real_t edge : { real_t(_time_to_x(loop_start)), real_t(_time_to_x(loop_end)) }) {
						if (edge >= _name_width() && edge <= size.x) {
							draw_line(Point2(edge, 0), Point2(edge, size.y), Color(loop_color, 0.7), 1.5 * EDSCALE);
						}
					}
				}
			}
			const int decimals = step < 0.05 ? 3 : (step < 0.1 ? 2 : (step < 1.0 ? 1 : 0));
			for (double t = Math::floor(MAX(0.0, view_start) / step) * step; t <= view_start + view_length + 0.0001; t += step) {
				const real_t x = _time_to_x(t);
				if (x < _name_width() - 0.5) {
					continue;
				}
				draw_line(Point2(x, _ruler_height() * 0.55), Point2(x, size.y), Color(font_color, 0.07));
				draw_line(Point2(x, _ruler_height() * 0.55), Point2(x, _ruler_height()), Color(font_color, 0.4));
				draw_string(font, Point2(x + 3 * EDSCALE, _ruler_height() * 0.7), String::num(t, decimals) + "s", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.85, dim);
				// Minor ticks.
				for (int minor = 1; minor < 5; minor++) {
					const real_t mx = _time_to_x(t + step * minor / 5.0);
					draw_line(Point2(mx, _ruler_height() * 0.8), Point2(mx, _ruler_height()), Color(font_color, 0.18));
				}
			}

			if (track_ids.is_empty()) {
				draw_string(font, Point2(_name_width() + 8 * EDSCALE, _ruler_height() + _row_height() * 0.66), TTR("Open or create an effect to see its tracks here."), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, dim);
			}
			const Ref<Texture2D> visible_icon = get_editor_theme_icon(SNAME("GuiVisibilityVisible"));
			const Ref<Texture2D> hidden_icon = get_editor_theme_icon(SNAME("GuiVisibilityHidden"));
			Ref<StyleBoxFlat> bar_style;
			bar_style.instantiate();
			bar_style->set_corner_radius_all(int(4 * EDSCALE));
			for (int row = 0; row < track_ids.size(); row++) {
				Node *track = _track_at_row(row);
				if (!track) {
					continue;
				}
				const real_t y = _ruler_height() + row * _row_height();
				const bool selected = track->get_instance_id() == selected_id;
				Node3D *track_3d = Object::cast_to<Node3D>(track);
				const bool visible = !track_3d || track_3d->is_visible();
				const bool muted = !visible || (solo_id.is_valid() && solo_id != track->get_instance_id());
				if (row % 2 == 1) {
					draw_rect(Rect2(0, y, size.x, _row_height()), Color(font_color, 0.025));
				}
				if (selected) {
					draw_rect(Rect2(0, y, size.x, _row_height()), Color(accent, 0.18));
				} else if (row == hover_row) {
					draw_rect(Rect2(0, y, size.x, _row_height()), Color(font_color, 0.05));
				}
				const Color row_color = get_track_color(track, row);
				draw_rect(Rect2(6 * EDSCALE, y + _row_height() * 0.25, 4 * EDSCALE, _row_height() * 0.5), muted ? Color(row_color, 0.3) : row_color);
				draw_string(font, Point2(16 * EDSCALE, y + _row_height() * 0.66), track->get_name(), HORIZONTAL_ALIGNMENT_LEFT, _name_width() - 66 * EDSCALE, font_size, muted ? dim : (selected ? font_color : Color(font_color, 0.85)));
				// Visibility and solo toggles.
				const Rect2 eye = _visibility_rect(row);
				draw_texture_rect(visible ? visible_icon : hidden_icon, eye, false, Color(1, 1, 1, visible ? 0.85 : 0.4));
				const Rect2 solo = _solo_rect(row);
				const bool is_solo = track->get_instance_id() == solo_id;
				if (is_solo) {
					draw_rect(solo, Color(1.0, 0.75, 0.2, 0.9));
				} else {
					draw_rect(solo, Color(font_color, 0.25), false, 1.0);
				}
				draw_string(font, Point2(solo.position.x + solo.size.x * 0.27, solo.position.y + solo.size.y * 0.78), "S", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8, is_solo ? Color(0, 0, 0) : dim);

				// Bars (clipped to the timeline area).
				double start, active_end, visual_end;
				ParticleEffect3D::get_track_window(track, start, active_end, visual_end);
				const real_t bar_top = y + _row_height() * 0.18;
				const real_t bar_height = _row_height() * 0.64;
				const real_t left_limit = _name_width();
				const real_t right_limit = size.x - 2 * EDSCALE;
				const real_t x_start = CLAMP(_time_to_x(start), left_limit, right_limit);
				const real_t x_end = active_end < 0.0 ? right_limit : CLAMP(_time_to_x(active_end), left_limit, right_limit);
				const real_t x_visual = visual_end < 0.0 ? right_limit : CLAMP(_time_to_x(visual_end), left_limit, right_limit);
				const float alpha = muted ? 0.3f : 1.0f;
				if (x_visual > x_end) {
					bar_style->set_bg_color(Color(row_color, 0.22 * alpha));
					draw_style_box(bar_style, Rect2(x_end - 4 * EDSCALE, bar_top + bar_height * 0.25, x_visual - x_end + 4 * EDSCALE, bar_height * 0.5));
				}
				if (x_end > x_start) {
					bar_style->set_bg_color(Color(row_color, (selected ? 0.95 : 0.75) * alpha));
					bar_style->set_border_width_all(selected ? int(1 * EDSCALE) : 0);
					bar_style->set_border_color(Color(1, 1, 1, 0.8));
					draw_style_box(bar_style, Rect2(x_start, bar_top, MAX(real_t(4.0), x_end - x_start), bar_height));
					bar_style->set_border_width_all(0);
					// Label inside the bar when it fits.
					String label;
					if (ParticleEffect3D::is_emitter(track)) {
						label = vformat(U"%d × %ss", int(track->get("amount")), String::num(double(track->get("lifetime")), 2));
					}
					const real_t label_width = font->get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8).x;
					if (!label.is_empty() && x_end - x_start > label_width + 12 * EDSCALE) {
						draw_string(font, Point2(x_start + 6 * EDSCALE, bar_top + bar_height * 0.72), label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8, Color(0, 0, 0, 0.75 * alpha));
					}
					if (active_end < 0.0) {
						draw_string(font, Point2(x_end - 16 * EDSCALE, bar_top + bar_height * 0.76), U"∞", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0, 0, 0, 0.7 * alpha));
					} else if (_time_to_x(active_end) <= right_limit) {
						draw_rect(Rect2(x_end - 3 * EDSCALE, bar_top + 3 * EDSCALE, 2 * EDSCALE, bar_height - 6 * EDSCALE), Color(1, 1, 1, 0.6 * alpha));
					}
				}
			}

			// Effect end marker.
			const real_t x_end_marker = _time_to_x(content_length);
			if (x_end_marker > _name_width() && x_end_marker < size.x) {
				for (real_t y = _ruler_height(); y < size.y; y += 8 * EDSCALE) {
					draw_line(Point2(x_end_marker, y), Point2(x_end_marker, MIN(size.y, y + 4 * EDSCALE)), Color(font_color, 0.35));
				}
				draw_string(font, Point2(x_end_marker + 3 * EDSCALE, size.y - 4 * EDSCALE), TTR("End"), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8, dim);
			}

			// Playhead with a handle in the ruler.
			const real_t x_play = _time_to_x(current_time);
			if (x_play >= _name_width() && x_play <= size.x) {
				const Color play_color(1.0, 0.36, 0.3);
				draw_line(Point2(x_play, _ruler_height() * 0.4), Point2(x_play, size.y), play_color, 2 * EDSCALE);
				Vector<Point2> handle;
				handle.push_back(Point2(x_play - 6 * EDSCALE, 0));
				handle.push_back(Point2(x_play + 6 * EDSCALE, 0));
				handle.push_back(Point2(x_play, _ruler_height() * 0.5));
				draw_colored_polygon(handle, play_color);
			}
			draw_line(Point2(_name_width(), 0), Point2(_name_width(), size.y), Color(font_color, 0.12));
			if (has_focus()) {
				draw_rect(Rect2(Point2(), size), Color(accent, 0.5), false, 1.0);
			}
		} break;
	}
}

void FPSParticleTimeline::_bind_methods() {
	ADD_SIGNAL(MethodInfo("scrub_requested", PropertyInfo(Variant::FLOAT, "time")));
	ADD_SIGNAL(MethodInfo("track_selected", PropertyInfo(Variant::OBJECT, "track")));
	ADD_SIGNAL(MethodInfo("timing_committed", PropertyInfo(Variant::OBJECT, "track"), PropertyInfo(Variant::FLOAT, "old_delay"), PropertyInfo(Variant::FLOAT, "old_duration")));
	ADD_SIGNAL(MethodInfo("track_action", PropertyInfo(Variant::OBJECT, "track"), PropertyInfo(Variant::INT, "action")));
	ADD_SIGNAL(MethodInfo("play_toggle_requested"));
	ADD_SIGNAL(MethodInfo("restart_requested"));
	ADD_SIGNAL(MethodInfo("loop_region_changed", PropertyInfo(Variant::FLOAT, "start"), PropertyInfo(Variant::FLOAT, "end")));
}

FPSParticleTimeline::FPSParticleTimeline() {
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_focus_mode(FOCUS_ALL);
	set_clip_contents(true);
	context_menu = memnew(PopupMenu);
	context_menu->connect(SceneStringName(id_pressed), callable_mp(this, &FPSParticleTimeline::_on_context_menu_id));
	add_child(context_menu);
}
