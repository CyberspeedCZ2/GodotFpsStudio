#ifndef FPS_STUDIO_GEOMETRY_H
#define FPS_STUDIO_GEOMETRY_H

#include "scene/resources/mesh.h"

namespace FPSStudioGeometry {

int get_shape_count();
const char *get_shape_name(int p_shape);
Ref<Mesh> create_mesh(int p_shape);

} // namespace FPSStudioGeometry

#endif // FPS_STUDIO_GEOMETRY_H
