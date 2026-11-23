/**************************************************************************/
/*  screen_flash_3d.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "scene/3d/node_3d.h"

// A short flash of color over the whole view (a blast going off close by, a
// flashbang), weaker with the distance from the camera. Fires on flash() or
// at its time inside a ParticleEffect3D.
class ScreenFlash3D : public Node3D {
	GDCLASS(ScreenFlash3D, Node3D);

	Color color = Color(1.0, 0.95, 0.85, 1.0);
	real_t strength = 0.6;
	real_t duration = 0.35;
	real_t inner_radius = 3.0;
	real_t radius = 12.0;

protected:
	static void _bind_methods();

public:
	void set_color(const Color &p_color);
	Color get_color() const;
	void set_strength(real_t p_strength);
	real_t get_strength() const;
	void set_duration(real_t p_duration);
	real_t get_duration() const;
	void set_inner_radius(real_t p_radius);
	real_t get_inner_radius() const;
	void set_radius(real_t p_radius);
	real_t get_radius() const;

	// Strength at the current camera, 0..strength.
	real_t get_strength_at_camera() const;
	void flash();
};
