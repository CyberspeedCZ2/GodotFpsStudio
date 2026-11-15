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

// How long shake() keeps waiting for a current camera, e.g. when an effect
// with autoplay starts in the same frame as the level, before the player's
// camera became current.
static constexpr double PENDING_CAMERA_WAIT = 0.5;

Camera3D *CameraShake3D::_get_shaken_camera() const {
	return Object::cast_to<Camera3D>(ObjectDB::get_instance(camera_id));
}

real_t CameraShake3D::_get_falloff(const Camera3D *p_camera) const {
	if (radius <= 0.0) {
		return 1.0;
	}
	const real_t distance = p_camera->get_global_position().distance_to(get_global_position());
	const real_t inner = MIN(inner_radius, radius);
	if (distance <= inner) {
		return 1.0;
	}
	const real_t t = CLAMP((distance - inner) / MAX(real_t(0.001), radius - inner), 0.0, 1.0);
	return (1.0 - t) * (1.0 - t);
}

void CameraShake3D::_apply_offset(const Vector3 &p_rotation, const Vector3 &p_translation) {
	// Offsets are additive on the camera, so several shakes (and the game's own
	// camera motion) combine instead of overwriting each other.
	Camera3D *camera = _get_shaken_camera();
	if (camera) {
		camera->add_shake_offset(p_rotation - applied_rotation, p_translation - applied_translation);
	}
	applied_rotation = p_rotation;
	applied_translation = p_translation;
}

void CameraShake3D::_update_processing() {
	set_process_internal(active || pending_time > 0.0);
}

bool CameraShake3D::_try_start() {
	Viewport *viewport = is_inside_tree() ? get_viewport() : nullptr;
	Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
	if (!camera) {
		return false;
	}
	const real_t new_intensity = strength * _get_falloff(camera);
	pending_time = 0.0;
	if (new_intensity <= 0.0) {
		_update_processing();
		return true;
	}
	if (active && camera->get_instance_id() == camera_id) {
		// A new shake on the same camera: keep the stronger one.
		const real_t decay = real_t(1.0 - elapsed / MAX(0.001, duration));
		intensity = MAX(intensity * decay, new_intensity);
	} else {
		_apply_offset(Vector3(), Vector3());
		camera_id = camera->get_instance_id();
		intensity = new_intensity;
		noise_seed = uint32_t(Math::rand());
	}
	elapsed = 0.0;
	active = true;
	_update_processing();
	emit_signal(SNAME("shake_started"), intensity);
	return true;
}

void CameraShake3D::shake() {
	if (!is_inside_tree()) {
		return;
	}
	if (!_try_start()) {
		pending_time = PENDING_CAMERA_WAIT;
		_update_processing();
	}
}

void CameraShake3D::stop() {
	pending_time = 0.0;
	if (active) {
		active = false;
		_apply_offset(Vector3(), Vector3());
		camera_id = ObjectID();
	}
	_update_processing();
}

bool CameraShake3D::is_shaking() const {
	return active;
}

void CameraShake3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			const double delta = get_process_delta_time();
			if (!active) {
				pending_time -= delta;
				if (!_try_start() && pending_time <= 0.0) {
					pending_time = 0.0;
					_update_processing();
				}
				return;
			}
			elapsed += delta;
			if (!_get_shaken_camera() || elapsed >= duration) {
				stop();
				return;
			}
			// Trauma-style shake: smooth noise per axis scaled by an eased decay.
			const real_t decay = real_t(1.0 - elapsed / MAX(0.001, duration));
			const real_t amount = intensity * decay * decay;
			const double t = elapsed * frequency;
			const Vector3 rotation = Vector3(
					Math::deg_to_rad(max_rotation_degrees.x) * shake_noise(noise_seed, t),
					Math::deg_to_rad(max_rotation_degrees.y) * shake_noise(noise_seed + 7919u, t),
					Math::deg_to_rad(max_rotation_degrees.z) * shake_noise(noise_seed + 15887u, t * 0.8));
			const Vector3 translation = Vector3(
					shake_noise(noise_seed + 23003u, t),
					shake_noise(noise_seed + 31013u, t),
					0.0);
			_apply_offset(rotation * amount, translation * (max_offset * amount));
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

void CameraShake3D::set_inner_radius(real_t p_radius) {
	inner_radius = MAX(0.0, p_radius);
}

real_t CameraShake3D::get_inner_radius() const {
	return inner_radius;
}

void CameraShake3D::set_max_rotation_degrees(const Vector3 &p_degrees) {
	max_rotation_degrees = p_degrees.abs();
}

Vector3 CameraShake3D::get_max_rotation_degrees() const {
	return max_rotation_degrees;
}

void CameraShake3D::set_max_offset(real_t p_offset) {
	max_offset = MAX(0.0, p_offset);
}

real_t CameraShake3D::get_max_offset() const {
	return max_offset;
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
	ClassDB::bind_method(D_METHOD("set_inner_radius", "radius"), &CameraShake3D::set_inner_radius);
	ClassDB::bind_method(D_METHOD("get_inner_radius"), &CameraShake3D::get_inner_radius);
	ClassDB::bind_method(D_METHOD("set_max_rotation_degrees", "degrees"), &CameraShake3D::set_max_rotation_degrees);
	ClassDB::bind_method(D_METHOD("get_max_rotation_degrees"), &CameraShake3D::get_max_rotation_degrees);
	ClassDB::bind_method(D_METHOD("set_max_offset", "offset"), &CameraShake3D::set_max_offset);
	ClassDB::bind_method(D_METHOD("get_max_offset"), &CameraShake3D::get_max_offset);
	ClassDB::bind_method(D_METHOD("shake"), &CameraShake3D::shake);
	ClassDB::bind_method(D_METHOD("stop"), &CameraShake3D::stop);
	ClassDB::bind_method(D_METHOD("is_shaking"), &CameraShake3D::is_shaking);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration", PROPERTY_HINT_RANGE, "0.01,5,0.01,or_greater,suffix:s"), "set_duration", "get_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency", PROPERTY_HINT_RANGE, "1,60,0.1,suffix:Hz"), "set_frequency", "get_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "max_rotation_degrees", PROPERTY_HINT_RANGE, "0,30,0.1,or_greater,degrees"), "set_max_rotation_degrees", "get_max_rotation_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_offset", PROPERTY_HINT_RANGE, "0,0.5,0.001,or_greater,suffix:m"), "set_max_offset", "get_max_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inner_radius", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:m"), "set_inner_radius", "get_inner_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,200,0.1,or_greater,suffix:m"), "set_radius", "get_radius");

	ADD_SIGNAL(MethodInfo("shake_started", PropertyInfo(Variant::FLOAT, "intensity")));
}

CameraShake3D::CameraShake3D() {
}
