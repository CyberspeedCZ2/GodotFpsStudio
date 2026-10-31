#ifndef FPS_STUDIO_GEOMETRY_H
#define FPS_STUDIO_GEOMETRY_H

#include "scene/resources/mesh.h"

namespace FPSStudioGeometry {

enum MeshOperation {
	MESH_OPERATION_MIRROR_X,
	MESH_OPERATION_MIRROR_Y,
	MESH_OPERATION_MIRROR_Z,
	MESH_OPERATION_EXTRUDE_FACES,
	MESH_OPERATION_INSET_FACES,
	MESH_OPERATION_DELETE_FACES,
	MESH_OPERATION_FLIP_FACES,
	MESH_OPERATION_SUBDIVIDE,
	MESH_OPERATION_CONFORM_NORMALS,
	MESH_OPERATION_MAX,
};

int get_shape_count();
const char *get_shape_name(int p_shape);
Ref<Mesh> create_mesh(int p_shape);
const char *get_mesh_operation_name(MeshOperation p_operation);
Ref<ArrayMesh> apply_mesh_operation(const Ref<Mesh> &p_mesh, MeshOperation p_operation, const Vector<int> &p_selected_faces = Vector<int>(), real_t p_amount = 1.0);

} // namespace FPSStudioGeometry

#endif // FPS_STUDIO_GEOMETRY_H
