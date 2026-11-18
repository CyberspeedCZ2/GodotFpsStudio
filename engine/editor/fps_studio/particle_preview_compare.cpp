/**************************************************************************/
/*  particle_preview_compare.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_preview_compare.h"

#include "core/input/input_event.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/font.h"
#include "scene/resources/texture.h"

bool FPSPreviewCompareOverlay::_near_divider(const Point2 &p_pos) const {
	return snapshot.is_valid() && Math::abs(p_pos.x - _split_x()) <= 8 * EDSCALE;
}

void FPSPreviewCompareOverlay::set_snapshot(const Ref<Texture2D> &p_texture, const String &p_label) {
	snapshot = p_texture;
	snapshot_label = p_label;
	dragging = false;
	queue_redraw();
}

void FPSPreviewCompareOverlay::clear_snapshot() {
	snapshot.unref();
	dragging = false;
	queue_redraw();
}

Control::CursorShape FPSPreviewCompareOverlay::get_cursor_shape(const Point2 &p_pos) const {
	return (dragging || _near_divider(p_pos)) ? CURSOR_HSIZE : Control::get_cursor_shape(p_pos);
}

String FPSPreviewCompareOverlay::get_tooltip(const Point2 &p_pos) const {
	if (_near_divider(p_pos)) {
		return TTR("Drag to compare: left is the snapshot (A), right is the live preview (B).");
	}
	return Control::get_tooltip(p_pos);
}

void FPSPreviewCompareOverlay::gui_input(const Ref<InputEvent> &p_event) {
	// Only the divider is handled here; everything else (orbit, zoom) falls
	// through to the preview container below.
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed() && _near_divider(mb->get_position())) {
			dragging = true;
			accept_event();
		} else if (!mb->is_pressed() && dragging) {
			dragging = false;
			accept_event();
		}
		return;
	}
	Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && dragging) {
		split = CLAMP(mm->get_position().x / MAX(real_t(1.0), get_size().x), 0.0, 1.0);
		queue_redraw();
		accept_event();
	}
}

void FPSPreviewCompareOverlay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_RESIZED: {
			queue_redraw();
		} break;
		case NOTIFICATION_DRAW: {
			if (snapshot.is_null()) {
				return;
			}
			const Size2 size = get_size();
			const real_t x = _split_x();
			if (x > 0.0) {
				// The snapshot is stretched to the current preview size, so it
				// still lines up after the panel was resized.
				const Size2 tex_size = snapshot->get_size();
				const Rect2 src(0, 0, tex_size.x * (x / size.x), tex_size.y);
				draw_texture_rect_region(snapshot, Rect2(0, 0, x, size.y), src);
			}
			const Color line_color(1, 1, 1, 0.9);
			draw_line(Point2(x, 0), Point2(x, size.y), Color(0, 0, 0, 0.5), 3 * EDSCALE);
			draw_line(Point2(x, 0), Point2(x, size.y), line_color, 1.5 * EDSCALE);
			// Grip in the middle of the divider.
			const real_t grip_h = 36 * EDSCALE;
			draw_rect(Rect2(x - 4 * EDSCALE, size.y * 0.5 - grip_h * 0.5, 8 * EDSCALE, grip_h), line_color);

			const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
			const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
			const String a_text = snapshot_label.is_empty() ? String("A") : "A  " + snapshot_label;
			const String b_text = TTR("B  now");
			const real_t pad = 6 * EDSCALE;
			const real_t top = size.y - font_size - pad * 3;
			const Size2 a_size = font->get_string_size(a_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
			const Size2 b_size = font->get_string_size(b_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
			if (x - a_size.x - pad * 3 > 0) {
				draw_rect(Rect2(x - a_size.x - pad * 3, top, a_size.x + pad * 2, font_size + pad * 2), Color(0, 0, 0, 0.55));
				draw_string(font, Point2(x - a_size.x - pad * 2, top + pad + font_size * 0.85), a_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(1, 1, 1));
			}
			if (x + b_size.x + pad * 3 < size.x) {
				draw_rect(Rect2(x + pad, top, b_size.x + pad * 2, font_size + pad * 2), Color(0, 0, 0, 0.55));
				draw_string(font, Point2(x + pad * 2, top + pad + font_size * 0.85), b_text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(1, 1, 1));
			}
		} break;
	}
}

FPSPreviewCompareOverlay::FPSPreviewCompareOverlay() {
	set_mouse_filter(MOUSE_FILTER_PASS);
}
