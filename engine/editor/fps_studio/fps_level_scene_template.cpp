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
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"

static void _add_owned_child(Node3D *p_root, Node3D *p_child) {
	p_root->add_child(p_child, true);
	p_child->set_owner(p_root);
}

Node3D *create_fps_level_scene_root() {
	Node3D *root = memnew(Node3D);
	root->set_name("FPSLevel");

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

	DirectionalLight3D *light = memnew(DirectionalLight3D);
	light->set_name("DirectionalLight3D");
	light->set_rotation(Vector3(-0.8, -0.6, 0));
	_add_owned_child(root, light);

	Camera3D *camera = memnew(Camera3D);
	camera->set_name("Camera3D");
	camera->set_position(Vector3(0, 2, 8));
	camera->set_rotation(Vector3(-0.18, 0, 0));
	camera->set_current(true);
	_add_owned_child(root, camera);

	return root;
}
