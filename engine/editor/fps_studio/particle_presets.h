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
	// Appended (ids stay stable); shown grouped by get_preset_category().
	PRESET_WOOD_IMPACT,
	PRESET_WATER_SPLASH,
	PRESET_GLASS_SHATTER,
	PRESET_ELECTRIC_SPARKS,
	PRESET_STEAM_LEAK,
	PRESET_SMOKE_GRENADE,
	PRESET_SIGNAL_FLARE,
	PRESET_DUST_LANDING,
	PRESET_AMBIENT_DUST,
	PRESET_BULLET_TRACER,
	PRESET_SHELL_EJECT,
	PRESET_ROCKET_TRAIL,
	PRESET_GIBS,
	PRESET_ENERGY_BALL,
	PRESET_ANTLION_BLOOD,
	PRESET_SPARK_SHOWER,
	PRESET_WATER_EXPLOSION,
	PRESET_UNDERWATER_EXPLOSION,
	PRESET_MAX,
};

// Meta on a preset's AudioStreamPlayer3D: Dictionary { "sound": int
// (VFXSoundGenerator::Sound), "seed": int, "file": String }. The editor
// generates the sound (or loads res://vfx_sounds/<file>.wav), assigns it as
// the stream and removes the meta.
inline constexpr const char *SOUND_RECIPE_META = "_fps_sound_recipe";

struct TextureRequest {
	Node *emitter = nullptr;
	VFXTextureGenerator::Settings settings;
	String file_name; // Unique per preset/emitter, e.g. "explosion_fireball".
	int resolution = 256;
	int columns = 1; // 1 = single texture, otherwise a columns x columns sheet.
	double sheet_duration = 1.0; // Seconds covered by the sheet frames.
	bool lifecycle = false; // Sheet plays once over the particle life.
	bool random_frame = false; // Each particle shows one random frame (variants, no animation).
};

String get_preset_name(int p_preset);
String get_preset_description(int p_preset);
String get_preset_category(int p_preset);
// All presets grouped by category (Weapons, Impacts, Explosions & Fire, Environment).
Vector<int> get_presets_in_display_order();
// Builds the effect. Texture requests reference emitters of the new effect.
ParticleEffect3D *create_preset(int p_preset, Vector<TextureRequest> &r_textures);

// Resource helpers shared with module presets.
Ref<GradientTexture1D> make_color_ramp(const Vector<float> &p_offsets, const Vector<Color> &p_colors);
Ref<CurveTexture> make_curve_texture(const Vector<Vector2> &p_points, float p_max = 1.0f);

} // namespace FPSParticlePresets
