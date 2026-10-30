/**************************************************************************/
/*  fps_level_scene_template.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          GODOT FPS STUDIO                              */
/**************************************************************************/
/* Copyright (c) 2007-present Godot Engine contributors.                   */
/* Copyright (c) 2026 Godot FPS Studio contributors.                      */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to   */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/**************************************************************************/

#include "fps_level_scene_template.h"

#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"
#include "scene/resources/sky.h"

static void _add_owned_child(Node *p_root, Node *p_child) {
	p_root->add_child(p_child, true);
	p_child->set_owner(p_root);
}

Color get_fps_level_horizon_color() {
	// Keep both halves of the procedural sky on the same horizon color. This
	// removes the hard seam where the sky and ground portions meet.
	return Color(0.6463, 0.6558, 0.6708);
}

Ref<Environment> create_fps_level_environment() {
	Ref<ProceduralSkyMaterial> sky_material;
	sky_material.instantiate();
	// Match the cool sky and warm ground used by the Godot 3D preview, with a
	// single shared horizon color so there is no visible color band.
	sky_material->set_sky_horizon_color(get_fps_level_horizon_color());
	sky_material->set_ground_horizon_color(get_fps_level_horizon_color());

	Ref<Sky> sky;
	sky.instantiate();
	sky->set_material(sky_material);

	Ref<Environment> environment;
	environment.instantiate();
	environment->set_background(Environment::BG_SKY);
	environment->set_sky(sky);
	environment->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
	environment->set_ambient_light_energy(1.0);
	environment->set_reflection_source(Environment::REFLECTION_SOURCE_SKY);
	// Fade the preview floor's finite edge into the same color as the sky
	// horizon, while leaving the sky itself unchanged.
	environment->set_fog_enabled(true);
	environment->set_fog_mode(Environment::FOG_MODE_DEPTH);
	environment->set_fog_light_color(get_fps_level_horizon_color());
	environment->set_fog_light_energy(1.0);
	environment->set_fog_sky_affect(0.0);
	environment->set_fog_depth_begin(45.0);
	environment->set_fog_depth_end(220.0);
	environment->set_fog_depth_curve(1.15);
	return environment;
}

DirectionalLight3D *create_fps_level_sun() {
	DirectionalLight3D *sun = memnew(DirectionalLight3D);
	sun->set_rotation(Vector3(-0.8, -0.6, 0.0));
	sun->set_color(Color(1.0, 1.0, 1.0));
	sun->set_param(Light3D::PARAM_ENERGY, 1.0);
	sun->set_shadow(true);
	sun->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
	sun->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 100.0);
	sun->set_param(Light3D::PARAM_SHADOW_OPACITY, 0.55);
	sun->set_param(Light3D::PARAM_SHADOW_BLUR, 2.0);
	return sun;
}

Node3D *create_fps_level_scene_root() {
	Node3D *root = memnew(Node3D);
	root->set_name("FPSLevel");

	WorldEnvironment *world_environment = memnew(WorldEnvironment);
	world_environment->set_name("WorldEnvironment");
	world_environment->set_environment(create_fps_level_environment());
	_add_owned_child(root, world_environment);

	Ref<PlaneMesh> floor_mesh;
	floor_mesh.instantiate();
	floor_mesh->set_size(Vector2(80, 80));

	Ref<StandardMaterial3D> floor_material;
	floor_material.instantiate();
	floor_material->set_albedo(Color(0.16, 0.18, 0.21, 1));
	floor_material->set_roughness(0.9);

	MeshInstance3D *floor = memnew(MeshInstance3D);
	floor->set_name("PreviewFloor");
	floor->set_mesh(floor_mesh);
	floor->set_surface_override_material(0, floor_material);
	_add_owned_child(root, floor);

	Ref<BoxMesh> block_mesh;
	block_mesh.instantiate();
	block_mesh->set_size(Vector3(2, 2, 2));

	Ref<StandardMaterial3D> block_material;
	block_material.instantiate();
	block_material->set_albedo(Color(0.4, 0.47, 0.55, 1));
	block_material->set_roughness(0.8);

	MeshInstance3D *block = memnew(MeshInstance3D);
	block->set_name("PreviewBlock");
	block->set_position(Vector3(0, 1, 0));
	block->set_mesh(block_mesh);
	block->set_surface_override_material(0, block_material);
	_add_owned_child(root, block);

	DirectionalLight3D *light = create_fps_level_sun();
	light->set_name("DirectionalLight3D");
	_add_owned_child(root, light);

	Camera3D *camera = memnew(Camera3D);
	camera->set_name("Camera3D");
	camera->set_position(Vector3(0, 2, 8));
	camera->set_rotation(Vector3(-0.18, 0, 0));
	camera->set_current(true);
	_add_owned_child(root, camera);

	return root;
}
