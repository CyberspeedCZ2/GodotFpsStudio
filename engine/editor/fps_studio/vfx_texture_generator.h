/**************************************************************************/
/*  vfx_texture_generator.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

// Procedural VFX texture generator used by the Particle workspace.
//
// The generator is deliberately free of editor and scene dependencies: it
// renders one square frame into a float RGBA buffer (straight alpha). The
// editor wraps it in Image/WorkerThreadPool, and the same source can be built
// as a small standalone tool to render reference images without the GUI.

#include <cstdint>

namespace VFXTextureGenerator {

// Stable IDs. They are stored in recipe files, so never renumber them.
enum TypeID {
	TYPE_FIRE = 0,
	TYPE_SMOKE = 1,
	TYPE_EXPLOSION = 2,
	TYPE_SPARK = 3,
	TYPE_LIGHTNING = 4,
	TYPE_CLOUD = 5,
	TYPE_RING = 6,
	TYPE_MAGIC_CIRCLE = 7,
	TYPE_NOISE = 8,
	TYPE_WAVE_RING = 9,
	TYPE_FLASH = 10,
	TYPE_BUBBLES = 11,
	TYPE_PLASMA = 12,
	TYPE_CAUSTICS = 13,
	TYPE_ELECTRIC_ARC = 14,
	TYPE_EXPLOSION_RING = 15,
	// Decals (straight albedo + alpha, for Decal nodes).
	TYPE_BULLET_HOLE = 16,
	TYPE_SCORCH = 17,
	TYPE_GLASS_CRACK = 18,
	TYPE_SPLATTER = 19,
	// Soft particle smoke: analytic 2D, alpha fades smoothly from the centre
	// so overlapping particles merge into one volume.
	TYPE_SOFT_SMOKE = 20,
	// Tileable patterns (Noise & Patterns).
	TYPE_CELL_NOISE = 21, // Worley / Voronoi cells, edges or flat cells.
	TYPE_MARBLE_NOISE = 22, // Turbulent sine veins.
	// Particle debris: a lit rock / concrete chip. In a sheet every frame is a
	// different chip, so particles can pick a random variant.
	TYPE_DEBRIS = 23,
	// Water splash of a falling drop, seen from the side: crown sheet with a
	// lip and fingers, flying droplets, an optional centre jet and a spreading
	// puddle. A sheet is several splash animations: each row is one variant,
	// the columns are its frames (default 8 columns x 4 rows). Alpha is the
	// water, colour is its glint, so a refracting shader can use it as water.
	TYPE_WATER_SPLASH = 24,
	// Column of water thrown up by an explosion, seen from the side: a dense
	// frothy column with vertical streaks that opens at the top into a crown
	// of lumps and spray. Still image (grow and fall it with particles).
	// Alpha is the water, colour its light (shadow side to lit side).
	TYPE_WATER_PLUME = 25,
	// Foam on the water seen from above: a patch or a ring of bubbles with a
	// torn edge. Alpha is the foam, colour its brightness.
	TYPE_FOAM = 26,
};

static constexpr int MAX_PARAMS = 10;

struct ParamInfo {
	const char *key = nullptr; // Stable recipe key.
	const char *label = nullptr;
	float min_value = 0.0f;
	float max_value = 1.0f;
	float step = 0.01f;
	float default_value = 0.0f;
	const char *tooltip = nullptr;
};

struct TypeInfo {
	int id = 0;
	const char *name = nullptr;
	const char *category = nullptr;
	const char *description = nullptr;
	float color_a[3] = { 1.0f, 1.0f, 1.0f }; // Deep / shadow color.
	float color_b[3] = { 1.0f, 1.0f, 1.0f }; // Hot / lit color.
	const char *color_a_label = nullptr;
	const char *color_b_label = nullptr;
	bool volumetric = false; // Rendered by ray marching (slower).
	bool supports_tiling = false;
	int param_count = 0;
	ParamInfo params[MAX_PARAMS];
};

int get_type_count();
const TypeInfo *get_type_info_by_index(int p_index);
const TypeInfo *get_type_info(int p_id);
// Returns the parameter index with this key or -1.
int find_param(const TypeInfo *p_info, const char *p_key);

struct Settings {
	int type = TYPE_FIRE;
	uint32_t seed = 7;
	float time = 0.0f; // Seconds, drives noise evolution.
	float life = 0.0f; // 0..1 normalized position in a sprite sheet (life cycle).
	float color_a[3] = { 1.0f, 0.3f, 0.05f };
	float color_b[3] = { 1.0f, 0.85f, 0.4f };
	float params[MAX_PARAMS] = {};
	// Global controls shared by every type.
	float speed = 1.0f;
	float rotation_degrees = 0.0f;
	float exposure = 1.0f;
	float opacity = 1.0f;
	bool polar = false;
	bool tiling = false;
	float tiling_mask = 1.0f;
	int quality = 1; // 0 = draft (fast preview), 1 = final.
	// Decal types only (see has_height_normal): output a tangent-space
	// normal map (OpenGL / Godot convention, +Y up) computed from the decal's
	// own height field (hole depth, chips, cracks, bent metal) instead of the
	// colour. Alpha stays the decal alpha. Other types ignore it.
	bool output_normal = false;
	float normal_strength = 3.0f; // Same scale as the editor's "Normal strength" (0.5..10).
};

// True for types that can render a height-based normal map (decals).
bool has_height_normal(int p_type);

// Fills the params of p_settings with the defaults of its type.
void apply_type_defaults(Settings &r_settings, bool p_colors = true);

// Renders rows [p_row_begin, p_row_end) of a p_size x p_size frame into
// r_rgba (p_size * p_size * 4 floats, straight alpha, values 0..1).
void render_rows(const Settings &p_settings, int p_size, float *r_rgba, int p_row_begin, int p_row_end);

// Same as render_rows, but writes 8-bit RGBA directly into r_rgba8
// (p_size * p_size * 4 bytes). Supports sizes up to 4096.
void render_rows_rgba8(const Settings &p_settings, int p_size, uint8_t *r_rgba8, int p_row_begin, int p_row_end);

} // namespace VFXTextureGenerator
