/**************************************************************************/
/*  effect_beam_3d.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "scene/3d/visual_instance_3d.h"

class ArrayMesh;
class Shader;
class ShaderMaterial;
class Texture2D;

// A camera-facing strip between this node and end_position: electric arcs,
// lasers, tracers, energy beams. Inside a ParticleEffect3D it is a timed
// track; ParticleEffect3D.set_target_position() aims every beam of an effect.
class EffectBeam3D : public GeometryInstance3D {
	GDCLASS(EffectBeam3D, GeometryInstance3D);

	Vector3 end_position = Vector3(0, 0, -5);
	real_t width = 0.08;
	real_t taper = 0.0;
	Color color = Color(0.55, 0.8, 1.0, 1.0);
	real_t brightness = 2.0;
	Ref<Texture2D> texture;
	real_t texture_tiling = 1.0;
	real_t texture_scroll = 0.0;
	real_t noise_amplitude = 0.0;
	real_t noise_frequency = 3.0;
	real_t noise_speed = 12.0;
	int segments = 24;
	bool additive = true;
	real_t intensity = 1.0;

	Ref<ArrayMesh> mesh;
	Ref<ShaderMaterial> material;

	void _build_mesh();
	void _update_material();

protected:
	static void _bind_methods();

public:
	void set_end_position(const Vector3 &p_position);
	Vector3 get_end_position() const;
	void set_width(real_t p_width);
	real_t get_width() const;
	void set_taper(real_t p_taper);
	real_t get_taper() const;
	void set_color(const Color &p_color);
	Color get_color() const;
	void set_brightness(real_t p_brightness);
	real_t get_brightness() const;
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;
	void set_texture_tiling(real_t p_tiling);
	real_t get_texture_tiling() const;
	void set_texture_scroll(real_t p_scroll);
	real_t get_texture_scroll() const;
	void set_noise_amplitude(real_t p_amplitude);
	real_t get_noise_amplitude() const;
	void set_noise_frequency(real_t p_frequency);
	real_t get_noise_frequency() const;
	void set_noise_speed(real_t p_speed);
	real_t get_noise_speed() const;
	void set_segments(int p_segments);
	int get_segments() const;
	void set_additive(bool p_additive);
	bool is_additive() const;
	// 0..1 fade of the whole beam; animated by ParticleEffect3D, not saved.
	void set_intensity(real_t p_intensity);
	real_t get_intensity() const;
	// Aims the beam at a point in world space.
	void set_global_end_position(const Vector3 &p_position);

	virtual AABB get_aabb() const override;

	// Frees the shared shaders (engine shutdown).
	static void finish();

	EffectBeam3D();
};
