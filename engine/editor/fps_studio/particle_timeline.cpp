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
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/particle_process_material.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/scroll_container.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box_flat.h"

static const double TIMELINE_SNAP = 0.05;
static const double TIMELINE_MIN_VIEW = 0.1;
static const char *MARKERS_META = "_fps_timeline_markers";

// Marker colors, as in DaVinci Resolve's marker palette.
static const struct {
	const char *name;
	Color color;
} MARKER_PALETTE[] = {
	{ "Blue", Color(0.33, 0.6, 1.0) },
	{ "Cyan", Color(0.3, 0.85, 0.9) },
	{ "Green", Color(0.4, 0.85, 0.4) },
	{ "Yellow", Color(1.0, 0.85, 0.3) },
	{ "Red", Color(1.0, 0.35, 0.35) },
	{ "Pink", Color(1.0, 0.5, 0.8) },
	{ "Purple", Color(0.7, 0.5, 1.0) },
	{ "White", Color(0.92, 0.92, 0.92) },
};
static const int MARKER_PALETTE_SIZE = sizeof(MARKER_PALETTE) / sizeof(MARKER_PALETTE[0]);

Color FPSParticleTimeline::get_marker_color(int p_index) {
	return MARKER_PALETTE[CLAMP(p_index, 0, MARKER_PALETTE_SIZE - 1)].color;
}

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

real_t FPSParticleTimeline::_ruler_top() const {
	// The ruler stays at the visible top while the track list scrolls.
	const ScrollContainer *scroll = Object::cast_to<ScrollContainer>(get_parent());
	return scroll ? real_t(scroll->get_v_scroll()) : real_t(0.0);
}

bool FPSParticleTimeline::_in_ruler(real_t p_y) const {
	const real_t top = _ruler_top();
	return p_y >= top && p_y < top + _ruler_height();
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
	if (p_y < _ruler_top() + _ruler_height()) {
		return -1; // Under the (sticky) ruler.
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
	// A start set without an end plays to the effect's end, also after the
	// effect gets longer or shorter.
	const double new_length = MAX(1.0, total);
	if (has_loop_region() && !Math::is_equal_approx(new_length, content_length) && (Math::is_equal_approx(loop_end, content_length) || loop_end > new_length)) {
		// The end flag sat on the effect's end (or is now past it): move it to
		// the new end. A start that no longer fits clears the range.
		content_length = new_length;
		if (loop_start < new_length - TIMELINE_SNAP) {
			set_loop_region(loop_start, new_length);
		} else {
			set_loop_region(0.0, -1.0);
		}
	}
	content_length = new_length;
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

// Flags and the playhead catch the edges of the tracks (start, end of
// emission, end of the last particle) within a few pixels, else the grid.
// Shift moves them freely.
double FPSParticleTimeline::_snap_to_edges(double p_time, bool p_free, bool p_grid_fallback) const {
	if (p_free) {
		return p_time;
	}
	double best = p_time;
	real_t best_px = 8 * EDSCALE;
	auto consider = [&](double p_edge) {
		if (p_edge < 0.0) {
			return;
		}
		const real_t px = Math::abs(_time_to_x(p_edge) - _time_to_x(p_time));
		if (px < best_px) {
			best_px = px;
			best = p_edge;
		}
	};
	consider(0.0);
	consider(content_length);
	for (int i = 0; i < markers.size(); i++) {
		if (drag_mode != DRAG_MARKER) { // A dragged marker never lands on itself or another one.
			consider(markers[i].time);
		}
	}
	for (const ObjectID &id : track_ids) {
		Node *track = Object::cast_to<Node>(ObjectDB::get_instance(id));
		if (!track) {
			continue;
		}
		const Node3D *track_3d = Object::cast_to<Node3D>(track);
		if (track_3d && !track_3d->is_visible()) {
			continue;
		}
		double start, active_end, visual_end;
		ParticleEffect3D::get_track_window(track, start, active_end, visual_end);
		consider(start);
		consider(active_end);
		consider(visual_end);
	}
	return best != p_time || !p_grid_fallback ? best : _snap(p_time, false);
}

void FPSParticleTimeline::zoom_in() {
	_zoom_at(0.7, _name_width() + _timeline_width() * 0.5);
}

void FPSParticleTimeline::zoom_out() {
	_zoom_at(1.0 / 0.7, _name_width() + _timeline_width() * 0.5);
}

// With a playback range set, Fit shows just that range; again shows the
// whole effect.
void FPSParticleTimeline::fit_view() {
	if (has_loop_region()) {
		const double span = loop_end - loop_start;
		const double length = MAX(TIMELINE_MIN_VIEW, span * 1.15);
		const double start = loop_start - (length - span) * 0.5;
		// Compare what the view really becomes (a range near 0 is clamped),
		// or the second F never gets to the whole effect.
		const double old_start = view_start;
		const double old_length = view_length;
		view_length = length;
		view_start = start;
		_clamp_view();
		if (!(user_zoomed && Math::is_equal_approx(old_start, view_start) && Math::is_equal_approx(old_length, view_length))) {
			user_zoomed = true;
			queue_redraw();
			return;
		}
	}
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
		drag_marker = -1;
		selected_marker = -1;
		if (rename_popup) {
			rename_popup->hide();
		}
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
	if (drag_mode != DRAG_MARKER) {
		_load_markers(); // Undo / redo and file loads change them from outside.
		if (selected_marker >= markers.size()) {
			selected_marker = -1;
		}
	}
	update_minimum_size();
	queue_redraw();
}

Size2 FPSParticleTimeline::get_minimum_size() const {
	return Size2(360 * EDSCALE, _ruler_height() + MAX(1, track_ids.size()) * _row_height() + 6 * EDSCALE);
}

String FPSParticleTimeline::get_tooltip(const Point2 &p_pos) const {
	const int marker = _marker_at(p_pos);
	if (marker >= 0) {
		const Marker &m = markers[marker];
		return vformat(TTR("Marker \"%s\" at %s s"), m.name, String::num(m.time, 2)) + "\n" + TTR("Click to jump here and select it (Delete removes it), drag to move (Shift: no snapping), double-click to rename, right-click for color. Keys 1-9 jump to the first nine markers, Shift + Up / Down to the previous / next one.");
	}
	if (_is_on_end_flag(p_pos)) {
		return TTR("Playback end: playback stops here, or goes back to the start flag with Loop on. Drag it, press O at the playhead or right-click the ruler. Alt + I plays the whole effect again.");
	}
	if (_is_on_start_flag(p_pos)) {
		return TTR("Playback start: Play and Restart begin here. Drag it, press I at the playhead or right-click the ruler. Alt + I plays the whole effect again.");
	}
	if (_in_ruler(p_pos.y)) {
		return TTR("Click or drag to scrub; , and . step one frame (Shift: 0.1 s), Up / Down jump to the previous / next edge or marker, M adds a marker. Ctrl/Cmd + wheel zooms, Shift + wheel or middle drag pans. Fit (F) shows the playback range, again the whole effect.") + "\n" + TTR("The green flags are where playback starts and ends (drag them, or I / O at the playhead). Shift + drag sets both; Alt + I or right-click plays the whole effect again.");
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
	if (p_pos.x < _name_width()) {
		return vformat(TTR("%s\nDrag the name up or down to reorder (or Alt + Up / Down). Double-click to rename."), track->get_name());
	}
	double start, active_end, visual_end;
	ParticleEffect3D::get_track_window(track, start, active_end, visual_end);
	const String active = active_end < 0.0 ? TTR("continuous") : vformat(TTR("%s s"), String::num(active_end - start, 2));
	String text = vformat(TTR("%s\nStart: %s s\nActive: %s"), track->get_name(), String::num(start, 2), active);
	if (ParticleEffect3D::is_emitter(track)) {
		text += vformat(TTR("\nParticles: %d, each lives %s s (the thin tail)"), int(track->get("amount")), String::num(double(track->get("lifetime")), 2));
		text += "\n" + TTR("White line: Alpha over Life of the first particle (diamonds are its points). Blue ticks: Scale over Life points.");
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

// Moves where playback begins and keeps the end (the effect's end when no
// loop region was set). Back at 0 with the effect's end is no region at all.
void FPSParticleTimeline::_set_playback_start(double p_time) {
	const double end = has_loop_region() ? loop_end : content_length;
	const double start = CLAMP(p_time, 0.0, MAX(0.0, end - TIMELINE_SNAP));
	if (start <= 0.0001 && end >= content_length - 0.0001) {
		set_loop_region(0.0, -1.0);
	} else {
		set_loop_region(start, MAX(end, start + TIMELINE_SNAP));
	}
}

// The flag sits in the lower half of the ruler; the playhead handle is in the
// upper half, so both can be grabbed when they overlap.
bool FPSParticleTimeline::_is_on_start_flag(const Point2 &p_pos) const {
	if (!_in_ruler(p_pos.y) || p_pos.x < _name_width() - 2 * EDSCALE) {
		return false;
	}
	const real_t x = _time_to_x(get_playback_start());
	return p_pos.y >= _ruler_top() + _ruler_height() * 0.5 && p_pos.x >= x - 4 * EDSCALE && p_pos.x <= x + 11 * EDSCALE;
}

// Moves where playback ends and keeps the start. Back at the effect's end
// with the start at 0 is no region at all.
void FPSParticleTimeline::_set_playback_end(double p_time) {
	const double start = get_playback_start();
	const double end = MAX(p_time, start + TIMELINE_SNAP);
	if (start <= 0.0001 && end >= content_length - 0.0001) {
		set_loop_region(0.0, -1.0);
	} else {
		set_loop_region(start, end);
	}
}

// Mirror of the start flag: it points left, from its line.
bool FPSParticleTimeline::_is_on_end_flag(const Point2 &p_pos) const {
	if (!_in_ruler(p_pos.y) || p_pos.x < _name_width() - 2 * EDSCALE) {
		return false;
	}
	const real_t x = _time_to_x(get_playback_end());
	if (!(p_pos.y >= _ruler_top() + _ruler_height() * 0.5 && p_pos.x >= x - 11 * EDSCALE && p_pos.x <= x + 4 * EDSCALE)) {
		return false;
	}
	// Flags touching each other: the nearer line wins.
	return !_is_on_start_flag(p_pos) || Math::abs(p_pos.x - x) < Math::abs(p_pos.x - _time_to_x(get_playback_start()));
}

void FPSParticleTimeline::_load_markers() {
	markers.clear();
	Node *effect = Object::cast_to<Node>(ObjectDB::get_instance(effect_id));
	if (!effect || !effect->has_meta(MARKERS_META)) {
		return;
	}
	const Array stored = effect->get_meta(MARKERS_META);
	for (int i = 0; i < stored.size(); i++) {
		const Dictionary entry = stored[i];
		Marker marker;
		marker.time = MAX(0.0, double(entry.get("time", 0.0)));
		marker.name = entry.get("name", String());
		marker.color = CLAMP(int(entry.get("color", 0)), 0, MARKER_PALETTE_SIZE - 1);
		markers.push_back(marker);
	}
}

Array FPSParticleTimeline::_markers_to_array() const {
	// Sorted by time, so jumping and the saved file read in order.
	Vector<Marker> sorted = markers;
	for (int i = 1; i < sorted.size(); i++) {
		for (int j = i; j > 0 && sorted[j - 1].time > sorted[j].time; j--) {
			SWAP(sorted.write[j - 1], sorted.write[j]);
		}
	}
	Array result;
	for (const Marker &marker : sorted) {
		Dictionary entry;
		entry["time"] = marker.time;
		entry["name"] = marker.name;
		entry["color"] = marker.color;
		result.push_back(entry);
	}
	return result;
}

// Writes the markers into the effect and tells the editor (undo, Save *).
void FPSParticleTimeline::_store_markers(const Array &p_old) {
	Node *effect = Object::cast_to<Node>(ObjectDB::get_instance(effect_id));
	if (!effect) {
		return;
	}
	const Array current = _markers_to_array();
	if (current.is_empty()) {
		effect->remove_meta(MARKERS_META);
	} else {
		effect->set_meta(MARKERS_META, current);
	}
	// Sorting may change indices: keep the same marker selected.
	const double selected_time = selected_marker >= 0 && selected_marker < markers.size() ? markers[selected_marker].time : -1.0;
	_load_markers();
	selected_marker = -1;
	for (int i = 0; selected_time >= 0.0 && i < markers.size(); i++) {
		if (Math::is_equal_approx(markers[i].time, selected_time)) {
			selected_marker = i;
		}
	}
	queue_redraw();
	emit_signal(SNAME("markers_committed"), p_old, current);
}

// Markers sit in the upper half of the ruler (the flags use the lower half).
int FPSParticleTimeline::_marker_at(const Point2 &p_pos) const {
	if (!_in_ruler(p_pos.y) || p_pos.y >= _ruler_top() + _ruler_height() * 0.5 || p_pos.x < _name_width() - 2 * EDSCALE) {
		return -1;
	}
	int best = -1;
	real_t best_distance = 8 * EDSCALE;
	for (int i = 0; i < markers.size(); i++) {
		const real_t distance = Math::abs(p_pos.x - _time_to_x(markers[i].time));
		if (distance <= best_distance) {
			best = i;
			best_distance = distance;
		}
	}
	if (best < 0) {
		// The name next to the head counts as the marker too.
		for (int i = 0; i < MIN(markers.size(), marker_label_rects.size()); i++) {
			if (marker_label_rects[i].has_point(p_pos)) {
				return i;
			}
		}
	}
	return best;
}

void FPSParticleTimeline::add_marker_at_playhead() {
	if (!Object::cast_to<Node>(ObjectDB::get_instance(effect_id))) {
		return;
	}
	for (const Marker &marker : markers) {
		if (Math::abs(marker.time - current_time) < 0.0005) {
			return; // One marker per spot.
		}
	}
	const Array old = _markers_to_array();
	Marker marker;
	marker.time = MAX(0.0, current_time);
	marker.name = vformat(TTR("Marker %d"), markers.size() + 1);
	marker.color = markers.size() % MARKER_PALETTE_SIZE;
	markers.push_back(marker);
	_store_markers(old);
}

void FPSParticleTimeline::_delete_marker(int p_index) {
	if (p_index < 0 || p_index >= markers.size()) {
		return;
	}
	const Array old = _markers_to_array();
	markers.remove_at(p_index);
	selected_marker = -1;
	_store_markers(old);
}

void FPSParticleTimeline::jump_to_marker(int p_index) {
	const Array sorted = _markers_to_array();
	if (p_index < 0 || p_index >= sorted.size()) {
		return;
	}
	const double time = double(Dictionary(sorted[p_index]).get("time", 0.0));
	for (int i = 0; i < markers.size(); i++) {
		if (Math::is_equal_approx(markers[i].time, time)) {
			selected_marker = i;
		}
	}
	emit_signal(SNAME("scrub_requested"), time);
	// Bring it into view when zoomed in.
	if (time < view_start || time > view_start + view_length) {
		view_start = time - view_length * 0.3;
		_clamp_view();
	}
	queue_redraw();
}

void FPSParticleTimeline::_open_marker_rename(int p_index) {
	if (p_index < 0 || p_index >= markers.size()) {
		return;
	}
	rename_marker = p_index;
	rename_edit->set_text(markers[p_index].name);
	const Point2 position = get_screen_position() + Point2(_time_to_x(markers[p_index].time), _ruler_top() + _ruler_height());
	rename_popup->set_position(position);
	rename_popup->reset_size();
	rename_popup->popup();
	rename_edit->grab_focus();
	rename_edit->select_all();
}

void FPSParticleTimeline::_on_marker_rename_submitted(const String &p_text) {
	rename_popup->hide();
	const String name = p_text.strip_edges();
	if (rename_marker < 0 || rename_marker >= markers.size() || name.is_empty() || name == markers[rename_marker].name) {
		return;
	}
	const Array old = _markers_to_array();
	markers.write[rename_marker].name = name;
	_store_markers(old);
}

// Previous / next edit point, as Up / Down in a video editor: track starts
// and ends, the playback flags, the effect's end and markers.
double FPSParticleTimeline::get_jump_time(int p_dir, bool p_markers_only) const {
	Vector<double> points;
	for (const Marker &marker : markers) {
		points.push_back(marker.time);
	}
	if (!p_markers_only) {
		points.push_back(0.0);
		points.push_back(content_length);
		if (has_loop_region()) {
			points.push_back(loop_start);
			points.push_back(loop_end);
		}
		for (const ObjectID &id : track_ids) {
			Node *track = Object::cast_to<Node>(ObjectDB::get_instance(id));
			if (!track) {
				continue;
			}
			double start, active_end, visual_end;
			ParticleEffect3D::get_track_window(track, start, active_end, visual_end);
			points.push_back(start);
			if (active_end >= 0.0) {
				points.push_back(active_end);
			}
			if (visual_end >= 0.0) {
				points.push_back(visual_end);
			}
		}
	}
	const double epsilon = 0.004;
	double best = current_time;
	bool found = false;
	for (double point : points) {
		if (point < 0.0 || point > content_length + 0.0001) {
			continue;
		}
		if (p_dir > 0 && point > current_time + epsilon && (!found || point < best)) {
			best = point;
			found = true;
		} else if (p_dir < 0 && point < current_time - epsilon && (!found || point > best)) {
			best = point;
			found = true;
		}
	}
	return best;
}

void FPSParticleTimeline::_jump(int p_dir, bool p_markers_only) {
	const double target = get_jump_time(p_dir, p_markers_only);
	if (!Math::is_equal_approx(target, current_time)) {
		emit_signal(SNAME("scrub_requested"), target);
	}
}

void FPSParticleTimeline::_on_context_menu_id(int p_id) {
	if (p_id >= MARKER_COLOR_FIRST && p_id < MARKER_COLOR_FIRST + MARKER_PALETTE_SIZE) {
		if (context_marker >= 0 && context_marker < markers.size() && markers[context_marker].color != p_id - MARKER_COLOR_FIRST) {
			const Array old = _markers_to_array();
			markers.write[context_marker].color = p_id - MARKER_COLOR_FIRST;
			_store_markers(old);
		}
		return;
	}
	switch (p_id) {
		case RULER_ADD_MARKER: {
			const double saved = current_time;
			current_time = context_time;
			add_marker_at_playhead();
			current_time = saved;
			return;
		}
		case RULER_DELETE_ALL_MARKERS: {
			if (!markers.is_empty()) {
				const Array old = _markers_to_array();
				markers.clear();
				selected_marker = -1;
				_store_markers(old);
			}
			return;
		}
		case MARKER_RENAME: {
			_open_marker_rename(context_marker);
			return;
		}
		case MARKER_DELETE: {
			_delete_marker(context_marker);
			return;
		}
		case RULER_LOOP_START: {
			_set_playback_start(context_time);
			return;
		}
		case RULER_LOOP_END: {
			_set_playback_end(context_time);
			return;
		}
		case RULER_LOOP_CLEAR: {
			set_loop_region(0.0, -1.0);
			return;
		}
	}
	Node *track = Object::cast_to<Node>(ObjectDB::get_instance(context_track));
	if (p_id == ACTION_PASTE) {
		emit_signal(SNAME("track_action"), track, p_id); // Track may be null.
		return;
	}
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
	if (key.is_valid() && key->is_pressed() && (key->get_keycode() == Key::COMMA || key->get_keycode() == Key::PERIOD)) {
		// One frame (1/30 s) back / forward, Shift: 0.1 s; held down it repeats.
		// Counted in whole steps: the time slider rounds to 0.01 s.
		const double step = key->is_shift_pressed() ? 0.1 : 1.0 / 30.0;
		const double dir = key->get_keycode() == Key::COMMA ? -1.0 : 1.0;
		emit_signal(SNAME("scrub_requested"), CLAMP((Math::round(current_time / step) + dir) * step, 0.0, content_length));
		accept_event();
		return;
	}
	if (key.is_valid() && key->is_pressed() && (key->get_keycode() == Key::UP || key->get_keycode() == Key::DOWN) && key->is_alt_pressed() && !key->is_command_or_control_pressed()) {
		Node *selected = Object::cast_to<Node>(ObjectDB::get_instance(selected_id));
		if (selected) {
			emit_signal(SNAME("track_action"), selected, key->get_keycode() == Key::UP ? ACTION_MOVE_UP : ACTION_MOVE_DOWN);
		}
		accept_event();
		return;
	}
	if (key.is_valid() && key->is_pressed() && (key->get_keycode() == Key::UP || key->get_keycode() == Key::DOWN) && !key->is_command_or_control_pressed() && !key->is_alt_pressed()) {
		_jump(key->get_keycode() == Key::UP ? -1 : 1, key->is_shift_pressed());
		accept_event();
		return;
	}
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		Node *selected = Object::cast_to<Node>(ObjectDB::get_instance(selected_id));
		const Key code = key->get_keycode();
		const bool command = key->is_command_or_control_pressed();
		if ((code == Key::KEY_DELETE || code == Key::BACKSPACE) && selected_marker >= 0 && selected_marker < markers.size()) {
			_delete_marker(selected_marker);
		} else if (code >= Key::KEY_1 && code <= Key::KEY_9 && !command && !key->is_alt_pressed()) {
			jump_to_marker(int(code) - int(Key::KEY_1));
		} else if (code == Key::ESCAPE && selected_marker >= 0) {
			selected_marker = -1;
			queue_redraw();
		} else if (code == Key::SPACE) {
			emit_signal(SNAME("play_toggle_requested"));
		} else if (code == Key::HOME || (code == Key::R && !command)) {
			emit_signal(SNAME("restart_requested"));
		} else if (code == Key::F) {
			fit_view();
		} else if ((code == Key::KEY_DELETE || code == Key::BACKSPACE) && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_DELETE);
		} else if (code == Key::D && command && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_DUPLICATE);
		} else if (code == Key::C && command && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_COPY);
		} else if (code == Key::X && command && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_CUT);
		} else if (code == Key::V && command) {
			emit_signal(SNAME("track_action"), selected, ACTION_PASTE);
		} else if (code == Key::H && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_TOGGLE_VISIBLE);
		} else if (code == Key::S && !command && selected) {
			emit_signal(SNAME("track_action"), selected, ACTION_SOLO);
		} else if (code == Key::I && !command) {
			// Loop in/out at the playhead (Alt+I clears).
			if (key->is_alt_pressed()) {
				set_loop_region(0.0, -1.0);
			} else {
				_set_playback_start(current_time);
			}
		} else if (code == Key::O && !command) {
			_set_playback_end(current_time);
		} else if (code == Key::M && !command && !key->is_alt_pressed()) {
			add_marker_at_playhead();
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
			if (mb->is_pressed() && drag_mode == DRAG_NONE) { // Not in the middle of a left drag: its release must still commit.
				drag_mode = DRAG_PAN;
				drag_pan_origin_x = pos.x;
				drag_pan_origin_start = view_start;
			} else if (drag_mode == DRAG_PAN) {
				drag_mode = DRAG_NONE;
			}
			accept_event();
			return;
		}
		if (mb->get_button_index() == MouseButton::RIGHT && mb->is_pressed() && _in_ruler(pos.y) && pos.x >= _name_width()) {
			context_time = _snap(MAX(0.0, _x_to_time(pos.x)), mb->is_shift_pressed());
			context_track = ObjectID();
			context_marker = _marker_at(pos);
			context_menu->clear();
			if (context_marker >= 0) {
				context_menu->add_item(TTR("Rename Marker..."), MARKER_RENAME);
				context_menu->add_separator(TTR("Color"));
				for (int i = 0; i < MARKER_PALETTE_SIZE; i++) {
					context_menu->add_radio_check_item(TTR(MARKER_PALETTE[i].name), MARKER_COLOR_FIRST + i);
					context_menu->set_item_checked(-1, markers[context_marker].color == i);
				}
				context_menu->add_separator();
				context_menu->add_item(TTR("Delete Marker"), MARKER_DELETE);
			} else {
				context_menu->add_item(TTR("Start Playback Here"), RULER_LOOP_START, Key::I);
				context_menu->add_item(TTR("End Playback Here"), RULER_LOOP_END, Key::O);
				context_menu->add_item(TTR("Play Whole Effect"), RULER_LOOP_CLEAR, KeyModifierMask::ALT | Key::I);
				context_menu->set_item_disabled(-1, !has_loop_region());
				context_menu->add_separator();
				context_menu->add_item(TTR("Add Marker Here"), RULER_ADD_MARKER, Key::M);
				context_menu->add_item(TTR("Delete All Markers"), RULER_DELETE_ALL_MARKERS);
				context_menu->set_item_disabled(-1, markers.is_empty());
			}
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
				context_menu->add_item(TTR("Copy"), ACTION_COPY, KeyModifierMask::CMD_OR_CTRL | Key::C);
				context_menu->add_item(TTR("Cut"), ACTION_CUT, KeyModifierMask::CMD_OR_CTRL | Key::X);
				context_menu->add_item(TTR("Paste"), ACTION_PASTE, KeyModifierMask::CMD_OR_CTRL | Key::V);
				context_menu->set_item_disabled(-1, !can_paste);
				context_menu->add_item(TTR("Rename"), ACTION_RENAME);
				context_menu->add_item(TTR("Move Up"), ACTION_MOVE_UP, KeyModifierMask::ALT | Key::UP);
				context_menu->add_item(TTR("Move Down"), ACTION_MOVE_DOWN, KeyModifierMask::ALT | Key::DOWN);
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
			} else if (can_paste && pos.x < get_size().x && !_in_ruler(pos.y)) {
				// Empty area: paste the copied tracks into the effect.
				context_track = ObjectID();
				context_menu->clear();
				context_menu->add_item(TTR("Paste"), ACTION_PASTE, KeyModifierMask::CMD_OR_CTRL | Key::V);
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
			if (_in_ruler(pos.y)) {
				const int marker = _marker_at(pos);
				if (marker >= 0) {
					if (mb->is_double_click()) {
						_open_marker_rename(marker);
					} else {
						selected_marker = marker;
						drag_mode = DRAG_MARKER;
						drag_marker = marker;
						drag_marker_moved = false;
						drag_marker_press_x = pos.x;
						drag_marker_old = _markers_to_array();
						emit_signal(SNAME("scrub_requested"), markers[marker].time);
					}
					accept_event();
					return;
				}
				selected_marker = -1; // Clicking elsewhere lets Delete act on tracks again.
				if (!mb->is_shift_pressed() && _is_on_end_flag(pos)) {
					drag_mode = DRAG_END;
				} else if (!mb->is_shift_pressed() && _is_on_start_flag(pos)) {
					drag_mode = DRAG_START;
				} else if (pos.x >= _name_width()) {
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
			if (selected_marker >= 0) {
				selected_marker = -1;
				queue_redraw();
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
				} else {
					// Dragging the name up or down reorders the track.
					drag_mode = DRAG_REORDER;
					drag_track = track->get_instance_id();
					drag_reorder_press_y = pos.y;
					drag_reorder_active = false;
					drag_reorder_gap = -1;
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
			if (finished == DRAG_REORDER) {
				Node *moved = Object::cast_to<Node>(ObjectDB::get_instance(drag_track));
				const int row = track_ids.find(drag_track);
				const int gap = drag_reorder_gap;
				const bool active = drag_reorder_active;
				drag_track = ObjectID();
				drag_reorder_active = false;
				drag_reorder_gap = -1;
				set_default_cursor_shape(CURSOR_ARROW);
				queue_redraw();
				if (active && moved && row >= 0 && gap >= 0 && gap != row && gap != row + 1) {
					emit_signal(SNAME("track_move_requested"), moved, gap < track_ids.size() ? _track_at_row(gap) : nullptr);
				}
				accept_event();
				return;
			}
			if (finished == DRAG_MARKER) {
				if (drag_marker_moved) {
					_store_markers(drag_marker_old);
				}
				drag_marker = -1;
				accept_event();
				return;
			}
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
			// Edges only (no grid): scrubbing stays smooth between them.
			emit_signal(SNAME("scrub_requested"), CLAMP(_snap_to_edges(time, mm->is_shift_pressed(), false), 0.0, content_length));
			accept_event();
			return;
		}
		if (drag_mode == DRAG_REORDER) {
			if (!drag_reorder_active && Math::abs(pos.y - drag_reorder_press_y) > 4 * EDSCALE) {
				drag_reorder_active = true;
			}
			if (drag_reorder_active) {
				const real_t rows_y = pos.y - _ruler_height();
				drag_reorder_gap = CLAMP(int(Math::round(rows_y / _row_height())), 0, track_ids.size());
				set_default_cursor_shape(CURSOR_VSIZE);
				queue_redraw();
			}
			accept_event();
			return;
		}
		if (drag_mode == DRAG_MARKER) {
			if (drag_marker >= 0 && drag_marker < markers.size() && (drag_marker_moved || Math::abs(pos.x - drag_marker_press_x) > 4 * EDSCALE)) {
				drag_marker_moved = true;
				markers.write[drag_marker].time = CLAMP(_snap_to_edges(MAX(0.0, time), mm->is_shift_pressed()), 0.0, content_length);
				emit_signal(SNAME("scrub_requested"), markers[drag_marker].time);
				queue_redraw();
			}
			accept_event();
			return;
		}
		if (drag_mode == DRAG_END) {
			_set_playback_end(_snap_to_edges(MAX(0.0, time), mm->is_shift_pressed()));
			accept_event();
			return;
		}
		if (drag_mode == DRAG_START) {
			_set_playback_start(_snap_to_edges(MAX(0.0, time), mm->is_shift_pressed()));
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
		} else if (_marker_at(pos) >= 0) {
			shape = CURSOR_POINTING_HAND;
		} else if (!mm->is_shift_pressed() && (_is_on_start_flag(pos) || _is_on_end_flag(pos))) {
			shape = CURSOR_HSIZE;
		} else if (_in_ruler(pos.y) && pos.x >= _name_width()) {
			shape = mm->is_shift_pressed() ? CURSOR_HSPLIT : CURSOR_POINTING_HAND; // Scrub the playhead.
		}
		set_default_cursor_shape(shape);
		if (row != hover_row) {
			hover_row = row;
			queue_redraw();
		}
	}
}

static Ref<Curve> get_curve_of(const Ref<Texture2D> &p_texture) {
	const CurveTexture *curve_texture = Object::cast_to<CurveTexture>(p_texture.ptr());
	return curve_texture ? curve_texture->get_curve() : Ref<Curve>();
}

void FPSParticleTimeline::_draw_life_curves(Node *p_track, double p_start, real_t p_row_y, float p_alpha) {
	const GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(p_track);
	const ParticleProcessMaterial *process = gpu ? Object::cast_to<ParticleProcessMaterial>(gpu->get_process_material().ptr()) : nullptr;
	if (!process) {
		return;
	}
	const double lifetime = MAX(0.001, gpu->get_lifetime());
	const real_t x0 = _time_to_x(p_start);
	const real_t x1 = _time_to_x(p_start + lifetime);
	if (x1 < _name_width() || x0 > get_size().x || x1 - x0 < 8 * EDSCALE) {
		return;
	}
	const real_t top = p_row_y + _row_height() * 0.12;
	const real_t height = _row_height() * 0.76;
	const real_t left = _name_width();
	const real_t right = get_size().x - 2 * EDSCALE;
	const Ref<Curve> alpha_curve = get_curve_of(process->get_alpha_curve());
	if (alpha_curve.is_valid() && alpha_curve->get_point_count() > 0) {
		const Color line_color(1, 1, 1, 0.55 * p_alpha);
		Vector<Point2> line;
		const int steps = CLAMP(int((x1 - x0) / (3 * EDSCALE)), 8, 200);
		const real_t max_value = MAX(real_t(0.001), alpha_curve->get_max_value());
		for (int i = 0; i <= steps; i++) {
			const real_t t = real_t(i) / steps;
			const real_t x = x0 + (x1 - x0) * t;
			if (x < left || x > right) {
				continue;
			}
			const real_t value = CLAMP(alpha_curve->sample_baked(t) / max_value, real_t(0.0), real_t(1.0));
			line.push_back(Point2(x, top + height * (1.0 - value)));
		}
		if (line.size() >= 2) {
			draw_polyline(line, line_color, 1.0 * EDSCALE, true);
		}
		for (int i = 0; i < alpha_curve->get_point_count(); i++) {
			const Vector2 point = alpha_curve->get_point_position(i);
			const real_t x = x0 + (x1 - x0) * point.x;
			if (x < left || x > right) {
				continue;
			}
			const real_t y = top + height * (1.0 - CLAMP(point.y / max_value, real_t(0.0), real_t(1.0)));
			const real_t r = 3 * EDSCALE;
			Vector<Point2> diamond = { Point2(x, y - r), Point2(x + r, y), Point2(x, y + r), Point2(x - r, y) };
			draw_colored_polygon(diamond, Color(1, 1, 1, 0.85 * p_alpha));
		}
	}
	const Ref<Curve> scale_curve = get_curve_of(process->get_param_texture(ParticleProcessMaterial::PARAM_SCALE));
	if (scale_curve.is_valid()) {
		for (int i = 0; i < scale_curve->get_point_count(); i++) {
			const real_t x = x0 + (x1 - x0) * scale_curve->get_point_position(i).x;
			if (x < left || x > right) {
				continue;
			}
			draw_line(Point2(x, p_row_y + _row_height() * 0.8), Point2(x, p_row_y + _row_height() * 0.96), Color(0.6, 0.9, 1.0, 0.8 * p_alpha), 2 * EDSCALE);
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
			const real_t ruler_top = _ruler_top();
			const int decimals = step < 0.05 ? 3 : (step < 0.1 ? 2 : (step < 1.0 ? 1 : 0));
			// Content behind the rows: loop region body and second grid lines.
			if (has_loop_region()) {
				const real_t lx0 = MAX(_name_width(), _time_to_x(loop_start));
				const real_t lx1 = MIN(size.x, _time_to_x(loop_end));
				if (lx1 > lx0) {
					draw_rect(Rect2(lx0, _ruler_height(), lx1 - lx0, size.y - _ruler_height()), Color(0.35, 0.75, 1.0, 0.06));
				}
			}
			for (double t = Math::floor(MAX(0.0, view_start) / step) * step; t <= view_start + view_length + 0.0001; t += step) {
				const real_t x = _time_to_x(t);
				if (x >= _name_width() - 0.5) {
					draw_line(Point2(x, _ruler_height()), Point2(x, size.y), Color(font_color, 0.07));
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
					// Label inside the bar when it fits: particle count and how long
					// each particle lives (the thin tail), shorter when narrow.
					String label;
					String short_label;
					if (ParticleEffect3D::is_emitter(track)) {
						const int amount = int(track->get("amount"));
						const String life = String::num(double(track->get("lifetime")), 2);
						label = amount == 1 ? vformat(TTR("1 particle, life %s s"), life) : vformat(TTR("%d particles, life %s s"), amount, life);
						short_label = vformat(TTR("life %s s"), life);
					}
					const real_t label_width = font->get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8).x;
					const real_t short_width = font->get_string_size(short_label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8).x;
					const String shown = x_end - x_start > label_width + 12 * EDSCALE ? label : (x_end - x_start > short_width + 12 * EDSCALE ? short_label : String());
					if (!shown.is_empty()) {
						draw_string(font, Point2(x_start + 6 * EDSCALE, bar_top + bar_height * 0.72), shown, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8, Color(0, 0, 0, 0.75 * alpha));
					}
					if (active_end < 0.0) {
						draw_string(font, Point2(x_end - 16 * EDSCALE, bar_top + bar_height * 0.76), U"∞", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0, 0, 0, 0.7 * alpha));
					} else if (_time_to_x(active_end) <= right_limit) {
						draw_rect(Rect2(x_end - 3 * EDSCALE, bar_top + 3 * EDSCALE, 2 * EDSCALE, bar_height - 6 * EDSCALE), Color(1, 1, 1, 0.6 * alpha));
					}
				}
				// Over-life curves of the first particle (born at the start):
				// the Alpha over Life shape as a line, its points as diamonds,
				// Scale over Life points as ticks at the bottom.
				_draw_life_curves(track, start, y, alpha);
			}

			// Effect end marker.
			const real_t x_end_marker = _time_to_x(content_length);
			if (x_end_marker > _name_width() && x_end_marker < size.x) {
				for (real_t y = _ruler_height(); y < size.y; y += 8 * EDSCALE) {
					draw_line(Point2(x_end_marker, y), Point2(x_end_marker, MIN(size.y, y + 4 * EDSCALE)), Color(font_color, 0.35));
				}
				draw_string(font, Point2(x_end_marker + 3 * EDSCALE, size.y - 4 * EDSCALE), TTR("End"), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8, dim);
			}

			// Where a dragged track would land.
			if (drag_mode == DRAG_REORDER && drag_reorder_active && drag_reorder_gap >= 0) {
				const real_t gy = _ruler_height() + drag_reorder_gap * _row_height();
				draw_line(Point2(0, gy), Point2(size.x, gy), accent, 2 * EDSCALE);
				draw_rect(Rect2(0, gy - 3 * EDSCALE, 6 * EDSCALE, 6 * EDSCALE), accent);
			}

			// Marker lines through the tracks.
			for (int i = 0; i < markers.size(); i++) {
				const real_t mx = _time_to_x(markers[i].time);
				if (mx >= _name_width() && mx <= size.x) {
					draw_line(Point2(mx, _ruler_height()), Point2(mx, size.y), Color(get_marker_color(markers[i].color), i == drag_marker ? 0.8 : 0.35), 1.0 * EDSCALE);
				}
			}

			// Playhead with a handle in the ruler.
			const real_t x_play = _time_to_x(current_time);
			if (x_play >= _name_width() && x_play <= size.x) {
				const Color play_color(1.0, 0.36, 0.3);
				draw_line(Point2(x_play, ruler_top + _ruler_height()), Point2(x_play, size.y), play_color, 2 * EDSCALE);
			}

			// Sticky ruler on top of the rows, with the playhead handle.
			draw_rect(Rect2(0, ruler_top, size.x, _ruler_height()), base);
			draw_rect(Rect2(0, ruler_top, _name_width(), _ruler_height()), Color(0, 0, 0, 0.12));
			draw_rect(Rect2(_name_width(), ruler_top, size.x - _name_width(), _ruler_height()), Color(0, 0, 0, 0.25));
			if (has_loop_region()) {
				const real_t lx0 = MAX(_name_width(), _time_to_x(loop_start));
				const real_t lx1 = MIN(size.x, _time_to_x(loop_end));
				if (lx1 > lx0) {
					const Color loop_color(0.35, 0.75, 1.0);
					draw_rect(Rect2(lx0, ruler_top, lx1 - lx0, _ruler_height()), Color(loop_color, 0.35));
					for (real_t edge : { real_t(_time_to_x(loop_start)), real_t(_time_to_x(loop_end)) }) {
						if (edge >= _name_width() && edge <= size.x) {
							draw_line(Point2(edge, 0), Point2(edge, size.y), Color(loop_color, 0.7), 1.5 * EDSCALE);
						}
					}
				}
			}
			for (double t = Math::floor(MAX(0.0, view_start) / step) * step; t <= view_start + view_length + 0.0001; t += step) {
				const real_t x = _time_to_x(t);
				if (x < _name_width() - 0.5) {
					continue;
				}
				draw_line(Point2(x, ruler_top + _ruler_height() * 0.55), Point2(x, ruler_top + _ruler_height()), Color(font_color, 0.4));
				draw_string(font, Point2(x + 3 * EDSCALE, ruler_top + _ruler_height() * 0.7), String::num(t, decimals) + "s", HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.85, dim);
				// Minor ticks.
				for (int minor = 1; minor < 5; minor++) {
					const real_t mx = _time_to_x(t + step * minor / 5.0);
					draw_line(Point2(mx, ruler_top + _ruler_height() * 0.8), Point2(mx, ruler_top + _ruler_height()), Color(font_color, 0.18));
				}
			}

			// Playback start flag (green), always shown.
			const real_t x_flag = _time_to_x(get_playback_start());
			if (x_flag >= _name_width() - 1 && x_flag <= size.x) {
				const Color flag_color(0.4, 0.85, 0.45);
				const real_t bottom = ruler_top + _ruler_height();
				const real_t flag_h = _ruler_height() * 0.45;
				draw_line(Point2(x_flag, bottom - flag_h), Point2(x_flag, bottom), flag_color, 2 * EDSCALE);
				Vector<Point2> flag;
				flag.push_back(Point2(x_flag, bottom - flag_h));
				flag.push_back(Point2(x_flag + 9 * EDSCALE, bottom - flag_h * 0.72));
				flag.push_back(Point2(x_flag, bottom - flag_h * 0.44));
				draw_colored_polygon(flag, flag_color);
				if (has_loop_region()) {
					draw_line(Point2(x_flag, bottom), Point2(x_flag, size.y), Color(flag_color, 0.5), 1.0 * EDSCALE);
				}
			}
			// Playback end flag, the start flag mirrored.
			const real_t x_end_flag = _time_to_x(get_playback_end());
			if (x_end_flag >= _name_width() - 1 && x_end_flag <= size.x + 1) {
				const Color flag_color(0.4, 0.85, 0.45);
				const real_t bottom = ruler_top + _ruler_height();
				const real_t flag_h = _ruler_height() * 0.45;
				draw_line(Point2(x_end_flag, bottom - flag_h), Point2(x_end_flag, bottom), flag_color, 2 * EDSCALE);
				Vector<Point2> flag;
				flag.push_back(Point2(x_end_flag, bottom - flag_h));
				flag.push_back(Point2(x_end_flag - 9 * EDSCALE, bottom - flag_h * 0.72));
				flag.push_back(Point2(x_end_flag, bottom - flag_h * 0.44));
				draw_colored_polygon(flag, flag_color);
				if (has_loop_region()) {
					draw_line(Point2(x_end_flag, bottom), Point2(x_end_flag, size.y), Color(flag_color, 0.5), 1.0 * EDSCALE);
				}
			}

			// Marker heads (pentagons, as in Resolve) with their names in the
			// upper half of the ruler.
			marker_label_rects.resize(markers.size());
			for (int i = 0; i < markers.size(); i++) {
				marker_label_rects.write[i] = Rect2();
				const real_t mx = _time_to_x(markers[i].time);
				if (mx < _name_width() - 1 || mx > size.x + 1) {
					continue;
				}
				const Color color = get_marker_color(markers[i].color);
				const real_t w = 5 * EDSCALE;
				const real_t h = _ruler_height() * 0.46;
				Vector<Point2> head;
				head.push_back(Point2(mx - w, ruler_top + 1 * EDSCALE));
				head.push_back(Point2(mx + w, ruler_top + 1 * EDSCALE));
				head.push_back(Point2(mx + w, ruler_top + h * 0.6));
				head.push_back(Point2(mx, ruler_top + h));
				head.push_back(Point2(mx - w, ruler_top + h * 0.6));
				draw_colored_polygon(head, color);
				if (i == selected_marker) {
					Vector<Point2> outline = head;
					outline.push_back(head[0]);
					draw_polyline(outline, Color(1, 1, 1), 1.5 * EDSCALE);
					draw_line(Point2(mx, ruler_top + _ruler_height()), Point2(mx, size.y), Color(color, 0.9), 1.5 * EDSCALE);
				}
				const int small_size = int(font_size * 0.72);
				const real_t next_x = i + 1 < markers.size() ? _time_to_x(markers[i + 1].time) : size.x;
				const real_t room = (next_x > mx ? next_x : size.x) - mx - w - 6 * EDSCALE;
				if (room > 24 * EDSCALE) {
					const real_t label_width = MIN(MIN(room, real_t(140 * EDSCALE)), font->get_string_size(markers[i].name, HORIZONTAL_ALIGNMENT_LEFT, -1, small_size).x);
					draw_string(font, Point2(mx + w + 3 * EDSCALE, ruler_top + h * 0.82), markers[i].name, HORIZONTAL_ALIGNMENT_LEFT, MIN(room, 140 * EDSCALE), small_size, i == selected_marker ? Color(1, 1, 1) : Color(color, 0.95));
					marker_label_rects.write[i] = Rect2(mx + w, ruler_top, label_width + 6 * EDSCALE, _ruler_height() * 0.5);
				}
			}

			if (x_play >= _name_width() && x_play <= size.x) {
				const Color play_color(1.0, 0.36, 0.3);
				draw_line(Point2(x_play, ruler_top + _ruler_height() * 0.4), Point2(x_play, ruler_top + _ruler_height()), play_color, 2 * EDSCALE);
				Vector<Point2> handle;
				handle.push_back(Point2(x_play - 6 * EDSCALE, ruler_top));
				handle.push_back(Point2(x_play + 6 * EDSCALE, ruler_top));
				handle.push_back(Point2(x_play, ruler_top + _ruler_height() * 0.5));
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
	ADD_SIGNAL(MethodInfo("track_move_requested", PropertyInfo(Variant::OBJECT, "track"), PropertyInfo(Variant::OBJECT, "before")));
	ADD_SIGNAL(MethodInfo("markers_committed", PropertyInfo(Variant::ARRAY, "old_markers"), PropertyInfo(Variant::ARRAY, "new_markers")));
	ADD_SIGNAL(MethodInfo("loop_region_changed", PropertyInfo(Variant::FLOAT, "start"), PropertyInfo(Variant::FLOAT, "end")));
}

FPSParticleTimeline::FPSParticleTimeline() {
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_focus_mode(FOCUS_ALL);
	set_clip_contents(true);
	context_menu = memnew(PopupMenu);
	context_menu->connect(SceneStringName(id_pressed), callable_mp(this, &FPSParticleTimeline::_on_context_menu_id));
	add_child(context_menu);
	rename_popup = memnew(PopupPanel);
	add_child(rename_popup);
	rename_edit = memnew(LineEdit);
	rename_edit->set_custom_minimum_size(Size2(180, 0) * EDSCALE);
	rename_edit->set_placeholder(TTR("Marker name"));
	rename_edit->connect(SceneStringName(text_submitted), callable_mp(this, &FPSParticleTimeline::_on_marker_rename_submitted));
	rename_popup->add_child(rename_edit);
}
