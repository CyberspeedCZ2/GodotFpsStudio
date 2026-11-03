/**************************************************************************/
/*  post_process_volume_3d.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           Godot FPS Studio                             */
/*                         https://godotengine.org                        */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors.                  */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
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
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "scene/3d/node_3d.h"
#include "scene/resources/environment.h"

class Camera3D;

class PostProcessVolume3D : public Node3D {
	GDCLASS(PostProcessVolume3D, Node3D);

public:
	enum EnvironmentOverrideFlags {
		OVERRIDE_BACKGROUND = 1 << 0,
		OVERRIDE_AMBIENT_LIGHT = 1 << 1,
		OVERRIDE_REFLECTED_LIGHT = 1 << 2,
		OVERRIDE_TONEMAP = 1 << 3,
		OVERRIDE_SSR = 1 << 4,
		OVERRIDE_SSAO = 1 << 5,
		OVERRIDE_SSIL = 1 << 6,
		OVERRIDE_SDFGI = 1 << 7,
		OVERRIDE_GLOW = 1 << 8,
		OVERRIDE_FOG = 1 << 9,
		OVERRIDE_VOLUMETRIC_FOG = 1 << 10,
		OVERRIDE_ADJUSTMENTS = 1 << 11,
		OVERRIDE_ALL = (1 << 12) - 1,
	};

private:
	struct WeightedVolume {
		PostProcessVolume3D *volume = nullptr;
		float weight = 0.0;
	};

	Vector3 box_size = Vector3(10.0, 4.0, 10.0);
	Ref<Environment> environment;
	int environment_overrides = 0;
	float blend_radius = 2.0;
	float blend_weight = 1.0;
	int priority = 0;
	bool enabled = true;

	ObjectID controlled_camera_id;
	StringName active_volume_group;
	Ref<Environment> original_camera_environment;
	Ref<Environment> base_environment;
	Ref<Environment> output_environment;
	Vector<Ref<Environment>> observed_environments;
	String last_blend_signature;
	bool environment_dirty = true;

	StringName _get_volume_group() const;
	float _get_weight_at_position(const Vector3 &p_world_position) const;
	void _update_camera_environment();
	void _restore_camera();
	void _refresh_environment_observers(const Vector<WeightedVolume> &p_volumes, const Ref<Environment> &p_base_environment);
	void _on_environment_changed();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_box_size(const Vector3 &p_size);
	Vector3 get_box_size() const;

	void set_environment(const Ref<Environment> &p_environment);
	Ref<Environment> get_environment() const;

	void set_environment_overrides(int p_overrides);
	int get_environment_overrides() const;

	void set_blend_radius(float p_radius);
	float get_blend_radius() const;

	void set_blend_weight(float p_weight);
	float get_blend_weight() const;

	void set_priority(int p_priority);
	int get_priority() const;

	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	PackedStringArray get_configuration_warnings() const override;

	PostProcessVolume3D();
	~PostProcessVolume3D();
};

VARIANT_ENUM_CAST(PostProcessVolume3D::EnvironmentOverrideFlags);
