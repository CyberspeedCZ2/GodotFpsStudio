/**************************************************************************/
/*  vfx_sound_generator.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

class AudioStreamWAV;

// Procedural sound effects for the particle presets (explosion, impacts,
// sparks, loops...). Everything is synthesized from filtered noise, decaying
// sine modes and grains, so presets get a usable sound without shipping
// audio files. Output: 16-bit mono 44.1 kHz AudioStreamWAV, deterministic
// for a given sound and seed. Loops are crossfaded and set to LOOP_FORWARD.
class VFXSoundGenerator {
public:
	// Stored in preset metadata: append only, never renumber.
	enum Sound {
		SOUND_EXPLOSION,
		SOUND_SHELL_CLINK,
		SOUND_SPARKS,
		SOUND_GLASS_SHATTER,
		SOUND_BULLET_IMPACT_CONCRETE,
		SOUND_BULLET_IMPACT_METAL,
		SOUND_BULLET_IMPACT_WOOD,
		SOUND_FLESH_IMPACT,
		SOUND_WATER_SPLASH,
		SOUND_ELECTRIC_ZAP,
		SOUND_STEAM_HISS, // Loop.
		SOUND_FIRE_LOOP, // Loop.
		SOUND_ROCKET_LOOP, // Loop.
		SOUND_ENERGY_HUM, // Loop.
		SOUND_FLARE_BURN, // Loop.
		SOUND_SMOKE_GRENADE_HISS, // Loop.
		SOUND_GIBS,
		SOUND_COUNT,
	};

	static constexpr int MIX_RATE = 44100;

	static String get_sound_name(int p_sound);
	static bool is_loop(int p_sound);
	static Ref<AudioStreamWAV> generate(int p_sound, uint32_t p_seed = 0);
	static Error save_wav(const Ref<AudioStreamWAV> &p_stream, const String &p_path);
};
