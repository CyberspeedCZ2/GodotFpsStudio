/**************************************************************************/
/*  particle_event_gizmos.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "editor/scene/3d/node_3d_editor_gizmos.h"

// Wire spheres for the reach of camera shakes and area damage: a full-strength
// inner radius and an outer fade-out radius. Shared by the Level editor gizmo
// and the Particle workspace preview.
namespace FPSParticleEventRadii {
// Appends line pairs for three orthogonal circles of the given radius.
void append_sphere_rings(Vector<Vector3> &r_lines, real_t p_radius, int p_segments = 64);
// Returns true and fills radii/color when p_node has a reach to visualize.
bool get_reach(const Node *p_node, real_t &r_inner, real_t &r_outer, Color &r_color);
} // namespace FPSParticleEventRadii

class FPSParticleEventGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(FPSParticleEventGizmoPlugin, EditorNode3DGizmoPlugin);

public:
	bool has_gizmo(Node3D *p_node) override;
	String get_gizmo_name() const override;
	int get_priority() const override;
	void redraw(EditorNode3DGizmo *p_gizmo) override;

	FPSParticleEventGizmoPlugin();
};
