/**************************************************************************/
/*  post_process_volume_3d.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           Godot FPS Studio                             */
/*                         https://godotengine.org                        */
/**************************************************************************/

#include "post_process_volume_3d.h"

#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"

namespace {

static bool _environment_property_is_overridden(int p_overrides, const String &p_name) {
	return ((p_overrides & PostProcessVolume3D::OVERRIDE_BACKGROUND) && (p_name.begins_with("background_") || p_name == "sky" || p_name.begins_with("sky_"))) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_AMBIENT_LIGHT) && p_name.begins_with("ambient_light_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_REFLECTED_LIGHT) && p_name.begins_with("reflected_light_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_TONEMAP) && p_name.begins_with("tonemap_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_SSR) && p_name.begins_with("ssr_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_SSAO) && p_name.begins_with("ssao_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_SSIL) && p_name.begins_with("ssil_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_SDFGI) && p_name.begins_with("sdfgi_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_GLOW) && p_name.begins_with("glow_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_FOG) && p_name.begins_with("fog_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_VOLUMETRIC_FOG) && p_name.begins_with("volumetric_fog_")) ||
			((p_overrides & PostProcessVolume3D::OVERRIDE_ADJUSTMENTS) && p_name.begins_with("adjustment_"));
}

static Variant _blend_environment_value(const Variant &p_from, const Variant &p_to, float p_weight, bool p_is_discrete) {
	if (p_weight <= 0.0) {
		return p_from;
	}
	if (p_weight >= 1.0) {
		return p_to;
	}

	switch (p_to.get_type()) {
		case Variant::FLOAT:
			return Math::lerp(real_t(p_from), real_t(p_to), real_t(p_weight));
		case Variant::INT:
			if (p_is_discrete) {
				return p_weight < 0.5f ? p_from : p_to;
			}
			return int64_t(Math::round(Math::lerp(real_t(int64_t(p_from)), real_t(int64_t(p_to)), real_t(p_weight))));
		case Variant::COLOR:
			return Color(p_from).lerp(Color(p_to), p_weight);
		case Variant::VECTOR2:
			return Vector2(p_from).lerp(Vector2(p_to), p_weight);
		case Variant::VECTOR3:
			return Vector3(p_from).lerp(Vector3(p_to), p_weight);
		case Variant::VECTOR4:
			return Vector4(p_from).lerp(Vector4(p_to), p_weight);
		case Variant::VECTOR2I: {
			Vector2i from = p_from;
			Vector2i to = p_to;
			return Vector2i(Math::round(Math::lerp(real_t(from.x), real_t(to.x), real_t(p_weight))), Math::round(Math::lerp(real_t(from.y), real_t(to.y), real_t(p_weight))));
		}
		case Variant::VECTOR3I: {
			Vector3i from = p_from;
			Vector3i to = p_to;
			return Vector3i(Math::round(Math::lerp(real_t(from.x), real_t(to.x), real_t(p_weight))), Math::round(Math::lerp(real_t(from.y), real_t(to.y), real_t(p_weight))), Math::round(Math::lerp(real_t(from.z), real_t(to.z), real_t(p_weight))));
		}
		case Variant::VECTOR4I: {
			Vector4i from = p_from;
			Vector4i to = p_to;
			return Vector4i(Math::round(Math::lerp(real_t(from.x), real_t(to.x), real_t(p_weight))), Math::round(Math::lerp(real_t(from.y), real_t(to.y), real_t(p_weight))), Math::round(Math::lerp(real_t(from.z), real_t(to.z), real_t(p_weight))), Math::round(Math::lerp(real_t(from.w), real_t(to.w), real_t(p_weight))));
		}
		default:
			return p_weight < 0.5f ? p_from : p_to;
	}
}

static void _copy_environment(const Ref<Environment> &p_from, const Ref<Environment> &p_to) {
	if (p_from.is_null() || p_to.is_null()) {
		return;
	}

	List<PropertyInfo> properties;
	p_from->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!(property.usage & PROPERTY_USAGE_STORAGE) || property.name.begins_with("resource_") || property.name.begins_with("metadata/")) {
			continue;
		}
		p_to->set(property.name, p_from->get(property.name));
	}
}

static void _blend_environment(const Ref<Environment> &p_from, const Ref<Environment> &p_to, const Ref<Environment> &p_output, int p_overrides, float p_weight) {
	if (p_from.is_null() || p_to.is_null() || p_output.is_null() || p_overrides == 0 || p_weight <= 0.0f) {
		return;
	}

	List<PropertyInfo> properties;
	p_to->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		const String property_name = property.name;
		if (!(property.usage & PROPERTY_USAGE_STORAGE) || !_environment_property_is_overridden(p_overrides, property_name)) {
			continue;
		}

		Variant from_value = p_output->get(property.name);
		Variant to_value = p_to->get(property.name);
		if (from_value.get_type() != to_value.get_type()) {
			if (p_weight >= 0.5f) {
				p_output->set(property.name, to_value);
			}
			continue;
		}

		const bool is_discrete = property.hint == PROPERTY_HINT_ENUM || property.hint == PROPERTY_HINT_FLAGS;
		p_output->set(property.name, _blend_environment_value(from_value, to_value, p_weight, is_discrete));
	}
}

static void _append_observed_environment(Vector<Ref<Environment>> &r_environments, const Ref<Environment> &p_environment) {
	if (p_environment.is_null()) {
		return;
	}
	for (int i = 0; i < r_environments.size(); i++) {
		if (r_environments[i] == p_environment) {
			return;
		}
	}
	r_environments.push_back(p_environment);
}

static Ref<Environment> _create_volume_environment() {
	Ref<Environment> new_environment;
	new_environment.instantiate();
	new_environment->set_local_to_scene(true);
	return new_environment;
}

} // namespace

PostProcessVolume3D::PostProcessVolume3D() {
	environment = _create_volume_environment();
	set_notify_transform(true);
}

PostProcessVolume3D::~PostProcessVolume3D() {
	_restore_camera();
}

StringName PostProcessVolume3D::_get_volume_group() const {
	if (!is_inside_tree() || !get_viewport() || get_viewport()->find_world_3d().is_null()) {
		return StringName();
	}
	return StringName("_post_process_volume_" + itos(get_viewport()->find_world_3d()->get_scenario().get_id()) + "_" + itos(get_viewport()->get_instance_id()));
}

void PostProcessVolume3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			if (!Engine::get_singleton()->is_editor_hint()) {
			active_volume_group = _get_volume_group();
			if (active_volume_group != StringName()) {
				add_to_group(active_volume_group);
					set_process(true);
				}
			}
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				_restore_camera();
			if (active_volume_group != StringName()) {
				remove_from_group(active_volume_group);
				}
			active_volume_group = StringName();
				set_process(false);
			}
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			environment_dirty = true;
		} break;
		case NOTIFICATION_PROCESS: {
			_update_camera_environment();
		} break;
	}
}

float PostProcessVolume3D::_get_weight_at_position(const Vector3 &p_world_position) const {
	if (!enabled || environment.is_null() || environment_overrides == 0 || blend_weight <= 0.0f) {
		return 0.0f;
	}

	const Vector3 local_position = to_local(p_world_position);
	const Vector3 half_size = box_size * 0.5f;
	const Vector3 outside(
		MAX(Math::abs(local_position.x) - half_size.x, 0.0f),
		MAX(Math::abs(local_position.y) - half_size.y, 0.0f),
		MAX(Math::abs(local_position.z) - half_size.z, 0.0f));
	const float distance_to_box = get_global_transform().basis.xform(outside).length();

	float weight = 0.0f;
	if (distance_to_box <= 0.0f) {
		weight = 1.0f;
	} else if (blend_radius > 0.0f) {
		weight = CLAMP(1.0f - distance_to_box / blend_radius, 0.0f, 1.0f);
		weight = weight * weight * (3.0f - 2.0f * weight);
	}
	return weight * blend_weight;
}

void PostProcessVolume3D::_refresh_environment_observers(const Vector<WeightedVolume> &p_volumes, const Ref<Environment> &p_base_environment) {
	Vector<Ref<Environment>> wanted_environments;
	_append_observed_environment(wanted_environments, p_base_environment);
	for (int i = 0; i < p_volumes.size(); i++) {
		_append_observed_environment(wanted_environments, p_volumes[i].volume->get_environment());
	}

	const Callable changed_callable = callable_mp(this, &PostProcessVolume3D::_on_environment_changed);
	for (int i = 0; i < wanted_environments.size(); i++) {
		if (!wanted_environments[i]->is_connected(SNAME("changed"), changed_callable)) {
			wanted_environments[i]->connect(SNAME("changed"), changed_callable);
		}
	}
	for (int i = 0; i < observed_environments.size(); i++) {
		bool still_used = false;
		for (int j = 0; j < wanted_environments.size(); j++) {
			if (observed_environments[i] == wanted_environments[j]) {
				still_used = true;
				break;
			}
		}
		if (!still_used && observed_environments[i].is_valid() && observed_environments[i]->is_connected(SNAME("changed"), changed_callable)) {
			observed_environments[i]->disconnect(SNAME("changed"), changed_callable);
		}
	}
	observed_environments = wanted_environments;
}

void PostProcessVolume3D::_on_environment_changed() {
	environment_dirty = true;
}

void PostProcessVolume3D::_update_camera_environment() {
	if (active_volume_group == StringName() || get_tree()->get_first_node_in_group(active_volume_group) != this) {
		return;
	}

	Camera3D *camera = get_viewport()->get_camera_3d();
	if (camera == nullptr) {
		_restore_camera();
		return;
	}

	if (controlled_camera_id != camera->get_instance_id()) {
		_restore_camera();
		controlled_camera_id = camera->get_instance_id();
		original_camera_environment = camera->get_environment();
		base_environment.unref();
		output_environment.unref();
		last_blend_signature = String();
		environment_dirty = true;
	}

	if (output_environment.is_valid() && camera->get_environment() != output_environment) {
		original_camera_environment = camera->get_environment();
		environment_dirty = true;
	}

	Ref<Environment> current_base_environment = original_camera_environment;
	if (current_base_environment.is_null()) {
		current_base_environment = get_viewport()->find_world_3d()->get_environment();
	}
	if (base_environment != current_base_environment) {
		base_environment = current_base_environment;
		environment_dirty = true;
	}

	Vector<Node *> volume_nodes = get_tree()->get_nodes_in_group(active_volume_group);
	Vector<WeightedVolume> weighted_volumes;
	String signature;
	signature += itos(camera->get_instance_id()) + ":";
	if (base_environment.is_valid()) {
		signature += itos(base_environment->get_instance_id());
	}
	signature += ":";

	for (int i = 0; i < volume_nodes.size(); i++) {
		PostProcessVolume3D *volume = Object::cast_to<PostProcessVolume3D>(volume_nodes[i]);
		if (volume == nullptr) {
			continue;
		}
		const float weight = volume->_get_weight_at_position(camera->get_global_position());
		if (weight <= 0.0f) {
			continue;
		}
		WeightedVolume weighted;
		weighted.volume = volume;
		weighted.weight = weight;
		weighted_volumes.push_back(weighted);
	}

	for (int i = 0; i < weighted_volumes.size(); i++) {
		for (int j = i + 1; j < weighted_volumes.size(); j++) {
			if (weighted_volumes[j].volume->get_priority() < weighted_volumes[i].volume->get_priority()) {
				SWAP(weighted_volumes.write[i], weighted_volumes.write[j]);
			}
		}
		PostProcessVolume3D *volume = weighted_volumes[i].volume;
		signature += ":" + itos(volume->get_instance_id()) + "," + itos(volume->get_environment_overrides()) + "," + itos(volume->get_priority()) + "," + String::num(weighted_volumes[i].weight, 4);
		if (volume->get_environment().is_valid()) {
			signature += "," + itos(volume->get_environment()->get_instance_id());
		}
	}

	_refresh_environment_observers(weighted_volumes, base_environment);
	if (weighted_volumes.is_empty()) {
		_restore_camera();
		return;
	}

	if (!environment_dirty && signature == last_blend_signature && output_environment.is_valid()) {
		return;
	}

	if (output_environment.is_null()) {
		output_environment.instantiate();
	}
	if (base_environment.is_valid()) {
		_copy_environment(base_environment, output_environment);
	}

	for (int i = 0; i < weighted_volumes.size(); i++) {
		const WeightedVolume &weighted = weighted_volumes[i];
		_blend_environment(output_environment, weighted.volume->get_environment(), output_environment, weighted.volume->get_environment_overrides(), weighted.weight);
	}

	camera->set_environment(output_environment);
	last_blend_signature = signature;
	environment_dirty = false;
}

void PostProcessVolume3D::_restore_camera() {
	if (controlled_camera_id.is_valid()) {
		Camera3D *camera = Object::cast_to<Camera3D>(ObjectDB::get_instance(controlled_camera_id));
		if (camera != nullptr && (output_environment.is_null() || camera->get_environment() == output_environment)) {
			camera->set_environment(original_camera_environment);
		}
	}

	const Callable changed_callable = callable_mp(this, &PostProcessVolume3D::_on_environment_changed);
	for (int i = 0; i < observed_environments.size(); i++) {
		if (observed_environments[i].is_valid() && observed_environments[i]->is_connected(SNAME("changed"), changed_callable)) {
			observed_environments[i]->disconnect(SNAME("changed"), changed_callable);
		}
	}
	observed_environments.clear();
	controlled_camera_id = ObjectID();
	original_camera_environment.unref();
	base_environment.unref();
	output_environment.unref();
	last_blend_signature = String();
	environment_dirty = true;
}

void PostProcessVolume3D::set_box_size(const Vector3 &p_size) {
	const Vector3 new_size(MAX(p_size.x, 0.01f), MAX(p_size.y, 0.01f), MAX(p_size.z, 0.01f));
	if (box_size == new_size) {
		return;
	}
	box_size = new_size;
	update_gizmos();
}

Vector3 PostProcessVolume3D::get_box_size() const {
	return box_size;
}

void PostProcessVolume3D::set_environment(const Ref<Environment> &p_environment) {
	const Ref<Environment> new_environment = p_environment.is_valid() ? p_environment : _create_volume_environment();
	if (environment == new_environment) {
		return;
	}
	environment = new_environment;
	environment_dirty = true;
	update_configuration_warnings();
}

Ref<Environment> PostProcessVolume3D::get_environment() const {
	return environment;
}

void PostProcessVolume3D::set_environment_overrides(int p_overrides) {
	const int new_overrides = p_overrides & OVERRIDE_ALL;
	if (environment_overrides == new_overrides) {
		return;
	}
	environment_overrides = new_overrides;
	environment_dirty = true;
	update_configuration_warnings();
}

int PostProcessVolume3D::get_environment_overrides() const {
	return environment_overrides;
}

void PostProcessVolume3D::set_blend_radius(float p_radius) {
	blend_radius = MAX(p_radius, 0.0f);
	update_gizmos();
}

float PostProcessVolume3D::get_blend_radius() const {
	return blend_radius;
}

void PostProcessVolume3D::set_blend_weight(float p_weight) {
	blend_weight = CLAMP(p_weight, 0.0f, 1.0f);
	environment_dirty = true;
}

float PostProcessVolume3D::get_blend_weight() const {
	return blend_weight;
}

void PostProcessVolume3D::set_priority(int p_priority) {
	priority = p_priority;
	environment_dirty = true;
}

int PostProcessVolume3D::get_priority() const {
	return priority;
}

void PostProcessVolume3D::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	environment_dirty = true;
}

bool PostProcessVolume3D::is_enabled() const {
	return enabled;
}

PackedStringArray PostProcessVolume3D::get_configuration_warnings() const {
	PackedStringArray warnings;
	if (enabled && environment_overrides == 0) {
		warnings.push_back(RTR("Choose at least one Environment Overrides category."));
	}
	return warnings;
}

void PostProcessVolume3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_box_size", "size"), &PostProcessVolume3D::set_box_size);
	ClassDB::bind_method(D_METHOD("get_box_size"), &PostProcessVolume3D::get_box_size);
	ClassDB::bind_method(D_METHOD("set_environment", "environment"), &PostProcessVolume3D::set_environment);
	ClassDB::bind_method(D_METHOD("get_environment"), &PostProcessVolume3D::get_environment);
	ClassDB::bind_method(D_METHOD("set_environment_overrides", "overrides"), &PostProcessVolume3D::set_environment_overrides);
	ClassDB::bind_method(D_METHOD("get_environment_overrides"), &PostProcessVolume3D::get_environment_overrides);
	ClassDB::bind_method(D_METHOD("set_blend_radius", "radius"), &PostProcessVolume3D::set_blend_radius);
	ClassDB::bind_method(D_METHOD("get_blend_radius"), &PostProcessVolume3D::get_blend_radius);
	ClassDB::bind_method(D_METHOD("set_blend_weight", "weight"), &PostProcessVolume3D::set_blend_weight);
	ClassDB::bind_method(D_METHOD("get_blend_weight"), &PostProcessVolume3D::get_blend_weight);
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &PostProcessVolume3D::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &PostProcessVolume3D::get_priority);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &PostProcessVolume3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &PostProcessVolume3D::is_enabled);
	ClassDB::bind_method(D_METHOD("get_configuration_warnings"), &PostProcessVolume3D::get_configuration_warnings);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "box_size"), "set_box_size", "get_box_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "environment_overrides", PROPERTY_HINT_FLAGS, "Background,Ambient Light,Reflected Light,Tonemap,SSR,SSAO,SSIL,SDFGI,Glow,Fog,Volumetric Fog,Adjustments"), "set_environment_overrides", "get_environment_overrides");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "environment", PROPERTY_HINT_RESOURCE_TYPE, "Environment"), "set_environment", "get_environment");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_radius", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:m"), "set_blend_radius", "get_blend_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_weight", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_blend_weight", "get_blend_weight");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "-1000,1000,1,or_less,or_greater"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");

	BIND_ENUM_CONSTANT(OVERRIDE_BACKGROUND);
	BIND_ENUM_CONSTANT(OVERRIDE_AMBIENT_LIGHT);
	BIND_ENUM_CONSTANT(OVERRIDE_REFLECTED_LIGHT);
	BIND_ENUM_CONSTANT(OVERRIDE_TONEMAP);
	BIND_ENUM_CONSTANT(OVERRIDE_SSR);
	BIND_ENUM_CONSTANT(OVERRIDE_SSAO);
	BIND_ENUM_CONSTANT(OVERRIDE_SSIL);
	BIND_ENUM_CONSTANT(OVERRIDE_SDFGI);
	BIND_ENUM_CONSTANT(OVERRIDE_GLOW);
	BIND_ENUM_CONSTANT(OVERRIDE_FOG);
	BIND_ENUM_CONSTANT(OVERRIDE_VOLUMETRIC_FOG);
	BIND_ENUM_CONSTANT(OVERRIDE_ADJUSTMENTS);
	BIND_ENUM_CONSTANT(OVERRIDE_ALL);
}
