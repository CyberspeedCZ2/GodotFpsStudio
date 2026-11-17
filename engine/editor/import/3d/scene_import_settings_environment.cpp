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


// FPS Studio Model View: preview environment (sun path, day/night, clouds).
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

Ref<Environment> create_model_view_environment(const Ref<ShaderMaterial> &p_sky_material) {
	Ref<Sky> sky;
	sky.instantiate();
	sky->set_material(p_sky_material);
	sky->set_process_mode(Sky::PROCESS_MODE_REALTIME);

	Ref<Environment> preview_environment;
	preview_environment.instantiate();
	preview_environment->set_background(Environment::BG_SKY);
	preview_environment->set_sky(sky);
	preview_environment->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
	preview_environment->set_ambient_light_energy(1.35);
	preview_environment->set_reflection_source(Environment::REFLECTION_SOURCE_SKY);
	preview_environment->set_fog_enabled(true);
	preview_environment->set_fog_mode(Environment::FOG_MODE_DEPTH);
	preview_environment->set_fog_light_color(Color(0.43, 0.51, 0.61));
	preview_environment->set_fog_light_energy(1.0);
	preview_environment->set_fog_sky_affect(0.0);
	preview_environment->set_fog_depth_begin(60.0);
	preview_environment->set_fog_depth_end(600.0);
	preview_environment->set_fog_depth_curve(1.2);
	return preview_environment;
}

void SceneImportSettingsScreen::_setup_model_preview_environment() {
	if (!base_viewport || !camera || !light1 || !preview_world_environment_node || preview_sky_material.is_null()) {
		return;
	}

	environment = create_model_view_environment(preview_sky_material);
	preview_world_environment_node->set_environment(environment);
	camera->set_environment(environment);

	preview_time_of_day = Math::fposmod((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "day_night_time", 12.0), 24.0f);
	const float cycle_duration_seconds = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_cycle_duration_seconds", 60.0), 10.0f, 600.0f);
	const float sun_azimuth_degrees = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_rotation_degrees", 0.0), -180.0f, 180.0f);
	const float sun_path_tilt = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_path_tilt_degrees", 35.0), 0.0f, 80.0f);
	const float sun_energy = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_energy", 1.2), 0.0f, 5.0f);
	preview_sun_animation_enabled = EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "sun_animation_enabled", false);
	const bool clouds_enabled = EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "clouds_enabled", true);
	const float cloud_coverage = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "cloud_coverage", 0.5), 0.0f, 1.0f);
	const float cloud_scale = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "cloud_scale", 1.5), 0.2f, 5.0f);
	const float cloud_speed = CLAMP((float)EditorSettings::get_singleton()->get_project_metadata("fps_studio_model_view", "cloud_speed", 0.01), 0.0f, 0.1f);
	preview_sun_cycle_duration_seconds = cycle_duration_seconds;

	preview_time_of_day_slider->set_value_no_signal(preview_time_of_day);
	preview_sun_cycle_duration_slider->set_value_no_signal(cycle_duration_seconds);
	preview_sun_azimuth_slider->set_value_no_signal(sun_azimuth_degrees);
	preview_sun_path_tilt_slider->set_value_no_signal(sun_path_tilt);
	preview_sun_energy_slider->set_value_no_signal(sun_energy);
	preview_sun_animation_toggle->set_pressed_no_signal(preview_sun_animation_enabled);
	preview_clouds_toggle->set_pressed_no_signal(clouds_enabled);
	preview_cloud_coverage_slider->set_value_no_signal(cloud_coverage);
	preview_cloud_scale_slider->set_value_no_signal(cloud_scale);
	preview_cloud_speed_slider->set_value_no_signal(cloud_speed);

	_update_preview_time_of_day_label(preview_time_of_day);
	_update_preview_sun_cycle_duration_label(cycle_duration_seconds);
	_update_preview_sun_azimuth_label(sun_azimuth_degrees);
	_update_preview_sun_path_tilt_label(sun_path_tilt);
	_update_preview_sun_energy_label(sun_energy);
	_update_preview_cloud_coverage_label(cloud_coverage);
	_update_preview_cloud_scale_label(cloud_scale);
	_update_preview_cloud_speed_label(cloud_speed);
	_update_model_preview_environment();

	if (preview_sun_animation_timer != nullptr) {
		preview_sun_animation_timer->set_wait_time(0.1);
		if (preview_sun_animation_enabled && is_visible_in_tree()) {
			preview_sun_animation_timer->start();
		} else {
			preview_sun_animation_timer->stop();
		}
	}
}

void SceneImportSettingsScreen::_update_model_preview_environment() {
	if (!light1 || !environment.is_valid() || preview_sky_material.is_null()) {
		return;
	}
	const float time_of_day = preview_time_of_day;
	const float azimuth_degrees = preview_sun_azimuth_slider != nullptr ? preview_sun_azimuth_slider->get_value() : 0.0f;
	const float path_tilt_degrees = preview_sun_path_tilt_slider != nullptr ? preview_sun_path_tilt_slider->get_value() : 35.0f;
	const float sun_max_energy = preview_sun_energy_slider != nullptr ? preview_sun_energy_slider->get_value() : 1.2f;
	const float cloud_coverage = preview_cloud_coverage_slider != nullptr ? preview_cloud_coverage_slider->get_value() : 0.5f;
	const float cloud_scale = preview_cloud_scale_slider != nullptr ? preview_cloud_scale_slider->get_value() : 1.5f;
	const float cloud_speed = preview_cloud_speed_slider != nullptr ? preview_cloud_speed_slider->get_value() : 0.01f;
	const bool clouds_enabled = preview_clouds_toggle != nullptr && preview_clouds_toggle->is_pressed();

	const float theta = ((time_of_day - 6.0f) / 24.0f) * Math::TAU;
	const float path_tilt = Math::deg_to_rad(path_tilt_degrees);
	Vector3 sun_direction(Math::cos(theta), Math::sin(theta) * Math::cos(path_tilt), Math::sin(theta) * Math::sin(path_tilt));
	sun_direction = sun_direction.rotated(Vector3::UP, Math::deg_to_rad(azimuth_degrees)).normalized();
	const Vector3 light_target = -sun_direction;
	const Vector3 light_up = Math::abs(sun_direction.y) < 0.98f ? Vector3::UP : Vector3::FORWARD;
	light1->look_at_from_position(Vector3(), light_target, light_up);

	const float daylight_amount = CLAMP((sun_direction.y + 0.02f) / 0.14f, 0.0f, 1.0f);
	const float daylight = daylight_amount * daylight_amount * (3.0f - 2.0f * daylight_amount);
	const float dawn = (1.0f - CLAMP(Math::abs(time_of_day - 6.5f) / 1.8f, 0.0f, 1.0f));
	const float dusk = (1.0f - CLAMP(Math::abs(time_of_day - 18.5f) / 1.8f, 0.0f, 1.0f));
	const float twilight = MAX(dawn, dusk);
	const Color night_top(0.025, 0.045, 0.075);
	const Color day_top(0.30, 0.42, 0.58);
	const Color night_horizon(0.07, 0.095, 0.14);
	const Color day_horizon(0.43, 0.51, 0.61);
	const Color twilight_horizon(0.72, 0.43, 0.34);
	const Color night_ground(0.06, 0.075, 0.10);
	const Color day_ground(0.43, 0.51, 0.61);
	const Color sky_top = night_top.lerp(day_top, daylight);
	const Color sky_horizon = night_horizon.lerp(day_horizon, daylight).lerp(twilight_horizon, twilight * 0.55f);
	const Color ground_color = night_ground.lerp(day_ground, daylight);
	const Color sun_color = Color(1.0, 0.68, 0.42).lerp(Color(1.0, 0.96, 0.88), daylight);
	light1->set_color(sun_color);
	light1->set_param(Light3D::PARAM_ENERGY, sun_max_energy * daylight);
	light1->set_visible(sun_direction.y > -0.06f);
	preview_sky_material->set_shader_parameter("sky_top_color", sky_top);
	preview_sky_material->set_shader_parameter("sky_horizon_color", sky_horizon);
	preview_sky_material->set_shader_parameter("ground_color", ground_color);
	preview_sky_material->set_shader_parameter("sun_color", sun_color);
	preview_sky_material->set_shader_parameter("clouds_enabled", clouds_enabled);
	preview_sky_material->set_shader_parameter("cloud_coverage", cloud_coverage);
	preview_sky_material->set_shader_parameter("cloud_scale", cloud_scale);
	preview_sky_material->set_shader_parameter("cloud_speed", cloud_speed);
	const float ambient_energy = Math::lerp(0.2f, 1.35f, daylight);
	environment->set_ambient_light_color(Color(0.72, 0.78, 0.9).lerp(Color(1.0, 1.0, 1.0), daylight));
	environment->set_ambient_light_energy(ambient_energy);
	environment->set_fog_light_color(sky_horizon);
	if (preview_floor_material.is_valid()) {
		preview_floor_material->set_shader_parameter("horizon_color", sky_horizon);
	}
}

void SceneImportSettingsScreen::_preview_time_of_day_changed(double p_value) {
	preview_time_of_day = Math::fposmod((float)p_value, 24.0f);
	preview_time_of_day_slider->set_value_no_signal(preview_time_of_day);
	_update_preview_time_of_day_label(preview_time_of_day);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "day_night_time", preview_time_of_day);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_sun_animation_step() {
	if (!preview_sun_animation_enabled || preview_sun_animation_timer == nullptr) {
		return;
	}
	preview_time_of_day = Math::fposmod(preview_time_of_day + 24.0f * (float)preview_sun_animation_timer->get_wait_time() / preview_sun_cycle_duration_seconds, 24.0f);
	preview_time_of_day_slider->set_value_no_signal(preview_time_of_day);
	_update_preview_time_of_day_label(preview_time_of_day);
	_update_model_preview_environment();
	preview_sun_animation_metadata_elapsed += (float)preview_sun_animation_timer->get_wait_time();
	if (preview_sun_animation_metadata_elapsed >= 1.0f) {
		preview_sun_animation_metadata_elapsed = 0.0f;
		EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "day_night_time", preview_time_of_day);
	}
}

void SceneImportSettingsScreen::_preview_sun_animation_toggled(bool p_enabled) {
	preview_sun_animation_enabled = p_enabled;
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_animation_enabled", p_enabled);
	if (preview_sun_animation_timer != nullptr) {
		if (p_enabled && is_visible_in_tree()) {
			preview_sun_animation_timer->start();
		} else {
			preview_sun_animation_timer->stop();
		}
	}
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "day_night_time", preview_time_of_day);
}

void SceneImportSettingsScreen::_preview_sun_cycle_duration_changed(double p_value) {
	preview_sun_cycle_duration_seconds = CLAMP((float)p_value, 10.0f, 600.0f);
	preview_sun_cycle_duration_slider->set_value_no_signal(preview_sun_cycle_duration_seconds);
	_update_preview_sun_cycle_duration_label(preview_sun_cycle_duration_seconds);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_cycle_duration_seconds", preview_sun_cycle_duration_seconds);
}

void SceneImportSettingsScreen::_preview_sun_azimuth_changed(double p_value) {
	const float azimuth_degrees = CLAMP((float)p_value, -180.0f, 180.0f);
	preview_sun_azimuth_slider->set_value_no_signal(azimuth_degrees);
	_update_preview_sun_azimuth_label(azimuth_degrees);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_rotation_degrees", azimuth_degrees);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_sun_path_tilt_changed(double p_value) {
	const float path_tilt_degrees = CLAMP((float)p_value, 0.0f, 80.0f);
	preview_sun_path_tilt_slider->set_value_no_signal(path_tilt_degrees);
	_update_preview_sun_path_tilt_label(path_tilt_degrees);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_path_tilt_degrees", path_tilt_degrees);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_sun_energy_changed(double p_value) {
	const float sun_energy = CLAMP((float)p_value, 0.0f, 5.0f);
	preview_sun_energy_slider->set_value_no_signal(sun_energy);
	_update_preview_sun_energy_label(sun_energy);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "sun_energy", sun_energy);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_clouds_toggled(bool p_enabled) {
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "clouds_enabled", p_enabled);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_cloud_coverage_changed(double p_value) {
	const float coverage = CLAMP((float)p_value, 0.0f, 1.0f);
	preview_cloud_coverage_slider->set_value_no_signal(coverage);
	_update_preview_cloud_coverage_label(coverage);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "cloud_coverage", coverage);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_cloud_scale_changed(double p_value) {
	const float scale = CLAMP((float)p_value, 0.2f, 5.0f);
	preview_cloud_scale_slider->set_value_no_signal(scale);
	_update_preview_cloud_scale_label(scale);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "cloud_scale", scale);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_preview_cloud_speed_changed(double p_value) {
	const float speed = CLAMP((float)p_value, 0.0f, 0.1f);
	preview_cloud_speed_slider->set_value_no_signal(speed);
	_update_preview_cloud_speed_label(speed);
	EditorSettings::get_singleton()->set_project_metadata("fps_studio_model_view", "cloud_speed", speed);
	_update_model_preview_environment();
}

void SceneImportSettingsScreen::_update_preview_time_of_day_label(double p_value) {
	if (!preview_time_of_day_label) {
		return;
	}
	const float time_of_day = Math::fposmod((float)p_value, 24.0f);
	const int hour = (int)time_of_day;
	int minute = Math::round((time_of_day - hour) * 60.0f);
	int display_hour = hour;
	if (minute >= 60) {
		minute = 0;
		display_hour = (display_hour + 1) % 24;
	}
	preview_time_of_day_label->set_text(vformat(TTR("Time of Day: %02d:%02d"), display_hour, minute));
}

void SceneImportSettingsScreen::_update_preview_sun_cycle_duration_label(double p_value) {
	if (!preview_sun_cycle_duration_label) {
		return;
	}
	preview_sun_cycle_duration_label->set_text(vformat(TTR("Cycle Duration: %d s"), (int64_t)Math::round(p_value)));
}

void SceneImportSettingsScreen::_update_preview_sun_azimuth_label(double p_value) {
	if (preview_sun_azimuth_label) {
		preview_sun_azimuth_label->set_text(vformat(TTR("Sun Azimuth: %d deg"), (int64_t)Math::round(p_value)));
	}
}

void SceneImportSettingsScreen::_update_preview_sun_path_tilt_label(double p_value) {
	if (preview_sun_path_tilt_label) {
		preview_sun_path_tilt_label->set_text(vformat(TTR("Sun Path Tilt: %d deg"), (int64_t)Math::round(p_value)));
	}
}

void SceneImportSettingsScreen::_update_preview_sun_energy_label(double p_value) {
	if (preview_sun_energy_label) {
		preview_sun_energy_label->set_text(vformat(TTR("Sun Energy: %s"), String::num(p_value, 2)));
	}
}

void SceneImportSettingsScreen::_update_preview_cloud_coverage_label(double p_value) {
	if (preview_cloud_coverage_label) {
		preview_cloud_coverage_label->set_text(vformat(TTR("Cloud Coverage: %d percent"), (int64_t)Math::round(p_value * 100.0)));
	}
}

void SceneImportSettingsScreen::_update_preview_cloud_scale_label(double p_value) {
	if (preview_cloud_scale_label) {
		preview_cloud_scale_label->set_text(vformat(TTR("Cloud Scale: %s"), String::num(p_value, 2)));
	}
}

void SceneImportSettingsScreen::_update_preview_cloud_speed_label(double p_value) {
	if (preview_cloud_speed_label) {
		preview_cloud_speed_label->set_text(vformat(TTR("Cloud Speed: %s"), String::num(p_value, 3)));
	}
}
