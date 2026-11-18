/**************************************************************************/
/*  particle_preview_compare.h                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "scene/gui/control.h"

class Texture2D;

// A/B wipe over the particle preview (like Gallery + Wipe in a color
// grading tool): left of the divider shows a snapshot taken earlier (A),
// right of it the live preview (B). Drag the divider to compare.
class FPSPreviewCompareOverlay : public Control {
	GDCLASS(FPSPreviewCompareOverlay, Control);

	Ref<Texture2D> snapshot;
	String snapshot_label;
	real_t split = 0.5; // 0..1 across the overlay.
	bool dragging = false;

	real_t _split_x() const { return get_size().x * split; }
	bool _near_divider(const Point2 &p_pos) const;

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;

public:
	void set_snapshot(const Ref<Texture2D> &p_texture, const String &p_label);
	bool has_snapshot() const { return snapshot.is_valid(); }
	void clear_snapshot();

	virtual CursorShape get_cursor_shape(const Point2 &p_pos) const override;
	virtual String get_tooltip(const Point2 &p_pos) const override;

	FPSPreviewCompareOverlay();
};
