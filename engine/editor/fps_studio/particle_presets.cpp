/**************************************************************************/
/*  particle_presets.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_presets.h"

#include "scene/3d/camera_shake_3d.h"
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
	emitter->set_draw_order(GPUParticles3D::DRAW_ORDER_VIEW_DEPTH);
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

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 4, 0.9, true, 0.8, 0.5, BLEND_ALPHA);
	ParticleEffect3D::set_emitter_delay(smoke.node, 0.03);
	smoke.set("direction", Vector3(0, 0, -1)).set("spread", 20.0).set("initial_velocity", Vector2(0.4, 1.0)).set("damping", Vector2(1.5, 2.0)).set("gravity", Vector3(0, 0.3, 0)).set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-30, 30));
	smoke.set("color", Color(0.7, 0.7, 0.7, 0.45));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	smoke.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.15, 1), Vector2(1, 0) }));
	r_textures.push_back(make_texture(smoke.node, VFXTextureGenerator::TYPE_SMOKE, "smoke_puff", 256));

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

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 14, 3.5, true, 0.85, 2.4, BLEND_ALPHA);
	ParticleEffect3D::set_emitter_delay(smoke.node, 0.35);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.8);
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 60.0).set("initial_velocity", Vector2(0.6, 1.6)).set("damping", Vector2(0.4, 0.8)).set("gravity", Vector3(0, 0.35, 0));
	smoke.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-12, 12)).set("scale", Vector2(1.0, 1.6));
	smoke.set("color", Color(0.32, 0.3, 0.29, 0.75));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	smoke.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.15, 1), Vector2(0.6, 0.8), Vector2(1, 0) }));
	r_textures.push_back(make_texture(smoke.node, VFXTextureGenerator::TYPE_SMOKE, "smoke_puff", 256));

	add_light(p_effect, "LightFlash", Color(1.0, 0.58, 0.28), 14.0, 12.0, 0.0, 0.8, 0.25, Vector3(0, 0.6, 0));
	add_camera_shake(p_effect, 0.85, 0.75, 6.0, 35.0, 0.0);
}

void build_bullet_impact(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Surface normal points along +Y; rotate the effect to match the hit normal.
	EmitterBuilder dust = add_emitter(p_effect, "Dust", 6, 1.2, true, 0.9, 0.45, BLEND_ALPHA);
	dust.set("direction", Vector3(0, 1, 0)).set("spread", 35.0).set("initial_velocity", Vector2(1.0, 2.6)).set("damping", Vector2(2.5, 3.5)).set("gravity", Vector3(0, -0.4, 0));
	dust.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-40, 40)).set("color", Color(0.62, 0.55, 0.46, 0.7));
	dust.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.4), Vector2(1, 1.0) }));
	dust.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_texture(dust.node, VFXTextureGenerator::TYPE_SMOKE, "smoke_puff", 256));

	EmitterBuilder chips = add_emitter(p_effect, "Debris", 10, 0.8, true, 1.0, 0.035, BLEND_ALPHA);
	chips.set("direction", Vector3(0, 1, 0)).set("spread", 45.0).set("initial_velocity", Vector2(2.5, 5.0)).set("gravity", Vector3(0, -9.8, 0)).set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-400, 400)).set("scale", Vector2(0.5, 1.4));
	chips.set("color", Color(0.25, 0.22, 0.19, 1.0));
	chips.material->set_proximity_fade_enabled(false);

	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 4, 0.2, true, 1.0, 0.08, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, 1, 0)).set("spread", 50.0).set("initial_velocity", Vector2(3.0, 7.0)).set("gravity", Vector3(0, -9.8, 0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.9, 0.6, 1.0) }, { 1.0f, Color(1.0, 0.4, 0.1, 0.0) } }));
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));
}

void build_impact_sparks(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 32, 0.7, true, 0.95, 0.14, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, 1, 0)).set("spread", 55.0).set("initial_velocity", Vector2(4.0, 10.0)).set("gravity", Vector3(0, -9.8, 0)).set("damping", Vector2(0.5, 1.0)).set("scale", Vector2(0.4, 1.0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.97, 0.8, 1.0) }, { 0.4f, Color(1.0, 0.7, 0.25, 1.0) }, { 1.0f, Color(0.9, 0.25, 0.05, 0.0) } }));
	sparks.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 1), Vector2(1, 0.4) }));
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	EmitterBuilder glow = add_emitter(p_effect, "Glow", 1, 0.12, true, 1.0, 0.8, BLEND_ADD);
	glow.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest glow_texture = make_texture(glow.node, VFXTextureGenerator::TYPE_FLASH, "impact_glow", 128);
	set_param(glow_texture, "rays", 4);
	set_param(glow_texture, "streak", 0.0f);
	r_textures.push_back(glow_texture);

	add_light(p_effect, "LightFlash", Color(1.0, 0.8, 0.5), 2.5, 3.0, 0.0, 0.1, 0.0, Vector3(0, 0.15, 0));
}

void build_blood_hit(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder mist = add_emitter(p_effect, "Mist", 5, 0.6, true, 0.95, 0.4, BLEND_ALPHA);
	mist.set("direction", Vector3(0, 0, 1)).set("spread", 30.0).set("initial_velocity", Vector2(0.8, 2.0)).set("damping", Vector2(3.0, 4.0)).set("angle", RANDOM_ANGLE);
	mist.set("color", Color(0.45, 0.02, 0.02, 0.85));
	mist.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	mist.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest mist_texture = make_texture(mist.node, VFXTextureGenerator::TYPE_SMOKE, "blood_mist", 256);
	set_colors(mist_texture, Color(0.35, 0.35, 0.35), Color(1.0, 1.0, 1.0));
	r_textures.push_back(mist_texture);

	EmitterBuilder drops = add_emitter(p_effect, "Droplets", 18, 0.7, true, 1.0, 0.05, BLEND_ALPHA, true);
	drops.set("direction", Vector3(0, 0.3, 1)).set("spread", 35.0).set("initial_velocity", Vector2(2.0, 5.0)).set("gravity", Vector3(0, -9.8, 0)).set("scale", Vector2(0.5, 1.3));
	drops.set("color", Color(0.4, 0.0, 0.0, 1.0));
	drops.material->set_proximity_fade_enabled(false);
	TextureRequest drop_texture = make_texture(drops.node, VFXTextureGenerator::TYPE_SPARK, "blood_droplet", 128);
	set_colors(drop_texture, Color(0.8, 0.8, 0.8), Color(1.0, 1.0, 1.0));
	set_param(drop_texture, "glow", 0.2f);
	set_param(drop_texture, "flicker", 0.0f);
	r_textures.push_back(drop_texture);
}

void build_smoke_plume(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 28, 4.5, false, 0.0, 1.6, BLEND_ALPHA);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.35);
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 14.0).set("initial_velocity", Vector2(0.6, 1.2)).set("damping", Vector2(0.1, 0.3)).set("gravity", Vector3(0, 0.25, 0));
	smoke.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-15, 15)).set("scale", Vector2(0.8, 1.3));
	smoke.set("turbulence_enabled", true).set("turbulence_noise_strength", 0.6).set("turbulence_noise_scale", 3.0);
	smoke.process->set_color_ramp(make_ramp({ { 0.0f, Color(0.36, 0.34, 0.33, 0.0) }, { 0.12f, Color(0.4, 0.38, 0.36, 0.7) }, { 1.0f, Color(0.6, 0.6, 0.62, 0.0) } }));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.4), Vector2(1, 1.0) }));
	smoke.node->set("preprocess", 4.0);
	r_textures.push_back(make_texture(smoke.node, VFXTextureGenerator::TYPE_SMOKE, "smoke_puff", 256));
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

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 10, 3.0, false, 0.0, 1.0, BLEND_ALPHA);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.2);
	smoke.node->set_position(Vector3(0, 0.8, 0));
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 12.0).set("initial_velocity", Vector2(0.5, 0.9)).set("gravity", Vector3(0, 0.3, 0)).set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-15, 15));
	smoke.process->set_color_ramp(make_ramp({ { 0.0f, Color(0.15, 0.14, 0.13, 0.0) }, { 0.2f, Color(0.2, 0.19, 0.18, 0.5) }, { 1.0f, Color(0.35, 0.35, 0.36, 0.0) } }));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	smoke.node->set("preprocess", 3.0);
	r_textures.push_back(make_texture(smoke.node, VFXTextureGenerator::TYPE_SMOKE, "smoke_puff", 256));

	add_light(p_effect, "FireLight", Color(1.0, 0.55, 0.22), 2.5, 6.0, 0.0, 0.0, 0.45, Vector3(0, 0.45, 0));
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
	}
	return String();
}

String get_preset_description(int p_preset) {
	switch (p_preset) {
		case PRESET_MUZZLE_FLASH:
			return "One-shot flash, flame tongues, sparks and a smoke puff along -Z.";
		case PRESET_EXPLOSION:
			return "Flash, volumetric fireball sprite sheet, sparks, shockwave and lingering smoke.";
		case PRESET_BULLET_IMPACT:
			return "Dust puff, debris chips and a few sparks along +Y (surface normal).";
		case PRESET_IMPACT_SPARKS:
			return "Bright metal sparks with a short glow along +Y.";
		case PRESET_BLOOD_HIT:
			return "Blood mist and droplets along +Z.";
		case PRESET_SMOKE_PLUME:
			return "Continuous rising smoke column with turbulence.";
		case PRESET_FIRE:
			return "Continuous animated flames, embers and smoke.";
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
	}
	return effect;
}

} // namespace FPSParticlePresets
