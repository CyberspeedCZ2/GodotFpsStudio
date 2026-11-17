/**************************************************************************/
/*  scene_import_settings.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/


// FPS Studio Model View: LOD preview, mesh statistics and the LOD settings panel (.lodcfg).
// Part of SceneImportSettingsScreen (Model View); split out of
// scene_import_settings.cpp, which keeps the Godot import dialog itself.

#include "scene_import_settings.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/file_access.h"
#include "core/io/config_file.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_saver.h"
#include "core/math/projection.h"
#include "core/object/callable_mp.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/3d/skeleton_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/3d/bone_attachment_3d.h"
#include "scene/3d/label_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/check_button.h"
#include "scene/gui/button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/panel.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/separator.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/sky.h"
#include "scene/resources/shader.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/surface_tool.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"
#include "servers/text/text_server.h"
#include "scene_import_settings_internal.h"

// Converts an index/vertex count to the number of rendered faces using the
// same rules as the renderer's _indices_to_primitives().
static uint64_t _mesh_faces_from_primitive(Mesh::PrimitiveType p_primitive, uint64_t p_index_count, uint64_t p_vertex_count) {
	uint64_t to_draw = p_index_count > 0 ? p_index_count : p_vertex_count;
	switch (p_primitive) {
		case Mesh::PRIMITIVE_TRIANGLES:
			return to_draw / 3;
		case Mesh::PRIMITIVE_TRIANGLE_STRIP:
			return to_draw >= 3 ? (to_draw - 2) : 0;
		default:
			return 0;
	}
}

// Compact number formatting for the mesh stats overlay (e.g. "12.3k", "4.5M").
static String _format_stats_count(uint64_t p_value) {
	if (p_value >= 1000000) {
		return String::num(p_value / 1000000.0, 2) + "M";
	} else if (p_value >= 10000) {
		return String::num(p_value / 1000.0, 1) + "k";
	}
	return itos(p_value);
}

void SceneImportSettingsScreen::_update_preview_lod_threshold() {
	if (!camera || !base_viewport) {
		return;
	}
	const real_t project_threshold = GLOBAL_GET("rendering/mesh_lod/lod_change/threshold_pixels");
	const Size2 size = base_viewport->get_size();
	if (size.x <= 0 || size.y <= 0 || !camera->is_inside_tree()) {
		base_viewport->set_mesh_lod_threshold(project_threshold);
		return;
	}
	const Projection reference = Projection::create_perspective(PREVIEW_FOV_DEGREES, REFERENCE_ASPECT, 0.05, 10000.0);
	const real_t actual_multiplier = camera->get_camera_projection().get_lod_multiplier();
	if (!Math::is_finite(actual_multiplier) || actual_multiplier <= 0.0) {
		base_viewport->set_mesh_lod_threshold(project_threshold);
		return;
	}
	// Match the reference camera even if this editor panel has a different
	// resolution or aspect ratio; this modifies only the preview viewport.
	const real_t threshold = project_threshold * size.x * reference.get_lod_multiplier() / (REFERENCE_VIEWPORT_WIDTH * actual_multiplier);
	base_viewport->set_mesh_lod_threshold(threshold);
}

Ref<ImporterMesh> SceneImportSettingsScreen::_duplicate_importer_mesh(const Ref<ImporterMesh> &p_source) const {
	if (p_source.is_null()) {
		return Ref<ImporterMesh>();
	}
	Ref<ImporterMesh> copy;
	copy.instantiate();
	copy->set_name(p_source->get_name());
	copy->set_blend_shape_mode(p_source->get_blend_shape_mode());
	for (int i = 0; i < p_source->get_blend_shape_count(); i++) {
		copy->add_blend_shape(p_source->get_blend_shape_name(i));
	}
	for (int i = 0; i < p_source->get_surface_count(); i++) {
	TypedArray<Array> blend_shape_arrays;
		for (int blend_shape = 0; blend_shape < p_source->get_blend_shape_count(); blend_shape++) {
			blend_shape_arrays.push_back(p_source->get_surface_blend_shape_arrays(i, blend_shape));
		}
		copy->add_surface(p_source->get_surface_primitive_type(i), p_source->get_surface_arrays(i), blend_shape_arrays, Dictionary(), p_source->get_surface_material(i), p_source->get_surface_name(i), p_source->get_surface_format(i));
	}
	return copy;
}

// Generates LOD levels for the preview meshes so the statistics overlay can
// report the LOD level the preview camera selects, mirroring the generation
// performed by ResourceImporterScene::_generate_meshes() on reimport. Runs on
// the pre-import scene (ImporterMeshInstance3D nodes, before conversion to
// MeshInstance3D), before _fill_scene() converts the meshes for the preview.
void SceneImportSettingsScreen::_generate_preview_lods(Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	ImporterMeshInstance3D *src_mesh_node = Object::cast_to<ImporterMeshInstance3D>(p_node);
	if (src_mesh_node) {
		Ref<ImporterMesh> importer_mesh = src_mesh_node->get_mesh();
		if (importer_mesh.is_valid() && importer_mesh->get_surface_count() > 0) {
			String mesh_id = importer_mesh->get_meta("import_id", importer_mesh->get_name());
			// generate_lods() only adds index lists and never touches the
			// surface arrays, so the mesh itself is a clean source for later
			// regeneration (_duplicate_importer_mesh drops its LODs): no deep
			// copy of every mesh at open.
			if (!mesh_id.is_empty() && !lod_source_meshes.has(mesh_id)) {
				lod_source_meshes[mesh_id] = importer_mesh;
			}
			// Start from the main "Generate LODs" import setting; per-mesh
			// overrides ("generate/lods") stored in .import subresources may
			// force LOD generation on or off for this mesh, same as on
			// reimport (see ResourceImporterScene::_generate_meshes()).
			// "meshes/generate_lods" defaults to true on reimport when the
			// option is absent from the saved .import parameters.
			bool generate_lods = defaults.has("meshes/generate_lods") ? bool(defaults["meshes/generate_lods"]) : true;
			float merge_angle = 20.0f;
			// Fixed LOD chain configuration (0 = legacy automatic halving).
			int lod_count = defaults.has("meshes/lod_count") ? (int)defaults["meshes/lod_count"] : 0;
			float lod_reduction = defaults.has("meshes/lod_reduction") ? (float)defaults["meshes/lod_reduction"] : 0.5f;
			bool custom_lod_levels = false;
			Array lod_levels;

			if (!mesh_id.is_empty() && base_subresource_settings.has("meshes")) {
				Dictionary meshes_dict = base_subresource_settings["meshes"];
				if (meshes_dict.has(mesh_id)) {
					Dictionary mesh_settings = meshes_dict[mesh_id];
					custom_lod_levels = mesh_settings.get("lods/custom_levels", false);
					lod_levels = mesh_settings.get("lods/levels", Array());
					if (mesh_settings.has("lods/normal_merge_angle")) {
						merge_angle = (float)mesh_settings["lods/normal_merge_angle"];
					}
					if (mesh_settings.has("lods/count")) {
						lod_count = (int)mesh_settings["lods/count"];
					}
					if (mesh_settings.has("lods/reduction")) {
						lod_reduction = (float)mesh_settings["lods/reduction"];
					}
					if (mesh_settings.has("generate/lods")) {
						// Same values as ResourceImporterScene::MeshOverride
						// (Default=0, Enable=1, Disable=2), which is private.
						const int lods = (int)mesh_settings["generate/lods"];
						if (lods == 1) {
							generate_lods = true;
						} else if (lods == 2) {
							generate_lods = false;
						}
					}
				}
			}

			if (!importer_mesh->has_mesh()) {
				// Bone pose transforms are needed for correct LOD
				// simplification of skinned meshes, same as on reimport.
				// Kept even when LODs are off now: turning them on later in
				// the panel simplifies with these.
				Array skin_pose_transform_array;
				const Ref<Skin> skin = src_mesh_node->get_skin();
				if (skin.is_valid()) {
					NodePath skeleton_path = src_mesh_node->get_skeleton_path();
					Node *node = src_mesh_node->get_node_or_null(skeleton_path);
					Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(node);
					if (skeleton) {
						int bind_count = skin->get_bind_count();
						for (int i = 0; i < bind_count; i++) {
							Transform3D bind_pose = skin->get_bind_pose(i);
							String bind_name = skin->get_bind_name(i);
							int bone_idx = bind_name.is_empty() ? skin->get_bind_bone(i) : skeleton->find_bone(bind_name);
							if (bone_idx >= skeleton->get_bone_count()) {
								bone_idx = -1;
							}
							Transform3D bp_global_rest;
							if (bone_idx >= 0) {
								bp_global_rest = skeleton->get_bone_global_pose(bone_idx);
							} else {
								bp_global_rest = skeleton->get_bone_global_pose(i);
							}
							skin_pose_transform_array.push_back(bp_global_rest * bind_pose);
						}
					}
				}
				if (!mesh_id.is_empty()) {
					lod_source_skin_pose_transforms[mesh_id] = skin_pose_transform_array;
				}
				if (generate_lods && lod_defer_generation && !mesh_id.is_empty()) {
					lod_deferred_meshes.insert(mesh_id);
				} else if (generate_lods) {
					importer_mesh->generate_lods(merge_angle, skin_pose_transform_array, lod_count, lod_reduction, custom_lod_levels, lod_levels);
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_generate_preview_lods(p_node->get_child(i));
	}
}

// Gathers the cached surface geometry of a single mesh (see MeshStats) so
// the active LOD can be re-evaluated on every camera/viewport change without
// touching the RenderingServer. Returns false when the mesh has no usable
// surface geometry.
bool SceneImportSettingsScreen::_gather_mesh_stats_for_mesh(const Ref<Mesh> &p_mesh, MeshStats &r_stats) {
	r_stats = MeshStats();
	if (p_mesh.is_null()) {
		return false;
	}

	// Original polygon (quad/n-gon) count from the source file, stored as
	// "source_face_count" mesh metadata by importers that still have
	// access to it before triangulation (OBJ, COLLADA). Formats that
	// only store triangles (e.g. glTF/GLB) provide no such metadata.
	if (p_mesh->has_meta("source_face_count")) {
		r_stats.has_source_faces = true;
		r_stats.source_faces = (uint64_t)(int64_t)p_mesh->get_meta("source_face_count");
	}

	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		MeshSurfaceStats surface;
		surface.primitive = p_mesh->surface_get_primitive_type(s);

		Array arrays = p_mesh->surface_get_arrays(s);
		if (arrays.size() != Mesh::ARRAY_MAX) {
			continue;
		}
		PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		surface.index_count = indices.size();
		PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		surface.vertex_count = vertices.size();

		r_stats.base_indices += surface.index_count;
		r_stats.base_faces += _mesh_faces_from_primitive(surface.primitive, surface.index_count, surface.vertex_count);

		Dictionary lods = p_mesh->surface_get_lods(s);
		r_stats.max_lod_count = MAX(r_stats.max_lod_count, lods.size());
		if (lods.size() > 0) {
			r_stats.has_any_lods = true;
		}
		// LOD keys are edge-length thresholds (doubles, exactly as stored
		// in the mesh); sort ascending to mirror the renderer's LOD
		// selection order (fine -> coarse).
		Vector<double> edge_lengths;
		for (const KeyValue<Variant, Variant> &K : lods) {
			ERR_CONTINUE(!K.key.is_num());
			edge_lengths.push_back(K.key);
		}
		edge_lengths.sort();
		for (int i = 0; i < edge_lengths.size(); i++) {
			Vector<int> lod_indices = lods[edge_lengths[i]];
			// Unique source vertices referenced by this level (reported by
			// the statistics rows as the vertex cost of the level).
			HashSet<int> unique_lod_vertices;
			for (const int &idx : lod_indices) {
				unique_lod_vertices.insert(idx);
			}
			surface.lod_index_counts.push_back((uint64_t)lod_indices.size());
			surface.lod_edge_lengths.push_back(edge_lengths[i]);
			surface.lod_vertex_counts.push_back((uint64_t)unique_lod_vertices.size());
		}
		r_stats.surfaces.push_back(surface);
	}

	return r_stats.surfaces.size() > 0;
}

void SceneImportSettingsScreen::_gather_mesh_stats() {
	stats_base_faces = 0;
	stats_base_indices = 0;
	stats_source_faces = 0;
	stats_has_source_faces = true;
	stats_lod_count = 0;
	stats_has_any_lods = false;
	mesh_stats_map.clear();

	if (scene == nullptr) {
		return;
	}

	// Collect every unique mesh used by visible mesh nodes in the previewed
	// scene and cache its surface geometry, so the active LOD can be
	// re-evaluated on every camera change without touching the RenderingServer.
	// Debug-only children (collider_view, preview meshes) are never part of
	// node_map, so they are excluded automatically.
	for (const KeyValue<String, NodeData> &E : node_map) {
		Node *node = E.value.node;
		if (node == nullptr) {
			continue;
		}
		Ref<Mesh> mesh;
		MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(node);
		if (mesh_node != nullptr && mesh_node->is_visible_in_tree()) {
			mesh = mesh_node->get_mesh();
			// Manual LOD preview swaps the mesh resource; gather from the
			// original so the level statistics keep the real LOD chain.
			if (lod_swapped_meshes.has(mesh_node)) {
				mesh = lod_swapped_meshes[mesh_node];
			}
		}
		MultiMeshInstance3D *multimesh_node = Object::cast_to<MultiMeshInstance3D>(node);
		if (mesh.is_null() && multimesh_node != nullptr && multimesh_node->is_visible_in_tree() && multimesh_node->get_multimesh().is_valid()) {
			mesh = multimesh_node->get_multimesh()->get_mesh();
		}
		if (mesh.is_null() || mesh_stats_map.has(mesh)) {
			continue;
		}

		MeshStats ms;
		_gather_mesh_stats_for_mesh(mesh, ms);
		mesh_stats_map[mesh] = ms;

		// Aggregate the scene-wide totals from the per-mesh statistics.
		stats_base_faces += ms.base_faces;
		stats_base_indices += ms.base_indices;
		if (ms.has_source_faces) {
			stats_source_faces += ms.source_faces;
		} else {
			stats_has_source_faces = false;
		}
		stats_lod_count = MAX(stats_lod_count, ms.max_lod_count);
		if (ms.has_any_lods) {
			stats_has_any_lods = true;
		}
	}
}

void SceneImportSettingsScreen::_update_mesh_stats() {
	if (mesh_stats_panel == nullptr || mesh_stats_label == nullptr) {
		return;
	}

	// Statistics are shown for the content currently previewed: the whole
	// model while the scene is selected ("Node" / "Animation" selections and
	// the empty selection), or the single mesh selected in the Mesh tab.
	// Material selections show a sphere preview with no meaningful mesh
	// statistics, so the overlay stays hidden for them.
	Ref<Mesh> selected_mesh;
	if (selected_type == "Mesh" && mesh_map.has(selected_id)) {
		const MeshData &md = mesh_map[selected_id];
		selected_mesh = md.live_preview_mesh.is_valid() ? md.live_preview_mesh : md.mesh;
	}
	const bool scene_mode = selected_type == "Node" || selected_type == "Animation" || selected_type.is_empty();
	if (scene == nullptr || (!scene_mode && selected_mesh.is_null())) {
		mesh_stats_panel->hide();
		return;
	}

	if (mesh_stats_dirty) {
		_gather_mesh_stats();
		mesh_stats_dirty = false;
	}

	// Per-selected-mesh statistics are cached (gathering fetches the mesh
	// surface arrays) and re-gathered whenever the mesh selection changes.
	const MeshStats *selected_stats = nullptr;
	if (!scene_mode) {
		if (stats_selected_mesh != selected_mesh) {
			_gather_mesh_stats_for_mesh(selected_mesh, stats_selected);
			stats_selected_mesh = selected_mesh;
		}
		selected_stats = &stats_selected;
	}

	struct PreviewMeshInstance {
		const MeshStats *stats = nullptr;
		AABB bounds;
		real_t lod_scale = 1.0;
	};
	Vector<PreviewMeshInstance> evaluated_meshes;
	if (selected_stats != nullptr) {
		const Transform3D transform = mesh_preview->get_global_transform();
		const Vector3 scale = transform.basis.get_scale_abs();
		PreviewMeshInstance entry;
		entry.stats = selected_stats;
		entry.bounds = transform.xform(selected_mesh->get_aabb());
		entry.lod_scale = MAX(scale.x, MAX(scale.y, scale.z)) * mesh_preview->get_lod_bias();
		evaluated_meshes.push_back(entry);
	} else {
		for (const KeyValue<String, NodeData> &E : node_map) {
			GeometryInstance3D *instance = Object::cast_to<GeometryInstance3D>(E.value.node);
			if (!instance || !instance->is_visible_in_tree()) {
				continue;
			}
			Ref<Mesh> mesh;
			MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(instance);
			MultiMeshInstance3D *multi = Object::cast_to<MultiMeshInstance3D>(instance);
			if (mesh_node) {
				mesh = lod_swapped_meshes.has(mesh_node) ? lod_swapped_meshes[mesh_node] : mesh_node->get_mesh();
			} else if (multi && multi->get_multimesh().is_valid()) {
				mesh = multi->get_multimesh()->get_mesh();
			}
			if (mesh.is_null() || !mesh_stats_map.has(mesh)) {
				continue;
			}
			const Transform3D transform = instance->get_global_transform();
			const Vector3 scale = transform.basis.get_scale_abs();
			PreviewMeshInstance entry;
			entry.stats = &mesh_stats_map[mesh];
			entry.bounds = transform.xform(instance->get_aabb());
			entry.lod_scale = MAX(scale.x, MAX(scale.y, scale.z)) * instance->get_lod_bias();
			evaluated_meshes.push_back(entry);
		}
	}
	const Size2 viewport_size = base_viewport->get_size();
	const float lod_threshold_pixels = base_viewport->get_mesh_lod_threshold();
	const float lod_distance_multiplier = camera->get_camera_projection().get_lod_multiplier();
	const bool lods_disabled = base_viewport->get_debug_draw() == Viewport::DEBUG_DRAW_DISABLE_LOD;
	const real_t lod_threshold = (viewport_size.width > 0 && lod_threshold_pixels > 0.0) ? lod_threshold_pixels / viewport_size.width : 0.0;
	const Vector3 camera_position = camera->get_global_transform().origin;

	// Evaluate the LOD level the renderer currently picks for the preview
	// camera, and the triangle count it actually draws with, using the same
	// rules as RendererRD::MeshStorage::mesh_surface_get_lod (screen size vs
	// threshold, ascending edge lengths).
	// Highest LOD level available across the evaluated surfaces (LOD 0 is
	// the full-detail base level).
	int levels_count = 1;
	for (const PreviewMeshInstance &instance : evaluated_meshes) {
		for (const MeshSurfaceStats &surface : instance.stats->surfaces) {
			levels_count = MAX(levels_count, surface.lod_edge_lengths.size() + 1);
		}
	}
	const Vector<int> *preview_map = nullptr;
	if (selected_type == "Mesh" && mesh_map.has(selected_id) && !mesh_map[selected_id].preview_lod_map.is_empty()) {
		preview_map = &mesh_map[selected_id].preview_lod_map;
		levels_count = preview_map->size();
	}
	stats_level_faces.clear();
	stats_level_faces.resize(levels_count);
	stats_level_faces.fill(0);
	stats_level_vertices.resize(levels_count);
	stats_level_vertices.fill(0);

	// Evaluate the LOD level the renderer currently picks for the preview
	// camera, and the triangle count it actually draws with, using the same
	// rules as RendererRD::MeshStorage::mesh_surface_get_lod (screen size vs
	// threshold, ascending edge lengths). In manual mode every surface is
	// pinned to the selected level instead.
	stats_active_faces = 0;
	stats_active_indices = 0;
	stats_active_vertices = 0;

	int lod_min = -1;
	int lod_max = -1;
	const bool manual_lod = lod_mode == LOD_MODE_MANUAL;
	for (const PreviewMeshInstance &instance : evaluated_meshes) {
		const Vector3 aabb_min = instance.bounds.position;
		const Vector3 aabb_max = instance.bounds.position + instance.bounds.size;
		const Vector3 surface_distance = Vector3().max(aabb_min - camera_position).max(camera_position - aabb_max);
		const real_t distance = MAX(surface_distance.length(), (real_t)CMP_EPSILON);
		for (const MeshSurfaceStats &surface : instance.stats->surfaces) {
			// LOD level the renderer picks for this surface right now, and
			// the triangle count it draws with.
			int lod = 0;
			uint64_t index_count = surface.index_count;
			uint64_t vertex_count = surface.vertex_count;
			if (manual_lod) {
				const int mapped_level = preview_map && lod_manual_level < preview_map->size() ? (*preview_map)[lod_manual_level] : lod_manual_level;
				const int surface_level = MIN(mapped_level, surface.lod_edge_lengths.size());
				if (surface_level > 0) {
					lod = preview_map ? lod_manual_level : surface_level;
					index_count = surface.lod_index_counts[surface_level - 1];
					vertex_count = surface.lod_vertex_counts[surface_level - 1];
				}
			} else if (!lods_disabled && surface.lod_edge_lengths.size() > 0 && lod_threshold > 0.0) {
				for (int i = 0; i < surface.lod_edge_lengths.size(); i++) {
					const real_t screen_size = surface.lod_edge_lengths[i] * instance.lod_scale / (distance * lod_distance_multiplier);
					if (screen_size > lod_threshold) {
						break;
					}
					lod = i + 1;
					index_count = surface.lod_index_counts[i];
					vertex_count = surface.lod_vertex_counts[i];
				}
			}
			stats_active_faces += _mesh_faces_from_primitive(surface.primitive, index_count, vertex_count);
			stats_active_indices += index_count;
			stats_active_vertices += vertex_count;
			lod_min = lod_min < 0 ? lod : MIN(lod_min, lod);
			lod_max = lod_max < 0 ? lod : MAX(lod_max, lod);

			// Per-level totals for the level breakdown row (level 0 = full
			// detail).
			for (int i = 0; i < levels_count; i++) {
				// A surface with fewer levels keeps its last available level, as
				// in the manual preview. Include every surface in each total.
				const int mapped_level = preview_map ? (*preview_map)[i] : i;
				const int surface_level = MIN(mapped_level, surface.lod_index_counts.size());
				const uint64_t level_indices = surface_level > 0 ? surface.lod_index_counts[surface_level - 1] : surface.index_count;
				const uint64_t level_vertices = surface_level > 0 ? surface.lod_vertex_counts[surface_level - 1] : surface.vertex_count;
				stats_level_faces.write[i] += _mesh_faces_from_primitive(surface.primitive, level_indices, level_vertices);
				stats_level_vertices.write[i] += level_vertices;
			}
		}
	}
	if (lod_min < 0) {
		lod_min = 0;
	}
	if (lod_max < 0) {
		lod_max = 0;
	}

	stats_lod_min = lod_min;
	stats_lod_max = lod_max;
	stats_mixed_lod = lod_min != lod_max;
	if (lod_preview_slider && lod_mode == LOD_MODE_AUTO) {
		lod_preview_slider->set_value_no_signal(lod_max);
	}

	// Format the overlay text: each metric on its own line, plain white text.
	// The "Polygons/Faces" row shows the original polygon count of the
	// full-detail content currently previewed (the whole model in scene mode,
	// the selected mesh in per-mesh mode). The "LOD" row shows ONLY the level
	// currently being viewed: the manually pinned level (the LOD buttons in
	// the viewport toolbar) or the level the preview camera selects in Auto
	// mode. The full per-level breakdown is intentionally not shown here;
	// per-level details live in the LOD settings cards of the inspector.
	// Polygons/Faces is the number of polygons in the source file before
	// triangulation (a quad or an n-gon each count as a single polygon). The
	// polygon row is only shown when the source format preserves it; formats
	// that only store triangles (e.g. glTF/GLB) lose the original polygon
	// count, so no misleading substitute row is shown for them.
	const bool has_source_faces = scene_mode ? stats_has_source_faces : selected_stats->has_source_faces;
	const uint64_t source_faces = scene_mode ? stats_source_faces : selected_stats->source_faces;
	String text;
	if (has_source_faces) {
		text += vformat(TTR("Polygons/Faces: %s"),
				_format_stats_count(source_faces));
	}

	if (!text.is_empty()) {
		text += "\n";
	}
	if (stats_mixed_lod) {
		text += vformat(TTR("LOD %s-%s Triangles: %s"), itos(stats_lod_min), itos(stats_lod_max), _format_stats_count(stats_active_faces));
	} else {
		text += vformat(TTR("LOD %s Triangles: %s"), itos(stats_lod_max), _format_stats_count(stats_active_faces));
	}
	text += "\n" + vformat(TTR("LOD Vertices: %s"), _format_stats_count(stats_active_vertices));
	if (selected_stats != nullptr && !evaluated_meshes.is_empty()) {
		const AABB &bounds = evaluated_meshes[0].bounds;
		text += "\n" + vformat(TTR("Camera distance: %s m"), String::num(camera_position.distance_to(bounds.get_center()), 1));
	}

	// Keep the LOD level buttons and the inspector LOD cards in sync with
	// the available levels (the guards prevent rebuilds on every camera
	// move, so slider edits are never interrupted).
	if (lod_preview_slider != nullptr && lod_picker_levels != levels_count - 1) {
		_update_lod_toolbar_buttons(levels_count - 1);
	}
	if (lod_settings_panel != nullptr && lod_level_settings.size() != stats_level_faces.size()) {
		_update_lod_settings_panel();
	}

	mesh_stats_label->set_text(text);
	mesh_stats_panel->show();
}

// Builds (and caches) a preview mesh holding the actual decimated geometry
// of a single LOD level, so manual inspection can zoom into any level.
Ref<Mesh> SceneImportSettingsScreen::_get_lod_preview_mesh(const Ref<Mesh> &p_mesh, int p_level) {
	if (p_mesh.is_null() || p_level <= 0) {
		return p_mesh;
	}
	// One build per mesh, for the level last pinned: every mesh of the scene
	// keeps its own entry (clearing on a miss rebuilt all of them each time).
	if (lod_preview_mesh_cache.has(p_mesh)) {
		const Ref<Mesh> &cached = lod_preview_mesh_cache[p_mesh];
		if (cached.is_valid() && cached->has_meta("lod_preview_level") && (int)cached->get_meta("lod_preview_level") == p_level) {
			return cached;
		}
	}

	Ref<ArrayMesh> preview;
	preview.instantiate();
	preview->set_name(vformat("%s (LOD %d preview)", p_mesh->get_name(), p_level));
	preview->set_meta("lod_preview_level", p_level);
	for (int b = 0; b < p_mesh->get_blend_shape_count(); b++) {
		preview->add_blend_shape(p_mesh->get_blend_shape_name(b));
	}
	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		Array arrays = p_mesh->surface_get_arrays(s);
		if (arrays.size() != Mesh::ARRAY_MAX) {
			continue;
		}
		Dictionary lods = p_mesh->surface_get_lods(s);
		Vector<double> edge_lengths;
		for (const KeyValue<Variant, Variant> &K : lods) {
			if (K.key.is_num()) {
				edge_lengths.push_back(K.key);
			}
		}
		edge_lengths.sort();
		if (!edge_lengths.is_empty()) {
			Vector<int> lod_indices = lods[edge_lengths[MIN(p_level, edge_lengths.size()) - 1]];
			if (!lod_indices.is_empty()) {
				arrays[Mesh::ARRAY_INDEX] = lod_indices;
			}
		}
		preview->add_surface_from_arrays(p_mesh->surface_get_primitive_type(s), arrays, p_mesh->surface_get_blend_shape_arrays(s));
		preview->surface_set_material(preview->get_surface_count() - 1, p_mesh->surface_get_material(s));
	}

	Ref<Mesh> result = preview;
	lod_preview_mesh_cache[p_mesh] = result;
	return result;
}

// Applies the current LOD preview state to the previewed scene meshes: in
// Auto mode the real meshes stay put and the renderer switches levels with
// the camera (the bias scales the screen-space threshold); in manual mode
// every visible mesh is swapped for the actual decimated geometry of the
// pinned level (LOD 0 restores the originals).
void SceneImportSettingsScreen::_apply_lod_preview() {
	// Restore the originals of any previous manual swap first.
	for (const KeyValue<MeshInstance3D *, Ref<Mesh>> &E : lod_swapped_meshes) {
		if (E.key != nullptr && E.value.is_valid()) {
			E.key->set_mesh(E.value);
		}
	}
	lod_swapped_meshes.clear();

	if (lod_mode != LOD_MODE_MANUAL || scene == nullptr) {
		_update_preview_lod_threshold();
		_apply_material_isolation();
		return;
	}

	if (selected_type == "Mesh" && mesh_map.has(selected_id) && mesh_preview->is_visible()) {
		const MeshData &md = mesh_map[selected_id];
		Ref<Mesh> original = md.live_preview_mesh.is_valid() ? md.live_preview_mesh : md.mesh;
		const int source_level = !md.preview_lod_map.is_empty() && lod_manual_level < md.preview_lod_map.size() ? md.preview_lod_map[lod_manual_level] : lod_manual_level;
		Ref<Mesh> preview = _get_lod_preview_mesh(original, source_level);
		if (preview != original) {
			lod_swapped_meshes[mesh_preview] = original;
			mesh_preview->set_mesh(preview);
		}
	}
	for (const KeyValue<String, NodeData> &E : node_map) {
		MeshInstance3D *mesh_node = Object::cast_to<MeshInstance3D>(E.value.node);
		if (mesh_node == nullptr || !mesh_node->is_visible_in_tree()) {
			continue;
		}
		Ref<Mesh> mesh = mesh_node->get_mesh();
		if (mesh.is_null()) {
			continue;
		}
		Ref<Mesh> preview_mesh = _get_lod_preview_mesh(mesh, lod_manual_level);
		if (preview_mesh != mesh) {
			lod_swapped_meshes[mesh_node] = mesh;
			mesh_node->set_mesh(preview_mesh);
		}
	}
	_apply_material_isolation();
}

// Rebuilds the flat per-level LOD buttons in the viewport toolbar. Called
// only when the number of available levels changes (p_max_level = highest
// available level index, LOD 0 = full detail).
void SceneImportSettingsScreen::_update_lod_toolbar_buttons(int p_max_level) {
	lod_picker_levels = MAX(p_max_level, 0);
	if (lod_preview_slider) {
		lod_preview_slider->set_value_no_signal(MIN(lod_manual_level, lod_picker_levels));
		lod_preview_slider->set_max(MAX(lod_picker_levels, 1));
		lod_preview_slider->set_editable(lod_picker_levels > 0);
	}
	_update_lod_button_states();
}

void SceneImportSettingsScreen::_on_lod_preview_changed(double p_value) {
	_on_lod_level_selected(CLAMP((int)p_value, 0, lod_picker_levels));
}

// Refreshes the pressed state of the per-level buttons (and the LOD card
// select buttons) from the current lod_mode / lod_manual_level.
void SceneImportSettingsScreen::_update_lod_button_states() {
	const bool manual = lod_mode == LOD_MODE_MANUAL;
	if (lod_preview_label) {
		lod_preview_label->set_text(manual ? vformat(TTR("LOD %d"), lod_manual_level) : TTR("Auto"));
	}
	if (lod_preview_slider && manual) {
		lod_preview_slider->set_value_no_signal(lod_manual_level);
	}
	for (int i = 0; i < lod_level_buttons.size(); i++) {
		if (lod_level_buttons[i]) {
			lod_level_buttons[i]->set_pressed(manual && lod_manual_level == i);
		}
	}
	for (int i = 0; i < lod_card_select_buttons.size(); i++) {
		Button *btn = lod_card_select_buttons[i];
		if (btn) {
			const bool active = manual && lod_manual_level == i;
			btn->set_text(active ? TTR("Viewing") : TTR("View"));
			btn->set_disabled(active);
		}
	}
}

void SceneImportSettingsScreen::_on_lod_level_selected(int p_level) {
	// Manual level pin: swap the preview meshes for this level's geometry.
	lod_mode = LOD_MODE_MANUAL;
	lod_manual_level = p_level;
	if (selected_type == "Mesh") {
		lod_selected_level = p_level;
	}
	if (lod_auto_toggle != nullptr) {
		lod_auto_toggle->set_pressed_no_signal(false);
	}
	_update_lod_button_states();
	_apply_lod_preview();
	_update_mesh_stats();
	if (selected_type == "Mesh") {
		_update_lod_settings_panel();
	}
}

void SceneImportSettingsScreen::_on_lod_auto_toggled(bool p_enabled) {
	if (p_enabled) {
		lod_mode = LOD_MODE_AUTO;
		lod_manual_level = 0;
	} else {
		// Turning Auto off pins the current level (LOD 0 by default) so a
		// single level can be inspected with the LOD buttons.
		lod_mode = LOD_MODE_MANUAL;
	}
	_update_lod_button_states();
	_apply_lod_preview();
	_update_mesh_stats();
}

// Default level entries mirror the already generated chain. The settings
// become explicit only after the user edits them.
Array SceneImportSettingsScreen::_get_custom_lod_levels() const {
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return Array();
	}
	const MeshData &md = mesh_map[selected_id];
	if (md.settings.has("lods/custom_levels") && bool(md.settings["lods/custom_levels"])) {
		return Array(md.settings["lods/levels"]).duplicate(true);
	}
	Array levels;
	for (int i = 1; i < stats_level_faces.size(); i++) {
		Dictionary level;
		const float keep = stats_level_faces[i - 1] ? (float)stats_level_faces[i] / stats_level_faces[i - 1] : 0.5f;
		level["keep"] = CLAMP(keep, 0.05f, 0.95f);
		level["transition_bias"] = 1.0f;
		levels.push_back(level);
	}
	return levels;
}

void SceneImportSettingsScreen::_set_custom_lod_levels(const Array &p_levels) {
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return;
	}
	MeshData &md = mesh_map[selected_id];
	if (!md.has_import_id) {
		return;
	}
	md.settings["lods/custom_levels"] = true;
	md.settings["lods/levels"] = p_levels.duplicate(true);
	md.lod_pending = true;
	md.lod_preview_dirty = true;
	lod_settings_pending = true;
	if (lod_transition_estimate) {
		lod_transition_estimate->set_text(TTR("Updating LOD preview…"));
	}
	if (lod_status_label) {
		lod_status_label->set_text(TTR("Updating LOD preview… Changes are pending. Apply Changes generates the project asset."));
	}
	_schedule_lod_edit_update();
}

void SceneImportSettingsScreen::_schedule_lod_edit_update() {
	if (lod_edit_timer) {
		lod_edit_timer->start();
	} else {
		_finish_lod_edit_update();
	}
}

// While a slider is still held, wait: finishing rebuilds the settings panel,
// which would free the slider under the mouse and end the drag.
void SceneImportSettingsScreen::_lod_edit_timer_timeout() {
	if (Input::get_singleton()->is_mouse_button_pressed(MouseButton::LEFT)) {
		lod_edit_timer->start();
		return;
	}
	_finish_lod_edit_update();
}

void SceneImportSettingsScreen::_save_lod_config() {
	if (base_path.is_empty() || mesh_map.is_empty()) {
		return;
	}
	// Only models whose LODs were edited get a sidecar; opening a model must
	// not create one (it would then override later Import dock changes).
	if (!lod_settings_pending && !FileAccess::exists(base_path + ".lodcfg")) {
		return;
	}
	for (const KeyValue<String, MeshData> &entry : mesh_map) {
		Dictionary saved_settings;
		for (const StringName &key : { StringName("generate/lods"), StringName("lods/normal_merge_angle"), StringName("lods/count"), StringName("lods/reduction"), StringName("lods/custom_levels"), StringName("lods/levels") }) {
			if (entry.value.settings.has(key)) {
				saved_settings[String(key)] = entry.value.settings[key];
			}
		}
		if (saved_settings.is_empty()) {
			lod_config_data.erase(entry.key);
		} else {
			lod_config_data[entry.key] = saved_settings;
		}
	}
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("lod", "version", 1);
	config->set_value("lod", "meshes", lod_config_data);
	const Error error = config->save(base_path + ".lodcfg");
	if (error != OK) {
		ERR_PRINT("Unable to save model LOD settings: " + base_path + ".lodcfg");
	}
}

void SceneImportSettingsScreen::_finish_lod_edit_update() {
	_save_lod_config();
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return;
	}
	MeshData &md = mesh_map[selected_id];
	if (!md.lod_pending || md.lod_source_mesh.is_null()) {
		return;
	}
	_regenerate_lod_preview(md);
}

// Rebuilds the selected mesh's preview from its clean source with its
// current LOD settings and refreshes the stats and the panel.
void SceneImportSettingsScreen::_regenerate_lod_preview(MeshData &md) {
	if (md.lod_source_mesh.is_null()) {
		return;
	}
	md.lod_preview_deferred = false;
	for (const KeyValue<MeshInstance3D *, Ref<Mesh>> &entry : lod_swapped_meshes) {
		if (entry.key != nullptr && entry.value.is_valid()) {
			entry.key->set_mesh(entry.value);
		}
	}
	lod_swapped_meshes.clear();

	const int mode = md.settings.has("generate/lods") ? (int)md.settings["generate/lods"] : 0;
	const bool generate_lods = mode == 1 || (mode == 0 && (!defaults.has("meshes/generate_lods") || bool(defaults["meshes/generate_lods"])));
	Ref<ImporterMesh> preview_source = _duplicate_importer_mesh(md.lod_source_mesh);
	if (preview_source.is_null()) {
		return;
	}
	if (generate_lods) {
		const float merge_angle = md.settings.has("lods/normal_merge_angle") ? (float)md.settings["lods/normal_merge_angle"] : 20.0f;
		const int lod_count = md.settings.has("lods/count") ? (int)md.settings["lods/count"] : (defaults.has("meshes/lod_count") ? (int)defaults["meshes/lod_count"] : 0);
		const float reduction = md.settings.has("lods/reduction") ? (float)md.settings["lods/reduction"] : (defaults.has("meshes/lod_reduction") ? (float)defaults["meshes/lod_reduction"] : 0.5f);
		const bool custom_levels = md.settings.has("lods/custom_levels") && bool(md.settings["lods/custom_levels"]);
		const Array levels = custom_levels ? Array(md.settings["lods/levels"]) : Array();
		preview_source->generate_lods(merge_angle, md.lod_skin_pose_transforms, lod_count, reduction, custom_levels, levels);
	}
	md.lod_unbiased_distances = preview_source->get_meta(SNAME("_lod_unbiased_distances"), Array());

	md.live_preview_mesh = preview_source->get_mesh();
	md.lod_preview_dirty = false;
	md.preview_lod_map.clear();
	lod_preview_mesh_cache.clear();
	stats_selected_mesh.unref();
	mesh_preview->set_mesh(md.live_preview_mesh);
	_apply_import_materials_to_mesh_preview(md.live_preview_mesh);
	_update_mesh_stats();
	const int max_level = MAX(stats_level_faces.size() - 1, 0);
	lod_manual_level = MIN(lod_manual_level, max_level);
	lod_selected_level = MIN(lod_selected_level, max_level);
	lod_auto_toggle->set_disabled(false);
	if (lod_mode == LOD_MODE_AUTO) {
		lod_auto_toggle->set_pressed_no_signal(true);
	} else {
		_apply_lod_preview();
	}
	_update_mesh_stats();
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_lod_select_level(int p_level) {
	lod_selected_level = p_level;
	if (p_level < stats_level_faces.size()) {
		_on_lod_level_selected(p_level);
	} else {
		_update_lod_settings_panel();
	}
}

void SceneImportSettingsScreen::_lod_add_level() {
	Array levels = _get_custom_lod_levels();
	if (levels.size() >= 10) {
		return;
	}
	Dictionary level;
	level["keep"] = 0.5;
	level["transition_bias"] = 1.0;
	levels.push_back(level);
	_set_custom_lod_levels(levels);
	lod_selected_level = levels.size();
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_lod_remove_level(int p_index) {
	Array levels = _get_custom_lod_levels();
	if (p_index < 0 || p_index >= levels.size()) {
		return;
	}
	const int removed_level = p_index + 1; // LOD 0 is the retained source mesh.
	MeshData &md = mesh_map[selected_id];
	// Keep the target of the next surviving level relative to its new
	// predecessor when the removed level was in the middle of the chain.
	if (p_index + 1 < levels.size()) {
		Dictionary removed = levels[p_index];
		Dictionary next = levels[p_index + 1];
		next["keep"] = CLAMP((double)removed.get("keep", 0.5) * (double)next.get("keep", 0.5), 0.05, 0.95);
		levels[p_index + 1] = next;
	}
	levels.remove_at(p_index);
	md.preview_lod_map.clear();
	_set_custom_lod_levels(levels);
	// Keep the same surviving LOD selected when an earlier row is removed.
	// If the selected row itself was removed, keep its index to select the
	// next level that shifted into its place, or fall back to the previous
	// level when the removed row was the last one.
	if (lod_selected_level > removed_level) {
		lod_selected_level--;
	} else if (lod_selected_level == removed_level) {
		lod_selected_level = MIN(removed_level, levels.size());
	}
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_lod_level_value_changed(double p_value, int p_index) {
	Array levels = _get_custom_lod_levels();
	if (p_index < 0 || p_index >= levels.size()) {
		return;
	}
	Dictionary level = levels[p_index];
	level["keep"] = CLAMP(p_value / 100.0, 0.05, 0.95);
	levels[p_index] = level;
	_set_custom_lod_levels(levels);
	if (lod_preset_selector) {
		lod_preset_selector->select(4);
	}
}

void SceneImportSettingsScreen::_lod_target_distance_changed(double p_value) {
	const int level_index = lod_selected_level - 1;
	if (p_value <= 0.0 || lod_target_anchor <= 0.0) {
		return;
	}
	Array levels = _get_custom_lod_levels();
	if (level_index < 0 || level_index >= levels.size()) {
		return;
	}
	// Adjust the stored per-level transition value while keeping the target
	// distance control as the only user-facing control for LOD timing.
	const double transition_bias = CLAMP(lod_target_anchor / p_value, 0.1, 10.0);
	Dictionary level = levels[level_index];
	level["transition_bias"] = transition_bias;
	levels[level_index] = level;
	_set_custom_lod_levels(levels);
	if (lod_target_distance_input) {
		lod_target_distance_input->set_value_no_signal(lod_target_anchor / transition_bias);
	}
	if (lod_preset_selector) {
		lod_preset_selector->select(4);
	}
}

void SceneImportSettingsScreen::_lod_preset_selected(int p_index) {
	if (p_index == 0) {
		_lod_reset_levels();
		return;
	}
	// Each fraction is relative to the preceding level.
	const double keep = p_index == 1 ? 0.7 : p_index == 2 ? 0.5 : 0.35;
	Array levels;
	for (int i = 0; i < 3; i++) {
		Dictionary level;
		level["keep"] = keep;
		level["transition_bias"] = 1.0;
		levels.push_back(level);
	}
	_set_custom_lod_levels(levels);
	if (mesh_map.has(selected_id)) {
		mesh_map[selected_id].preview_lod_map.clear();
	}
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_lod_reset_levels() {
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		return;
	}
	// Stored as "off" rather than erased: the sidecar only overrides keys it
	// has, so an erased key brought back custom levels from the .import file.
	mesh_map[selected_id].settings["lods/custom_levels"] = false;
	mesh_map[selected_id].settings["lods/levels"] = Array();
	mesh_map[selected_id].preview_lod_map.clear();
	mesh_map[selected_id].live_preview_mesh.unref();
	stats_selected_mesh.unref();
	lod_auto_toggle->set_disabled(false);
	lod_picker_levels = -1;
	_apply_lod_preview();
	_update_mesh_stats();
	mesh_map[selected_id].lod_pending = true;
	mesh_map[selected_id].lod_preview_dirty = true;
	lod_settings_pending = true;
	_schedule_lod_edit_update();
	_update_lod_settings_panel();
}

void SceneImportSettingsScreen::_build_lod_settings_panel() {
	lod_settings_panel = memnew(VBoxContainer);
	lod_settings_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_settings_panel->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	Label *header = memnew(Label);
	header->set_text(TTR("LOD Settings"));
	lod_settings_panel->add_child(header);
}

void SceneImportSettingsScreen::_gather_lod_level_settings() {
	lod_level_settings.clear();
	for (int i = 0; i < stats_level_faces.size(); i++) {
		LODLevelSettings level;
		level.level = i;
		level.triangle_count = stats_level_faces[i];
		lod_level_settings.push_back(level);
	}
}

void SceneImportSettingsScreen::_update_lod_settings_panel() {
	if (!lod_settings_panel) {
		return;
	}
	while (lod_settings_panel->get_child_count() > 1) {
		Node *child = lod_settings_panel->get_child(1);
		lod_settings_panel->remove_child(child);
		child->queue_free();
	}
	lod_card_select_buttons.clear();
	lod_status_label = nullptr;
	lod_transition_estimate = nullptr;
	lod_target_distance_input = nullptr;
	lod_target_anchor = 0.0;
	lod_preset_selector = nullptr;
	_gather_lod_level_settings();
	if (selected_type != "Mesh" || !mesh_map.has(selected_id)) {
		Label *hint = memnew(Label);
		hint->set_text(TTR("Select a mesh to configure its LOD levels."));
		hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		lod_settings_panel->add_child(hint);
		return;
	}
	MeshData &md = mesh_map[selected_id];
	const int mode = md.settings.has("generate/lods") ? (int)md.settings["generate/lods"] : 0;
	const bool enabled = mode == 1 || (mode == 0 && (!defaults.has("meshes/generate_lods") || bool(defaults["meshes/generate_lods"])));
	CheckButton *generate = memnew(CheckButton);
	generate->set_text(TTR("Generate LODs"));
	generate->set_pressed_no_signal(enabled);
	generate->set_disabled(!md.has_import_id);
	generate->connect(SceneStringName(toggled), callable_mp(this, &SceneImportSettingsScreen::_lod_generate_toggled));
	lod_settings_panel->add_child(generate);
	HBoxContainer *angle_row = memnew(HBoxContainer);
	lod_settings_panel->add_child(angle_row);
	Label *angle_label = memnew(Label);
	angle_label->set_text(TTR("Normal Merge Angle"));
	angle_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	angle_row->add_child(angle_label);
	SpinBox *angle = memnew(SpinBox);
	angle->set_min(0); angle->set_max(180); angle->set_step(1);
	angle->set_editable(md.has_import_id);
	angle->set_suffix(" deg");
	angle->set_value_no_signal(md.settings.has("lods/normal_merge_angle") ? (double)md.settings["lods/normal_merge_angle"] : 20.0);
	angle->set_tooltip_text(TTR("Lower values preserve more geometry edges during LOD simplification."));
	angle->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_lod_merge_angle_changed));
	angle_row->add_child(angle);
	HBoxContainer *preset_row = memnew(HBoxContainer);
	lod_settings_panel->add_child(preset_row);
	Label *preset_label = memnew(Label);
	preset_label->set_text(TTR("LOD Preset"));
	preset_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	preset_row->add_child(preset_label);
	OptionButton *preset = memnew(OptionButton);
	lod_preset_selector = preset;
	preset->add_item(TTR("Automatic"));
	preset->add_item(TTR("Quality"));
	preset->add_item(TTR("Balanced"));
	preset->add_item(TTR("Performance"));
	preset->add_item(TTR("Custom"));
	// Recognise the presets from their levels (3 levels, the same keep
	// fraction, neutral timing), so a picked preset does not read "Custom".
	int preset_index = 0;
	if (md.settings.has("lods/custom_levels") && bool(md.settings["lods/custom_levels"])) {
		preset_index = 4;
		const Array preset_levels = md.settings.has("lods/levels") ? Array(md.settings["lods/levels"]) : Array();
		const double preset_keeps[3] = { 0.7, 0.5, 0.35 };
		for (int candidate = 0; candidate < 3 && preset_levels.size() == 3; candidate++) {
			bool matches = true;
			for (int i = 0; i < 3 && matches; i++) {
				const Dictionary level = preset_levels[i];
				matches = Math::is_equal_approx((double)level.get("keep", -1.0), preset_keeps[candidate]) && Math::is_equal_approx((double)level.get("transition_bias", -1.0), 1.0);
			}
			if (matches) {
				preset_index = candidate + 1;
				break;
			}
		}
	}
	preset->select(preset_index);
	preset->set_item_disabled(4, true);
	preset->set_disabled(!md.has_import_id);
	preset->connect(SceneStringName(item_selected), callable_mp(this, &SceneImportSettingsScreen::_lod_preset_selected));
	preset_row->add_child(preset);

	const Array levels = _get_custom_lod_levels();
	lod_selected_level = CLAMP(lod_selected_level, 0, levels.size());
	lod_status_label = memnew(Label);
	lod_status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	String lod_status;
	if (md.lod_preview_dirty) {
		lod_status = TTR("Updating LOD preview… Changes are pending. Apply Changes generates the project asset when you are ready.");
	} else if (lod_settings_pending) {
		lod_status = TTR("Preview is up to date. LOD settings are saved with this model; Apply Changes generates the project asset when you are ready.");
	} else {
		lod_status = TTR("Statistics from the current imported mesh.");
	}
	if (!md.has_import_id) {
		lod_status = TTR("This mesh has no stable import ID. Name it in the source file before saving per-mesh LOD settings.");
	} else if (!md.lod_preview_dirty && enabled && md.settings.has("lods/custom_levels") && bool(md.settings["lods/custom_levels"]) && levels.size() > MAX(stats_level_faces.size() - 1, 0)) {
		lod_status = vformat(TTR("Generated %d of %d requested LODs. The mesh could not be simplified further."), MAX(stats_level_faces.size() - 1, 0), levels.size());
	}
	lod_status_label->set_text(lod_status);
	lod_settings_panel->add_child(lod_status_label);

	GridContainer *table = memnew(GridContainer);
	table->set_columns(4);
	table->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	lod_settings_panel->add_child(table);
	for (const String &title : { TTR("Level"), TTR("Triangles"), TTR("Vertices"), String() }) {
		Label *heading = memnew(Label);
		heading->set_text(title);
		heading->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		table->add_child(heading);
	}
	for (int i = 0; i <= levels.size(); i++) {
		Button *select = memnew(Button);
		select->set_text(vformat(TTR("LOD %d"), i));
		select->set_toggle_mode(true);
		select->set_pressed_no_signal(i == lod_selected_level);
		select->set_tooltip_text(i == 0 ? TTR("Base mesh") : TTR("Select this level to edit its settings and preview it."));
		select->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_lod_select_level).bind(i));
		table->add_child(select);
		const bool known = !md.lod_preview_dirty && i < stats_level_faces.size();
		Label *triangles = memnew(Label);
		triangles->set_text(known ? _format_stats_count(stats_level_faces[i]) : (md.lod_preview_dirty ? TTR("Updating…") : TTR("Unavailable")));
		if (!known) {
			triangles->set_tooltip_text(TTR("This LOD level is being generated in the preview, or the mesh could not be simplified further."));
		}
		triangles->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		table->add_child(triangles);
		Label *vertices = memnew(Label);
		vertices->set_text(known && i < stats_level_vertices.size() ? _format_stats_count(stats_level_vertices[i]) : (md.lod_preview_dirty ? TTR("Updating…") : TTR("Unavailable")));
		if (!known) {
			vertices->set_tooltip_text(TTR("This LOD level is being generated in the preview, or the mesh could not be simplified further."));
		}
		vertices->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		table->add_child(vertices);
		Button *remove = memnew(Button);
		remove->set_text(TTR("Remove"));
		remove->set_disabled(i == 0 || !md.has_import_id);
		if (i > 0) {
			remove->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_lod_remove_level).bind(i - 1));
		}
		table->add_child(remove);
	}
	Button *add = memnew(Button);
	add->set_text(TTR("+ Add LOD"));
	add->set_disabled(levels.size() >= 10 || !md.has_import_id);
	add->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_lod_add_level));
	lod_settings_panel->add_child(add);

	PanelContainer *details = memnew(PanelContainer);
	lod_settings_panel->add_child(details);
	VBoxContainer *content = memnew(VBoxContainer);
	details->add_child(content);
	Label *selected = memnew(Label);
	selected->set_text(vformat(TTR("Selected LOD %d"), lod_selected_level));
	content->add_child(selected);
	if (lod_selected_level == 0) {
		Label *base_hint = memnew(Label);
		base_hint->set_text(md.lod_preview_dirty ? TTR("Updating LOD preview…") : TTR("The original mesh is always retained."));
		base_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		content->add_child(base_hint);
	} else {
		const int i = lod_selected_level - 1;
		Dictionary level = levels[i];
		HBoxContainer *row = memnew(HBoxContainer);
		content->add_child(row);
		Label *keep_label = memnew(Label);
		keep_label->set_text(TTR("Retain triangles (%)"));
		keep_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_child(keep_label);
		SpinBox *keep = memnew(SpinBox);
		keep->set_min(5); keep->set_max(95); keep->set_step(1);
		keep->set_editable(md.has_import_id);
		keep->set_value_no_signal((double)level.get("keep", 0.5) * 100.0);
		keep->set_suffix("%");
		keep->set_tooltip_text(TTR("Target percentage of triangles retained from the preceding LOD. The simplifier may stop before reaching the target."));
		keep->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_lod_level_value_changed).bind(i));
		HSlider *keep_slider = memnew(HSlider);
		keep_slider->set_min(5); keep_slider->set_max(95); keep_slider->set_step(1);
		keep_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		keep_slider->set_editable(md.has_import_id);
		keep_slider->set_tooltip_text(TTR("Adjust the percentage of triangles retained at this level."));
		keep->share(keep_slider);
		row->add_child(keep_slider);
		row->add_child(keep);
		lod_transition_estimate = memnew(Label);
		lod_transition_estimate->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		String estimate;
		double reference_distance = 0.0;
		const float pixel_threshold = GLOBAL_GET("rendering/mesh_lod/lod_change/threshold_pixels");
		if (md.lod_preview_dirty) {
			estimate = TTR("Updating LOD preview…");
		} else if (pixel_threshold <= 0.0f) {
			estimate = TTR("Automatic LOD selection is disabled by the project pixel threshold.");
		} else {
			// Renderer uses error * model_scale / (distance * projection
			// multiplier) <= pixel threshold / viewport width. Estimate
			// for a unit-scale instance with per-instance LOD bias 1.
			const Projection reference = Projection::create_perspective(75.0, 16.0 / 9.0, 0.05, 10000.0);
			const double divisor = pixel_threshold * reference.get_lod_multiplier();
			double near_distance = 0.0;
			double far_distance = 0.0;
			bool found = false;
			for (const MeshSurfaceStats &surface : stats_selected.surfaces) {
				if (i >= surface.lod_edge_lengths.size()) {
					continue;
				}
				const double distance = surface.lod_edge_lengths[i] * 1920.0 / divisor;
				if (!Math::is_finite(distance) || distance < 0.0) {
					continue;
				}
				if (!found) {
					near_distance = distance;
					far_distance = distance;
					found = true;
				} else {
					near_distance = MIN(near_distance, distance);
					far_distance = MAX(far_distance, distance);
				}
			}
			if (found) {
				reference_distance = near_distance;
			}
			if (!found) {
				estimate = TTR("No imported transition is available for this level.");
			} else if (Math::is_equal_approx(near_distance, far_distance)) {
				estimate = vformat(TTR("Reference switch distance: approximately %s m"), String::num(near_distance, 1));
			} else {
				estimate = vformat(TTR("Reference switch distance: approximately %s-%s m across surfaces"), String::num(near_distance, 1), String::num(far_distance, 1));
			}
		}
		lod_transition_estimate->set_text(estimate);
		lod_transition_estimate->set_tooltip_text(TTR("Estimate for a 1920 x 1080 perspective viewport, vertical FOV 75 degrees, unit mesh scale, instance LOD Bias 1, and the current project LOD pixel threshold. Distance is measured from the camera to the nearest point of the mesh bounding box. Different surfaces can switch at different distances. Actual results depend on camera, scale, viewport resolution and runtime settings."));
		content->add_child(lod_transition_estimate);
		if (reference_distance > 0.0 && md.has_import_id) {
			// For meshes with multiple surfaces, the target controls the
			// earliest transition across surfaces. The anchor (distance at bias
			// 1) comes from the distances before the engine pushed a level past
			// the previous one: derived from the stored, pushed distance it was
			// wrong and a typed target did not land where it was typed.
			const double level_bias = (double)level.get("transition_bias", 1.0);
			const double anchor_divisor = pixel_threshold * Projection::create_perspective(75.0, 16.0 / 9.0, 0.05, 10000.0).get_lod_multiplier();
			double unbiased_anchor = 0.0;
			for (const Variant &surface_value : md.lod_unbiased_distances) {
				const PackedFloat32Array surface_distances = surface_value;
				if (i < surface_distances.size() && anchor_divisor > 0.0) {
					const double distance = surface_distances[i] * 1920.0 / anchor_divisor;
					unbiased_anchor = unbiased_anchor <= 0.0 ? distance : MIN(unbiased_anchor, distance);
				}
			}
			lod_target_anchor = unbiased_anchor > 0.0 ? unbiased_anchor : reference_distance * level_bias;
			if (unbiased_anchor > 0.0 && reference_distance > unbiased_anchor / level_bias * 1.01) {
				lod_transition_estimate->set_text(estimate + "\n" + vformat(TTR("Pushed past LOD %d: a level cannot switch nearer than the previous one, so the target %s m is not reachable."), i, String::num(unbiased_anchor / level_bias, 1)));
			}
			HBoxContainer *target_row = memnew(HBoxContainer);
			content->add_child(target_row);
			Label *target_label = memnew(Label);
			target_label->set_text(TTR("Reference target (m)"));
			target_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			target_row->add_child(target_label);
			lod_target_distance_input = memnew(SpinBox);
			lod_target_distance_input->set_min(MAX(lod_target_anchor / 10.0, 0.001));
			lod_target_distance_input->set_max(MAX(lod_target_anchor / 0.1, 0.01));
			lod_target_distance_input->set_step(0.01);
			lod_target_distance_input->set_value_no_signal(reference_distance);
			lod_target_distance_input->set_tooltip_text(TTR("Approximate target for the earliest surface switch under the reference camera. This updates LOD timing and the live preview; Apply Changes generates the project asset when ready."));
			lod_target_distance_input->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_lod_target_distance_changed));
			HSlider *target_slider = memnew(HSlider);
			target_slider->set_min(lod_target_distance_input->get_min());
			target_slider->set_max(lod_target_distance_input->get_max());
			target_slider->set_step(0.01);
			target_slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			target_slider->set_editable(md.has_import_id);
			target_slider->set_tooltip_text(lod_target_distance_input->get_tooltip_text());
			lod_target_distance_input->share(target_slider);
			target_row->add_child(target_slider);
			target_row->add_child(lod_target_distance_input);
		}
	}

	Button *reset = memnew(Button);
	reset->set_text(TTR("Use automatic levels"));
	reset->set_disabled(!md.has_import_id);
	reset->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_lod_reset_levels));
	lod_settings_panel->add_child(reset);
}
