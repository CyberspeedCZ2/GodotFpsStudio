#ifndef FPS_STUDIO_GEOMETRY_GIZMO_PLUGIN_H
#define FPS_STUDIO_GEOMETRY_GIZMO_PLUGIN_H

#include "editor/scene/3d/node_3d_editor_gizmos.h"

#include "core/templates/hash_map.h"

class MeshInstance3D;

class FPSStudioGeometryGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(FPSStudioGeometryGizmoPlugin, EditorNode3DGizmoPlugin);

	struct VertexReference {
		int surface = 0;
		int vertex = 0;
	};

	struct VertexGroup {
		Vector3 position;
		Vector<VertexReference> references;
	};

	struct DragState {
		Ref<Mesh> original_mesh;
		Variant original_vertex_arrays;
	};

	HashMap<ObjectID, DragState> drag_states;
	bool vertex_mode = false;

	static Vector<VertexGroup> _get_vertex_groups(const Ref<Mesh> &p_mesh);
	static Ref<ArrayMesh> _build_mesh_with_vertex_at(const Ref<Mesh> &p_source, const Vector<VertexReference> &p_references, const Vector3 &p_position);

protected:
	Ref<EditorNode3DGizmo> create_gizmo(Node3D *p_spatial) override;

public:
	void set_vertex_mode(bool p_enabled);
	bool has_gizmo(Node3D *p_spatial) override;
	String get_gizmo_name() const override;
	int get_priority() const override;
	void redraw(EditorNode3DGizmo *p_gizmo) override;
	int subgizmos_intersect_ray(const EditorNode3DGizmo *p_gizmo, Camera3D *p_camera, const Vector2 &p_point) const override;
	Vector<int> subgizmos_intersect_frustum(const EditorNode3DGizmo *p_gizmo, const Camera3D *p_camera, const Vector<Plane> &p_frustum) const override;
	Transform3D get_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id) const override;
	void set_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id, Transform3D p_transform) override;
	void commit_subgizmos(const EditorNode3DGizmo *p_gizmo, const Vector<int> &p_ids, const Vector<Transform3D> &p_restore, bool p_cancel = false) override;

	FPSStudioGeometryGizmoPlugin();
};

#endif // FPS_STUDIO_GEOMETRY_GIZMO_PLUGIN_H
