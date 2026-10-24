// SPDX-License-Identifier: MIT
#ifndef NPC_ROUTE_GEOMETRY_H
#define NPC_ROUTE_GEOMETRY_H

#include "core/math/vector3.h"
#include "core/templates/vector.h"

namespace NPCPathGeometry {

struct CornerArc {
	Vector<Vector3> points;
	real_t radius = 0.0;
};

// Inputs and radius are world-space metres, including under scaled Path3D nodes.
// Replace a straight-segment corner with a circular fillet; endpoints stay on
// the adjoining segments and neighbouring corners retain room for their fillets.
inline CornerArc create_corner_arc(const Vector3 &p_previous, const Vector3 &p_corner, const Vector3 &p_next, real_t p_radius) {
	CornerArc result;
	if (!Math::is_finite(p_radius) || p_radius <= 0.0) {
		return result;
	}
	const Vector3 incoming = p_corner - p_previous;
	const Vector3 outgoing = p_next - p_corner;
	const real_t incoming_length = incoming.length();
	const real_t outgoing_length = outgoing.length();
	if (incoming_length < 0.01 || outgoing_length < 0.01) {
		return result;
	}
	const Vector3 in_direction = incoming / incoming_length;
	const Vector3 out_direction = outgoing / outgoing_length;
	const real_t angle = Math::acos(CLAMP(in_direction.dot(out_direction), real_t(-1.0), real_t(1.0)));
	// A straight segment needs no fillet, and a reversal has no finite fillet.
	if (angle < Math::deg_to_rad(real_t(5.0)) || angle > Math::deg_to_rad(real_t(175.0))) {
		return result;
	}
	const real_t tangent_factor = Math::tan(angle * 0.5);
	const real_t trim = MIN(p_radius * tangent_factor, MIN(incoming_length, outgoing_length) * 0.45);
	result.radius = trim / tangent_factor;
	const Vector3 normal = in_direction.cross(out_direction).normalized();
	if (normal.is_zero_approx()) {
		return result;
	}
	Vector3 rotation_axis = normal;
	rotation_axis.normalize();
	const Vector3 entry = p_corner - in_direction * trim;
	const Vector3 exit = p_corner + out_direction * trim;
	const Vector3 center = entry + rotation_axis.cross(in_direction) * result.radius;
	const Vector3 radial = entry - center;
	// Short linear waypoints work with existing NPC controllers and have no
	// hidden Bezier handles that the runtime might ignore.
	const int segments = CLAMP(MAX(int(Math::ceil(angle / Math::deg_to_rad(real_t(22.5)))), int(Math::ceil(angle * result.radius / 0.35))), 2, 128);
	result.points.push_back(entry);
	for (int i = 1; i < segments; i++) {
		const real_t rotation_angle = angle * real_t(i) / real_t(segments);
		const real_t cosine = Math::cos(rotation_angle);
		const real_t sine = Math::sin(rotation_angle);
		const Vector3 rotated_radial = radial * cosine + rotation_axis.cross(radial) * sine + rotation_axis * (rotation_axis.dot(radial) * (1.0 - cosine));
		result.points.push_back(center + rotated_radial);
	}
	result.points.push_back(exit);
	return result;
}

struct CornerBezier {
	Vector3 entry;
	Vector3 exit;
	Vector3 entry_out_handle;
	Vector3 exit_in_handle;
	real_t radius = 0.0;
	bool valid = false;
};

// Creates a smooth 2-point cubic Bezier arc approximating a circular fillet
// between adjoining straight segments.
inline CornerBezier create_corner_bezier(const Vector3 &p_previous, const Vector3 &p_corner, const Vector3 &p_next, real_t p_radius) {
	CornerBezier result;
	if (!Math::is_finite(p_radius) || p_radius <= 0.0) {
		return result;
	}
	const Vector3 incoming = p_corner - p_previous;
	const Vector3 outgoing = p_next - p_corner;
	const real_t incoming_length = incoming.length();
	const real_t outgoing_length = outgoing.length();
	if (incoming_length < 0.01 || outgoing_length < 0.01) {
		return result;
	}
	const Vector3 in_direction = incoming / incoming_length;
	const Vector3 out_direction = outgoing / outgoing_length;
	const real_t angle = Math::acos(CLAMP(in_direction.dot(out_direction), real_t(-1.0), real_t(1.0)));
	// A straight segment needs no fillet, and a reversal has no finite fillet.
	if (angle < Math::deg_to_rad(real_t(5.0)) || angle > Math::deg_to_rad(real_t(175.0))) {
		return result;
	}
	const real_t tangent_factor = Math::tan(angle * 0.5);
	const real_t trim = MIN(p_radius * tangent_factor, MIN(incoming_length, outgoing_length) * 0.45);
	result.radius = trim / tangent_factor;
	result.entry = p_corner - in_direction * trim;
	result.exit = p_corner + out_direction * trim;

	// Cubic Bezier circular arc approximation:
	// Handle distance from anchor L = (4/3) * tan(angle / 4) * radius
	const real_t handle_length = (4.0 / 3.0) * Math::tan(angle * 0.25) * result.radius;
	result.entry_out_handle = in_direction * handle_length;
	result.exit_in_handle = -out_direction * handle_length;
	result.valid = true;
	return result;
}

} // namespace NPCPathGeometry

#endif // NPC_ROUTE_GEOMETRY_H
