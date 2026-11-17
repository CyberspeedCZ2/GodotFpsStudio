/**************************************************************************/
/*  particle_presets.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_presets.h"

#include "vfx_sound_generator.h"

#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/camera_shake_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/radial_damage_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
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
	Vector<float> offsets;
	Vector<Color> colors;
	for (const GradientStop &stop : p_stops) {
		offsets.push_back(stop.offset);
		colors.push_back(stop.color);
	}
	return make_color_ramp(offsets, colors);
}

Ref<CurveTexture> make_curve(std::initializer_list<Vector2> p_points, float p_max = 1.0f) {
	Vector<Vector2> points;
	for (const Vector2 &point : p_points) {
		points.push_back(point);
	}
	return make_curve_texture(points, p_max);
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

// The editor generates (or reuses res://vfx_sounds/<file>.wav) the procedural
// sound described here and assigns it when the preset is created.
AudioStreamPlayer3D *set_sound_recipe(AudioStreamPlayer3D *p_player, int p_sound, const String &p_file, real_t p_volume_db = 0.0, uint32_t p_seed = 1) {
	Dictionary recipe;
	recipe["sound"] = p_sound;
	recipe["seed"] = int64_t(p_seed);
	recipe["file"] = p_file;
	p_player->set_meta(SOUND_RECIPE_META, recipe);
	p_player->set_volume_db(p_volume_db);
	return p_player;
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

// Soft particle smoke shared by every smoke, dust, steam and mist emitter,
// set up the way Source (HL2) and UE5 Niagara build smoke:
// - texture: soft puff with lit inner billows; a 4x4 life-cycle flipbook
//   (grows, breaks into wisps, thins out) played over the particle life,
// - lit by the scene (per-vertex, cheap) so it is not glowing in dark levels,
// - mixed sizes, random rotation with a slow spin, varied lifetimes.
// A neutral light texture: the particle color tints it.
TextureRequest make_soft_smoke(EmitterBuilder &r_builder) {
	TextureRequest request = make_texture(r_builder.node, VFXTextureGenerator::TYPE_SOFT_SMOKE, "smoke_soft_life", 1024, 4, 1.0, true);
	set_colors(request, Color(0.5, 0.5, 0.52), Color(1.0, 0.99, 0.97));
	set_param(request, "lifecycle", 1.0f);
	set_param(request, "radius", 0.95f);
	r_builder.set("anim_speed", Vector2(1.0, 1.0));
	r_builder.material->set_shading_mode(BaseMaterial3D::SHADING_MODE_PER_VERTEX);
	r_builder.process->set_lifetime_randomness(0.3);
	const Vector2 scale = r_builder.process->get("scale");
	r_builder.set("scale", Vector2(scale.x * 0.55f, scale.y * 1.25f)); // Bigger and smaller puffs.
	const Vector2 angle = r_builder.process->get("angle");
	if (angle.x == angle.y) {
		r_builder.set("angle", RANDOM_ANGLE);
	}
	const Vector2 spin = r_builder.process->get("angular_velocity");
	if (spin.x == 0.0f && spin.y == 0.0f) {
		r_builder.set("angular_velocity", Vector2(-10, 10));
	}
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
	r_textures.push_back(make_soft_smoke(smoke));

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

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 18, 5.0, true, 0.55, 4.0, BLEND_ALPHA);
	ParticleEffect3D::set_emitter_delay(smoke.node, 0.25);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 1.0);
	// Thrown outward, slowed quickly by drag, then a slow buoyant rise.
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 80.0).set("initial_velocity", Vector2(1.2, 3.2)).set("damping", Vector2(2.0, 3.0)).set("gravity", Vector3(0, 0.4, 0));
	// Drag only early in life: it stops the outward throw, then fades so the
	// buoyancy can lift the smoke (drag larger than gravity would hold it).
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_DAMPING, make_curve({ Vector2(0, 1), Vector2(0.3, 0.0), Vector2(1, 0.0) }));
	smoke.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-8, 8)).set("scale", Vector2(1.0, 1.6));
	smoke.set("color", Color(0.3, 0.28, 0.27, 0.7));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.35), Vector2(0.35, 0.8), Vector2(1, 1.0) }));
	smoke.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.08, 1), Vector2(0.5, 0.75), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke(smoke));

	add_light(p_effect, "LightFlash", Color(1.0, 0.58, 0.28), 14.0, 12.0, 0.0, 0.8, 0.25, Vector3(0, 0.6, 0));
	add_camera_shake(p_effect, 0.85, 0.75, 6.0, 35.0, 0.0);
	{
		Decal *scorch = add_decal(p_effect, "Scorch", Vector3(4.5, 3.0, 4.5), 20.0, 0.3);
		ParticleEffect3D::set_emitter_delay(scorch, 0.05);
		r_textures.push_back(make_texture(scorch, VFXTextureGenerator::TYPE_SCORCH, "decal_scorch", 512));
	}
	{
		set_sound_recipe(add_sound_slot(p_effect, 12.0, 120.0), VFXSoundGenerator::SOUND_EXPLOSION, "explosion", 3.0);
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
	r_textures.push_back(make_soft_smoke(dust));

	// Quad a bit larger than the chip, which fills ~80% of its frame.
	EmitterBuilder chips = add_emitter(p_effect, "Debris", 10, 0.8, true, 1.0, 0.045, BLEND_ALPHA);
	chips.set("direction", Vector3(0, 1, 0)).set("spread", 45.0).set("initial_velocity", Vector2(2.5, 5.0)).set("gravity", Vector3(0, -9.8, 0)).set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-400, 400)).set("scale", Vector2(0.5, 1.4));
	chips.set("color", Color(0.42, 0.38, 0.33, 1.0)); // Tints the lit chip texture to the old dark chips.
	chips.material->set_proximity_fade_enabled(false);
	enable_collision(chips, 0.3, 0.6);
	TextureRequest chip_texture = make_texture(chips.node, VFXTextureGenerator::TYPE_DEBRIS, "debris_chunk", 512, 4);
	chip_texture.random_frame = true; // Every chip a different shape.
	r_textures.push_back(chip_texture);

	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 4, 0.2, true, 1.0, 0.08, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, 1, 0)).set("spread", 50.0).set("initial_velocity", Vector2(3.0, 7.0)).set("gravity", Vector3(0, -9.8, 0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.9, 0.6, 1.0) }, { 1.0f, Color(1.0, 0.4, 0.1, 0.0) } }));
	enable_collision(sparks, 0.35, 0.3);
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	Decal *hole = add_decal(p_effect, "BulletHole", Vector3(0.22, 0.3, 0.22), 12.0, 0.2);
	r_textures.push_back(make_bullet_hole_texture(hole, "decal_bullet_hole_concrete", 0, Color(0.62, 0.6, 0.56)));
	set_sound_recipe(add_sound_slot(p_effect, 2.0, 25.0), VFXSoundGenerator::SOUND_BULLET_IMPACT_CONCRETE, "impact_concrete", -3.0, 1);
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
	set_sound_recipe(add_sound_slot(p_effect, 2.0, 30.0), VFXSoundGenerator::SOUND_BULLET_IMPACT_METAL, "impact_metal", -3.0, 1);
}

void build_blood_hit(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder mist = add_emitter(p_effect, "Mist", 5, 0.6, true, 0.95, 0.4, BLEND_ALPHA);
	mist.set("direction", Vector3(0, 0, 1)).set("spread", 30.0).set("initial_velocity", Vector2(0.8, 2.0)).set("damping", Vector2(3.0, 4.0)).set("angle", RANDOM_ANGLE);
	mist.set("color", Color(0.45, 0.02, 0.02, 0.85));
	mist.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	mist.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke(mist));

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
	set_sound_recipe(add_sound_slot(p_effect, 2.0, 20.0), VFXSoundGenerator::SOUND_FLESH_IMPACT, "flesh_impact", -3.0, 1);
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
	r_textures.push_back(make_soft_smoke(smoke));
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
	r_textures.push_back(make_soft_smoke(smoke));

	add_light(p_effect, "FireLight", Color(1.0, 0.55, 0.22), 2.5, 6.0, 0.0, 0.0, 0.45, Vector3(0, 0.45, 0));
	set_sound_recipe(add_sound_slot(p_effect, 3.0, 30.0), VFXSoundGenerator::SOUND_FIRE_LOOP, "fire_loop", -6.0, 1);
}


const Color WHITE_TINT_A(0.72, 0.72, 0.72);
const Color WHITE_TINT_B(1.0, 1.0, 1.0);

void build_wood_impact(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder dust = add_emitter(p_effect, "Dust", 5, 1.0, true, 0.9, 0.4, BLEND_ALPHA);
	dust.set("direction", Vector3(0, 1, 0)).set("spread", 30.0).set("initial_velocity", Vector2(0.8, 2.0)).set("damping", Vector2(2.5, 3.5)).set("gravity", Vector3(0, -0.3, 0));
	dust.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-40, 40)).set("color", Color(0.55, 0.42, 0.28, 0.6));
	dust.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.4), Vector2(1, 1.0) }));
	dust.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke(dust));

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
	set_sound_recipe(add_sound_slot(p_effect, 2.0, 25.0), VFXSoundGenerator::SOUND_BULLET_IMPACT_WOOD, "impact_wood", -3.0, 1);
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
	r_textures.push_back(make_soft_smoke(mist));

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

	set_sound_recipe(add_sound_slot(p_effect, 4.0, 40.0), VFXSoundGenerator::SOUND_WATER_SPLASH, "water_splash", -2.0);
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

	set_sound_recipe(add_sound_slot(p_effect, 5.0, 50.0), VFXSoundGenerator::SOUND_GLASS_SHATTER, "glass_shatter", 0.0);
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
	set_sound_recipe(add_sound_slot(p_effect, 3.0, 25.0), VFXSoundGenerator::SOUND_ELECTRIC_ZAP, "electric_zap", -2.0);
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
	r_textures.push_back(make_soft_smoke(steam));
	set_sound_recipe(add_sound_slot(p_effect, 3.0, 25.0), VFXSoundGenerator::SOUND_STEAM_HISS, "steam_hiss", -6.0);
}

void build_smoke_grenade(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Thick smoke screen for ~8 s that fills the room and stays off walls.
	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 32, 7.0, false, 0.0, 3.8, BLEND_ALPHA);
	ParticleEffect3D::set_emitter_duration(smoke.node, 8.0);
	smoke.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.2);
	smoke.set("direction", Vector3(0, 1, 0)).set("spread", 80.0).set("initial_velocity", Vector2(1.0, 2.5)).set("damping", Vector2(0.9, 1.4)).set("gravity", Vector3(0, 0.04, 0));
	smoke.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-8, 8)).set("scale", Vector2(0.9, 1.3)).set("color", Color(0.72, 0.72, 0.7, 0.85));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.3), Vector2(0.4, 1.0), Vector2(1, 1.3) }, 1.3f));
	smoke.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.1, 1), Vector2(0.75, 0.9), Vector2(1, 0) }));
	enable_collision(smoke, 0.0, 1.0);
	r_textures.push_back(make_soft_smoke(smoke));
	set_sound_recipe(add_sound_slot(p_effect, 3.0, 30.0), VFXSoundGenerator::SOUND_SMOKE_GRENADE_HISS, "smoke_grenade_hiss", -6.0);
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
	r_textures.push_back(make_soft_smoke(smoke));

	add_light(p_effect, "FlareLight", Color(1.0, 0.2, 0.12), 3.0, 9.0, 0.0, 0.0, 0.35, Vector3(0, 0.1, 0));
	set_sound_recipe(add_sound_slot(p_effect, 2.0, 20.0), VFXSoundGenerator::SOUND_FLARE_BURN, "flare_burn", -6.0);
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
	r_textures.push_back(make_soft_smoke(dust));
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

// Solid 3D pieces (casings, gibs) instead of camera-facing quads: lit mesh
// with its own material, spinning around Y as it tumbles.
void use_solid_mesh(EmitterBuilder &r_builder, const Ref<PrimitiveMesh> &p_mesh, const Color &p_albedo, real_t p_metallic, real_t p_roughness) {
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_albedo(p_albedo);
	material->set_metallic(p_metallic);
	material->set_roughness(p_roughness);
	p_mesh->set_material(material);
	r_builder.node->set_draw_pass_mesh(0, p_mesh);
	r_builder.node->set_draw_order(GPUParticles3D::DRAW_ORDER_INDEX); // Opaque.
	r_builder.material = material;
	r_builder.set("particle_flag_rotate_y", true);
}

void build_shell_eject(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// One brass casing per shot, thrown to the right (+X) and up from the
	// ejection port; spawn it at the weapon's port. The casing simulates in
	// world space so it stays behind when the weapon moves.
	EmitterBuilder casing = add_emitter(p_effect, "Casing", 1, 1.6, true, 1.0, 0.03, BLEND_ALPHA);
	Ref<CylinderMesh> shell;
	shell.instantiate();
	shell->set_top_radius(0.0045);
	shell->set_bottom_radius(0.005);
	shell->set_height(0.022);
	shell->set_radial_segments(10);
	shell->set_rings(1);
	use_solid_mesh(casing, shell, Color(0.78, 0.6, 0.26), 1.0, 0.32);
	casing.node->set_use_local_coordinates(false);
	casing.set("direction", Vector3(1, 0.8, 0.15)).set("spread", 12.0).set("initial_velocity", Vector2(1.8, 2.6)).set("gravity", Vector3(0, -9.8, 0));
	casing.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-900, 900));
	casing.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(0.85, 1), Vector2(1, 0) }));
	enable_collision(casing, 0.45, 0.35);
	// Clink when it lands; drop a casing sound on the Sound track.
	AudioStreamPlayer3D *clink = add_sound_slot(p_effect, 1.0, 12.0, 0.35);
	set_sound_recipe(clink, VFXSoundGenerator::SOUND_SHELL_CLINK, "shell_clink", -6.0);
	clink->set_meta(ParticleEffect3D::META_PITCH_RANDOM, 0.15);
}

void build_rocket_trail(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Attach to the rocket (flying along -Z). Smoke and sparks stay in the
	// world behind it; the flame stays on the nozzle.
	EmitterBuilder flame = add_emitter(p_effect, "Flame", 8, 0.08, false, 0.0, 0.45, BLEND_ADD);
	flame.node->set_use_local_coordinates(true);
	flame.set("direction", Vector3(0, 0, 1)).set("spread", 6.0).set("initial_velocity", Vector2(2.0, 4.0)).set("angle", RANDOM_ANGLE).set("scale", Vector2(0.7, 1.2));
	flame.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.95, 0.75, 1.0) }, { 1.0f, Color(1.0, 0.45, 0.1, 0.0) } }));
	TextureRequest flame_texture = make_texture(flame.node, VFXTextureGenerator::TYPE_FLASH, "rocket_flame", 128);
	set_param(flame_texture, "rays", 4);
	set_param(flame_texture, "streak", 0.35f);
	r_textures.push_back(flame_texture);

	EmitterBuilder smoke = add_emitter(p_effect, "Smoke", 60, 2.8, false, 0.0, 0.9, BLEND_ALPHA);
	smoke.set("direction", Vector3(0, 0, 1)).set("spread", 10.0).set("initial_velocity", Vector2(0.3, 0.8)).set("damping", Vector2(0.6, 1.0)).set("gravity", Vector3(0, 0.15, 0));
	smoke.set("turbulence_enabled", true).set("turbulence_noise_strength", 0.35).set("turbulence_noise_scale", 3.0);
	smoke.set("color", Color(0.75, 0.74, 0.72, 0.6));
	smoke.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.25), Vector2(1, 1.4) }, 1.5f));
	smoke.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.06, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke(smoke));

	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 16, 0.4, false, 0.0, 0.06, BLEND_ADD, true);
	sparks.set("direction", Vector3(0, 0, 1)).set("spread", 25.0).set("initial_velocity", Vector2(2.0, 5.0)).set("gravity", Vector3(0, -6.0, 0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.9, 0.6, 1.0) }, { 1.0f, Color(1.0, 0.35, 0.08, 0.0) } }));
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	add_light(p_effect, "ExhaustLight", Color(1.0, 0.65, 0.3), 3.0, 5.0, 0.0, 0.0, 0.3, Vector3(0, 0, 0.2));
	set_sound_recipe(add_sound_slot(p_effect, 6.0, 60.0), VFXSoundGenerator::SOUND_ROCKET_LOOP, "rocket_loop", -3.0);
}

void build_gibs(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	EmitterBuilder chunks = add_emitter(p_effect, "Chunks", 10, 3.0, true, 1.0, 0.1, BLEND_ALPHA);
	Ref<SphereMesh> chunk;
	chunk.instantiate();
	chunk->set_radius(0.06);
	chunk->set_height(0.09);
	chunk->set_radial_segments(5); // Low poly reads as an irregular lump.
	chunk->set_rings(3);
	use_solid_mesh(chunks, chunk, Color(0.38, 0.05, 0.04), 0.0, 0.45);
	chunks.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.25);
	chunks.set("direction", Vector3(0, 1, 0)).set("spread", 70.0).set("initial_velocity", Vector2(2.5, 6.0)).set("gravity", Vector3(0, -9.8, 0));
	chunks.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-360, 360)).set("scale", Vector2(0.6, 1.6));
	chunks.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(0.9, 1), Vector2(1, 0) }));
	enable_collision(chunks, 0.15, 0.8);

	EmitterBuilder bones = add_emitter(p_effect, "Bones", 5, 3.0, true, 1.0, 0.1, BLEND_ALPHA);
	Ref<CapsuleMesh> bone;
	bone.instantiate();
	bone->set_radius(0.015);
	bone->set_height(0.14);
	bone->set_radial_segments(6);
	bone->set_rings(1);
	use_solid_mesh(bones, bone, Color(0.86, 0.82, 0.7), 0.0, 0.6);
	bones.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.2);
	bones.set("direction", Vector3(0, 1, 0)).set("spread", 60.0).set("initial_velocity", Vector2(3.0, 6.5)).set("gravity", Vector3(0, -9.8, 0));
	bones.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-540, 540));
	bones.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(0.9, 1), Vector2(1, 0) }));
	enable_collision(bones, 0.3, 0.6);

	EmitterBuilder mist = add_emitter(p_effect, "Mist", 8, 0.9, true, 0.95, 0.9, BLEND_ALPHA);
	mist.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.3);
	mist.set("spread", 180.0).set("initial_velocity", Vector2(0.6, 1.8)).set("damping", Vector2(2.5, 3.5)).set("angle", RANDOM_ANGLE);
	mist.set("color", Color(0.42, 0.02, 0.02, 0.85));
	mist.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	mist.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke(mist));

	EmitterBuilder drops = add_emitter(p_effect, "Droplets", 40, 0.9, true, 1.0, 0.05, BLEND_ALPHA, true);
	drops.set("spread", 180.0).set("initial_velocity", Vector2(2.0, 6.0)).set("gravity", Vector3(0, -9.8, 0)).set("scale", Vector2(0.5, 1.4));
	drops.set("color", Color(0.4, 0.0, 0.0, 1.0));
	drops.material->set_proximity_fade_enabled(false);
	TextureRequest drop_texture = make_texture(drops.node, VFXTextureGenerator::TYPE_SPARK, "blood_droplet", 128);
	set_colors(drop_texture, Color(0.8, 0.8, 0.8), Color(1.0, 1.0, 1.0));
	set_param(drop_texture, "glow", 0.2f);
	set_param(drop_texture, "flicker", 0.0f);
	r_textures.push_back(drop_texture);

	// Pool on the floor below (projects along -Y).
	Decal *pool = add_decal(p_effect, "Splatter", Vector3(2.2, 2.0, 2.2), 20.0, 0.3);
	ParticleEffect3D::set_emitter_delay(pool, 0.1);
	r_textures.push_back(make_texture(pool, VFXTextureGenerator::TYPE_SPLATTER, "decal_blood_splatter", 256));
	set_sound_recipe(add_sound_slot(p_effect, 4.0, 35.0), VFXSoundGenerator::SOUND_GIBS, "gibs", 0.0);
}

void build_energy_ball(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Glowing energy orb (place it on a projectile or a dispenser).
	EmitterBuilder core = add_emitter(p_effect, "Core", 3, 0.4, false, 0.0, 0.7, BLEND_ADD);
	core.set("angle", RANDOM_ANGLE).set("angular_velocity", Vector2(-90, 90)).set("scale", Vector2(0.9, 1.1));
	core.set("color", Color(2.2, 2.4, 3.0, 1.0)); // HDR: blooms with Glow.
	core.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.2, 1), Vector2(0.8, 1), Vector2(1, 0) }));
	core.node->set("preprocess", 0.5);
	TextureRequest core_texture = make_texture(core.node, VFXTextureGenerator::TYPE_PLASMA, "energy_ball_core", 256);
	set_colors(core_texture, Color(0.55, 0.75, 1.0), Color(0.95, 0.98, 1.0));
	r_textures.push_back(core_texture);

	EmitterBuilder halo = add_emitter(p_effect, "Halo", 1, 1.0, false, 0.0, 1.6, BLEND_ADD);
	halo.set("color", Color(0.4, 0.6, 1.0, 0.6));
	halo.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.9), Vector2(0.5, 1.0), Vector2(1, 0.9) }));
	halo.node->set("preprocess", 1.0);
	TextureRequest halo_texture = make_texture(halo.node, VFXTextureGenerator::TYPE_FLASH, "energy_ball_halo", 256);
	set_param(halo_texture, "rays", 0);
	set_param(halo_texture, "streak", 0.0f);
	set_colors(halo_texture, WHITE_TINT_A, WHITE_TINT_B);
	r_textures.push_back(halo_texture);

	EmitterBuilder wisps = add_emitter(p_effect, "Wisps", 6, 0.15, false, 0.3, 0.6, BLEND_ADD);
	wisps.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE_SURFACE)).set("emission_sphere_radius", 0.15);
	wisps.set("angle", RANDOM_ANGLE).set("scale", Vector2(0.6, 1.1)).set("color", Color(0.7, 0.85, 1.0, 1.0));
	wisps.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_texture(wisps.node, VFXTextureGenerator::TYPE_ELECTRIC_ARC, "electric_arc", 256));

	add_light(p_effect, "OrbLight", Color(0.55, 0.72, 1.0), 3.0, 6.0, 0.0, 0.0, 0.35, Vector3());
	set_sound_recipe(add_sound_slot(p_effect, 3.0, 30.0), VFXSoundGenerator::SOUND_ENERGY_HUM, "energy_hum", -6.0);
}

void build_antlion_blood(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Yellow-green insect blood; same layout as Blood Hit (along +Z).
	EmitterBuilder mist = add_emitter(p_effect, "Mist", 5, 0.6, true, 0.95, 0.45, BLEND_ALPHA);
	mist.set("direction", Vector3(0, 0, 1)).set("spread", 30.0).set("initial_velocity", Vector2(0.8, 2.0)).set("damping", Vector2(3.0, 4.0)).set("angle", RANDOM_ANGLE);
	mist.set("color", Color(0.62, 0.62, 0.08, 0.8));
	mist.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.5), Vector2(1, 1.0) }));
	mist.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke(mist));

	EmitterBuilder drops = add_emitter(p_effect, "Droplets", 20, 0.7, true, 1.0, 0.055, BLEND_ALPHA, true);
	drops.set("direction", Vector3(0, 0.3, 1)).set("spread", 35.0).set("initial_velocity", Vector2(2.0, 5.0)).set("gravity", Vector3(0, -9.8, 0)).set("scale", Vector2(0.5, 1.3));
	drops.set("color", Color(0.7, 0.68, 0.05, 1.0));
	drops.material->set_proximity_fade_enabled(false);
	TextureRequest drop_texture = make_texture(drops.node, VFXTextureGenerator::TYPE_SPARK, "blood_droplet", 128);
	set_colors(drop_texture, Color(0.8, 0.8, 0.8), Color(1.0, 1.0, 1.0));
	set_param(drop_texture, "glow", 0.2f);
	set_param(drop_texture, "flicker", 0.0f);
	r_textures.push_back(drop_texture);

	Decal *splatter = add_decal(p_effect, "Splatter", Vector3(0.9, 2.0, 0.9), 15.0, 0.25, Transform3D(Basis::from_euler(Vector3(-Math::PI * 0.5, 0, 0)), Vector3(0, 0, 1.0)));
	ParticleEffect3D::set_emitter_delay(splatter, 0.05);
	TextureRequest splatter_texture = make_texture(splatter, VFXTextureGenerator::TYPE_SPLATTER, "decal_antlion_splatter", 256);
	set_colors(splatter_texture, Color(0.55, 0.52, 0.02), Color(0.75, 0.72, 0.12));
	r_textures.push_back(splatter_texture);
	set_sound_recipe(add_sound_slot(p_effect, 2.0, 20.0), VFXSoundGenerator::SOUND_FLESH_IMPACT, "insect_flesh_impact", -3.0, 2);
}

void build_spark_shower(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Broken ceiling light: irregular bursts of falling sparks that bounce on
	// the floor, with a buzzing flicker. Place it at the fixture.
	EmitterBuilder sparks = add_emitter(p_effect, "Sparks", 28, 1.4, false, 0.85, 0.09, BLEND_ADD, true);
	sparks.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.08);
	sparks.set("direction", Vector3(0, -1, 0)).set("spread", 35.0).set("initial_velocity", Vector2(0.5, 2.5)).set("gravity", Vector3(0, -9.8, 0)).set("scale", Vector2(0.4, 1.0));
	sparks.process->set_color_ramp(make_ramp({ { 0.0f, Color(1.0, 0.98, 0.85, 1.0) }, { 0.35f, Color(1.0, 0.75, 0.3, 1.0) }, { 1.0f, Color(0.9, 0.3, 0.05, 0.0) } }));
	sparks.process->set_lifetime_randomness(0.4);
	enable_collision(sparks, 0.35, 0.25);
	r_textures.push_back(make_texture(sparks.node, VFXTextureGenerator::TYPE_SPARK, "spark_streak", 128));

	EmitterBuilder pop = add_emitter(p_effect, "Pop", 1, 0.7, false, 1.0, 0.6, BLEND_ADD);
	pop.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(0.12, 0), Vector2(1, 0) }));
	TextureRequest pop_texture = make_texture(pop.node, VFXTextureGenerator::TYPE_FLASH, "impact_glow", 128);
	set_param(pop_texture, "rays", 4);
	set_param(pop_texture, "streak", 0.0f);
	r_textures.push_back(pop_texture);

	add_light(p_effect, "FixtureLight", Color(1.0, 0.9, 0.7), 2.5, 6.0, 0.0, 0.0, 0.95, Vector3(0, -0.1, 0));
	set_sound_recipe(add_sound_slot(p_effect, 3.0, 25.0), VFXSoundGenerator::SOUND_SPARKS, "spark_shower", -3.0);
}

// Visibility bounds from the motion instead of a fixed 16 m cube: tighter
// culling, and a smaller (finer) level collision bake.
void draw_over_water(EmitterBuilder &r_builder);

// Water as a translucent body: lit per pixel with light passing through from
// behind (a billboard facing away from the sun would otherwise go grey).
void light_as_water(EmitterBuilder &r_builder) {
	r_builder.material->set_shading_mode(BaseMaterial3D::SHADING_MODE_PER_PIXEL);
	r_builder.material->set_diffuse_mode(BaseMaterial3D::DIFFUSE_LAMBERT_WRAP);
	r_builder.material->set_feature(BaseMaterial3D::FEATURE_BACKLIGHT, true);
	r_builder.material->set_backlight(Color(0.45, 0.5, 0.55));
	r_builder.material->set_flag(BaseMaterial3D::FLAG_DONT_RECEIVE_SHADOWS, true);
	// A little light of its own: churned white water stays white under an
	// overcast sky instead of turning smoke grey.
	r_builder.material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
	r_builder.material->set_emission(Color(0.3, 0.33, 0.37));
	draw_over_water(r_builder);
}

// Under water only: a billboard particle material that fades particles out at
// the water surface. The pool sets the instance uniform water_surface_y on the
// emitter when it spawns the effect (default: no surface, nothing clipped).
void clip_at_water_surface(EmitterBuilder &r_builder, bool p_keep_above = false, bool p_billboard = true, bool p_clip_vertical = true, bool p_clip_to_area = false) {
	static Ref<Shader> shader;
	if (shader.is_null()) {
		shader.instantiate();
		shader->set_code(R"(shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled, shadows_disabled;
uniform sampler2D albedo_texture : source_color, filter_linear_mipmap, hint_default_white;
// Flipbook over the particle's life (sprite sheet frames per side; 1 = still).
uniform int anim_frames = 1;
// false: bubbles (shown below the surface); true: spray (shown above it, it
// ends where a droplet falls back into the water).
uniform bool keep_above_surface = false;
// Camera facing quad; off for velocity-aligned streaks (emitter aligns them).
uniform bool billboard = true;
// Off for things lying on the surface (foam): no fade above or below it.
uniform bool clip_vertical = true;
// Only inside the water's area (the pool sets water_bounds), so foam near a
// wall does not spread over the deck.
uniform bool clip_to_water_area = false;
instance uniform vec4 water_bounds = vec4(-1000000.0, -1000000.0, 1000000.0, 1000000.0);
varying vec3 world_vertex;
instance uniform float water_surface_y = 100000.0;
varying float particle_y;
void vertex() {
	if (billboard) {
		// Facing the camera, keeping the particle's scale.
		mat4 facing = mat4(INV_VIEW_MATRIX[0], INV_VIEW_MATRIX[1], INV_VIEW_MATRIX[2], MODEL_MATRIX[3]);
		facing[0] *= length(MODEL_MATRIX[0].xyz);
		facing[1] *= length(MODEL_MATRIX[1].xyz);
		facing[2] *= length(MODEL_MATRIX[2].xyz);
		MODELVIEW_MATRIX = VIEW_MATRIX * facing;
		world_vertex = (facing * vec4(VERTEX, 1.0)).xyz;
	} else {
		world_vertex = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	}
	particle_y = MODEL_MATRIX[3].y;
	if (anim_frames > 1) {
		float frames = float(anim_frames);
		float frame = clamp(floor(INSTANCE_CUSTOM.z * frames * frames), 0.0, frames * frames - 1.0);
		UV = (UV + vec2(mod(frame, frames), floor(frame / frames))) / frames;
	}
}
void fragment() {
	vec4 tex = texture(albedo_texture, UV);
	ALBEDO = tex.rgb * COLOR.rgb;
	// Bubbles end at the surface (they do not fly on into the air); spray ends
	// when it falls back into the water.
	float below = 1.0 - smoothstep(water_surface_y - 0.25, water_surface_y - 0.02, particle_y);
	float above = smoothstep(water_surface_y - 0.05, water_surface_y + 0.1, particle_y);
	float vertical = clip_vertical ? (keep_above_surface ? above : below) : 1.0;
	float area = 1.0;
	if (clip_to_water_area) {
		vec2 inside = min(world_vertex.xz - water_bounds.xy, water_bounds.zw - world_vertex.xz);
		area = smoothstep(0.0, 0.08, min(inside.x, inside.y));
	}
	ALPHA = tex.a * COLOR.a * vertical * area;
}
)");
	}
	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	material->set_shader_parameter("keep_above_surface", p_keep_above);
	material->set_shader_parameter("billboard", p_billboard);
	material->set_shader_parameter("clip_vertical", p_clip_vertical);
	material->set_shader_parameter("clip_to_water_area", p_clip_to_area);
	// Below the surface: after the player's underwater haze (priority 100).
	material->set_render_priority(p_keep_above || !p_clip_vertical ? r_builder.material->get_render_priority() : 110);
	Ref<QuadMesh> quad = r_builder.node->get_draw_pass_mesh(0);
	quad->set_material(material);
}

// Drawn after the water surface: the pool's surface is transparent too and,
// sorted by distance, covered foam and the foot of the column.
void draw_over_water(EmitterBuilder &r_builder) {
	r_builder.material->set_render_priority(2);
}

// Explosion breaking the water surface (+Y up, origin on the surface): a
// column of white water that grows from the surface and falls back, spray,
// lingering mist, foam spreading on the water and a ring wave.
void build_water_explosion(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Column: upright sprites (fixed Y billboard, so they stay vertical when
	// seen from above) growing from their base.
	// Few large sprites: they cover the screen up close, so each one costs
	// fill rate (overdraw), not particle count.
	EmitterBuilder column = add_emitter(p_effect, "Column", 5, 2.2, true, 0.85, 1.0, BLEND_ALPHA);
	Ref<QuadMesh> column_quad = column.node->get_draw_pass_mesh(0);
	column_quad->set_size(Vector2(3.8, 5.0));
	// Pivot at the base, a little under the surface: the column rises out of
	// the water instead of starting above it.
	column_quad->set_center_offset(Vector3(0, 2.4, 0));
	column.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_FIXED_Y);
	column.material->set_flag(BaseMaterial3D::FLAG_BILLBOARD_KEEP_SCALE, true);
	light_as_water(column);
	column.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.45);
	// No gravity: falling, the sprites would sink through the surface and show
	// as white slabs from under water. The column collapses by shrinking back
	// into its base on the surface instead.
	column.set("direction", Vector3(0, 1, 0)).set("spread", 6.0).set("initial_velocity", Vector2(0.6, 1.6)).set("damping", Vector2(1.5, 2.5));
	column.set("scale", Vector2(0.6, 1.2)).set("angle", Vector2(-7, 7));
	column.set("color", Color(0.8, 0.85, 0.9, 0.95));
	column.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.12), Vector2(0.18, 1.0), Vector2(0.55, 1.1), Vector2(1, 0.25) }, 1.2f));
	column.process->set_alpha_curve(make_curve({ Vector2(0, 0.9), Vector2(0.55, 0.85), Vector2(1, 0) }));
	TextureRequest column_texture = make_texture(column.node, VFXTextureGenerator::TYPE_WATER_PLUME, "water_plume", 512);
	r_textures.push_back(column_texture);

	// Spray: droplets thrown up and out, falling back.
	EmitterBuilder spray = add_emitter(p_effect, "Spray", 160, 1.6, true, 0.95, 0.2, BLEND_ALPHA, true);
	spray.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.35);
	// Mostly up: the droplets fall back into the pool, not onto the deck.
	spray.set("direction", Vector3(0, 1, 0)).set("spread", 16.0).set("initial_velocity", Vector2(5.0, 11.0)).set("gravity", Vector3(0, -9.8, 0)).set("damping", Vector2(0.3, 0.8)).set("scale", Vector2(0.6, 1.4));
	spray.set("color", Color(0.95, 0.98, 1.0, 1.0));
	spray.material->set_proximity_fade_enabled(false);
	draw_over_water(spray);
	clip_at_water_surface(spray, true, false); // Ends where it falls back in.
	TextureRequest spray_texture = make_texture(spray.node, VFXTextureGenerator::TYPE_SPARK, "water_droplet", 128);
	set_colors(spray_texture, WHITE_TINT_A, WHITE_TINT_B);
	set_param(spray_texture, "glow", 0.3f);
	set_param(spray_texture, "flicker", 0.0f);
	r_textures.push_back(spray_texture);

	// Mist hanging in the air after the column falls.
	EmitterBuilder mist = add_emitter(p_effect, "Mist", 6, 3.5, true, 0.8, 3.0, BLEND_ALPHA);
	ParticleEffect3D::set_emitter_delay(mist.node, 0.15);
	mist.node->set_position(Vector3(0, 2.4, 0)); // High enough not to dip under the surface.
	mist.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.8);
	mist.set("direction", Vector3(0, 1, 0)).set("spread", 70.0).set("initial_velocity", Vector2(1.5, 4.0)).set("damping", Vector2(2.0, 3.0)).set("gravity", Vector3(0, 0.2, 0));
	mist.process->set_param_texture(ParticleProcessMaterial::PARAM_DAMPING, make_curve({ Vector2(0, 1), Vector2(0.35, 0.0), Vector2(1, 0.0) }));
	mist.set("color", Color(0.92, 0.95, 1.0, 0.5));
	light_as_water(mist);
	mist.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.45), Vector2(1, 1.3) }, 1.3f));
	mist.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.1, 1), Vector2(1, 0) }));
	r_textures.push_back(make_soft_smoke(mist));

	// Foam spreading on the surface (flat, no spin: the angle would tilt it).
	EmitterBuilder foam = add_emitter(p_effect, "Foam", 2, 6.0, true, 1.0, 4.0, BLEND_ALPHA);
	Ref<QuadMesh> foam_quad = foam.node->get_draw_pass_mesh(0);
	foam_quad->set_orientation(PlaneMesh::FACE_Y);
	foam.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_DISABLED);
	foam.material->set_proximity_fade_enabled(false);
	foam.material->set_shading_mode(BaseMaterial3D::SHADING_MODE_PER_VERTEX);
	foam.node->set_position(Vector3(0, 0.03, 0));
	draw_over_water(foam);
	// Lying on the surface, kept inside the pool (it spread over the deck).
	clip_at_water_surface(foam, false, false, false, true);
	foam.set("scale", Vector2(0.8, 1.3)).set("color", Color(0.95, 0.98, 1.0, 0.95));
	foam.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.25), Vector2(0.15, 0.8), Vector2(1, 1.3) }, 1.3f));
	foam.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.05, 0.9), Vector2(0.6, 0.6), Vector2(1, 0) }));
	r_textures.push_back(make_texture(foam.node, VFXTextureGenerator::TYPE_FOAM, "water_foam", 512));

	// Ring wave running out over the water.
	EmitterBuilder ring = add_emitter(p_effect, "Ring", 2, 1.4, true, 0.7, 1.0, BLEND_ALPHA);
	Ref<QuadMesh> ring_quad = ring.node->get_draw_pass_mesh(0);
	ring_quad->set_orientation(PlaneMesh::FACE_Y);
	ring.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_DISABLED);
	ring.material->set_proximity_fade_enabled(false);
	ring.node->set_position(Vector3(0, 0.04, 0));
	draw_over_water(ring);
	clip_at_water_surface(ring, false, false, false, true);
	ring.set("scale", Vector2(4.0, 6.0)).set("color", Color(0.92, 0.97, 1.0, 0.6));
	ring.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.15), Vector2(1, 1.0) }));
	ring.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	TextureRequest ring_texture = make_texture(ring.node, VFXTextureGenerator::TYPE_WAVE_RING, "water_ripple", 256);
	set_param(ring_texture, "progress", 0.85f);
	set_param(ring_texture, "animate_progress", 0.0f);
	set_colors(ring_texture, WHITE_TINT_A, WHITE_TINT_B);
	r_textures.push_back(ring_texture);

	add_camera_shake(p_effect, 0.5, 0.6, 4.0, 25.0, 0.0);
	add_sound_slot(p_effect, 10.0, 90.0); // Pick a water blast sound in the editor.
}

// Explosion under water (origin at the blast): a bright flash and light, a
// gas bubble that swells and collapses, a cloud of bubbles rising and murk.
void build_underwater_explosion(ParticleEffect3D *p_effect, Vector<TextureRequest> &r_textures) {
	// Under water everything here is drawn after the player's underwater haze
	// (render priority, see clip_at_water_surface): the haze reads the depth of
	// the wall behind the particles and covered them as if they were there.
	EmitterBuilder flash = add_emitter(p_effect, "Flash", 1, 0.18, true, 1.0, 5.0, BLEND_ADD);
	flash.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(1, 0) }));
	flash.material->set_render_priority(110);
	TextureRequest flash_texture = make_texture(flash.node, VFXTextureGenerator::TYPE_FLASH, "underwater_flash", 256);
	set_param(flash_texture, "rays", 0);
	set_colors(flash_texture, Color(0.55, 0.8, 1.0), Color(0.95, 0.98, 1.0));
	r_textures.push_back(flash_texture);

	// Gas bubble: swells and collapses.
	EmitterBuilder shock = add_emitter(p_effect, "ShockBubble", 1, 0.45, true, 1.0, 3.5, BLEND_ALPHA);
	shock.material->set_proximity_fade_enabled(false);
	shock.set("color", Color(1.0, 1.0, 1.0, 1.0));
	shock.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.15), Vector2(0.45, 1.0), Vector2(1, 0.25) }));
	shock.process->set_alpha_curve(make_curve({ Vector2(0, 1.0), Vector2(0.6, 0.85), Vector2(1, 0) }));
	clip_at_water_surface(shock);
	TextureRequest bubble_texture = make_texture(shock.node, VFXTextureGenerator::TYPE_UNDERWATER_BUBBLE, "underwater_bubble_sphere", 256);
	r_textures.push_back(bubble_texture);

	// Bubble cloud: a dense white mass of tiny bubbles billowing out and
	// rising to the surface, the main thing seen under water.
	EmitterBuilder cloud = add_emitter(p_effect, "BubbleCloud", 14, 3.2, true, 0.9, 2.2, BLEND_ALPHA);
	cloud.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.5);
	cloud.set("spread", 180.0).set("initial_velocity", Vector2(1.5, 4.0)).set("damping", Vector2(2.5, 3.5)).set("gravity", Vector3(0, 2.2, 0));
	cloud.process->set_param_texture(ParticleProcessMaterial::PARAM_DAMPING, make_curve({ Vector2(0, 1), Vector2(0.35, 0.3), Vector2(1, 0.3) }));
	cloud.set("color", Color(0.95, 0.98, 1.0, 0.75));
	cloud.process->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, make_curve({ Vector2(0, 0.35), Vector2(0.3, 1.0), Vector2(1, 1.5) }, 1.5f));
	cloud.process->set_alpha_curve(make_curve({ Vector2(0, 0), Vector2(0.06, 1), Vector2(0.6, 0.7), Vector2(1, 0) }));
	TextureRequest cloud_texture = make_soft_smoke(cloud);
	clip_at_water_surface(cloud);
	r_textures.push_back(cloud_texture);

	// Single bubbles: many, larger, rising fast and wobbling up.
	EmitterBuilder bubbles = add_emitter(p_effect, "Bubbles", 240, 4.0, true, 0.85, 0.16, BLEND_ALPHA);
	bubbles.material->set_proximity_fade_enabled(false);
	bubbles.set("emission_shape", int(ParticleProcessMaterial::EMISSION_SHAPE_SPHERE)).set("emission_sphere_radius", 0.6);
	bubbles.set("spread", 180.0).set("initial_velocity", Vector2(2.0, 7.0)).set("damping", Vector2(2.5, 4.0)).set("gravity", Vector3(0, 5.0, 0)).set("scale", Vector2(0.35, 1.2));
	bubbles.set("turbulence_enabled", true).set("turbulence_noise_strength", 0.6).set("turbulence_noise_scale", 2.0);
	bubbles.set("color", Color(1.0, 1.0, 1.0, 0.6));
	bubbles.process->set_alpha_curve(make_curve({ Vector2(0, 1), Vector2(0.85, 0.9), Vector2(1, 0) }));
	clip_at_water_surface(bubbles);
	// Real bubbles (sphere, rim, highlight): 4x4 sheet of differently squashed
	// ones, a random one per particle.
	TextureRequest single_bubbles = make_texture(bubbles.node, VFXTextureGenerator::TYPE_UNDERWATER_BUBBLE, "underwater_bubble_sheet", 512, 4);
	single_bubbles.random_frame = true;
	r_textures.push_back(single_bubbles);

	add_light(p_effect, "LightFlash", Color(0.75, 0.9, 1.0), 16.0, 12.0, 0.0, 0.3, 0.0, Vector3());
	add_camera_shake(p_effect, 0.7, 0.7, 5.0, 20.0, 0.0);
	add_sound_slot(p_effect, 8.0, 60.0);
}

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
		case PRESET_SHELL_EJECT:
			return "Shell Eject";
		case PRESET_ROCKET_TRAIL:
			return "Rocket Trail";
		case PRESET_GIBS:
			return "Gibs";
		case PRESET_ENERGY_BALL:
			return "Energy Ball";
		case PRESET_ANTLION_BLOOD:
			return "Insect Blood";
		case PRESET_SPARK_SHOWER:
			return "Spark Shower";
		case PRESET_WATER_EXPLOSION:
			return "Water Explosion";
		case PRESET_UNDERWATER_EXPLOSION:
			return "Underwater Explosion";
	}
	return String();
}

String get_preset_category(int p_preset) {
	switch (p_preset) {
		case PRESET_MUZZLE_FLASH:
		case PRESET_BULLET_TRACER:
		case PRESET_SHELL_EJECT:
		case PRESET_ROCKET_TRAIL:
		case PRESET_ENERGY_BALL:
			return "Weapons";
		case PRESET_BULLET_IMPACT:
		case PRESET_WOOD_IMPACT:
		case PRESET_IMPACT_SPARKS:
		case PRESET_BLOOD_HIT:
		case PRESET_GLASS_SHATTER:
		case PRESET_WATER_SPLASH:
		case PRESET_GIBS:
		case PRESET_ANTLION_BLOOD:
			return "Impacts";
		case PRESET_EXPLOSION:
		case PRESET_FIRE:
		case PRESET_SMOKE_GRENADE:
		case PRESET_SIGNAL_FLARE:
		case PRESET_WATER_EXPLOSION:
		case PRESET_UNDERWATER_EXPLOSION:
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
		case PRESET_SHELL_EJECT:
			return "A brass casing thrown to the right (+X) that bounces off the level, with a delayed clink sound slot; spawn it at the ejection port.";
		case PRESET_ROCKET_TRAIL:
			return "Continuous trail for a rocket flying along -Z: nozzle flame, world-space smoke and sparks, exhaust light and a sound slot.";
		case PRESET_GIBS:
			return "Meat chunks and bone pieces that bounce off the level, blood mist, droplets, a floor splatter decal and a sound slot.";
		case PRESET_ENERGY_BALL:
			return "Looping glowing energy orb: HDR core, halo, electric wisps, flickering blue light and a sound slot.";
		case PRESET_ANTLION_BLOOD:
			return "Yellow-green insect blood: mist and droplets along +Z with a matching splatter decal on the wall behind.";
		case PRESET_SPARK_SHOWER:
			return "Broken light fixture: irregular bursts of falling sparks that bounce, a pop flash, buzzing flicker light and a sound slot.";
		case PRESET_WATER_EXPLOSION:
			return "Explosion breaking the water (+Y up, origin on the surface): a column of white water that grows and falls back, spray, lingering mist, foam spreading on the water, a ring wave and camera shake.";
		case PRESET_UNDERWATER_EXPLOSION:
			return "Explosion under water: flash and light, a gas bubble that swells and collapses, a cloud of rising bubbles, murk and camera shake.";
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
		case PRESET_SHELL_EJECT:
			build_shell_eject(effect, r_textures);
			break;
		case PRESET_ROCKET_TRAIL:
			build_rocket_trail(effect, r_textures);
			break;
		case PRESET_GIBS:
			build_gibs(effect, r_textures);
			break;
		case PRESET_ENERGY_BALL:
			build_energy_ball(effect, r_textures);
			break;
		case PRESET_ANTLION_BLOOD:
			build_antlion_blood(effect, r_textures);
			break;
		case PRESET_SPARK_SHOWER:
			build_spark_shower(effect, r_textures);
			break;
		case PRESET_WATER_EXPLOSION:
			build_water_explosion(effect, r_textures);
			break;
		case PRESET_UNDERWATER_EXPLOSION:
			build_underwater_explosion(effect, r_textures);
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
