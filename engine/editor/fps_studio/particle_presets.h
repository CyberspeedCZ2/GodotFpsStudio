/**************************************************************************/
/*  particle_presets.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "vfx_texture_generator.h"

#include "core/object/ref_counted.h"

class CurveTexture;
class GradientTexture1D;
class Node;
class ParticleEffect3D;

// Ready-made multi-emitter effects for first-person shooters. A preset
// builds the ParticleEffect3D hierarchy with timing, motion, color and size
// curves; the textures are described as generator requests that the editor
// bakes (or reuses) in the project.
namespace FPSParticlePresets {

enum Preset {
	PRESET_MUZZLE_FLASH,
	PRESET_EXPLOSION,
	PRESET_BULLET_IMPACT,
	PRESET_IMPACT_SPARKS,
	PRESET_BLOOD_HIT,
	PRESET_SMOKE_PLUME,
	PRESET_FIRE,
	PRESET_MAX,
};

struct TextureRequest {
	Node *emitter = nullptr;
	VFXTextureGenerator::Settings settings;
	String file_name; // Unique per preset/emitter, e.g. "explosion_fireball".
	int resolution = 256;
	int columns = 1; // 1 = single texture, otherwise a columns x columns sheet.
	double sheet_duration = 1.0; // Seconds covered by the sheet frames.
	bool lifecycle = false; // Sheet plays once over the particle life.
};

String get_preset_name(int p_preset);
String get_preset_description(int p_preset);
// Builds the effect. Texture requests reference emitters of the new effect.
ParticleEffect3D *create_preset(int p_preset, Vector<TextureRequest> &r_textures);

// Resource helpers shared with module presets.
Ref<GradientTexture1D> make_color_ramp(const Vector<float> &p_offsets, const Vector<Color> &p_colors);
Ref<CurveTexture> make_curve_texture(const Vector<Vector2> &p_points, float p_max = 1.0f);

} // namespace FPSParticlePresets
