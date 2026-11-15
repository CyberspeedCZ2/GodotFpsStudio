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

	real_t strength = 0.6;
	real_t frequency = 18.0;
	double duration = 0.6;
	real_t radius = 30.0;
	real_t inner_radius = 4.0;
	Vector3 max_rotation_degrees = Vector3(3.0, 3.0, 4.0);
	real_t max_offset = 0.03;

	bool active = false;
	double elapsed = 0.0;
	double pending_time = 0.0;
	real_t intensity = 0.0;
	ObjectID camera_id;
	Vector3 applied_rotation;
	Vector3 applied_translation;
	uint32_t noise_seed = 0;

	Camera3D *_get_shaken_camera() const;
	real_t _get_falloff(const Camera3D *p_camera) const;
	bool _try_start();
	void _apply_offset(const Vector3 &p_rotation, const Vector3 &p_translation);
	void _update_processing();

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
	void set_inner_radius(real_t p_radius);
	real_t get_inner_radius() const;
	void set_max_rotation_degrees(const Vector3 &p_degrees);
	Vector3 get_max_rotation_degrees() const;
	void set_max_offset(real_t p_offset);
	real_t get_max_offset() const;

	void shake();
	void stop();
	bool is_shaking() const;

	CameraShake3D();
};
