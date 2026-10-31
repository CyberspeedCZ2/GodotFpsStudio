#include "fps_studio_geometry_gizmo_plugin.h"

#include "core/math/math_funcs.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/mesh.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/surface_tool.h"

static Vector3i _fps_studio_geometry_position_key(const Vector3 &p_position) {
	constexpr real_t precision = 100000.0;
	return Vector3i(int(Math::round(p_position.x * precision)), int(Math::round(p_position.y * precision)), int(Math::round(p_position.z * precision)));
}

Vector<FPSStudioGeometryGizmoPlugin::VertexGroup> FPSStudioGeometryGizmoPlugin::_get_vertex_groups(const Ref<Mesh> &p_mesh) {
	Vector<VertexGroup> groups;
	if (p_mesh.is_null()) {
		return groups;
	}

	HashMap<Vector3i, int> position_to_group;
	for (int surface = 0; surface < p_mesh->get_surface_count(); surface++) {
		const Array arrays = p_mesh->surface_get_arrays(surface);
		if (arrays.is_empty() || arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY) {
			continue;
		}

		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		for (int vertex = 0; vertex < vertices.size(); vertex++) {
			const Vector3 position = vertices[vertex];
			const Vector3i position_key = _fps_studio_geometry_position_key(position);
			int *group_index = position_to_group.getptr(position_key);
			if (!group_index) {
				const int new_group_index = groups.size();
				position_to_group.insert(position_key, new_group_index);
				VertexGroup group;
				group.position = position;
				groups.push_back(group);
				group_index = position_to_group.getptr(position_key);
			}

			VertexReference reference;
			reference.surface = surface;
			reference.vertex = vertex;
			groups.write[*group_index].references.push_back(reference);
		}
	}

	return groups;
}

Ref<ArrayMesh> FPSStudioGeometryGizmoPlugin::_build_mesh_with_vertex_at(const Ref<Mesh> &p_source, const Vector<VertexReference> &p_references, const Vector3 &p_position) {
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
		for (const VertexReference &reference : p_references) {
			if (reference.surface == surface) {
				ERR_FAIL_INDEX_V(reference.vertex, vertices.size(), Ref<ArrayMesh>());
				vertices[reference.vertex].vertex = p_position;
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

Ref<EditorNode3DGizmo> FPSStudioGeometryGizmoPlugin::create_gizmo(Node3D *p_spatial) {
	(void)p_spatial;
	return Ref<EditorNode3DGizmo>(memnew(EditorNode3DGizmo));
}

void FPSStudioGeometryGizmoPlugin::set_vertex_mode(bool p_enabled) {
	if (vertex_mode == p_enabled) {
		return;
	}
	vertex_mode = p_enabled;
}

bool FPSStudioGeometryGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_spatial);
	return vertex_mode && mesh_instance && mesh_instance->has_meta("fps_studio_geometry_kind");
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
	if (!mesh_instance || !vertex_mode) {
		return;
	}

	const Vector<VertexGroup> groups = _get_vertex_groups(mesh_instance->get_mesh());
	PackedVector3Array unselected_vertices;
	PackedVector3Array selected_vertices;
	const PackedInt32Array selected_ids = p_gizmo->get_subgizmo_selection();
	for (int i = 0; i < groups.size(); i++) {
		if (selected_ids.has(i)) {
			selected_vertices.push_back(groups[i].position);
		} else {
			unselected_vertices.push_back(groups[i].position);
		}
	}

	Ref<StandardMaterial3D> vertex_material = get_material("geometry_vertices", p_gizmo);
	Ref<StandardMaterial3D> selected_material = get_material("geometry_vertices_selected", p_gizmo);
	if (!unselected_vertices.is_empty()) {
		p_gizmo->add_vertices(unselected_vertices, vertex_material, Mesh::PRIMITIVE_POINTS);
	}
	if (!selected_vertices.is_empty()) {
		p_gizmo->add_vertices(selected_vertices, selected_material, Mesh::PRIMITIVE_POINTS);
	}
}

int FPSStudioGeometryGizmoPlugin::subgizmos_intersect_ray(const EditorNode3DGizmo *p_gizmo, Camera3D *p_camera, const Vector2 &p_point) const {
	if (!vertex_mode || !p_camera) {
		return -1;
	}
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(mesh_instance, -1);
	const Vector<VertexGroup> groups = _get_vertex_groups(mesh_instance->get_mesh());

	int closest_id = -1;
	real_t closest_screen_distance = 12.0 * 12.0;
	for (int i = 0; i < groups.size(); i++) {
		const Vector3 world_position = mesh_instance->get_global_transform().xform(groups[i].position);
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

Vector<int> FPSStudioGeometryGizmoPlugin::subgizmos_intersect_frustum(const EditorNode3DGizmo *p_gizmo, const Camera3D *p_camera, const Vector<Plane> &p_frustum) const {
	Vector<int> contained_vertices;
	if (!vertex_mode || !p_camera) {
		return contained_vertices;
	}
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(mesh_instance, contained_vertices);
	const Vector<VertexGroup> groups = _get_vertex_groups(mesh_instance->get_mesh());
	const Transform3D global_transform = mesh_instance->get_global_transform();

	for (int vertex_index = 0; vertex_index < groups.size(); vertex_index++) {
		const Vector3 world_position = global_transform.xform(groups[vertex_index].position);
		bool inside = true;
		for (const Plane &plane : p_frustum) {
			if (plane.distance_to(world_position) > 0.0) {
				inside = false;
				break;
			}
		}
		if (inside) {
			contained_vertices.push_back(vertex_index);
		}
	}
	return contained_vertices;
}

Transform3D FPSStudioGeometryGizmoPlugin::get_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id) const {
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL_V(mesh_instance, Transform3D());
	const Vector<VertexGroup> groups = _get_vertex_groups(mesh_instance->get_mesh());
	ERR_FAIL_INDEX_V(p_id, groups.size(), Transform3D());
	return Transform3D(Basis(), groups[p_id].position);
}

void FPSStudioGeometryGizmoPlugin::set_subgizmo_transform(const EditorNode3DGizmo *p_gizmo, int p_id, Transform3D p_transform) {
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL(mesh_instance);
	const Ref<Mesh> source_mesh = mesh_instance->get_mesh();
	const Vector<VertexGroup> groups = _get_vertex_groups(source_mesh);
	ERR_FAIL_INDEX(p_id, groups.size());
	if (groups[p_id].position.is_equal_approx(p_transform.origin)) {
		return;
	}

	const ObjectID object_id = mesh_instance->get_instance_id();
	if (!drag_states.has(object_id)) {
		DragState state;
		state.original_mesh = source_mesh;
		state.original_vertex_arrays = mesh_instance->get_meta("fps_studio_geometry_arrays", Array());
		drag_states.insert(object_id, state);
	}

	Ref<ArrayMesh> updated_mesh = _build_mesh_with_vertex_at(source_mesh, groups[p_id].references, p_transform.origin);
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
		undo_redo->create_action(TTR("Move Geometry Vertices"));
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
	Ref<StandardMaterial3D> vertex_material = get_material("geometry_vertices");
	Ref<StandardMaterial3D> selected_material = get_material("geometry_vertices_selected");
	for (Ref<StandardMaterial3D> material : { vertex_material, selected_material }) {
		material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
		material->set_flag(StandardMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		material->set_flag(StandardMaterial3D::FLAG_USE_POINT_SIZE, true);
		material->set_point_size(10.0);
		material->set_render_priority(StandardMaterial3D::RENDER_PRIORITY_MAX);
	}
}
