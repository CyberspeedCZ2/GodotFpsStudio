#include "fps_studio_geometry.h"

#include "core/math/math_funcs.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/surface_tool.h"

namespace FPSStudioGeometry {

static const char *shape_names[] = {
	"Cube", "Plane", "Cylinder", "Cone", "Prism", "Stairs", "Arch", "Poly Shape",
	"Door", "Sphere", "Torus", "Pipe", "Curved Stairs", "Trim", "Trim Walls", "Wall",
};

int get_shape_count() {
	return int(sizeof(shape_names) / sizeof(shape_names[0]));
}

const char *get_shape_name(int p_shape) {
	return p_shape >= 0 && p_shape < get_shape_count() ? shape_names[p_shape] : "Geometry";
}

const char *get_mesh_operation_name(MeshOperation p_operation) {
	static const char *operation_names[] = { "Mirror X", "Mirror Y", "Mirror Z", "Subdivide", "Conform Normals" };
	return p_operation >= 0 && p_operation < MESH_OPERATION_MAX ? operation_names[p_operation] : "Mesh Operation";
}

static void _set_smooth_groups_from_normals(Ref<SurfaceTool> p_surface_tool) {
	HashMap<Vector3, uint32_t> normal_groups;
	uint32_t next_group = 0;
	for (SurfaceTool::Vertex &vertex : p_surface_tool->get_vertex_array()) {
		uint32_t *group = normal_groups.getptr(vertex.normal);
		if (!group) {
			normal_groups.insert(vertex.normal, next_group);
			group = normal_groups.getptr(vertex.normal);
			next_group++;
		}
		vertex.smooth_group = *group;
	}
}

static SurfaceTool::Vertex _interpolate_geometry_vertex(const SurfaceTool::Vertex &p_a, const SurfaceTool::Vertex &p_b) {
	SurfaceTool::Vertex midpoint;
	midpoint.vertex = (p_a.vertex + p_b.vertex) * 0.5;
	midpoint.normal = (p_a.normal + p_b.normal).normalized();
	midpoint.binormal = (p_a.binormal + p_b.binormal).normalized();
	midpoint.tangent = (p_a.tangent + p_b.tangent).normalized();
	midpoint.uv = (p_a.uv + p_b.uv) * 0.5;
	midpoint.uv2 = (p_a.uv2 + p_b.uv2) * 0.5;
	midpoint.color = p_a.color.lerp(p_b.color, 0.5);
	midpoint.smooth_group = p_a.smooth_group == p_b.smooth_group ? p_a.smooth_group : UINT32_MAX;
	for (int channel = 0; channel < RSE::ARRAY_CUSTOM_COUNT; channel++) {
		midpoint.custom[channel] = p_a.custom[channel].lerp(p_b.custom[channel], 0.5);
	}
	if (p_a.bones.size() == p_b.bones.size() && p_a.weights.size() == p_b.weights.size()) {
		midpoint.bones = p_a.bones;
		midpoint.weights.resize(p_a.weights.size());
		for (int i = 0; i < p_a.weights.size(); i++) {
			midpoint.weights.write[i] = (p_a.weights[i] + p_b.weights[i]) * 0.5;
		}
	}
	return midpoint;
}

Ref<ArrayMesh> apply_mesh_operation(const Ref<Mesh> &p_mesh, MeshOperation p_operation) {
	ERR_FAIL_COND_V(p_mesh.is_null() || p_mesh->get_surface_count() == 0, Ref<ArrayMesh>());
	ERR_FAIL_INDEX_V(p_operation, MESH_OPERATION_MAX, Ref<ArrayMesh>());

	Ref<ArrayMesh> result;
	result.instantiate();
	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		const Mesh::PrimitiveType primitive_type = p_mesh->surface_get_primitive_type(surface);
		Ref<SurfaceTool> surface_tool;
		surface_tool.instantiate();
		surface_tool->create_from_arrays(p_mesh->surface_get_arrays(surface), primitive_type);
		const Ref<Material> surface_material = p_mesh->surface_get_material(surface);
		surface_tool->set_material(surface_material);

		if (primitive_type == Mesh::PRIMITIVE_TRIANGLES && (p_operation == MESH_OPERATION_CONFORM_NORMALS || p_operation == MESH_OPERATION_SUBDIVIDE || p_operation <= MESH_OPERATION_MIRROR_Z)) {
			_set_smooth_groups_from_normals(surface_tool);
		}

		if (p_operation <= MESH_OPERATION_MIRROR_Z) {
			const int axis = int(p_operation);
			if (primitive_type == Mesh::PRIMITIVE_TRIANGLES) {
				surface_tool->deindex();
			}
			LocalVector<SurfaceTool::Vertex> &vertices = surface_tool->get_vertex_array();
			for (SurfaceTool::Vertex &vertex : vertices) {
				vertex.vertex[axis] = -vertex.vertex[axis];
				if (primitive_type != Mesh::PRIMITIVE_TRIANGLES) {
					vertex.normal[axis] = -vertex.normal[axis];
					vertex.binormal[axis] = -vertex.binormal[axis];
					vertex.tangent[axis] = -vertex.tangent[axis];
				}
			}
			if (primitive_type == Mesh::PRIMITIVE_TRIANGLES) {
				for (int i = 0; i + 2 < vertices.size(); i += 3) {
					SWAP(vertices[i + 1], vertices[i + 2]);
				}
				surface_tool->generate_normals();
			}
		} else if (p_operation == MESH_OPERATION_CONFORM_NORMALS && primitive_type == Mesh::PRIMITIVE_TRIANGLES) {
			surface_tool->generate_normals();
		} else if (p_operation == MESH_OPERATION_SUBDIVIDE && primitive_type == Mesh::PRIMITIVE_TRIANGLES) {
			surface_tool->deindex();
			LocalVector<SurfaceTool::Vertex> &vertices = surface_tool->get_vertex_array();
			if (vertices.size() % 3 != 0) {
				return Ref<ArrayMesh>();
			}
			LocalVector<SurfaceTool::Vertex> subdivided_vertices;
			subdivided_vertices.reserve(vertices.size() * 4);
			for (int i = 0; i + 2 < vertices.size(); i += 3) {
				const SurfaceTool::Vertex a = vertices[i];
				const SurfaceTool::Vertex b = vertices[i + 1];
				const SurfaceTool::Vertex c = vertices[i + 2];
				const SurfaceTool::Vertex ab = _interpolate_geometry_vertex(a, b);
				const SurfaceTool::Vertex bc = _interpolate_geometry_vertex(b, c);
				const SurfaceTool::Vertex ca = _interpolate_geometry_vertex(c, a);
				const SurfaceTool::Vertex triangles[] = {
					a, ab, ca,
					ab, b, bc,
					ca, bc, c,
					ab, bc, ca,
				};
				for (const SurfaceTool::Vertex &vertex : triangles) {
					subdivided_vertices.push_back(vertex);
				}
			}
			vertices.clear();
			for (const SurfaceTool::Vertex &vertex : subdivided_vertices) {
				vertices.push_back(vertex);
			}
			surface_tool->generate_normals();
		}

		const int result_surface = result->get_surface_count();
		result = surface_tool->commit(result);
		ERR_FAIL_COND_V(result.is_null(), Ref<ArrayMesh>());
		result->surface_set_material(result_surface, surface_material);
	}
	return result;
}

Ref<Mesh> create_mesh(int p_shape) {
	Ref<BoxMesh> box;
	Ref<PlaneMesh> plane;
	Ref<CylinderMesh> cylinder;
	Ref<PrismMesh> prism;
	Ref<SphereMesh> sphere;
	Ref<TorusMesh> torus;

	switch (p_shape) {
		case 0: // Box.
			box.instantiate();
			box->set_size(Vector3(2.0, 2.0, 2.0));
			return box;
		case 1: // Plane.
			plane.instantiate();
			plane->set_size(Size2(2.0, 2.0));
			return plane;
		case 2: // Cylinder.
			cylinder.instantiate();
			cylinder->set_top_radius(0.65);
			cylinder->set_bottom_radius(0.65);
			cylinder->set_radial_segments(32);
			return cylinder;
		case 3: // Cone.
			cylinder.instantiate();
			cylinder->set_top_radius(0.0);
			cylinder->set_bottom_radius(1.0);
			cylinder->set_radial_segments(32);
			return cylinder;
		case 4: // Prism.
			prism.instantiate();
			prism->set_size(Vector3(2.0, 2.0, 2.0));
			return prism;
		case 5: // Stairs.
		case 6: // Arch.
		case 7: // Poly Shape.
		case 8: // Door.
		case 11: // Pipe.
		case 12: // Curved Stairs.
		case 13: // Trim.
		case 14: // Trim Walls.
		case 15: { // Wall.
			Ref<SurfaceTool> surface_tool;
			surface_tool.instantiate();
			surface_tool->begin(Mesh::PRIMITIVE_TRIANGLES);

			auto append_box = [&](const Vector3 &p_center, const Vector3 &p_size, const Vector3 &p_rotation) {
				Ref<BoxMesh> part;
				part.instantiate();
				part->set_size(p_size);
				Transform3D transform(Basis::from_euler(p_rotation), p_center);
				surface_tool->append_from(part, 0, transform);
			};

			if (p_shape == 5) {
				for (int i = 0; i < 8; i++) {
					const real_t height = (i + 1) * 0.25;
					append_box(Vector3(-0.875 + i * 0.25, height * 0.5, 0.0), Vector3(0.25, height, 1.5), Vector3());
				}
			} else if (p_shape == 6) {
				append_box(Vector3(-0.75, -0.25, 0.0), Vector3(0.28, 1.5, 0.45), Vector3());
				append_box(Vector3(0.75, -0.25, 0.0), Vector3(0.28, 1.5, 0.45), Vector3());
				for (int i = 0; i < 8; i++) {
					const real_t angle = Math::PI * i / 7.0;
					const real_t center_x = 0.75 * Math::cos(angle);
					const real_t center_y = 0.55 + 0.75 * Math::sin(angle);
					append_box(Vector3(center_x, center_y, 0.0), Vector3(0.36, 0.22, 0.45), Vector3(0.0, 0.0, angle + Math::PI / 2.0));
				}
			} else if (p_shape == 8) {
				append_box(Vector3(-0.82, 0.0, 0.0), Vector3(0.28, 2.0, 0.45), Vector3());
				append_box(Vector3(0.82, 0.0, 0.0), Vector3(0.28, 2.0, 0.45), Vector3());
				append_box(Vector3(0.0, 0.86, 0.0), Vector3(1.36, 0.28, 0.45), Vector3());
			} else if (p_shape == 7) {
				cylinder.instantiate();
				cylinder->set_top_radius(1.0);
				cylinder->set_bottom_radius(1.0);
				cylinder->set_radial_segments(6);
				return cylinder;
			} else if (p_shape == 11) {
				const int segments = 32;
				auto add_quad = [&](const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Vector3 &p_d) {
					const Vector3 normal = (p_b - p_a).cross(p_c - p_a).normalized();
					const Vector3 vertices[6] = { p_a, p_b, p_c, p_a, p_c, p_d };
					const Vector2 uvs[6] = { Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 0), Vector2(1, 1), Vector2(0, 1) };
					for (int vertex = 0; vertex < 6; vertex++) {
						surface_tool->set_normal(normal);
						surface_tool->set_uv(uvs[vertex]);
						surface_tool->add_vertex(vertices[vertex]);
					}
				};
				for (int i = 0; i < segments; i++) {
					const real_t a0 = Math::TAU * i / segments;
					const real_t a1 = Math::TAU * (i + 1) / segments;
					const Vector3 outer_bottom_0(Math::cos(a0) * 0.6, -1.0, Math::sin(a0) * 0.6);
					const Vector3 outer_bottom_1(Math::cos(a1) * 0.6, -1.0, Math::sin(a1) * 0.6);
					const Vector3 outer_top_0(Math::cos(a0) * 0.6, 1.0, Math::sin(a0) * 0.6);
					const Vector3 outer_top_1(Math::cos(a1) * 0.6, 1.0, Math::sin(a1) * 0.6);
					const Vector3 inner_bottom_0(Math::cos(a0) * 0.38, -1.0, Math::sin(a0) * 0.38);
					const Vector3 inner_bottom_1(Math::cos(a1) * 0.38, -1.0, Math::sin(a1) * 0.38);
					const Vector3 inner_top_0(Math::cos(a0) * 0.38, 1.0, Math::sin(a0) * 0.38);
					const Vector3 inner_top_1(Math::cos(a1) * 0.38, 1.0, Math::sin(a1) * 0.38);
					add_quad(outer_bottom_0, outer_top_0, outer_top_1, outer_bottom_1);
					add_quad(inner_bottom_1, inner_top_1, inner_top_0, inner_bottom_0);
					add_quad(outer_top_0, inner_top_0, inner_top_1, outer_top_1);
					add_quad(outer_bottom_1, inner_bottom_1, inner_bottom_0, outer_bottom_0);
				}
			} else if (p_shape == 12) {
				for (int i = 0; i < 8; i++) {
					const real_t angle = Math::deg_to_rad(-70.0 + i * 20.0);
					const real_t radius = 1.0;
					const real_t height = (i + 1) * 0.25;
					append_box(Vector3(Math::sin(angle) * radius, height * 0.5, Math::cos(angle) * radius), Vector3(0.52, height, 0.55), Vector3(0.0, -angle, 0.0));
				}
			} else if (p_shape == 13) {
				append_box(Vector3(0.0, 0.85, 0.0), Vector3(2.0, 0.15, 0.12), Vector3());
			} else if (p_shape == 14) {
				append_box(Vector3(0.0, 0.85, -0.95), Vector3(2.0, 0.15, 0.12), Vector3());
				append_box(Vector3(-0.95, 0.85, 0.0), Vector3(0.12, 0.15, 2.0), Vector3());
				append_box(Vector3(0.95, 0.85, 0.0), Vector3(0.12, 0.15, 2.0), Vector3());
			} else {
				append_box(Vector3(0.0, 0.0, 0.0), Vector3(4.0, 2.5, 0.2), Vector3());
			}

		return surface_tool->commit();
		}
		case 9: // Sphere.
			sphere.instantiate();
			sphere->set_radius(1.0);
			sphere->set_radial_segments(32);
			sphere->set_rings(16);
			return sphere;
		case 10: // Torus.
			torus.instantiate();
			torus->set_inner_radius(0.65);
			torus->set_outer_radius(1.0);
			return torus;
	}
	return Ref<Mesh>();
}

} // namespace FPSStudioGeometry
