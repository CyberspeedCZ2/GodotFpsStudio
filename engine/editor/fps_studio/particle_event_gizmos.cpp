/**************************************************************************/
/*  particle_event_gizmos.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_event_gizmos.h"

#include "scene/3d/camera_shake_3d.h"
#include "scene/3d/radial_damage_3d.h"

static const Color DAMAGE_COLOR = Color(1.0, 0.32, 0.26);
static const Color SHAKE_COLOR = Color(0.36, 0.72, 1.0);

void FPSParticleEventRadii::append_sphere_rings(Vector<Vector3> &r_lines, real_t p_radius, int p_segments) {
	if (p_radius <= 0.0) {
		return;
	}
	for (int axis = 0; axis < 3; axis++) {
		for (int i = 0; i < p_segments; i++) {
			const real_t a0 = Math::TAU * i / p_segments;
			const real_t a1 = Math::TAU * (i + 1) / p_segments;
			const Vector2 p0 = Vector2(Math::cos(a0), Math::sin(a0)) * p_radius;
			const Vector2 p1 = Vector2(Math::cos(a1), Math::sin(a1)) * p_radius;
			switch (axis) {
				case 0: // Ground plane first: the most readable ring in an FPS level.
					r_lines.push_back(Vector3(p0.x, 0, p0.y));
					r_lines.push_back(Vector3(p1.x, 0, p1.y));
					break;
				case 1:
					r_lines.push_back(Vector3(p0.x, p0.y, 0));
					r_lines.push_back(Vector3(p1.x, p1.y, 0));
					break;
				default:
					r_lines.push_back(Vector3(0, p0.x, p0.y));
					r_lines.push_back(Vector3(0, p1.x, p1.y));
					break;
			}
		}
	}
}

bool FPSParticleEventRadii::get_reach(const Node *p_node, real_t &r_inner, real_t &r_outer, Color &r_color) {
	if (const RadialDamage3D *damage = Object::cast_to<RadialDamage3D>(p_node)) {
		r_inner = MIN(damage->get_inner_radius(), damage->get_radius());
		r_outer = damage->get_radius();
		r_color = DAMAGE_COLOR;
		return true;
	}
	if (const CameraShake3D *shake = Object::cast_to<CameraShake3D>(p_node)) {
		r_inner = shake->get_radius() > 0.0 ? MIN(shake->get_inner_radius(), shake->get_radius()) : 0.0;
		r_outer = shake->get_radius();
		r_color = SHAKE_COLOR;
		return true;
	}
	return false;
}

bool FPSParticleEventGizmoPlugin::has_gizmo(Node3D *p_node) {
	return Object::cast_to<RadialDamage3D>(p_node) || Object::cast_to<CameraShake3D>(p_node);
}

String FPSParticleEventGizmoPlugin::get_gizmo_name() const {
	return "ParticleEventReach";
}

int FPSParticleEventGizmoPlugin::get_priority() const {
	return -1;
}

void FPSParticleEventGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();
	real_t inner = 0.0;
	real_t outer = 0.0;
	Color color;
	if (!FPSParticleEventRadii::get_reach(p_gizmo->get_node_3d(), inner, outer, color)) {
		return;
	}
	const bool damage = Object::cast_to<RadialDamage3D>(p_gizmo->get_node_3d()) != nullptr;
	Vector<Vector3> inner_lines;
	FPSParticleEventRadii::append_sphere_rings(inner_lines, inner);
	Vector<Vector3> outer_lines;
	FPSParticleEventRadii::append_sphere_rings(outer_lines, outer);
	if (!inner_lines.is_empty()) {
		p_gizmo->add_lines(inner_lines, get_material(damage ? "damage_inner" : "shake_inner", p_gizmo));
	}
	if (!outer_lines.is_empty()) {
		p_gizmo->add_lines(outer_lines, get_material(damage ? "damage_outer" : "shake_outer", p_gizmo));
	}
}

FPSParticleEventGizmoPlugin::FPSParticleEventGizmoPlugin() {
	create_material("damage_inner", DAMAGE_COLOR);
	create_material("damage_outer", Color(DAMAGE_COLOR, 0.45));
	create_material("shake_inner", SHAKE_COLOR);
	create_material("shake_outer", Color(SHAKE_COLOR, 0.45));
}
