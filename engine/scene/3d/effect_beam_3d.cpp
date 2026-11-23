/**************************************************************************/
/*  effect_beam_3d.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "effect_beam_3d.h"

#include "core/object/class_db.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/shader.h"
#include "scene/resources/texture.h"

namespace {
Ref<Shader> beam_shaders[2]; // 0 additive, 1 alpha blended.

Ref<Shader> get_beam_shader(bool p_additive) {
	Ref<Shader> &shader = beam_shaders[p_additive ? 0 : 1];
	if (shader.is_null()) {
		shader.instantiate();
		String code = "shader_type spatial;\nrender_mode unshaded, ";
		code += p_additive ? "blend_add" : "blend_mix";
		code += R"(, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled, world_vertex_coords;
uniform vec3 beam_end = vec3(0.0, 0.0, -5.0);
uniform float beam_width = 0.08;
uniform float taper = 0.0;
uniform vec4 beam_color : source_color = vec4(1.0);
uniform float brightness = 1.0;
uniform float intensity = 1.0;
uniform sampler2D beam_texture : source_color, repeat_enable, filter_linear_mipmap, hint_default_white;
uniform bool has_texture = false;
uniform float tiling = 1.0;
uniform float scroll = 0.0;
uniform float noise_amplitude = 0.0;
uniform float noise_frequency = 3.0;
uniform float noise_speed = 12.0;
uniform float seed = 0.0;

float beam_hash(float n) {
	return fract(sin(n) * 43758.5453);
}

float beam_noise(float x) {
	float i = floor(x);
	float f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(beam_hash(i), beam_hash(i + 1.0), f) * 2.0 - 1.0;
}

void vertex() {
	// UV.x runs along the beam, UV.y across it.
	float u = UV.x;
	vec3 start_w = MODEL_MATRIX[3].xyz;
	vec3 end_w = (MODEL_MATRIX * vec4(beam_end, 1.0)).xyz;
	vec3 axis = end_w - start_w;
	float len = length(axis);
	axis = len > 0.0001 ? axis / len : vec3(0.0, 0.0, -1.0);
	vec3 p = mix(start_w, end_w, u);
	if (noise_amplitude > 0.0) {
		// Sideways wobble, pinned at both ends.
		vec3 helper = abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
		vec3 n1 = normalize(cross(axis, helper));
		vec3 n2 = cross(axis, n1);
		float t = TIME * noise_speed + seed;
		float x = u * len * noise_frequency;
		float a = beam_noise(x + t) + 0.5 * beam_noise(x * 2.3 - t * 1.7);
		float b = beam_noise(x + 31.7 - t) + 0.5 * beam_noise(x * 2.3 + 11.0 + t * 1.3);
		p += (n1 * a + n2 * b) * noise_amplitude * sin(u * PI);
	}
	// A strip that always shows its face to the camera.
	vec3 side = cross(axis, CAMERA_POSITION_WORLD - p);
	float side_length = length(side);
	side = side_length > 0.0001 ? side / side_length : vec3(1.0, 0.0, 0.0);
	p += side * (UV.y - 0.5) * beam_width * mix(1.0, 1.0 - u, taper);
	VERTEX = p;
	UV = vec2(u * len * tiling - TIME * scroll, UV.y);
}

void fragment() {
	vec4 tex = has_texture ? texture(beam_texture, UV) : vec4(1.0);
	// Without a texture: a bright core that falls off to the edges.
	float edge = has_texture ? 1.0 : pow(sin(clamp(UV.y, 0.0, 1.0) * PI), 2.0);
	ALBEDO = beam_color.rgb * tex.rgb * brightness;
	ALPHA = clamp(beam_color.a * tex.a * edge * intensity, 0.0, 1.0);
}
)";
		shader->set_code(code);
	}
	return shader;
}
} // namespace

void EffectBeam3D::finish() {
	beam_shaders[0].unref();
	beam_shaders[1].unref();
}

void EffectBeam3D::_build_mesh() {
	// Positions only give the mesh sensible bounds; the shader places the strip.
	PackedVector3Array vertices;
	PackedVector2Array uvs;
	PackedInt32Array indices;
	for (int i = 0; i <= segments; i++) {
		const real_t u = real_t(i) / real_t(segments);
		for (int side = 0; side < 2; side++) {
			vertices.push_back(end_position * u);
			uvs.push_back(Vector2(u, real_t(side)));
		}
		if (i < segments) {
			const int base = i * 2;
			indices.push_back(base);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 1);
			indices.push_back(base + 3);
			indices.push_back(base + 2);
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	mesh->clear_surfaces();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	mesh->surface_set_material(0, material);
	mesh->set_custom_aabb(get_aabb());
}

void EffectBeam3D::_update_material() {
	material->set_shader(get_beam_shader(additive));
	material->set_shader_parameter("beam_end", end_position);
	material->set_shader_parameter("beam_width", width);
	material->set_shader_parameter("taper", taper);
	material->set_shader_parameter("beam_color", color);
	material->set_shader_parameter("brightness", brightness);
	material->set_shader_parameter("intensity", intensity);
	material->set_shader_parameter("beam_texture", texture);
	material->set_shader_parameter("has_texture", texture.is_valid());
	material->set_shader_parameter("tiling", texture_tiling);
	material->set_shader_parameter("scroll", texture_scroll);
	material->set_shader_parameter("noise_amplitude", noise_amplitude);
	material->set_shader_parameter("noise_frequency", noise_frequency);
	material->set_shader_parameter("noise_speed", noise_speed);
	material->set_shader_parameter("seed", real_t(uint64_t(get_instance_id()) % 1000) * 0.37);
	mesh->set_custom_aabb(get_aabb());
	update_gizmos();
}

AABB EffectBeam3D::get_aabb() const {
	AABB box;
	box.expand_to(end_position);
	return box.grow(width * 0.5 + noise_amplitude * 1.6 + 0.01);
}

void EffectBeam3D::set_end_position(const Vector3 &p_position) {
	end_position = p_position;
	_update_material();
}

Vector3 EffectBeam3D::get_end_position() const {
	return end_position;
}

void EffectBeam3D::set_global_end_position(const Vector3 &p_position) {
	set_end_position(is_inside_tree() ? to_local(p_position) : p_position);
}

void EffectBeam3D::set_width(real_t p_width) {
	width = MAX(real_t(0.001), p_width);
	_update_material();
}

real_t EffectBeam3D::get_width() const {
	return width;
}

void EffectBeam3D::set_taper(real_t p_taper) {
	taper = CLAMP(p_taper, real_t(0.0), real_t(1.0));
	_update_material();
}

real_t EffectBeam3D::get_taper() const {
	return taper;
}

void EffectBeam3D::set_color(const Color &p_color) {
	color = p_color;
	_update_material();
}

Color EffectBeam3D::get_color() const {
	return color;
}

void EffectBeam3D::set_brightness(real_t p_brightness) {
	brightness = MAX(real_t(0.0), p_brightness);
	_update_material();
}

real_t EffectBeam3D::get_brightness() const {
	return brightness;
}

void EffectBeam3D::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	_update_material();
}

Ref<Texture2D> EffectBeam3D::get_texture() const {
	return texture;
}

void EffectBeam3D::set_texture_tiling(real_t p_tiling) {
	texture_tiling = p_tiling;
	_update_material();
}

real_t EffectBeam3D::get_texture_tiling() const {
	return texture_tiling;
}

void EffectBeam3D::set_texture_scroll(real_t p_scroll) {
	texture_scroll = p_scroll;
	_update_material();
}

real_t EffectBeam3D::get_texture_scroll() const {
	return texture_scroll;
}

void EffectBeam3D::set_noise_amplitude(real_t p_amplitude) {
	noise_amplitude = MAX(real_t(0.0), p_amplitude);
	_update_material();
}

real_t EffectBeam3D::get_noise_amplitude() const {
	return noise_amplitude;
}

void EffectBeam3D::set_noise_frequency(real_t p_frequency) {
	noise_frequency = MAX(real_t(0.0), p_frequency);
	_update_material();
}

real_t EffectBeam3D::get_noise_frequency() const {
	return noise_frequency;
}

void EffectBeam3D::set_noise_speed(real_t p_speed) {
	noise_speed = p_speed;
	_update_material();
}

real_t EffectBeam3D::get_noise_speed() const {
	return noise_speed;
}

void EffectBeam3D::set_segments(int p_segments) {
	segments = CLAMP(p_segments, 1, 256);
	_build_mesh();
}

int EffectBeam3D::get_segments() const {
	return segments;
}

void EffectBeam3D::set_additive(bool p_additive) {
	additive = p_additive;
	_update_material();
}

bool EffectBeam3D::is_additive() const {
	return additive;
}

void EffectBeam3D::set_intensity(real_t p_intensity) {
	const real_t value = CLAMP(p_intensity, real_t(0.0), real_t(1.0));
	if (value == intensity) {
		return;
	}
	intensity = value;
	material->set_shader_parameter("intensity", intensity);
}

real_t EffectBeam3D::get_intensity() const {
	return intensity;
}

void EffectBeam3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_end_position", "position"), &EffectBeam3D::set_end_position);
	ClassDB::bind_method(D_METHOD("get_end_position"), &EffectBeam3D::get_end_position);
	ClassDB::bind_method(D_METHOD("set_global_end_position", "position"), &EffectBeam3D::set_global_end_position);
	ClassDB::bind_method(D_METHOD("set_width", "width"), &EffectBeam3D::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &EffectBeam3D::get_width);
	ClassDB::bind_method(D_METHOD("set_taper", "taper"), &EffectBeam3D::set_taper);
	ClassDB::bind_method(D_METHOD("get_taper"), &EffectBeam3D::get_taper);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &EffectBeam3D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &EffectBeam3D::get_color);
	ClassDB::bind_method(D_METHOD("set_brightness", "brightness"), &EffectBeam3D::set_brightness);
	ClassDB::bind_method(D_METHOD("get_brightness"), &EffectBeam3D::get_brightness);
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &EffectBeam3D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &EffectBeam3D::get_texture);
	ClassDB::bind_method(D_METHOD("set_texture_tiling", "tiling"), &EffectBeam3D::set_texture_tiling);
	ClassDB::bind_method(D_METHOD("get_texture_tiling"), &EffectBeam3D::get_texture_tiling);
	ClassDB::bind_method(D_METHOD("set_texture_scroll", "scroll"), &EffectBeam3D::set_texture_scroll);
	ClassDB::bind_method(D_METHOD("get_texture_scroll"), &EffectBeam3D::get_texture_scroll);
	ClassDB::bind_method(D_METHOD("set_noise_amplitude", "amplitude"), &EffectBeam3D::set_noise_amplitude);
	ClassDB::bind_method(D_METHOD("get_noise_amplitude"), &EffectBeam3D::get_noise_amplitude);
	ClassDB::bind_method(D_METHOD("set_noise_frequency", "frequency"), &EffectBeam3D::set_noise_frequency);
	ClassDB::bind_method(D_METHOD("get_noise_frequency"), &EffectBeam3D::get_noise_frequency);
	ClassDB::bind_method(D_METHOD("set_noise_speed", "speed"), &EffectBeam3D::set_noise_speed);
	ClassDB::bind_method(D_METHOD("get_noise_speed"), &EffectBeam3D::get_noise_speed);
	ClassDB::bind_method(D_METHOD("set_segments", "segments"), &EffectBeam3D::set_segments);
	ClassDB::bind_method(D_METHOD("get_segments"), &EffectBeam3D::get_segments);
	ClassDB::bind_method(D_METHOD("set_additive", "additive"), &EffectBeam3D::set_additive);
	ClassDB::bind_method(D_METHOD("is_additive"), &EffectBeam3D::is_additive);
	ClassDB::bind_method(D_METHOD("set_intensity", "intensity"), &EffectBeam3D::set_intensity);
	ClassDB::bind_method(D_METHOD("get_intensity"), &EffectBeam3D::get_intensity);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "end_position", PROPERTY_HINT_NONE, "suffix:m"), "set_end_position", "get_end_position");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "width", PROPERTY_HINT_RANGE, "0.001,2,0.001,or_greater,suffix:m"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "taper", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_taper", "get_taper");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brightness", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_brightness", "get_brightness");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "additive"), "set_additive", "is_additive");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_tiling", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_texture_tiling", "get_texture_tiling");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "texture_scroll", PROPERTY_HINT_RANGE, "-20,20,0.01,or_less,or_greater"), "set_texture_scroll", "get_texture_scroll");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_amplitude", PROPERTY_HINT_RANGE, "0,2,0.001,or_greater,suffix:m"), "set_noise_amplitude", "get_noise_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_frequency", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"), "set_noise_frequency", "get_noise_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_speed", PROPERTY_HINT_RANGE, "0,60,0.1,or_greater"), "set_noise_speed", "get_noise_speed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "segments", PROPERTY_HINT_RANGE, "1,256,1"), "set_segments", "get_segments");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intensity", PROPERTY_HINT_RANGE, "0,1,0.01", PROPERTY_USAGE_NONE), "set_intensity", "get_intensity");
}

EffectBeam3D::EffectBeam3D() {
	mesh.instantiate();
	material.instantiate();
	_update_material();
	_build_mesh();
	set_base(mesh->get_rid());
	set_cast_shadows_setting(SHADOW_CASTING_SETTING_OFF);
}
