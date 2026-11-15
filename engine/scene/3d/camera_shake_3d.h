/**************************************************************************/
/*  camera_shake_3d.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/object/object_id.h"
#include "scene/3d/node_3d.h"

// Shakes the active 3D camera of its viewport for a short time, with an
// intensity that decays over the duration and falls off with distance.
// Used as a timed event of ParticleEffect3D (explosions, impacts) and
// usable on its own by calling shake().
class CameraShake3D : public Node3D {
	GDCLASS(CameraShake3D, Node3D);

	real_t strength = 0.25;
	real_t frequency = 22.0;
	double duration = 0.5;
	real_t radius = 20.0;

	bool active = false;
	double elapsed = 0.0;
	real_t applied_strength = 0.0;
	ObjectID camera_id;
	real_t camera_h_offset = 0.0;
	real_t camera_v_offset = 0.0;
	uint32_t noise_seed = 0;

	void _restore_camera();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_strength(real_t p_strength);
	real_t get_strength() const;
	void set_frequency(real_t p_frequency);
	real_t get_frequency() const;
	void set_duration(double p_duration);
	double get_duration() const;
	void set_radius(real_t p_radius);
	real_t get_radius() const;

	void shake();
	void stop();
	bool is_shaking() const;

	CameraShake3D();
};
