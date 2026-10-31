#include "fps_studio_geometry_gizmo_plugin.h"

#include "core/math/geometry_3d.h"
#include "core/math/math_funcs.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/mesh.h"
#include "scene/resources/surface_tool.h"

static Vector3i _fps_studio_geometry_position_key(const Vector3 &p_position) {
	constexpr real_t precision = 100000.0;
	return Vector3i(int(Math::round(p_position.x * precision)), int(Math::round(p_position.y * precision)), int(Math::round(p_position.z * precision)));
}

FPSStudioGeometryGizmoPlugin::Topology FPSStudioGeometryGizmoPlugin::_get_topology(const Ref<Mesh> &p_mesh) {
	Topology topology;
	if (p_mesh.is_null()) {
		return topology;
	}

	HashMap<Vector3i, int> position_to_group;
	Vector<Vector<int>> surface_vertex_groups;
	surface_vertex_groups.resize(p_mesh->get_surface_count());
	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		const Array arrays = p_mesh->surface_get_arrays(surface);
		if (arrays.is_empty() || arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY) {
			continue;
		}

		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		Vector<int> &vertex_groups = surface_vertex_groups.write[surface];
		vertex_groups.resize(vertices.size());
		for (int vertex = 0; vertex < vertices.size(); vertex++) {
			const Vector3 position = vertices[vertex];
			const Vector3i position_key = _fps_studio_geometry_position_key(position);
			int *group_index = position_to_group.getptr(position_key);
			if (!group_index) {
				const int new_group_index = topology.vertices.size();
				position_to_group.insert(position_key, new_group_index);
				VertexGroup group;
				group.position = position;
				topology.vertices.push_back(group);
				group_index = position_to_group.getptr(position_key);
			}

			vertex_groups.write[vertex] = *group_index;
			VertexReference reference;
			reference.surface = surface;
			reference.vertex = vertex;
			topology.vertices.write[*group_index].references.push_back(reference);
		}
	}

	HashMap<Vector2i, int> edge_to_index;
	auto add_edge = [&](int p_vertex_a, int p_vertex_b, const Vector3 &p_face_normal) {
		if (p_vertex_a == p_vertex_b) {
			return;
		}
		const Vector2i key(MIN(p_vertex_a, p_vertex_b), MAX(p_vertex_a, p_vertex_b));
		int *edge_index = edge_to_index.getptr(key);
		if (edge_index) {
			EdgeGroup &edge = topology.edges.write[*edge_index];
			edge.adjacent_face_count++;
			edge.is_crease |= edge.first_face_normal.dot(p_face_normal) < 0.999;
			return;
		}
		EdgeGroup edge;
		edge.vertex_a = key.x;
		edge.vertex_b = key.y;
		edge.adjacent_face_count = 1;
		edge.first_face_normal = p_face_normal;
		edge_to_index.insert(key, topology.edges.size());
		topology.edges.push_back(edge);
	};

	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		if (p_mesh->surface_get_primitive_type(surface) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		const Array arrays = p_mesh->surface_get_arrays(surface);
		if (arrays.is_empty() || arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY) {
			continue;
		}
		const Vector<int> &vertex_groups = surface_vertex_groups[surface];
		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		const int index_count = indices.is_empty() ? vertices.size() : indices.size();
		for (int index = 0; index + 2 < index_count; index += 3) {
			const int source_a = indices.is_empty() ? index : indices[index];
			const int source_b = indices.is_empty() ? index + 1 : indices[index + 1];
			const int source_c = indices.is_empty() ? index + 2 : indices[index + 2];
			if (source_a < 0 || source_a >= vertex_groups.size() || source_b < 0 || source_b >= vertex_groups.size() || source_c < 0 || source_c >= vertex_groups.size()) {
				continue;
			}

			FaceGroup face;
			face.vertex_a = vertex_groups[source_a];
			face.vertex_b = vertex_groups[source_b];
			face.vertex_c = vertex_groups[source_c];
			topology.faces.push_back(face);
			const Vector3 normal = (topology.vertices[face.vertex_b].position - topology.vertices[face.vertex_a].position).cross(topology.vertices[face.vertex_c].position - topology.vertices[face.vertex_a].position).normalized();
			add_edge(face.vertex_a, face.vertex_b, normal);
			add_edge(face.vertex_b, face.vertex_c, normal);
			add_edge(face.vertex_c, face.vertex_a, normal);
		}
	}

	Vector<EdgeGroup> visible_edges;
	for (const EdgeGroup &edge : topology.edges) {
		if (edge.adjacent_face_count != 2 || edge.is_crease) {
			visible_edges.push_back(edge);
		}
	}
	topology.edges = visible_edges;

	return topology;
}

Ref<ArrayMesh> FPSStudioGeometryGizmoPlugin::_build_mesh_with_vertex_positions(const Ref<Mesh> &p_source, const Topology &p_topology, const HashMap<int, Vector3> &p_positions) {
	ERR_FAIL_COND_V(p_source.is_null(), Ref<ArrayMesh>());
	Ref<ArrayMesh> result;
	result.instantiate();

	for (int surface = 0; surface < p_source->get_surface_count(); surface++) {
		const Array arrays = p_source->surface_get_arrays(surface);
		const Mesh::PrimitiveType primitive_type = p_source->surface_get_primitive_type(surface);
		Ref<SurfaceTool> surface_tool;
		surface_tool.instantiate();
		surface_tool->create_from_arrays(arrays, primitive_type);
		LocalVector<SurfaceTool::Vertex> &vertices = surface_tool->get_vertex_array();
		for (int group_index = 0; group_index < p_topology.vertices.size(); group_index++) {
			const Vector3 *position = p_positions.getptr(group_index);
			if (!position) {
				continue;
			}
			for (const VertexReference &reference : p_topology.vertices[group_index].references) {
				if (reference.surface == surface && reference.vertex >= 0 && reference.vertex < vertices.size()) {
					vertices[reference.vertex].vertex = *position;
				}
			}
		}

		if (primitive_type == Mesh::PRIMITIVE_TRIANGLES) {
			HashMap<Vector3, uint32_t> original_normal_groups;
			uint32_t next_smooth_group = 0;
			for (SurfaceTool::Vertex &vertex : vertices) {
				uint32_t *smooth_group = original_normal_groups.getptr(vertex.normal);
				if (!smooth_group) {
					original_normal_groups.insert(vertex.normal, next_smooth_group);
					smooth_group = original_normal_groups.getptr(vertex.normal);
					next_smooth_group++;
				}
				vertex.smooth_group = *smooth_group;
			}
			surface_tool->generate_normals();
		}

		const Ref<Material> surface_material = p_source->surface_get_material(surface);
		surface_tool->set_material(surface_material);
		const int new_surface = result->get_surface_count();
		result = surface_tool->commit(result);
		ERR_FAIL_COND_V(result.is_null(), Ref<ArrayMesh>());
		result->surface_set_material(new_surface, surface_material);
	}

	return result;
}

Vector<int> FPSStudioGeometryGizmoPlugin::_get_affected_vertices(const Topology &p_topology, int p_id) const {
	Vector<int> affected_vertices;
	switch (selection_mode) {
		case 1:
			if (p_id >= 0 && p_id < p_topology.vertices.size()) {
				affected_vertices.push_back(p_id);
			}
			break;
		case 2:
			if (p_id >= 0 && p_id < p_topology.edges.size()) {
				affected_vertices.push_back(p_topology.edges[p_id].vertex_a);
				affected_vertices.push_back(p_topology.edges[p_id].vertex_b);
			}
			break;
		case 3:
			if (p_id >= 0 && p_id < p_topology.faces.size()) {
				affected_vertices.push_back(p_topology.faces[p_id].vertex_a);
				affected_vertices.push_back(p_topology.faces[p_id].vertex_b);
				affected_vertices.push_back(p_topology.faces[p_id].vertex_c);
			}
			break;
	}
	return affected_vertices;
}

Vector3 FPSStudioGeometryGizmoPlugin::_get_element_center(const Topology &p_topology, int p_id) const {
	const Vector<int> affected_vertices = _get_affected_vertices(p_topology, p_id);
	if (affected_vertices.is_empty()) {
		return Vector3();
	}
	Vector3 center;
	for (int vertex_index : affected_vertices) {
		center += p_topology.vertices[vertex_index].position;
	}
	return center / affected_vertices.size();
}

int FPSStudioGeometryGizmoPlugin::_get_element_count(const Topology &p_topology) const {
	switch (selection_mode) {
		case 1:
			return p_topology.vertices.size();
		case 2:
			return p_topology.edges.size();
		case 3:
			return p_topology.faces.size();
	}
	return 0;
}

Ref<EditorNode3DGizmo> FPSStudioGeometryGizmoPlugin::create_gizmo(Node3D *p_spatial) {
	(void)p_spatial;
	return Ref<EditorNode3DGizmo>(memnew(EditorNode3DGizmo));
}

void FPSStudioGeometryGizmoPlugin::set_selection_mode(int p_mode) {
	selection_mode = CLAMP(p_mode, 0, 3);
}

bool FPSStudioGeometryGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_spatial);
	return selection_mode > 0 && mesh_instance && mesh_instance->has_meta("fps_studio_geometry_kind");
}

String FPSStudioGeometryGizmoPlugin::get_gizmo_name() const {
	return "FPSStudioGeometry";
}

int FPSStudioGeometryGizmoPlugin::get_priority() const {
	return 1;
}

void FPSStudioGeometryGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	if (!mesh_instance || selection_mode == 0) {
		return;
	}

	const Topology topology = _get_topology(mesh_instance->get_mesh());
	const PackedInt32Array selected_ids = p_gizmo->get_subgizmo_selection();
	if (selection_mode == 1) {
		PackedVector3Array unselected_vertices;
		PackedVector3Array selected_vertices;
		for (int i = 0; i < topology.vertices.size(); i++) {
			if (selected_ids.has(i)) {
				selected_vertices.push_back(topology.vertices[i].position);
			} else {
				unselected_vertices.push_back(topology.vertices[i].position);
			}
		}
		if (!unselected_vertices.is_empty()) {
			p_gizmo->add_vertices(unselected_vertices, get_material("geometry_vertices", p_gizmo), Mesh::PRIMITIVE_POINTS);
		}
		if (!selected_vertices.is_empty()) {
			p_gizmo->add_vertices(selected_vertices, get_material("geometry_vertices_selected", p_gizmo), Mesh::PRIMITIVE_POINTS);
		}
	} else if (selection_mode == 2) {
		Vector<Vector3> unselected_edges;
		Vector<Vector3> selected_edges;
		for (int i = 0; i < topology.edges.size(); i++) {
			const EdgeGroup &edge = topology.edges[i];
			Vector<Vector3> &lines = selected_ids.has(i) ? selected_edges : unselected_edges;
			lines.push_back(topology.vertices[edge.vertex_a].position);
			lines.push_back(topology.vertices[edge.vertex_b].position);
		}
		if (!unselected_edges.is_empty()) {
			p_gizmo->add_lines(unselected_edges, get_material("geometry_edges", p_gizmo));
		}
		if (!selected_edges.is_empty()) {
			p_gizmo->add_lines(selected_edges, get_material("geometry_edges_selected", p_gizmo));
		}
	} else if (selection_mode == 3) {
		PackedVector3Array unselected_faces;
		PackedVector3Array selected_faces;
		for (int i = 0; i < topology.faces.size(); i++) {
			const FaceGroup &face = topology.faces[i];
			PackedVector3Array &triangles = selected_ids.has(i) ? selected_faces : unselected_faces;
			triangles.push_back(topology.vertices[face.vertex_a].position);
			triangles.push_back(topology.vertices[face.vertex_b].position);
			triangles.push_back(topology.vertices[face.vertex_c].position);
		}
		if (!unselected_faces.is_empty()) {
			p_gizmo->add_vertices(unselected_faces, get_material("geometry_faces", p_gizmo), Mesh::PRIMITIVE_TRIANGLES);
		}
		if (!selected_faces.is_empty()) {
			p_gizmo->add_vertices(selected_faces, get_material("geometry_faces_selected", p_gizmo), Mesh::PRIMITIVE_TRIANGLES);
		}
	}
}

int FPSStudioGeometryGizmoPlugin::subgizmos_intersect_ray(const EditorNode3DGizmo *p_gizmo, Camera3D *p_camera, const Vector2 &p_point) const {
	if (selection_mode == 0 || !p_camera) {
		return -1;
	}
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(mesh_instance, -1);
	const Topology topology = _get_topology(mesh_instance->get_mesh());
	const Transform3D global_transform = mesh_instance->get_global_transform();

	if (selection_mode == 1) {
		int closest_id = -1;
		real_t closest_screen_distance = 12.0 * 12.0;
		for (int i = 0; i < topology.vertices.size(); i++) {
			const Vector3 world_position = global_transform.xform(topology.vertices[i].position);
			if (p_camera->is_position_behind(world_position)) {
				continue;
			}
			const real_t screen_distance = p_camera->unproject_position(world_position).distance_squared_to(p_point);
			if (screen_distance < closest_screen_distance) {
				closest_screen_distance = screen_distance;
				closest_id = i;
			}
		}
		return closest_id;
	}

	if (selection_mode == 2) {
		int closest_id = -1;
		real_t closest_screen_distance = 10.0 * 10.0;
		for (int i = 0; i < topology.edges.size(); i++) {
			const EdgeGroup &edge = topology.edges[i];
			const Vector3 world_a = global_transform.xform(topology.vertices[edge.vertex_a].position);
			const Vector3 world_b = global_transform.xform(topology.vertices[edge.vertex_b].position);
			if (p_camera->is_position_behind(world_a) || p_camera->is_position_behind(world_b)) {
				continue;
			}
			const Vector2 screen_a = p_camera->unproject_position(world_a);
			const Vector2 screen_b = p_camera->unproject_position(world_b);
			const Vector2 segment = screen_b - screen_a;
			const real_t denominator = segment.length_squared();
			const real_t factor = denominator > 0.0 ? CLAMP((p_point - screen_a).dot(segment) / denominator, 0.0, 1.0) : 0.0;
			const real_t screen_distance = p_point.distance_squared_to(screen_a + segment * factor);
			if (screen_distance < closest_screen_distance) {
				closest_screen_distance = screen_distance;
				closest_id = i;
			}
		}
		return closest_id;
	}

	const Vector3 ray_origin = p_camera->project_ray_origin(p_point);
	const Vector3 ray_direction = p_camera->project_ray_normal(p_point);
	real_t closest_distance = INFINITY;
	int closest_id = -1;
	for (int i = 0; i < topology.faces.size(); i++) {
		const FaceGroup &face = topology.faces[i];
		const Vector3 a = global_transform.xform(topology.vertices[face.vertex_a].position);
		const Vector3 b = global_transform.xform(topology.vertices[face.vertex_b].position);
		const Vector3 c = global_transform.xform(topology.vertices[face.vertex_c].position);
		Vector3 intersection;
		if (Geometry3D::ray_intersects_triangle(ray_origin, ray_direction, a, b, c, &intersection)) {
			const real_t distance = ray_origin.distance_to(intersection);
			if (distance < closest_distance) {
				closest_distance = distance;
				closest_id = i;
			}
		}
	}
	return closest_id;
}

Vector<int> FPSStudioGeometryGizmoPlugin::subgizmos_intersect_frustum(const EditorNode3DGizmo *p_gizmo, const Camera3D *p_camera, const Vector<Plane> &p_frustum) const {
	Vector<int> contained_elements;
	if (selection_mode == 0 || !p_camera) {
		return contained_elements;
	}
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(mesh_instance, contained_elements);
	const Topology topology = _get_topology(mesh_instance->get_mesh());
	const Transform3D global_transform = mesh_instance->get_global_transform();
	auto point_is_inside = [&](const Vector3 &p_local_position) {
		const Vector3 world_position = global_transform.xform(p_local_position);
		for (const Plane &plane : p_frustum) {
			if (plane.distance_to(world_position) > 0.0) {
				return false;
			}
		}
		return true;
	};

	const int count = _get_element_count(topology);
	for (int id = 0; id < count; id++) {
		const Vector<int> affected_vertices = _get_affected_vertices(topology, id);
		bool fully_inside = !affected_vertices.is_empty();
		for (int vertex_id : affected_vertices) {
			if (!point_is_inside(topology.vertices[vertex_id].position)) {
				fully_inside = false;
				break;
			}
		}
		if (fully_inside) {
			contained_elements.push_back(id);
		}
	}
	return contained_elements;
}

Transform3D FPSStudioGeometryGizmoPlugin::get_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id) const {
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(mesh_instance, Transform3D());
	const Topology topology = _get_topology(mesh_instance->get_mesh());
	ERR_FAIL_INDEX_V(p_id, _get_element_count(topology), Transform3D());
	return Transform3D(Basis(), _get_element_center(topology, p_id));
}

void FPSStudioGeometryGizmoPlugin::set_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id, Transform3D p_transform) {
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL(mesh_instance);
	const ObjectID object_id = mesh_instance->get_instance_id();
	if (!drag_states.has(object_id)) {
		DragState state;
		state.original_mesh = mesh_instance->get_mesh();
		state.original_vertex_arrays = mesh_instance->get_meta("fps_studio_geometry_arrays", Array());
		drag_states.insert(object_id, state);
	}

	DragState *state = drag_states.getptr(object_id);
	const Topology topology = _get_topology(state->original_mesh);
	ERR_FAIL_INDEX(p_id, _get_element_count(topology));
	const Vector<int> affected_vertices = _get_affected_vertices(topology, p_id);
	const Vector3 element_center = _get_element_center(topology, p_id);
	for (int vertex_id : affected_vertices) {
		const Vector3 initial_offset = topology.vertices[vertex_id].position - element_center;
		state->pending_vertex_positions.insert(vertex_id, p_transform.basis.xform(initial_offset) + p_transform.origin);
	}

	Ref<ArrayMesh> updated_mesh = _build_mesh_with_vertex_positions(state->original_mesh, topology, state->pending_vertex_positions);
	if (updated_mesh.is_null()) {
		return;
	}
	mesh_instance->set_mesh(updated_mesh);
	if (updated_mesh->get_surface_count() > 0) {
		mesh_instance->set_meta("fps_studio_geometry_arrays", updated_mesh->surface_get_arrays(0));
	}
	mesh_instance->update_gizmos();
}

void FPSStudioGeometryGizmoPlugin::commit_subgizmos(const EditorNode3DGizmo *p_gizmo, const Vector<int> &p_ids, const Vector<Transform3D> &p_restore, bool p_cancel) {
	(void)p_ids;
	(void)p_restore;
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL(mesh_instance);
	const ObjectID object_id = mesh_instance->get_instance_id();
	DragState *state = drag_states.getptr(object_id);
	if (!state) {
		return;
	}

	const Ref<Mesh> original_mesh = state->original_mesh;
	const Variant original_vertex_arrays = state->original_vertex_arrays;
	const Ref<Mesh> updated_mesh = mesh_instance->get_mesh();
	const Variant updated_vertex_arrays = updated_mesh.is_valid() && updated_mesh->get_surface_count() > 0 ? Variant(updated_mesh->surface_get_arrays(0)) : Variant(Array());
	if (p_cancel) {
		mesh_instance->set_mesh(original_mesh);
		mesh_instance->set_meta("fps_studio_geometry_arrays", original_vertex_arrays);
	} else if (original_mesh != updated_mesh) {
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Transform Geometry Elements"));
		undo_redo->add_do_property(mesh_instance, "mesh", updated_mesh);
		undo_redo->add_undo_property(mesh_instance, "mesh", original_mesh);
		undo_redo->add_do_method(mesh_instance, "set_meta", "fps_studio_geometry_arrays", updated_vertex_arrays);
		undo_redo->add_undo_method(mesh_instance, "set_meta", "fps_studio_geometry_arrays", original_vertex_arrays);
		undo_redo->commit_action();
	}

	drag_states.erase(object_id);
	mesh_instance->update_gizmos();
}

FPSStudioGeometryGizmoPlugin::FPSStudioGeometryGizmoPlugin() {
	create_material("geometry_vertices", Color(0.16, 0.88, 0.82));
	create_material("geometry_vertices_selected", Color(1.0, 0.72, 0.2));
	create_material("geometry_edges", Color(0.16, 0.88, 0.82));
	create_material("geometry_edges_selected", Color(1.0, 0.72, 0.2));
	create_material("geometry_faces", Color(0.16, 0.88, 0.82, 0.18));
	create_material("geometry_faces_selected", Color(1.0, 0.72, 0.2, 0.48));

	const String material_names[] = {
		"geometry_vertices", "geometry_vertices_selected", "geometry_edges", "geometry_edges_selected", "geometry_faces", "geometry_faces_selected",
	};
	for (int i = 0; i < 6; i++) {
		Ref<StandardMaterial3D> material = get_material(material_names[i]);
		material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
		material->set_flag(StandardMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		material->set_render_priority(StandardMaterial3D::RENDER_PRIORITY_MAX);
		if (i < 2) {
			material->set_flag(StandardMaterial3D::FLAG_USE_POINT_SIZE, true);
			material->set_point_size(10.0);
		}
		if (i >= 4) {
			material->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
		}
	}
}
