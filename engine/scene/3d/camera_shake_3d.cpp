/**************************************************************************/
/*  camera_shake_3d.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "camera_shake_3d.h"

#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"

static real_t shake_noise(uint32_t p_seed, double p_t) {
	// Smooth 1D value noise in -1..1.
	const double i = Math::floor(p_t);
	const double f = p_t - i;
	auto hash = [](uint32_t p_x) -> real_t {
		p_x ^= p_x >> 16;
		p_x *= 0x7feb352du;
		p_x ^= p_x >> 15;
		p_x *= 0x846ca68bu;
		p_x ^= p_x >> 16;
		return real_t(p_x & 0xffffu) / 32767.5 - 1.0;
	};
	const real_t a = hash(uint32_t(int64_t(i)) * 2654435761u + p_seed);
	const real_t b = hash(uint32_t(int64_t(i) + 1) * 2654435761u + p_seed);
	const real_t s = real_t(f * f * (3.0 - 2.0 * f));
	return a + (b - a) * s;
}

void CameraShake3D::_restore_camera() {
	Camera3D *camera = Object::cast_to<Camera3D>(ObjectDB::get_instance(camera_id));
	if (camera) {
		camera->set_h_offset(camera_h_offset);
		camera->set_v_offset(camera_v_offset);
	}
	camera_id = ObjectID();
}

void CameraShake3D::shake() {
	if (!is_inside_tree()) {
		return;
	}
	Viewport *viewport = get_viewport();
	Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
	if (!camera) {
		return;
	}
	real_t falloff = 1.0;
	if (radius > 0.0) {
		const real_t distance = camera->get_global_position().distance_to(get_global_position());
		falloff = CLAMP(1.0 - distance / radius, 0.0, 1.0);
		falloff *= falloff;
	}
	if (falloff <= 0.0 || strength <= 0.0) {
		return;
	}
	if (active && camera->get_instance_id() == camera_id) {
		// A new shake on the same camera: keep the stronger one.
		applied_strength = MAX(applied_strength * real_t(1.0 - elapsed / MAX(0.001, duration)), strength * falloff);
		elapsed = 0.0;
		return;
	}
	_restore_camera();
	camera_id = camera->get_instance_id();
	camera_h_offset = camera->get_h_offset();
	camera_v_offset = camera->get_v_offset();
	applied_strength = strength * falloff;
	elapsed = 0.0;
	noise_seed = uint32_t(Math::rand());
	active = true;
	set_process_internal(true);
}

void CameraShake3D::stop() {
	if (!active) {
		return;
	}
	active = false;
	set_process_internal(false);
	_restore_camera();
}

bool CameraShake3D::is_shaking() const {
	return active;
}

void CameraShake3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			Camera3D *camera = Object::cast_to<Camera3D>(ObjectDB::get_instance(camera_id));
			elapsed += get_process_delta_time();
			if (!camera || elapsed >= duration) {
				stop();
				return;
			}
			const real_t decay = real_t(1.0 - elapsed / MAX(0.001, duration));
			const real_t amount = applied_strength * decay * decay;
			const double t = elapsed * frequency;
			camera->set_h_offset(camera_h_offset + shake_noise(noise_seed, t) * amount);
			camera->set_v_offset(camera_v_offset + shake_noise(noise_seed + 7919u, t) * amount);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			stop();
		} break;
	}
}

void CameraShake3D::set_strength(real_t p_strength) {
	strength = MAX(0.0, p_strength);
}

real_t CameraShake3D::get_strength() const {
	return strength;
}

void CameraShake3D::set_frequency(real_t p_frequency) {
	frequency = MAX(0.1, p_frequency);
}

real_t CameraShake3D::get_frequency() const {
	return frequency;
}

void CameraShake3D::set_duration(double p_duration) {
	duration = MAX(0.01, p_duration);
}

double CameraShake3D::get_duration() const {
	return duration;
}

void CameraShake3D::set_radius(real_t p_radius) {
	radius = MAX(0.0, p_radius);
}

real_t CameraShake3D::get_radius() const {
	return radius;
}

void CameraShake3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &CameraShake3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &CameraShake3D::get_strength);
	ClassDB::bind_method(D_METHOD("set_frequency", "frequency"), &CameraShake3D::set_frequency);
	ClassDB::bind_method(D_METHOD("get_frequency"), &CameraShake3D::get_frequency);
	ClassDB::bind_method(D_METHOD("set_duration", "duration"), &CameraShake3D::set_duration);
	ClassDB::bind_method(D_METHOD("get_duration"), &CameraShake3D::get_duration);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &CameraShake3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &CameraShake3D::get_radius);
	ClassDB::bind_method(D_METHOD("shake"), &CameraShake3D::shake);
	ClassDB::bind_method(D_METHOD("stop"), &CameraShake3D::stop);
	ClassDB::bind_method(D_METHOD("is_shaking"), &CameraShake3D::is_shaking);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:m"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency", PROPERTY_HINT_RANGE, "1,60,0.1,suffix:Hz"), "set_frequency", "get_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0.01,5,0.01,or_greater,suffix:s"), "set_duration", "get_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,200,0.1,or_greater,suffix:m"), "set_radius", "get_radius");
}

CameraShake3D::CameraShake3D() {
}
