/**************************************************************************/
/*  particle_presets.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_presets.h"

#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/camera_shake_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/radial_damage_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/gradient.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"

namespace FPSParticlePresets {

namespace {

struct GradientStop {
	float offset;
	Color color;
};

Ref<GradientTexture1D> make_ramp(std::initializer_list<GradientStop> p_stops) {
	Ref<Gradient> gradient;
	gradient.instantiate();
	Vector<float> offsets;
	Vector<Color> colors;
	for (const GradientStop &stop : p_stops) {
		offsets.push_back(stop.offset);
		colors.push_back(stop.color);
	}
	gradient->set_offsets(offsets);
	gradient->set_colors(colors);
	Ref<GradientTexture1D> texture;
	texture.instantiate();
	texture->set_gradient(gradient);
	return texture;
}

Ref<CurveTexture> make_curve(std::initializer_list<Vector2> p_points, float p_max = 1.0f) {
	Ref<Curve> curve;
	curve.instantiate();
	curve->set_max_value(p_max);
	for (const Vector2 &point : p_points) {
		curve->add_point(point);
	}
	Ref<CurveTexture> texture;
	texture.instantiate();
	texture->set_curve(curve);
	return texture;
}

enum Blend {
	BLEND_ALPHA,
	BLEND_ADD,
};

struct EmitterBuilder {
	GPUParticles3D *node = nullptr;
	Ref<ParticleProcessMaterial> process;
	Ref<StandardMaterial3D> material;

	EmitterBuilder &set(const StringName &p_property, const Variant &p_value) {
		process->set(p_property, p_value);
		return *this;
	}
};

EmitterBuilder add_emitter(ParticleEffect3D *p_effect, const String &p_name, int p_amount, double p_lifetime, bool p_one_shot, double p_explosiveness, real_t p_quad_size, Blend p_blend, bool p_align_to_velocity = false) {
	EmitterBuilder builder;
	GPUParticles3D *emitter = memnew(GPUParticles3D);
	emitter->set_name(p_name);
	emitter->set_amount(p_amount);
	emitter->set_lifetime(p_lifetime);
	emitter->set_one_shot(p_one_shot);
	emitter->set_explosiveness_ratio(p_explosiveness);
	emitter->set_visibility_aabb(AABB(Vector3(-8, -8, -8), Vector3(16, 16, 16)));
	// Additive particles look the same in any order; only alpha blended
	// particles pay for depth sorting.
	emitter->set_draw_order(p_blend == BLEND_ADD ? GPUParticles3D::DRAW_ORDER_INDEX : GPUParticles3D::DRAW_ORDER_VIEW_DEPTH);
	if (p_align_to_velocity) {
		emitter->set_transform_align(GPUParticles3D::TRANSFORM_ALIGN_Z_BILLBOARD_Y_TO_VELOCITY);
	}

	Ref<ParticleProcessMaterial> process;
	process.instantiate();
	process->set_gravity(Vector3());
	emitter->set_process_material(process);

	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	material->set_blend_mode(p_blend == BLEND_ADD ? BaseMaterial3D::BLEND_MODE_ADD : BaseMaterial3D::BLEND_MODE_MIX);
	material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, p_blend == BLEND_ADD);
	// Velocity-aligned streaks are oriented by the emitter, everything else
	// faces the camera and can spin.
	material->set_billboard_mode(p_align_to_velocity ? BaseMaterial3D::BILLBOARD_DISABLED : BaseMaterial3D::BILLBOARD_PARTICLES);
	material->set_proximity_fade_enabled(p_blend == BLEND_ALPHA);
	material->set_proximity_fade_distance(0.35);

	Ref<QuadMesh> quad;
	quad.instantiate();
	quad->set_size(Vector2(p_quad_size, p_quad_size));
	quad->set_material(material);
	emitter->set_draw_pass_mesh(0, quad);

	p_effect->add_child(emitter, true);
	emitter->set_owner(p_effect);
	builder.node = emitter;
	builder.process = process;
	builder.material = material;
	return builder;
}

TextureRequest make_texture(Node *p_emitter, int p_type, const String &p_file_name, int p_resolution, int p_columns = 1, double p_duration = 1.0, bool p_lifecycle = false) {
	TextureRequest request;
	request.emitter = p_emitter;
	request.settings.type = p_type;
	request.settings.seed = 7;
	request.settings.time = 0.35f;
	VFXTextureGenerator::apply_type_defaults(request.settings);
	request.file_name = p_file_name;
	request.resolution = p_resolution;
	request.columns = p_columns;
	request.sheet_duration = p_duration;
	request.lifecycle = p_lifecycle;
	return request;
}

void set_param(TextureRequest &r_request, const char *p_key, float p_value) {
	const int index = VFXTextureGenerator::find_param(VFXTextureGenerator::get_type_info(r_request.settings.type), p_key);
	if (index >= 0) {
		r_request.settings.params[index] = p_value;
	}
}

void set_colors(TextureRequest &r_request, const Color &p_a, const Color &p_b) {
	r_request.settings.color_a[0] = p_a.r;
	r_request.settings.color_a[1] = p_a.g;
	r_request.settings.color_a[2] = p_a.b;
	r_request.settings.color_b[0] = p_b.r;
	r_request.settings.color_b[1] = p_b.g;
	r_request.settings.color_b[2] = p_b.b;
}

const Vector2 RANDOM_ANGLE(0.0, 360.0);

// Timed light event. p_duration 0 keeps the light on (with optional flicker).
void add_light(ParticleEffect3D *p_effect, const String &p_name, const Color &p_color, real_t p_energy, real_t p_range, double p_delay, double p_duration, real_t p_flicker, const Vector3 &p_position) {
	OmniLight3D *light = memnew(OmniLight3D);
	light->set_name(p_name);
	light->set_color(p_color);
	light->set_param(Light3D::PARAM_ENERGY, p_energy);
	light->set_param(Light3D::PARAM_RANGE, p_range);
	light->set_position(p_position);
	ParticleEffect3D::set_emitter_delay(light, p_delay);
	ParticleEffect3D::set_emitter_duration(light, p_duration);
	if (p_flicker > 0.0) {
		light->set_meta(ParticleEffect3D::META_FLICKER, p_flicker);
	}
	p_effect->add_child(light, true);
	light->set_owner(p_effect);
}

void add_camera_shake(ParticleEffect3D *p_effect, real_t p_strength, double p_duration, real_t p_inner_radius, real_t p_radius, double p_delay) {
	CameraShake3D *shake = memnew(CameraShake3D);
	shake->set_name("CameraShake");
	shake->set_strength(p_strength);
	shake->set_duration(p_duration);
	shake->set_inner_radius(p_inner_radius);
	shake->set_radius(p_radius);
	ParticleEffect3D::set_emitter_delay(shake, p_delay);
	p_effect->add_child(shake, true);
	shake->set_owner(p_effect);
}

// Collision against level walls and floors (ParticleEffect3D bakes the
// physics collision automatically in the game).
void enable_collision(EmitterBuilder &r_builder, real_t p_bounce, real_t p_friction) {
	r_builder.process->set_collision_mode(ParticleProcessMaterial::COLLISION_RIGID);
	r_builder.process->set_collision_bounce(p_bounce);
	r_builder.process->set_collision_friction(p_friction);
	r_builder.node->set_collision_base_size(ParticleEffect3D::get_suggested_collision_size(r_builder.node));
}

AudioStreamPlayer3D *add_sound_slot(ParticleEffect3D *p_effect, real_t p_unit_size, real_t p_max_distance, double p_delay = 0.0) {
	// Empty sound track: pick or drop a sound in the editor.
	AudioStreamPlayer3D *sound = memnew(AudioStreamPlayer3D);
	sound->set_name("Sound");
	sound->set_unit_size(p_unit_size);
	sound->set_max_distance(p_max_distance);
	sound->set_meta(ParticleEffect3D::META_PITCH_RANDOM, 0.08);
	ParticleEffect3D::set_emitter_delay(sound, p_delay);
	p_effect->add_child(sound, true);
	sound->set_owner(p_effect);
	return sound;
}

// Decal event projecting along its local -Y (surface normal of impact presets
// is +Y). The texture comes from the generator's Decals category.
Decal *add_decal(ParticleEffect3D *p_effect, const String &p_name, const Vector3 &p_size, double p_duration, double p_fade, const Transform3D &p_transform = Transform3D()) {
	Decal *decal = memnew(Decal);
	decal->set_name(p_name);
	decal->set_size(p_size);
	decal->set_transform(p_transform);
	decal->set_normal_fade(0.3); // Avoid stretching on surfaces at a steep angle.
	ParticleEffect3D::set_emitter_duration(decal, p_duration);
	decal->set_meta(ParticleEffect3D::META_FADE, p_fade);
	p_effect->add_child(decal, true);
	decal->set_owner(p_effect);
	return decal;
}

TextureRequest make_bullet_hole_texture(Node *p_decal, const String &p_file_name, int p_surface, const Color &p_surface_color) {
	TextureRequest request = make_texture(p_decal, VFXTextureGenerator::TYPE_BULLET_HOLE, p_file_name, 256);
	set_param(request, "surface", float(p_surface));
	set_colors(request, Color(0.04, 0.035, 0.03), p_surface_color);
	if (p_surface == 1) {
		set_param(request, "cracks", 0.0f);
		set_param(request, "soot", 0.35f);
	} else if (p_surface == 2) {
		set_param(request, "cracks", 5.0f);
		set_param(request, "chip_size", 0.32f);
	}
	return request;
}

// Soft particle smoke shared by every smoke, dust, steam and mist emitter:
// a neutral light texture tinted by the particle color. 16 variants in a 4x4
// sheet; each particle shows a random one (anim_offset), so a few large soft
// particles merge into one cloud instead of many separate balls.
TextureRequest make_soft_smoke_texture(Node *p_emitter) {
	TextureRequest request = make_texture(p_emitter, VFXTextureGenerator::TYPE_SOFT_SMOKE, "smoke_soft", 512, 4, 40.0, false);
	set_colors(request, Color(0.5, 0.5, 0.52), Color(1.0, 0.99, 0.97));
	request.random_frame = true;
	return request;
}

void build_muzzle_flash(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Muzzle points along -Z (Godot forward).
	EmitterBuilder flash = add_emitter(p_effect, "Flash", 2, 0.07, true, 1.0, 0.7, BLEND_ADD);
	flash.set("angle", RANDOM_ANGLE).set("scale", Vector2(0.8, 1.2));
	flash.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.6), Vector2(0.25, 1.0), Vector2(1, 0.3) }));
	flash.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest flash_texture = make_texture(flash.node, VFXTextureGenerator::TYPE_FLASH, "muzzle_flash_core", 256);
	set_param(flash_texture, "rays", 5);
	set_param(flash_texture, "streak", 0.2f);
	r_textures.push_back(flash_texture);

	EmitterBuilder flames = add_emitter(p_effect, "Flames", 6, 0.08, true, 1.0, 0.35, BLEND_ADD, true);
	flames.set("direction", Vector3(0, 0, -1)).set("spread", 14.0).set("initial_velocity", Vector2(4.0, 8.0)).set("scale", Vector2(0.8, 1.4));
	flames.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.9, 0.6, 1.0) }, { 1.0f, Color(1.0, 0.4, 0.1, 0.0) } }));
	TextureRequest flame_texture = make_texture(flames.node, VFXTextureGenerator::TYPE_FIRE, "muzzle_flash_flame", 256);
	set_param(flame_texture, "width", 0.35f);
	r_textures.push_back(flame_texture);

	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 8, 0.25, true, 0.9, 0.12, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, 0, -1)).set("spread", 28.0).set("initial_velocity", Vector2(6.0, 12.0)).set("gravity", Vector3(0, -6, 0)).set("scale", Vector2(0.5, 1.0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.95, 0.7, 1.0) }, { 0.6f, Color(1.0, 0.55, 0.15, 1.0) }, { 1.0f, Color(0.8, 0.2, 0.05, 0.0) } }));
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 4, 0.9, true, 0.8, 0.7, BLEND_ALPHA);
	ParticleEffect3D::set_emitter_delay(smoke.node, 0.03);
	smoke.set("direction", Vector3(0, 0, -1)).set("spread", 20.0).set("initial_velocity", Vector2(0.4, 1.0)).set("damping", Vector2(1.5, 2.0)).set("gravity", Vector3(0, 0.3, 0)).set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-30, 30));
	smoke.set("color", Color(0.7, 0.7, 0.7, 0.45));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	smoke.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.15, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke_texture(smoke.node));

	add_light(p_effect, "LightFlash", Color(1.0, 0.78, 0.45), 5.0, 5.0, 0.0, 0.07, 0.0, Vector3(0, 0, -0.2));
}

void build_explosion(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder flash = add_emitter(p_effect, "Flash", 1, 0.15, true, 1.0, 5.0, BLEND_ADD);
	flash.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest flash_texture = make_texture(flash.node, VFXTextureGenerator::TYPE_FLASH, "explosion_flash", 256);
	set_param(flash_texture, "rays", 0);
	set_param(flash_texture, "core", 0.25f);
	r_textures.push_back(flash_texture);

	EmitterBuilder fireball = add_emitter(p_effect, "Fireball", 7, 1.4, true, 1.0, 2.2, BLEND_ALPHA);
	fireball.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.5);
	fireball.set("spread", 180.0).set("initial_velocity", Vector2(0.4, 1.4)).set("damping", Vector2(1.0, 2.0)).set("gravity", Vector3(0, 0.6, 0));
	fireball.set("angle", RANDOM_ANGLE).set("scale", Vector2(0.9, 1.5)).set("anim_speed", Vector2(1.0, 1.0));
	fireball.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.55), Vector2(0.3, 1.0), Vector2(1, 1.25) }, 1.5f));
	fireball.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(0.75, 1), Vector2(1, 0) }));
	TextureRequest fireball_texture = make_texture(fireball.node, VFXTextureGenerator::TYPE_EXPLOSION, "explosion_fireball", 1024, 8, 1.4, true);
	set_param(fireball_texture, "lifecycle", 1.0f);
	set_param(fireball_texture, "radius", 0.7f);
	r_textures.push_back(fireball_texture);

	EmitterBuilder debris = add_emitter(p_effect, "Sparks", 48, 1.3, true, 0.95, 0.18, BLEND_ADD, true);
	debris.set("spread", 180.0).set("initial_velocity", Vector2(7.0, 15.0)).set("gravity", Vector3(0, -9.8, 0)).set("damping", Vector2(0.5, 1.5)).set("scale", Vector2(0.5, 1.2));
	debris.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.92, 0.6, 1.0) }, { 0.5f, Color(1.0, 0.5, 0.12, 1.0) }, { 1.0f, Color(0.6, 0.12, 0.02, 0.0) } }));
	enable_collision(debris, 0.25, 0.4);
	r_textures.push_back(make_texture(debris.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	EmitterBuilder shockwave = add_emitter(p_effect, "Shockwave", 1, 0.45, true, 1.0, 1.0, BLEND_ADD);
	shockwave.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.1), Vector2(1, 1.0) }));
	shockwave.set("scale", Vector2(7.0, 7.0));
	shockwave.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest wave_texture = make_texture(shockwave.node, VFXTextureGenerator::TYPE_WAVE_RING, "explosion_shockwave", 256);
	set_param(wave_texture, "progress", 0.85f);
	set_param(wave_texture, "animate_progress", 0.0f);
	set_colors(wave_texture, Color(1.0, 0.6, 0.3), Color(1.0, 0.95, 0.85));
	r_textures.push_back(wave_texture);

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 12, 3.5, true, 0.85, 3.2, BLEND_ALPHA);
	ParticleEffect3D::set_emitter_delay(smoke.node, 0.35);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.8);
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 60.0).set("initial_velocity", Vector2(0.6, 1.6)).set("damping", Vector2(0.4, 0.8)).set("gravity", Vector3(0, 0.35, 0));
	smoke.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-12, 12)).set("scale", Vector2(1.0, 1.6));
	smoke.set("color", Color(0.32, 0.3, 0.29, 0.75));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	smoke.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.15, 1), Vector2(0.6, 0.8), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke_texture(smoke.node));

	add_light(p_effect, "LightFlash", Color(1.0, 0.58, 0.28), 14.0, 12.0, 0.0, 0.8, 0.25, Vector3(0, 0.6, 0));
	add_camera_shake(p_effect, 0.85, 0.75, 6.0, 35.0, 0.0);
	{
		Decal *scorch = add_decal(p_effect, "Scorch", Vector3(4.5, 3.0, 4.5), 20.0, 0.3);
		ParticleEffect3D::set_emitter_delay(scorch, 0.05);
		r_textures.push_back(make_texture(scorch, VFXTextureGenerator::TYPE_SCORCH, "decal_scorch", 512));
	}
	{
		add_sound_slot(p_effect, 12.0, 120.0);
		RadialDamage3D *damage = memnew(RadialDamage3D);
		damage->set_name("Damage");
		damage->set_damage(90.0);
		damage->set_inner_radius(2.0);
		damage->set_radius(7.0);
		damage->set_impulse(12.0);
		p_effect->add_child(damage, true);
		damage->set_owner(p_effect);
	}
}

void build_bullet_impact(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Surface normal points along +Y; rotate the effect to match the hit normal.
	EmitterBuilder dust = add_emitter(p_effect, "Dust", 5, 1.2, true, 0.9, 0.65, BLEND_ALPHA);
	dust.set("direction", Vector3(0, 1, 0)).set("spread", 35.0).set("initial_velocity", Vector2(1.0, 2.6)).set("damping", Vector2(2.5, 3.5)).set("gravity", Vector3(0, -0.4, 0));
	dust.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-40, 40)).set("color", Color(0.62, 0.55, 0.46, 0.7));
	dust.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.4), Vector2(1, 1.0) }));
	dust.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke_texture(dust.node));

	EmitterBuilder chips = add_emitter(p_effect, "Debris", 10, 0.8, true, 1.0, 0.035, BLEND_ALPHA);
	chips.set("direction", Vector3(0, 1, 0)).set("spread", 45.0).set("initial_velocity", Vector2(2.5, 5.0)).set("gravity", Vector3(0, -9.8, 0)).set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-400, 400)).set("scale", Vector2(0.5, 1.4));
	chips.set("color", Color(0.25, 0.22, 0.19, 1.0));
	chips.material->set_proximity_fade_enabled(false);
	enable_collision(chips, 0.3, 0.6);

	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 4, 0.2, true, 1.0, 0.08, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, 1, 0)).set("spread", 50.0).set("initial_velocity", Vector2(3.0, 7.0)).set("gravity", Vector3(0, -9.8, 0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.9, 0.6, 1.0) }, { 1.0f, Color(1.0, 0.4, 0.1, 0.0) } }));
	enable_collision(sparks, 0.35, 0.3);
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	Decal *hole = add_decal(p_effect, "BulletHole", Vector3(0.22, 0.3, 0.22), 12.0, 0.2);
	r_textures.push_back(make_bullet_hole_texture(hole, "decal_bullet_hole_concrete", 0, Color(0.62, 0.6, 0.56)));
}

void build_impact_sparks(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 32, 0.7, true, 0.95, 0.14, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, 1, 0)).set("spread", 55.0).set("initial_velocity", Vector2(4.0, 10.0)).set("gravity", Vector3(0, -9.8, 0)).set("damping", Vector2(0.5, 1.0)).set("scale", Vector2(0.4, 1.0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.97, 0.8, 1.0) }, { 0.4f, Color(1.0, 0.7, 0.25, 1.0) }, { 1.0f, Color(0.9, 0.25, 0.05, 0.0) } }));
	sparks.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 1), Vector2(1, 0.4) }));
	enable_collision(sparks, 0.4, 0.2);
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	EmitterBuilder glow = add_emitter(p_effect, "Glow", 1, 0.12, true, 1.0, 0.8, BLEND_ADD);
	glow.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest glow_texture = make_texture(glow.node, VFXTextureGenerator::TYPE_FLASH, "impact_glow", 128);
	set_param(glow_texture, "rays", 4);
	set_param(glow_texture, "streak", 0.0f);
	r_textures.push_back(glow_texture);

	add_light(p_effect, "LightFlash", Color(1.0, 0.8, 0.5), 2.5, 3.0, 0.0, 0.1, 0.0, Vector3(0, 0.15, 0));

	Decal *hole = add_decal(p_effect, "BulletHole", Vector3(0.16, 0.3, 0.16), 12.0, 0.2);
	r_textures.push_back(make_bullet_hole_texture(hole, "decal_bullet_hole_metal", 1, Color(0.7, 0.72, 0.75)));
}

void build_blood_hit(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder mist = add_emitter(p_effect, "Mist", 5, 0.6, true, 0.95, 0.4, BLEND_ALPHA);
	mist.set("direction", Vector3(0, 0, 1)).set("spread", 30.0).set("initial_velocity", Vector2(0.8, 2.0)).set("damping", Vector2(3.0, 4.0)).set("angle", RANDOM_ANGLE);
	mist.set("color", Color(0.45, 0.02, 0.02, 0.85));
	mist.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	mist.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke_texture(mist.node));

	EmitterBuilder drops = add_emitter(p_effect, "Droplets", 18, 0.7, true, 1.0, 0.05, BLEND_ALPHA, true);
	drops.set("direction", Vector3(0, 0.3, 1)).set("spread", 35.0).set("initial_velocity", Vector2(2.0, 5.0)).set("gravity", Vector3(0, -9.8, 0)).set("scale", Vector2(0.5, 1.3));
	drops.set("color", Color(0.4, 0.0, 0.0, 1.0));
	drops.material->set_proximity_fade_enabled(false);
	TextureRequest drop_texture = make_texture(drops.node, VFXTextureGenerator::TYPE_SPARK, "blood_droplet", 128);
	set_colors(drop_texture, Color(0.8, 0.8, 0.8), Color(1.0, 1.0, 1.0));
	set_param(drop_texture, "glow", 0.2f);
	set_param(drop_texture, "flicker", 0.0f);
	r_textures.push_back(drop_texture);

	// Splatter on the wall behind the target: projects along +Z up to 2 m.
	Decal *splatter = add_decal(p_effect, "Splatter", Vector3(0.9, 2.0, 0.9), 15.0, 0.25, Transform3D(Basis::from_euler(Vector3(-Math::PI * 0.5, 0, 0)), Vector3(0, 0, 1.0)));
	ParticleEffect3D::set_emitter_delay(splatter, 0.05);
	r_textures.push_back(make_texture(splatter, VFXTextureGenerator::TYPE_SPLATTER, "decal_blood_splatter", 256));
}

void build_smoke_plume(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 20, 4.5, false, 0.0, 2.3, BLEND_ALPHA);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.35);
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 14.0).set("initial_velocity", Vector2(0.6, 1.2)).set("damping", Vector2(0.1, 0.3)).set("gravity", Vector3(0, 0.25, 0));
	smoke.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-15, 15)).set("scale", Vector2(0.8, 1.3));
	smoke.set("turbulence_enabled", true).set("turbulence_noise_strength", 0.6).set("turbulence_noise_scale", 3.0);
	smoke.process->set_color_ramp(make_ramp({ { 0.0f, Color(0.36, 0.34, 0.33, 0.0) }, { 0.12f, Color(0.4, 0.38, 0.36, 0.7) }, { 1.0f, Color(0.6, 0.6, 0.62, 0.0) } }));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.4), Vector2(1, 1.0) }));
	smoke.node->set("preprocess", 4.0);
	r_textures.push_back(make_soft_smoke_texture(smoke.node));
}

void build_fire(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder flames = add_emitter(p_effect, "Flames", 18, 0.9, false, 0.0, 0.9, BLEND_ADD);
	flames.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.22);
	flames.set("direction", Vector3(0, 1, 0)).set("spread", 8.0).set("initial_velocity", Vector2(0.4, 0.9)).set("gravity", Vector3(0, 1.2, 0)).set("angle", Vector2(-8, 8)).set("anim_offset", Vector2(0.0, 1.0));
	flames.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.7), Vector2(0.3, 1.0), Vector2(1, 0.35) }));
	flames.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.15, 1), Vector2(0.7, 0.8), Vector2(1, 0) }));
	flames.node->set("preprocess", 1.0);
	TextureRequest flame_texture = make_texture(flames.node, VFXTextureGenerator::TYPE_FIRE, "fire_flames", 512, 4, 1.0, false);
	r_textures.push_back(flame_texture);

	EmitterBuilder embers = add_emitter(p_effect, "Embers", 14, 2.0, false, 0.0, 0.05, BLEND_ADD);
	embers.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.3);
	embers.set("direction", Vector3(0, 1, 0)).set("spread", 20.0).set("initial_velocity", Vector2(0.8, 1.8)).set("gravity", Vector3(0, 0.4, 0)).set("scale", Vector2(0.5, 1.2));
	embers.set("turbulence_enabled", true).set("turbulence_noise_strength", 1.2).set("turbulence_noise_scale", 2.0);
	embers.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.8, 0.4, 1.0) }, { 1.0f, Color(1.0, 0.3, 0.05, 0.0) } }));
	embers.node->set("preprocess", 2.0);
	embers.material->set_proximity_fade_enabled(false);

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 10, 3.0, false, 0.0, 1.4, BLEND_ALPHA);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.2);
	smoke.node->set_position(Vector3(0, 0.8, 0));
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 12.0).set("initial_velocity", Vector2(0.5, 0.9)).set("gravity", Vector3(0, 0.3, 0)).set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-15, 15));
	smoke.process->set_color_ramp(make_ramp({ { 0.0f, Color(0.15, 0.14, 0.13, 0.0) }, { 0.2f, Color(0.2, 0.19, 0.18, 0.5) }, { 1.0f, Color(0.35, 0.35, 0.36, 0.0) } }));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	smoke.node->set("preprocess", 3.0);
	r_textures.push_back(make_soft_smoke_texture(smoke.node));

	add_light(p_effect, "FireLight", Color(1.0, 0.55, 0.22), 2.5, 6.0, 0.0, 0.0, 0.45, Vector3(0, 0.45, 0));
}


const Color WHITE_TINT_A(0.72, 0.72, 0.72);
const Color WHITE_TINT_B(1.0, 1.0, 1.0);

void build_wood_impact(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder dust = add_emitter(p_effect, "Dust", 5, 1.0, true, 0.9, 0.4, BLEND_ALPHA);
	dust.set("direction", Vector3(0, 1, 0)).set("spread", 30.0).set("initial_velocity", Vector2(0.8, 2.0)).set("damping", Vector2(2.5, 3.5)).set("gravity", Vector3(0, -0.3, 0));
	dust.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-40, 40)).set("color", Color(0.55, 0.42, 0.28, 0.6));
	dust.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.4), Vector2(1, 1.0) }));
	dust.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke_texture(dust.node));

	EmitterBuilder splinters = add_emitter(p_effect, "Splinters", 12, 0.9, true, 1.0, 0.07, BLEND_ALPHA, true);
	splinters.set("direction", Vector3(0, 1, 0)).set("spread", 40.0).set("initial_velocity", Vector2(2.5, 6.0)).set("gravity", Vector3(0, -9.8, 0)).set("scale", Vector2(0.5, 1.3));
	splinters.set("color", Color(0.62, 0.45, 0.26, 1.0));
	splinters.material->set_proximity_fade_enabled(false);
	enable_collision(splinters, 0.25, 0.7);
	TextureRequest splinter_texture = make_texture(splinters.node, VFXTextureGenerator::TYPE_SPARK, "wood_splinter", 128);
	set_colors(splinter_texture, WHITE_TINT_A, WHITE_TINT_B);
	set_param(splinter_texture, "glow", 0.1f);
	set_param(splinter_texture, "flicker", 0.0f);
	r_textures.push_back(splinter_texture);

	Decal *hole = add_decal(p_effect, "BulletHole", Vector3(0.24, 0.3, 0.24), 12.0, 0.2);
	r_textures.push_back(make_bullet_hole_texture(hole, "decal_bullet_hole_wood", 2, Color(0.72, 0.55, 0.34)));
}

void build_water_splash(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Water surface normal is +Y.
	EmitterBuilder droplets = add_emitter(p_effect, "Droplets", 40, 0.9, true, 0.95, 0.07, BLEND_ALPHA, true);
	droplets.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.08);
	droplets.set("direction", Vector3(0, 1, 0)).set("spread", 18.0).set("initial_velocity", Vector2(2.5, 6.0)).set("gravity", Vector3(0, -9.8, 0)).set("scale", Vector2(0.5, 1.2));
	droplets.set("color", Color(0.85, 0.93, 1.0, 0.85));
	droplets.material->set_proximity_fade_enabled(false);
	TextureRequest drop_texture = make_texture(droplets.node, VFXTextureGenerator::TYPE_SPARK, "water_droplet", 128);
	set_colors(drop_texture, WHITE_TINT_A, WHITE_TINT_B);
	set_param(drop_texture, "glow", 0.3f);
	set_param(drop_texture, "flicker", 0.0f);
	r_textures.push_back(drop_texture);

	EmitterBuilder mist = add_emitter(p_effect, "Mist", 6, 1.2, true, 0.9, 0.7, BLEND_ALPHA);
	mist.set("direction", Vector3(0, 1, 0)).set("spread", 40.0).set("initial_velocity", Vector2(0.5, 1.5)).set("damping", Vector2(2.0, 3.0)).set("angle", RANDOM_ANGLE);
	mist.set("color", Color(0.9, 0.95, 1.0, 0.35));
	mist.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.4), Vector2(1, 1.0) }));
	mist.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke_texture(mist.node));

	// Flat ripple ring lying on the water.
	EmitterBuilder ripple = add_emitter(p_effect, "Ripple", 2, 1.0, true, 0.7, 1.0, BLEND_ALPHA);
	Ref<QuadMesh> flat = ripple.node->get_draw_pass_mesh(0);
	flat->set_orientation(PlaneMesh::FACE_Y);
	ripple.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_DISABLED);
	ripple.material->set_proximity_fade_enabled(false);
	ripple.node->set_position(Vector3(0, 0.02, 0));
	ripple.set("scale", Vector2(1.5, 2.2)).set("color", Color(0.9, 0.96, 1.0, 0.6));
	ripple.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.2), Vector2(1, 1.0) }));
	ripple.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest ripple_texture = make_texture(ripple.node, VFXTextureGenerator::TYPE_WAVE_RING, "water_ripple", 256);
	set_param(ripple_texture, "progress", 0.85f);
	set_param(ripple_texture, "animate_progress", 0.0f);
	set_colors(ripple_texture, WHITE_TINT_A, WHITE_TINT_B);
	r_textures.push_back(ripple_texture);

	add_sound_slot(p_effect, 4.0, 40.0);
}

void build_glass_shatter(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// The pane faces +Z; shards fly out along +Z and bounce on the floor.
	EmitterBuilder shards = add_emitter(p_effect, "Shards", 40, 2.5, true, 1.0, 0.09, BLEND_ALPHA);
	shards.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_BOX)).set("emission_box_extents", Vector3(0.5, 0.5, 0.02));
	shards.set("direction", Vector3(0, 0.2, 1)).set("spread", 45.0).set("initial_velocity", Vector2(1.0, 4.0)).set("gravity", Vector3(0, -9.8, 0));
	shards.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-720, 720)).set("scale", Vector2(0.4, 1.4)).set("color", Color(0.8, 0.92, 1.0, 0.7));
	shards.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(0.8, 1), Vector2(1, 0) }));
	shards.material->set_proximity_fade_enabled(false);
	enable_collision(shards, 0.35, 0.6);
	TextureRequest shard_texture = make_texture(shards.node, VFXTextureGenerator::TYPE_FLASH, "glass_shard", 128);
	set_param(shard_texture, "rays", 3);
	set_param(shard_texture, "streak", 0.0f);
	set_colors(shard_texture, WHITE_TINT_A, WHITE_TINT_B);
	r_textures.push_back(shard_texture);

	EmitterBuilder glints = add_emitter(p_effect, "Glints", 12, 0.5, true, 0.9, 0.12, BLEND_ADD);
	glints.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_BOX)).set("emission_box_extents", Vector3(0.5, 0.5, 0.02));
	glints.set("direction", Vector3(0, 0, 1)).set("spread", 50.0).set("initial_velocity", Vector2(1.0, 3.0)).set("gravity", Vector3(0, -4.0, 0));
	glints.set("color", Color(0.85, 0.95, 1.0, 1.0));
	glints.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest glint_texture = make_texture(glints.node, VFXTextureGenerator::TYPE_FLASH, "impact_glow", 128);
	set_param(glint_texture, "rays", 4);
	set_param(glint_texture, "streak", 0.0f);
	r_textures.push_back(glint_texture);

	add_sound_slot(p_effect, 5.0, 50.0);
}

void build_electric_sparks(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// A broken cable: repeating bursts of blue-white sparks with a flickering light.
	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 24, 0.7, false, 0.95, 0.1, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, -0.3, 0)).set("spread", 70.0).set("initial_velocity", Vector2(2.0, 6.0)).set("gravity", Vector3(0, -9.8, 0)).set("scale", Vector2(0.4, 1.0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(0.9, 0.95, 1.0, 1.0) }, { 0.4f, Color(0.5, 0.75, 1.0, 1.0) }, { 1.0f, Color(0.2, 0.35, 1.0, 0.0) } }));
	enable_collision(sparks, 0.3, 0.2);
	TextureRequest spark_texture = make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak_blue", 128);
	set_colors(spark_texture, Color(0.4, 0.6, 1.0), Color(0.9, 0.95, 1.0));
	r_textures.push_back(spark_texture);

	EmitterBuilder arc = add_emitter(p_effect, "Arc", 2, 0.12, false, 0.5, 0.5, BLEND_ADD);
	arc.set("angle", RANDOM_ANGLE).set("scale", Vector2(0.7, 1.2));
	arc.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest arc_texture = make_texture(arc.node, VFXTextureGenerator::TYPE_ELECTRIC_ARC, "electric_arc", 256);
	r_textures.push_back(arc_texture);

	add_light(p_effect, "ArcLight", Color(0.55, 0.75, 1.0), 2.0, 4.0, 0.0, 0.0, 0.9, Vector3(0, 0.05, 0));
	add_sound_slot(p_effect, 3.0, 25.0);
}

void build_steam_leak(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Jet along +Z from a broken pipe.
	EmitterBuilder steam = add_emitter(p_effect, "Steam", 40, 1.6, false, 0.0, 0.6, BLEND_ALPHA);
	steam.set("direction", Vector3(0, 0, 1)).set("spread", 6.0).set("initial_velocity", Vector2(4.0, 6.0)).set("damping", Vector2(3.0, 4.0)).set("gravity", Vector3(0, 0.8, 0));
	steam.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-30, 30)).set("color", Color(0.95, 0.97, 1.0, 0.35));
	steam.set("turbulence_enabled", true).set("turbulence_noise_strength", 0.5).set("turbulence_noise_scale", 2.5);
	steam.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.15), Vector2(1, 1.5) }, 1.5f));
	steam.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.1, 1), Vector2(1, 0) }));
	steam.node->set("preprocess", 2.0);
	r_textures.push_back(make_soft_smoke_texture(steam.node));
	add_sound_slot(p_effect, 3.0, 25.0);
}

void build_smoke_grenade(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Thick smoke screen for ~8 s that fills the room and stays off walls.
	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 32, 7.0, false, 0.0, 3.8, BLEND_ALPHA);
	ParticleEffect3D::set_emitter_duration(smoke.node, 8.0);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.2);
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 80.0).set("initial_velocity", Vector2(1.0, 2.5)).set("damping", Vector2(0.5, 0.9)).set("gravity", Vector3(0, 0.05, 0));
	smoke.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-8, 8)).set("scale", Vector2(0.9, 1.3)).set("color", Color(0.72, 0.72, 0.7, 0.85));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.3), Vector2(0.4, 1.0), Vector2(1, 1.3) }, 1.3f));
	smoke.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.1, 1), Vector2(0.75, 0.9), Vector2(1, 0) }));
	enable_collision(smoke, 0.0, 1.0);
	r_textures.push_back(make_soft_smoke_texture(smoke.node));
	add_sound_slot(p_effect, 3.0, 30.0);
}

void build_signal_flare(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder core = add_emitter(p_effect, "Core", 6, 0.15, false, 0.0, 0.6, BLEND_ADD);
	core.set("angle", RANDOM_ANGLE).set("scale", Vector2(0.7, 1.2)).set("color", Color(1.0, 0.25, 0.15, 1.0));
	core.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest core_texture = make_texture(core.node, VFXTextureGenerator::TYPE_FLASH, "flare_core", 128);
	set_param(core_texture, "rays", 6);
	set_param(core_texture, "streak", 0.3f);
	set_colors(core_texture, WHITE_TINT_A, WHITE_TINT_B);
	r_textures.push_back(core_texture);

	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 20, 0.8, false, 0.0, 0.08, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, 1, 0)).set("spread", 60.0).set("initial_velocity", Vector2(1.0, 3.0)).set("gravity", Vector3(0, -6.0, 0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.85, 0.7, 1.0) }, { 1.0f, Color(1.0, 0.15, 0.05, 0.0) } }));
	enable_collision(sparks, 0.3, 0.3);
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 16, 3.5, false, 0.0, 1.4, BLEND_ALPHA);
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 10.0).set("initial_velocity", Vector2(0.6, 1.0)).set("gravity", Vector3(0, 0.3, 0)).set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-15, 15));
	smoke.set("turbulence_enabled", true).set("turbulence_noise_strength", 0.4).set("turbulence_noise_scale", 3.0);
	smoke.process->set_color_ramp(make_ramp({ { 0.0f, Color(0.9, 0.3, 0.25, 0.0) }, { 0.15f, Color(0.85, 0.35, 0.3, 0.6) }, { 1.0f, Color(0.7, 0.55, 0.55, 0.0) } }));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.3), Vector2(1, 1.0) }));
	smoke.node->set("preprocess", 3.0);
	r_textures.push_back(make_soft_smoke_texture(smoke.node));

	add_light(p_effect, "FlareLight", Color(1.0, 0.2, 0.12), 3.0, 9.0, 0.0, 0.0, 0.35, Vector3(0, 0.1, 0));
	add_sound_slot(p_effect, 2.0, 20.0);
}

void build_dust_landing(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Flat ring of dust along the ground (landing, heavy footstep, body fall).
	EmitterBuilder dust = add_emitter(p_effect, "Dust", 14, 1.2, true, 0.95, 0.6, BLEND_ALPHA);
	dust.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.2);
	dust.set("direction", Vector3(1, 0, 0)).set("spread", 180.0).set("flatness", 1.0).set("initial_velocity", Vector2(1.5, 3.0)).set("damping", Vector2(3.5, 4.5)).set("gravity", Vector3(0, 0.15, 0));
	dust.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-30, 30)).set("color", Color(0.6, 0.55, 0.48, 0.5));
	dust.node->set_position(Vector3(0, 0.1, 0));
	dust.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.4), Vector2(1, 1.0) }));
	dust.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.1, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke_texture(dust.node));
}

void build_ambient_dust(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Slowly drifting motes in a 6 x 3 x 6 m volume, visible in light shafts.
	EmitterBuilder motes = add_emitter(p_effect, "Motes", 80, 8.0, false, 0.0, 0.025, BLEND_ADD);
	motes.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_BOX)).set("emission_box_extents", Vector3(3.0, 1.5, 3.0));
	motes.set("spread", 180.0).set("initial_velocity", Vector2(0.02, 0.08)).set("gravity", Vector3(0, -0.01, 0)).set("scale", Vector2(0.5, 1.2));
	motes.set("turbulence_enabled", true).set("turbulence_noise_strength", 0.15).set("turbulence_noise_scale", 4.0);
	motes.set("color", Color(1.0, 0.95, 0.85, 0.6));
	motes.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.2, 1), Vector2(0.8, 1), Vector2(1, 0) }));
	motes.material->set_proximity_fade_enabled(false);
	motes.node->set_position(Vector3(0, 1.5, 0));
	motes.node->set("preprocess", 8.0);
	TextureRequest mote_texture = make_texture(motes.node, VFXTextureGenerator::TYPE_FLASH, "dust_mote", 64);
	set_param(mote_texture, "rays", 0);
	set_param(mote_texture, "streak", 0.0f);
	set_colors(mote_texture, WHITE_TINT_A, WHITE_TINT_B);
	r_textures.push_back(mote_texture);
}

void build_bullet_tracer(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// One fast streak along -Z (weapon forward); spawn it at the muzzle.
	EmitterBuilder tracer = add_emitter(p_effect, "Tracer", 1, 0.15, true, 1.0, 0.05, BLEND_ADD, true);
	Ref<QuadMesh> streak = tracer.node->get_draw_pass_mesh(0);
	streak->set_size(Vector2(0.05, 2.0));
	tracer.set("direction", Vector3(0, 0, -1)).set("spread", 0.0).set("initial_velocity", Vector2(250.0, 250.0));
	tracer.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.95, 0.7, 1.0) }, { 1.0f, Color(1.0, 0.6, 0.2, 0.6) } }));
	tracer.node->set_visibility_aabb(AABB(Vector3(-1, -1, -40), Vector3(2, 2, 41)));
	r_textures.push_back(make_texture(tracer.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));
}

// Visibility bounds from the motion instead of a fixed 16 m cube: tighter
// culling, and a smaller (finer) level collision bake.
void fit_visibility_aabb(GPUParticles3D *p_emitter) {
	const AABB default_aabb(Vector3(-8, -8, -8), Vector3(16, 16, 16));
	if (p_emitter->get_visibility_aabb() != default_aabb) {
		return; // Set explicitly by the preset.
	}
	const Ref<ParticleProcessMaterial> process = p_emitter->get_process_material();
	if (process.is_null()) {
		return;
	}
	const real_t life = p_emitter->get_lifetime();
	const real_t speed = process->get_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY);
	const real_t damping = process->get_param_min(ParticleProcessMaterial::PARAM_DAMPING);
	real_t travel = speed * life;
	if (damping > 0.0) {
		travel = MIN(travel, speed * speed / (2.0 * damping));
	}
	const real_t fall = 0.5 * process->get_gravity().length() * life * life;
	real_t emission = 0.0;
	if (process->get_emission_shape() == ParticleProcessMaterial::EMISSION_SHAPE_SPHERE || process->get_emission_shape() == ParticleProcessMaterial::EMISSION_SHAPE_SPHERE_SURFACE) {
		emission = process->get_emission_sphere_radius();
	} else if (process->get_emission_shape() == ParticleProcessMaterial::EMISSION_SHAPE_BOX) {
		emission = process->get_emission_box_extents().length();
	}
	real_t size = 0.5;
	if (const QuadMesh *quad = Object::cast_to<QuadMesh>(p_emitter->get_draw_pass_mesh(0).ptr())) {
		size = MAX(quad->get_size().x, quad->get_size().y) * process->get_param_max(ParticleProcessMaterial::PARAM_SCALE) * 1.5;
	}
	const real_t turbulence = process->get_turbulence_enabled() ? process->get_turbulence_noise_strength() * life : 0.0;
	// Spread in every direction, falling (or rising) only along gravity.
	// Capped: farther particles are small and short-lived, and a huge box
	// would make the level collision bake coarse.
	const real_t extent = CLAMP(emission + MIN(travel, real_t(10.0)) + size + turbulence + 0.25, real_t(0.5), real_t(12.0));
	AABB aabb(Vector3(-extent, -extent, -extent), Vector3(extent, extent, extent) * 2.0);
	const Vector3 gravity = process->get_gravity();
	if (gravity.length() > 0.001) {
		aabb.expand_to(gravity.normalized() * (extent + MIN(fall, real_t(10.0))));
	}
	p_emitter->set_visibility_aabb(aabb);
}

} // namespace

String get_preset_name(int p_preset) {
	switch (p_preset) {
		case PRESET_MUZZLE_FLASH:
			return "Muzzle Flash";
		case PRESET_EXPLOSION:
			return "Explosion";
		case PRESET_BULLET_IMPACT:
			return "Bullet Impact (Dust)";
		case PRESET_IMPACT_SPARKS:
			return "Impact Sparks (Metal)";
		case PRESET_BLOOD_HIT:
			return "Blood Hit";
		case PRESET_SMOKE_PLUME:
			return "Smoke Plume";
		case PRESET_FIRE:
			return "Fire";
		case PRESET_WOOD_IMPACT:
			return "Bullet Impact (Wood)";
		case PRESET_WATER_SPLASH:
			return "Water Splash";
		case PRESET_GLASS_SHATTER:
			return "Glass Shatter";
		case PRESET_ELECTRIC_SPARKS:
			return "Electric Sparks";
		case PRESET_STEAM_LEAK:
			return "Steam Leak";
		case PRESET_SMOKE_GRENADE:
			return "Smoke Grenade";
		case PRESET_SIGNAL_FLARE:
			return "Signal Flare";
		case PRESET_DUST_LANDING:
			return "Dust Puff (Landing)";
		case PRESET_AMBIENT_DUST:
			return "Ambient Dust";
		case PRESET_BULLET_TRACER:
			return "Bullet Tracer";
	}
	return String();
}

String get_preset_category(int p_preset) {
	switch (p_preset) {
		case PRESET_MUZZLE_FLASH:
		case PRESET_BULLET_TRACER:
			return "Weapons";
		case PRESET_BULLET_IMPACT:
		case PRESET_WOOD_IMPACT:
		case PRESET_IMPACT_SPARKS:
		case PRESET_BLOOD_HIT:
		case PRESET_GLASS_SHATTER:
		case PRESET_WATER_SPLASH:
			return "Impacts";
		case PRESET_EXPLOSION:
		case PRESET_FIRE:
		case PRESET_SMOKE_GRENADE:
		case PRESET_SIGNAL_FLARE:
			return "Explosions & Fire";
	}
	return "Environment";
}

Vector<int> get_presets_in_display_order() {
	Vector<int> order;
	const char *categories[] = { "Weapons", "Impacts", "Explosions & Fire", "Environment" };
	for (const char *category : categories) {
		for (int preset = 0; preset < PRESET_MAX; preset++) {
			if (get_preset_category(preset) == category) {
				order.push_back(preset);
			}
		}
	}
	return order;
}

String get_preset_description(int p_preset) {
	switch (p_preset) {
		case PRESET_MUZZLE_FLASH:
			return "One-shot flash, flame tongues, sparks and a smoke puff along -Z.";
		case PRESET_EXPLOSION:
			return "Flash, volumetric fireball sprite sheet, colliding sparks, shockwave, smoke, scorch decal, light, shake, sound and damage.";
		case PRESET_BULLET_IMPACT:
			return "Dust puff, debris chips and sparks that bounce off the level, and a concrete bullet hole decal along +Y (surface normal).";
		case PRESET_IMPACT_SPARKS:
			return "Bright metal sparks that bounce off the level, a short glow and a metal bullet hole decal along +Y.";
		case PRESET_BLOOD_HIT:
			return "Blood mist and droplets along +Z, with a splatter decal on the wall behind.";
		case PRESET_SMOKE_PLUME:
			return "Continuous rising smoke column with turbulence.";
		case PRESET_FIRE:
			return "Continuous animated flames, embers and smoke.";
		case PRESET_WOOD_IMPACT:
			return "Wood dust, splinters that bounce off the level and a wood bullet hole decal along +Y (surface normal).";
		case PRESET_WATER_SPLASH:
			return "Droplet column, mist and a flat ripple on the water (+Y), with a sound slot.";
		case PRESET_GLASS_SHATTER:
			return "Glass shards that tumble and bounce off the floor, glints and a sound slot; the pane faces +Z.";
		case PRESET_ELECTRIC_SPARKS:
			return "Repeating blue sparks that bounce, electric arc, flickering light and a sound slot.";
		case PRESET_STEAM_LEAK:
			return "Continuous steam jet along +Z with turbulence and a sound slot.";
		case PRESET_SMOKE_GRENADE:
			return "Thick smoke screen for 8 seconds that fills the room and stays off walls, with a sound slot.";
		case PRESET_SIGNAL_FLARE:
			return "Red burning flare: core, bouncing sparks, red smoke, flickering light and a sound slot.";
		case PRESET_DUST_LANDING:
			return "Flat ring of dust along the ground for landings and heavy steps.";
		case PRESET_AMBIENT_DUST:
			return "Slowly drifting dust motes in a 6 x 3 x 6 m volume.";
		case PRESET_BULLET_TRACER:
			return "One fast glowing streak along -Z; spawn it at the muzzle.";
	}
	return String();
}

Ref<GradientTexture1D> make_color_ramp(const Vector<float> &p_offsets, const Vector<Color> &p_colors) {
	Ref<Gradient> gradient;
	gradient.instantiate();
	gradient->set_offsets(p_offsets);
	gradient->set_colors(p_colors);
	Ref<GradientTexture1D> texture;
	texture.instantiate();
	texture->set_gradient(gradient);
	return texture;
}

Ref<CurveTexture> make_curve_texture(const Vector<Vector2> &p_points, float p_max) {
	Ref<Curve> curve;
	curve.instantiate();
	curve->set_max_value(p_max);
	for (const Vector2 &point : p_points) {
		curve->add_point(point);
	}
	Ref<CurveTexture> texture;
	texture.instantiate();
	texture->set_curve(curve);
	return texture;
}

ParticleEffect3D *create_preset(int p_preset, Vector<TextureRequest> &r_textures) {
	ERR_FAIL_INDEX_V(p_preset, PRESET_MAX, nullptr);
	ParticleEffect3D *effect = memnew(ParticleEffect3D);
	effect->set_name(get_preset_name(p_preset).replace("(", "").replace(")", "").to_pascal_case());
	switch (p_preset) {
		case PRESET_MUZZLE_FLASH:
			build_muzzle_flash(effect, r_textures);
			break;
		case PRESET_EXPLOSION:
			build_explosion(effect, r_textures);
			break;
		case PRESET_BULLET_IMPACT:
			build_bullet_impact(effect, r_textures);
			break;
		case PRESET_IMPACT_SPARKS:
			build_impact_sparks(effect, r_textures);
			break;
		case PRESET_BLOOD_HIT:
			build_blood_hit(effect, r_textures);
			break;
		case PRESET_SMOKE_PLUME:
			build_smoke_plume(effect, r_textures);
			break;
		case PRESET_FIRE:
			build_fire(effect, r_textures);
			break;
		case PRESET_WOOD_IMPACT:
			build_wood_impact(effect, r_textures);
			break;
		case PRESET_WATER_SPLASH:
			build_water_splash(effect, r_textures);
			break;
		case PRESET_GLASS_SHATTER:
			build_glass_shatter(effect, r_textures);
			break;
		case PRESET_ELECTRIC_SPARKS:
			build_electric_sparks(effect, r_textures);
			break;
		case PRESET_STEAM_LEAK:
			build_steam_leak(effect, r_textures);
			break;
		case PRESET_SMOKE_GRENADE:
			build_smoke_grenade(effect, r_textures);
			break;
		case PRESET_SIGNAL_FLARE:
			build_signal_flare(effect, r_textures);
			break;
		case PRESET_DUST_LANDING:
			build_dust_landing(effect, r_textures);
			break;
		case PRESET_AMBIENT_DUST:
			build_ambient_dust(effect, r_textures);
			break;
		case PRESET_BULLET_TRACER:
			build_bullet_tracer(effect, r_textures);
			break;
	}
	for (int i = 0; i < effect->get_child_count(); i++) {
		if (GPUParticles3D *emitter = Object::cast_to<GPUParticles3D>(effect->get_child(i))) {
			fit_visibility_aabb(emitter);
		}
	}
	return effect;
}

} // namespace FPSParticlePresets
