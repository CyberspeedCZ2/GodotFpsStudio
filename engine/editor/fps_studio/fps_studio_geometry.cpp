#include "fps_studio_geometry.h"

#include "core/math/math_funcs.h"
#include "core/templates/hash_set.h"
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
	static const char *operation_names[] = { "Mirror X", "Mirror Y", "Mirror Z", "Extrude Faces", "Subdivide", "Conform Normals" };
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

struct GeometryExtrusionFace {
	SurfaceTool::Vertex vertices[3];
	int position_ids[3] = { -1, -1, -1 };
	Vector3 normal;
	bool selected = false;
};

struct GeometryExtrusionEdgeUse {
	SurfaceTool::Vertex vertex_a;
	SurfaceTool::Vertex vertex_b;
	int position_a = -1;
	int position_b = -1;
};

struct GeometryExtrusionEdge {
	int adjacent_faces = 0;
	int selected_faces = 0;
	Vector<int> adjacent_face_ids;
	Vector<GeometryExtrusionEdgeUse> selected_uses;
};

static int _get_geometry_position_id(const Vector3 &p_position, HashMap<Vector3i, int> &r_position_ids, Vector<Vector3i> &r_positions) {
	constexpr real_t position_precision = 100000.0;
	const Vector3i position_key(
		int(Math::round(p_position.x * position_precision)),
		int(Math::round(p_position.y * position_precision)),
		int(Math::round(p_position.z * position_precision)));
	int *position_id = r_position_ids.getptr(position_key);
	if (position_id) {
		return *position_id;
	}
	const int new_position_id = r_positions.size();
	r_position_ids.insert(position_key, new_position_id);
	r_positions.push_back(position_key);
	return new_position_id;
}

static bool _extrude_geometry_surface(Ref<SurfaceTool> p_surface_tool, const HashSet<int> &p_selected_faces, int p_face_offset, real_t p_amount) {
	p_surface_tool->deindex();
	const LocalVector<SurfaceTool::Vertex> &source_vertices = p_surface_tool->get_vertex_array();
	if (source_vertices.size() % 3 != 0) {
		return false;
	}

	HashMap<Vector3i, int> position_ids;
	Vector<Vector3i> positions;
	Vector<GeometryExtrusionFace> faces;
	faces.resize(source_vertices.size() / 3);
	HashMap<Vector2i, GeometryExtrusionEdge> edges;
	static const int edge_vertex_indices[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
	bool has_selected_faces = false;

	for (int face_index = 0; face_index < faces.size(); face_index++) {
		GeometryExtrusionFace &face = faces.write[face_index];
		for (int corner = 0; corner < 3; corner++) {
			face.vertices[corner] = source_vertices[face_index * 3 + corner];
			face.position_ids[corner] = _get_geometry_position_id(face.vertices[corner].vertex, position_ids, positions);
		}
		face.selected = p_selected_faces.has(p_face_offset + face_index);
		face.normal = (face.vertices[1].vertex - face.vertices[0].vertex).cross(face.vertices[2].vertex - face.vertices[0].vertex).normalized();
		has_selected_faces |= face.selected;
		for (int edge_index = 0; edge_index < 3; edge_index++) {
			const int corner_a = edge_vertex_indices[edge_index][0];
			const int corner_b = edge_vertex_indices[edge_index][1];
			const int position_a = face.position_ids[corner_a];
			const int position_b = face.position_ids[corner_b];
			const Vector2i edge_key(MIN(position_a, position_b), MAX(position_a, position_b));
			GeometryExtrusionEdge *edge = edges.getptr(edge_key);
			if (!edge) {
				edges.insert(edge_key, GeometryExtrusionEdge());
				edge = edges.getptr(edge_key);
			}
			edge->adjacent_faces++;
			edge->adjacent_face_ids.push_back(face_index);
		}
	}

	if (!has_selected_faces) {
		return false;
	}

	Vector<int> pending_faces;
	for (int face_index = 0; face_index < faces.size(); face_index++) {
		if (faces[face_index].selected) {
			pending_faces.push_back(face_index);
		}
	}
	for (int cursor = 0; cursor < pending_faces.size(); cursor++) {
		const int face_index = pending_faces[cursor];
		const GeometryExtrusionFace &face = faces[face_index];
		for (int edge_index = 0; edge_index < 3; edge_index++) {
			const int corner_a = edge_vertex_indices[edge_index][0];
			const int corner_b = edge_vertex_indices[edge_index][1];
			const int position_a = face.position_ids[corner_a];
			const int position_b = face.position_ids[corner_b];
			const Vector2i edge_key(MIN(position_a, position_b), MAX(position_a, position_b));
			const GeometryExtrusionEdge *edge = edges.getptr(edge_key);
			if (!edge || edge->adjacent_face_ids.size() != 2) {
				continue;
			}
			const int adjacent_face = edge->adjacent_face_ids[0] == face_index ? edge->adjacent_face_ids[1] : edge->adjacent_face_ids[0];
			if (!faces[adjacent_face].selected && face.normal.dot(faces[adjacent_face].normal) > 0.999) {
				faces.write[adjacent_face].selected = true;
				pending_faces.push_back(adjacent_face);
			}
		}
	}

	HashMap<int, Vector3> selected_normal_sums;
	for (int face_index = 0; face_index < faces.size(); face_index++) {
		const GeometryExtrusionFace &face = faces[face_index];
		if (!face.selected) {
			continue;
		}
		for (int corner = 0; corner < 3; corner++) {
			const int position_id = face.position_ids[corner];
			Vector3 *normal_sum = selected_normal_sums.getptr(position_id);
			if (normal_sum) {
				*normal_sum += face.normal;
			} else {
				selected_normal_sums.insert(position_id, face.normal);
			}
		}
		for (int edge_index = 0; edge_index < 3; edge_index++) {
			const int corner_a = edge_vertex_indices[edge_index][0];
			const int corner_b = edge_vertex_indices[edge_index][1];
			const int position_a = face.position_ids[corner_a];
			const int position_b = face.position_ids[corner_b];
			const Vector2i edge_key(MIN(position_a, position_b), MAX(position_a, position_b));
			GeometryExtrusionEdge *edge = edges.getptr(edge_key);
			ERR_CONTINUE(!edge);
			edge->selected_faces++;
			GeometryExtrusionEdgeUse use;
			use.vertex_a = face.vertices[corner_a];
			use.vertex_b = face.vertices[corner_b];
			use.position_a = position_a;
			use.position_b = position_b;
			edge->selected_uses.push_back(use);
		}
	}

	HashMap<int, Vector3> extrusion_offsets;
	for (const KeyValue<int, Vector3> &normal_entry : selected_normal_sums) {
		if (normal_entry.value.length_squared() > CMP_EPSILON2) {
			extrusion_offsets.insert(normal_entry.key, normal_entry.value.normalized() * p_amount);
		}
	}

	LocalVector<SurfaceTool::Vertex> result_vertices;
	result_vertices.reserve(faces.size() * 3 + edges.size() * 6);
	for (const GeometryExtrusionFace &face : faces) {
		for (int corner = 0; corner < 3; corner++) {
			SurfaceTool::Vertex vertex = face.vertices[corner];
			if (face.selected) {
				const Vector3 *offset = extrusion_offsets.getptr(face.position_ids[corner]);
				if (offset) {
					vertex.vertex += *offset;
				}
			}
			result_vertices.push_back(vertex);
		}
	}

	for (const KeyValue<Vector2i, GeometryExtrusionEdge> &edge_entry : edges) {
		const GeometryExtrusionEdge &edge = edge_entry.value;
		if (edge.selected_faces == 0 || (edge.selected_faces == edge.adjacent_faces && edge.adjacent_faces > 1)) {
			continue;
		}
		for (const GeometryExtrusionEdgeUse &use : edge.selected_uses) {
			SurfaceTool::Vertex bottom_a = use.vertex_a;
			SurfaceTool::Vertex bottom_b = use.vertex_b;
			SurfaceTool::Vertex top_a = use.vertex_a;
			SurfaceTool::Vertex top_b = use.vertex_b;
			const Vector3 *offset_a = extrusion_offsets.getptr(use.position_a);
			const Vector3 *offset_b = extrusion_offsets.getptr(use.position_b);
			if (offset_a) {
				top_a.vertex += *offset_a;
			}
			if (offset_b) {
				top_b.vertex += *offset_b;
			}
			top_a.uv.y += Math::abs(p_amount);
			top_b.uv.y += Math::abs(p_amount);
			const Vector3 side_normal = (bottom_b.vertex - bottom_a.vertex).cross(top_b.vertex - bottom_b.vertex).normalized();
			if (side_normal.length_squared() <= CMP_EPSILON2) {
				continue;
			}
			bottom_a.normal = side_normal;
			bottom_b.normal = side_normal;
			top_a.normal = side_normal;
			top_b.normal = side_normal;
			result_vertices.push_back(bottom_a);
			result_vertices.push_back(bottom_b);
			result_vertices.push_back(top_b);
			result_vertices.push_back(bottom_a);
			result_vertices.push_back(top_b);
			result_vertices.push_back(top_a);
		}
	}

	LocalVector<SurfaceTool::Vertex> &surface_vertices = p_surface_tool->get_vertex_array();
	surface_vertices.clear();
	for (const SurfaceTool::Vertex &vertex : result_vertices) {
		surface_vertices.push_back(vertex);
	}
	return true;
}

Ref<ArrayMesh> apply_mesh_operation(const Ref<Mesh> &p_mesh, MeshOperation p_operation, const Vector<int> &p_selected_faces, real_t p_amount) {
	ERR_FAIL_COND_V(p_mesh.is_null() || p_mesh->get_surface_count() == 0, Ref<ArrayMesh>());
	ERR_FAIL_INDEX_V(p_operation, MESH_OPERATION_MAX, Ref<ArrayMesh>());
	ERR_FAIL_COND_V(p_operation == MESH_OPERATION_EXTRUDE_FACES && (p_selected_faces.is_empty() || Math::is_zero_approx(p_amount)), Ref<ArrayMesh>());

	HashSet<int> selected_faces;
	if (p_operation == MESH_OPERATION_EXTRUDE_FACES) {
		for (int face_id : p_selected_faces) {
			selected_faces.insert(face_id);
		}
	}

	Ref<ArrayMesh> result;
	result.instantiate();
	int face_offset = 0;
	bool changed_selected_faces = false;
	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		const Mesh::PrimitiveType primitive_type = p_mesh->surface_get_primitive_type(surface);
		Ref<SurfaceTool> surface_tool;
		surface_tool.instantiate();
		surface_tool->create_from_arrays(p_mesh->surface_get_arrays(surface), primitive_type);
		const Ref<Material> surface_material = p_mesh->surface_get_material(surface);
		surface_tool->set_material(surface_material);
		const Array surface_arrays = p_mesh->surface_get_arrays(surface);
		const PackedVector3Array surface_vertices = surface_arrays[Mesh::ARRAY_VERTEX];
		const PackedInt32Array surface_indices = surface_arrays[Mesh::ARRAY_INDEX];
		const int surface_face_count = primitive_type == Mesh::PRIMITIVE_TRIANGLES ? (surface_indices.is_empty() ? surface_vertices.size() : surface_indices.size()) / 3 : 0;

		if (primitive_type == Mesh::PRIMITIVE_TRIANGLES && (p_operation == MESH_OPERATION_CONFORM_NORMALS || p_operation == MESH_OPERATION_SUBDIVIDE || p_operation <= MESH_OPERATION_MIRROR_Z)) {
			_set_smooth_groups_from_normals(surface_tool);
		}

		if (p_operation == MESH_OPERATION_EXTRUDE_FACES) {
			if (primitive_type == Mesh::PRIMITIVE_TRIANGLES) {
				changed_selected_faces |= _extrude_geometry_surface(surface_tool, selected_faces, face_offset, p_amount);
				face_offset += surface_face_count;
			}
		} else if (p_operation <= MESH_OPERATION_MIRROR_Z) {
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
	if (p_operation == MESH_OPERATION_EXTRUDE_FACES && !changed_selected_faces) {
		return Ref<ArrayMesh>();
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
