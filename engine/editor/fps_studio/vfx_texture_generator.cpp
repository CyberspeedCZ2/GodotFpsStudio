/**************************************************************************/
/*  vfx_texture_generator.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

// Original procedural VFX texture generator for Godot FPS Studio.
//
// Fire, smoke, clouds and explosions are rendered as participating media:
// a 3D density/temperature field is ray marched through the sprite with
// Beer-Lambert absorption, temperature driven emission, single scattering
// with a short shadow march towards the key light and an exponential tone
// mapper. Thin energy effects (sparks, lightning, flares...) are evaluated
// analytically as HDR emission and pass through the same tone mapper, which
// gives them soft, photographic falloff instead of flat colour bands.

#include "vfx_texture_generator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace VFXTextureGenerator {

namespace {

/* Math helpers ----------------------------------------------------------- */

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;

struct V3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	V3() {}
	V3(float p_x, float p_y, float p_z) :
			x(p_x), y(p_y), z(p_z) {}
	V3 operator+(const V3 &o) const { return V3(x + o.x, y + o.y, z + o.z); }
	V3 operator-(const V3 &o) const { return V3(x - o.x, y - o.y, z - o.z); }
	V3 operator*(float s) const { return V3(x * s, y * s, z * s); }
	V3 operator*(const V3 &o) const { return V3(x * o.x, y * o.y, z * o.z); }
	V3 &operator+=(const V3 &o) {
		x += o.x;
		y += o.y;
		z += o.z;
		return *this;
	}
};

inline float dot3(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length3(const V3 &a) { return std::sqrt(dot3(a, a)); }
inline V3 normalize3(const V3 &a) {
	const float l = length3(a);
	return l > 1e-6f ? a * (1.0f / l) : V3(0.0f, 1.0f, 0.0f);
}
inline float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline V3 mix3(const V3 &a, const V3 &b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float a, float b, float v) {
	const float t = saturate((v - a) / ((b - a) == 0.0f ? 1e-5f : (b - a)));
	return t * t * (3.0f - 2.0f * t);
}
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline int maxi(int a, int b) { return a > b ? a : b; }
inline float absf(float v) { return v < 0.0f ? -v : v; }
inline float fractf(float v) { return v - std::floor(v); }
inline float len2(float x, float y) { return std::sqrt(x * x + y * y); }

/* Hashing and noise ------------------------------------------------------ */

inline uint32_t mix_u32(uint32_t h) {
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return h;
}

inline uint32_t hash3i(int x, int y, int z, uint32_t seed) {
	return mix_u32(uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu ^ (seed * 0x9e3779b9u + 0x632be5abu));
}

inline float hash01(uint32_t h) { return float(h & 0xffffffu) / float(0xffffff); }

inline float grad3(uint32_t h, float x, float y, float z) {
	const uint32_t hh = h & 15u;
	const float u = hh < 8u ? x : y;
	const float v = hh < 4u ? y : (hh == 12u || hh == 14u ? x : z);
	return ((hh & 1u) ? -u : u) + ((hh & 2u) ? -v : v);
}

inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Gradient noise, approximately -1..1. A period > 0 wraps the lattice so the
// result tiles seamlessly every p_period units on X and Y.
float gnoise3(float px, float py, float pz, uint32_t seed, int period = 0) {
	const float fx0 = std::floor(px);
	const float fy0 = std::floor(py);
	const float fz0 = std::floor(pz);
	int ix = int(fx0);
	int iy = int(fy0);
	const int iz = int(fz0);
	const float x = px - fx0;
	const float y = py - fy0;
	const float z = pz - fz0;
	int ix1 = ix + 1;
	int iy1 = iy + 1;
	if (period > 0) {
		ix = ((ix % period) + period) % period;
		iy = ((iy % period) + period) % period;
		ix1 = (ix + 1) % period;
		iy1 = (iy + 1) % period;
	}
	const float u = fade(x);
	const float v = fade(y);
	const float w = fade(z);
	const float n000 = grad3(hash3i(ix, iy, iz, seed), x, y, z);
	const float n100 = grad3(hash3i(ix1, iy, iz, seed), x - 1.0f, y, z);
	const float n010 = grad3(hash3i(ix, iy1, iz, seed), x, y - 1.0f, z);
	const float n110 = grad3(hash3i(ix1, iy1, iz, seed), x - 1.0f, y - 1.0f, z);
	const float n001 = grad3(hash3i(ix, iy, iz + 1, seed), x, y, z - 1.0f);
	const float n101 = grad3(hash3i(ix1, iy, iz + 1, seed), x - 1.0f, y, z - 1.0f);
	const float n011 = grad3(hash3i(ix, iy1, iz + 1, seed), x, y - 1.0f, z - 1.0f);
	const float n111 = grad3(hash3i(ix1, iy1, iz + 1, seed), x - 1.0f, y - 1.0f, z - 1.0f);
	const float nx00 = mixf(n000, n100, u);
	const float nx10 = mixf(n010, n110, u);
	const float nx01 = mixf(n001, n101, u);
	const float nx11 = mixf(n011, n111, u);
	return mixf(mixf(nx00, nx10, v), mixf(nx01, nx11, v), w);
}

inline float gnoise(const V3 &p, uint32_t seed) { return gnoise3(p.x, p.y, p.z, seed); }

// Octave rotation removes visible lattice alignment between octaves.
inline V3 octave_rotate(const V3 &p) {
	return V3(0.00f * p.x + 0.80f * p.y + 0.60f * p.z,
				   -0.80f * p.x + 0.36f * p.y - 0.48f * p.z,
				   -0.60f * p.x - 0.48f * p.y + 0.64f * p.z) *
			2.03f;
}

// Fractal sum normalized to roughly -1..1.
float fbm(V3 p, int octaves, float gain, uint32_t seed) {
	float sum = 0.0f;
	float amp = 1.0f;
	float norm = 0.0f;
	for (int i = 0; i < octaves; i++) {
		sum += gnoise(p, seed + uint32_t(i) * 101u) * amp;
		norm += amp;
		amp *= gain;
		p = octave_rotate(p);
	}
	return sum / norm;
}

// Billowy turbulence (sum of |noise|), 0..1. Produces the rounded, cauliflower
// lumps typical for smoke and fireballs.
float billow(V3 p, int octaves, float gain, uint32_t seed) {
	float sum = 0.0f;
	float amp = 1.0f;
	float norm = 0.0f;
	for (int i = 0; i < octaves; i++) {
		sum += std::fabs(gnoise(p, seed + uint32_t(i) * 131u)) * amp;
		norm += amp;
		amp *= gain;
		p = octave_rotate(p);
	}
	return sum / norm;
}

// Periodic 2D fbm (via 3D noise with a fixed Z slice). Tiles every p_period.
float fbm_periodic(float x, float y, float z, int octaves, float gain, int period, uint32_t seed) {
	float sum = 0.0f;
	float amp = 1.0f;
	float norm = 0.0f;
	float freq = 1.0f;
	for (int i = 0; i < octaves; i++) {
		sum += gnoise3(x * freq, y * freq, z * freq, seed + uint32_t(i) * 101u, period * int(freq)) * amp;
		norm += amp;
		amp *= gain;
		freq *= 2.0f;
	}
	return sum / norm;
}

// Cellular noise. Returns F1 and F2 distances. Optional period for tiling.
void voronoi2(float px, float py, uint32_t seed, float jitter, int period, float &r_f1, float &r_f2, float &r_cell) {
	const int x0 = int(std::floor(px));
	const int y0 = int(std::floor(py));
	float d1 = 1e9f;
	float d2 = 1e9f;
	float cell = 0.0f;
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			int cx = x0 + dx;
			int cy = y0 + dy;
			int hx = cx;
			int hy = cy;
			if (period > 0) {
				hx = ((cx % period) + period) % period;
				hy = ((cy % period) + period) % period;
			}
			const uint32_t h = hash3i(hx, hy, 0, seed);
			const float ox = 0.5f + (hash01(h) - 0.5f) * jitter;
			const float oy = 0.5f + (hash01(mix_u32(h)) - 0.5f) * jitter;
			const float ddx = float(cx) + ox - px;
			const float ddy = float(cy) + oy - py;
			const float d = std::sqrt(ddx * ddx + ddy * ddy);
			if (d < d1) {
				d2 = d1;
				d1 = d;
				cell = hash01(mix_u32(h ^ 0x51ed27u));
			} else if (d < d2) {
				d2 = d;
			}
		}
	}
	r_f1 = d1;
	r_f2 = d2;
	r_cell = cell;
}

/* Colour and tone -------------------------------------------------------- */

inline V3 to_v3(const float *c) { return V3(c[0], c[1], c[2]); }

// Temperature ramp: soot -> deep colour -> hot colour -> white-hot core.
V3 heat_ramp(float t, const V3 &deep, const V3 &hot) {
	t = saturate(t);
	const V3 ember = deep * 0.22f;
	const V3 white(1.0f, 0.97f, 0.9f);
	if (t < 0.35f) {
		return mix3(ember, deep, t / 0.35f);
	}
	if (t < 0.8f) {
		return mix3(deep, hot, (t - 0.35f) / 0.45f);
	}
	return mix3(hot, white, (t - 0.8f) / 0.2f);
}

inline float tonemap(float v) { return 1.0f - std::exp(-maxf(v, 0.0f)); }

// Converts premultiplied HDR radiance and coverage into straight-alpha LDR.
// Emissive light that is not covered by absorbing media still raises alpha,
// so the texture composites correctly with ordinary alpha blending.
inline void write_pixel(float *r_px, const V3 &p_radiance, float p_coverage, const Settings &s) {
	const float e = maxf(s.exposure, 0.0f);
	const float r = tonemap(p_radiance.x * e);
	const float g = tonemap(p_radiance.y * e);
	const float b = tonemap(p_radiance.z * e);
	float a = maxf(saturate(p_coverage), maxf(r, maxf(g, b)));
	if (a <= 1e-5f) {
		r_px[0] = r_px[1] = r_px[2] = r_px[3] = 0.0f;
		return;
	}
	r_px[0] = saturate(r / a);
	r_px[1] = saturate(g / a);
	r_px[2] = saturate(b / a);
	r_px[3] = saturate(a * s.opacity);
}

// Straight colour + alpha (used by non-emissive 2D types).
inline void write_flat(float *r_px, const V3 &p_color, float p_alpha, const Settings &s) {
	r_px[0] = saturate(p_color.x);
	r_px[1] = saturate(p_color.y);
	r_px[2] = saturate(p_color.z);
	r_px[3] = saturate(p_alpha * s.opacity);
}

/* Type table ------------------------------------------------------------- */

TypeInfo make_type(int p_id, const char *p_name, const char *p_category, const char *p_description, float ar, float ag, float ab, float br, float bg, float bb, const char *p_a_label, const char *p_b_label, bool p_volumetric, bool p_tiling) {
	TypeInfo info;
	info.id = p_id;
	info.name = p_name;
	info.category = p_category;
	info.description = p_description;
	info.color_a[0] = ar;
	info.color_a[1] = ag;
	info.color_a[2] = ab;
	info.color_b[0] = br;
	info.color_b[1] = bg;
	info.color_b[2] = bb;
	info.color_a_label = p_a_label;
	info.color_b_label = p_b_label;
	info.volumetric = p_volumetric;
	info.supports_tiling = p_tiling;
	return info;
}

void add_param(TypeInfo &r_info, const char *p_key, const char *p_label, float p_min, float p_max, float p_step, float p_default, const char *p_tooltip) {
	if (r_info.param_count >= MAX_PARAMS) {
		return;
	}
	ParamInfo &param = r_info.params[r_info.param_count++];
	param.key = p_key;
	param.label = p_label;
	param.min_value = p_min;
	param.max_value = p_max;
	param.step = p_step;
	param.default_value = p_default;
	param.tooltip = p_tooltip;
}

constexpr int TYPE_COUNT = 28;

struct TypeTable {
	TypeInfo types[TYPE_COUNT];
	TypeTable() {
		int i = 0;
		// Adding a type without raising TYPE_COUNT must fail loudly, not
		// write past the table.
		auto push = [&](const TypeInfo &p_type) {
			if (i >= TYPE_COUNT) {
				std::abort();
			}
			types[i++] = p_type;
		};
		// Order defines the gallery order in the editor.
		{
			TypeInfo t = make_type(TYPE_EXPLOSION, "Explosion", "Fire & Explosions", "Volumetric fireball with billowing smoke, a white-hot core and self-shadowed soot.", 1.0f, 0.24f, 0.03f, 1.0f, 0.6f, 0.12f, "Flame color", "Hot color", true, false);
			add_param(t, "radius", "Radius", 0.2f, 0.95f, 0.01f, 0.62f, "Size of the fireball inside the frame.");
			add_param(t, "turbulence", "Billows", 0.0f, 1.5f, 0.01f, 0.8f, "Height of the rounded billows on the surface.");
			add_param(t, "noise_scale", "Billow size", 0.5f, 6.0f, 0.05f, 2.1f, "Frequency of the billows. Larger values give smaller lumps.");
			add_param(t, "detail", "Detail", 0.25f, 0.75f, 0.01f, 0.52f, "Strength of fine turbulent detail.");
			add_param(t, "temperature", "Fire", 0.0f, 2.0f, 0.01f, 1.0f, "How much of the volume burns. Lower values leave more smoke.");
			add_param(t, "smoke", "Smoke", 0.0f, 1.0f, 0.01f, 0.2f, "Amount of dark soot on the outer billows.");
			add_param(t, "density", "Density", 0.2f, 4.0f, 0.01f, 1.6f, "Optical density of the medium.");
			add_param(t, "light_angle", "Light angle", -180.0f, 180.0f, 1.0f, 110.0f, "Direction of the key light that shades the smoke.");
			add_param(t, "lifecycle", "Life cycle", 0.0f, 1.0f, 0.01f, 0.0f, "In sprite sheets: expand, cool down and turn to smoke over the animation.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_EXPLOSION_RING, "Explosion Ring", "Fire & Explosions", "Burning torus of fire and smoke, as seen from above a ground blast.", 1.0f, 0.24f, 0.03f, 1.0f, 0.6f, 0.12f, "Flame color", "Hot color", true, false);
			add_param(t, "radius", "Radius", 0.2f, 0.8f, 0.01f, 0.55f, "Radius of the ring.");
			add_param(t, "thickness", "Thickness", 0.05f, 0.4f, 0.01f, 0.18f, "Thickness of the burning tube.");
			add_param(t, "turbulence", "Billows", 0.0f, 1.5f, 0.01f, 0.85f, "Height of the rounded billows.");
			add_param(t, "noise_scale", "Billow size", 0.5f, 8.0f, 0.05f, 3.0f, "Frequency of the billows.");
			add_param(t, "temperature", "Fire", 0.0f, 2.0f, 0.01f, 1.0f, "How much of the volume burns.");
			add_param(t, "smoke", "Smoke", 0.0f, 1.0f, 0.01f, 0.3f, "Amount of dark soot.");
			add_param(t, "density", "Density", 0.2f, 4.0f, 0.01f, 1.6f, "Optical density of the medium.");
			add_param(t, "light_angle", "Light angle", -180.0f, 180.0f, 1.0f, 110.0f, "Direction of the key light.");
			add_param(t, "lifecycle", "Life cycle", 0.0f, 1.0f, 0.01f, 0.0f, "In sprite sheets: expand and cool down over the animation.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_FIRE, "Fire", "Fire & Explosions", "Volumetric flame with rising turbulence, soft blackbody gradient and flickering tongues.", 1.0f, 0.28f, 0.03f, 1.0f, 0.74f, 0.25f, "Flame color", "Hot color", true, false);
			add_param(t, "height", "Height", 0.3f, 1.0f, 0.01f, 0.86f, "Height of the flame inside the frame.");
			add_param(t, "width", "Width", 0.1f, 0.8f, 0.01f, 0.55f, "Width of the flame base.");
			add_param(t, "turbulence", "Turbulence", 0.0f, 2.0f, 0.01f, 1.1f, "Strength of the turbulent distortion.");
			add_param(t, "noise_scale", "Noise scale", 1.0f, 8.0f, 0.05f, 3.8f, "Size of the flame structures. Larger values give finer detail.");
			add_param(t, "rise", "Rise speed", 0.0f, 4.0f, 0.01f, 1.4f, "How fast the flame structures rise.");
			add_param(t, "intensity", "Intensity", 0.2f, 4.0f, 0.01f, 1.4f, "Brightness of the flame.");
			add_param(t, "core", "Core heat", 0.0f, 1.0f, 0.01f, 0.55f, "Size of the bright inner core.");
			add_param(t, "smoke", "Smoke", 0.0f, 1.0f, 0.01f, 0.0f, "Dark soot above the flame tips.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_SMOKE, "Smoke", "Smoke & Clouds", "Lit volumetric smoke puff with soft self-shadowing.", 0.11f, 0.11f, 0.12f, 0.78f, 0.77f, 0.75f, "Shadow color", "Lit color", true, false);
			add_param(t, "radius", "Radius", 0.2f, 0.9f, 0.01f, 0.58f, "Size of the puff.");
			add_param(t, "turbulence", "Billows", 0.0f, 1.5f, 0.01f, 0.75f, "Height of the billows.");
			add_param(t, "noise_scale", "Billow size", 0.5f, 6.0f, 0.05f, 2.4f, "Frequency of the billows.");
			add_param(t, "detail", "Detail", 0.25f, 0.75f, 0.01f, 0.55f, "Strength of fine detail.");
			add_param(t, "density", "Density", 0.2f, 4.0f, 0.01f, 1.2f, "Optical density.");
			add_param(t, "softness", "Edge softness", 0.0f, 1.0f, 0.01f, 0.3f, "Wispy, transparent outer edge.");
			add_param(t, "light_angle", "Light angle", -180.0f, 180.0f, 1.0f, 120.0f, "Direction of the key light.");
			add_param(t, "shadow", "Shadow", 0.0f, 1.0f, 0.01f, 0.75f, "Self-shadowing strength.");
			add_param(t, "ambient", "Ambient", 0.0f, 1.0f, 0.01f, 0.2f, "Ambient light filling the shadows.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_SOFT_SMOKE, "Soft Smoke (Particles)", "Smoke & Clouds", "Soft smoke puff for particles: no hard outline, alpha fades smoothly from the centre so overlapping particles merge into one cloud. Fast 2D; use a 4x4 sheet for random variants.", 0.36f, 0.36f, 0.37f, 0.8f, 0.79f, 0.77f, "Shadow color", "Lit color", false, false);
			add_param(t, "radius", "Radius", 0.3f, 0.95f, 0.01f, 0.85f, "Size of the puff. Stays inside the frame.");
			add_param(t, "softness", "Softness", 0.0f, 1.0f, 0.01f, 0.7f, "How gradually the puff fades towards its edge.");
			add_param(t, "wisps", "Wisps", 0.0f, 1.0f, 0.01f, 0.55f, "Breaks the puff into wisps with noise.");
			add_param(t, "noise_scale", "Noise scale", 0.5f, 8.0f, 0.05f, 2.6f, "Size of the wisps.");
			add_param(t, "detail", "Detail", 0.25f, 0.75f, 0.01f, 0.5f, "Strength of fine detail.");
			add_param(t, "density", "Density", 0.2f, 2.0f, 0.01f, 0.9f, "Opacity of the puff.");
			add_param(t, "light_angle", "Light angle", -180.0f, 180.0f, 1.0f, 110.0f, "Direction of the soft key light.");
			add_param(t, "shading", "Shading", 0.0f, 1.0f, 0.01f, 0.5f, "Contrast of the lighting. Low values keep particles from looking like separate balls.");
			add_param(t, "billows", "Inner billows", 0.0f, 1.0f, 0.01f, 0.55f, "Lit billowy detail inside the puff (keeps it from looking like a blur).");
			add_param(t, "lifecycle", "Life cycle", 0.0f, 1.0f, 0.01f, 0.0f, "In sprite sheets: the puff grows, breaks into wisps and thins out over the animation (play it over the particle life).");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_CLOUD, "Cloud", "Smoke & Clouds", "Cumulus cloud with a flat base and sunlit billows.", 0.42f, 0.47f, 0.56f, 1.0f, 0.99f, 0.96f, "Shadow color", "Lit color", true, false);
			add_param(t, "size", "Size", 0.3f, 1.0f, 0.01f, 0.8f, "Size of the cloud.");
			add_param(t, "turbulence", "Billows", 0.0f, 1.5f, 0.01f, 0.7f, "Height of the billows.");
			add_param(t, "noise_scale", "Billow size", 0.5f, 6.0f, 0.05f, 2.6f, "Frequency of the billows.");
			add_param(t, "density", "Density", 0.2f, 4.0f, 0.01f, 1.4f, "Optical density.");
			add_param(t, "flat_base", "Flat base", 0.0f, 1.0f, 0.01f, 0.6f, "Flatten the bottom of the cloud.");
			add_param(t, "light_angle", "Light angle", -180.0f, 180.0f, 1.0f, 70.0f, "Direction of the sun.");
			add_param(t, "shadow", "Shadow", 0.0f, 1.0f, 0.01f, 0.7f, "Self-shadowing strength.");
			add_param(t, "ambient", "Ambient", 0.0f, 1.0f, 0.01f, 0.45f, "Sky light filling the shadows.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_PLASMA, "Plasma Sun", "Fire & Explosions", "Glowing star with granulated surface and a turbulent corona.", 1.0f, 0.3f, 0.03f, 1.0f, 0.86f, 0.5f, "Corona color", "Surface color", false, false);
			add_param(t, "radius", "Radius", 0.1f, 0.7f, 0.01f, 0.36f, "Radius of the star surface.");
			add_param(t, "granulation", "Granulation", 0.0f, 1.0f, 0.01f, 0.6f, "Contrast of the convection cells.");
			add_param(t, "cell_size", "Cell size", 2.0f, 30.0f, 0.5f, 12.0f, "Number of convection cells across the star.");
			add_param(t, "corona", "Corona", 0.0f, 2.0f, 0.01f, 1.0f, "Brightness of the corona.");
			add_param(t, "corona_size", "Corona size", 0.05f, 0.8f, 0.01f, 0.35f, "Reach of the corona flames.");
			add_param(t, "intensity", "Intensity", 0.2f, 4.0f, 0.01f, 1.5f, "Overall brightness.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_SPARK, "Spark", "Energy & Light", "Hot spark streak with a glowing head and tapered tail.", 1.0f, 0.42f, 0.08f, 1.0f, 0.9f, 0.62f, "Glow color", "Core color", false, false);
			add_param(t, "length", "Length", 0.2f, 1.8f, 0.01f, 1.3f, "Length of the streak.");
			add_param(t, "thickness", "Thickness", 0.002f, 0.08f, 0.001f, 0.018f, "Width of the hot core.");
			add_param(t, "glow", "Glow", 0.0f, 2.0f, 0.01f, 0.8f, "Size of the soft glow around the core.");
			add_param(t, "head", "Head size", 0.0f, 2.0f, 0.01f, 1.0f, "Brightness and size of the leading head.");
			add_param(t, "flicker", "Flicker", 0.0f, 1.0f, 0.01f, 0.35f, "Irregular brightness along the tail.");
			add_param(t, "intensity", "Intensity", 0.2f, 6.0f, 0.01f, 2.0f, "Overall brightness.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_LIGHTNING, "Lightning", "Energy & Light", "Fractal lightning bolt with branches and a soft ionized glow.", 0.36f, 0.3f, 1.0f, 0.86f, 0.93f, 1.0f, "Glow color", "Core color", false, false);
			add_param(t, "jaggedness", "Jaggedness", 0.0f, 1.0f, 0.01f, 0.55f, "Amplitude of the zig-zag displacement.");
			add_param(t, "branches", "Branches", 0.0f, 8.0f, 1.0f, 4.0f, "Number of side branches.");
			add_param(t, "thickness", "Thickness", 0.001f, 0.03f, 0.001f, 0.006f, "Width of the bolt core.");
			add_param(t, "glow", "Glow", 0.0f, 2.0f, 0.01f, 1.0f, "Size of the glow.");
			add_param(t, "intensity", "Intensity", 0.2f, 6.0f, 0.01f, 2.2f, "Overall brightness.");
			add_param(t, "flicker_rate", "Change rate", 0.0f, 30.0f, 0.5f, 12.0f, "How many times per second the bolt changes shape.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_ELECTRIC_ARC, "Electric Burst", "Energy & Light", "Radial electric discharge with crackling arcs.", 0.32f, 0.36f, 1.0f, 0.82f, 0.94f, 1.0f, "Glow color", "Core color", false, false);
			add_param(t, "arcs", "Arcs", 1.0f, 16.0f, 1.0f, 6.0f, "Number of arcs.");
			add_param(t, "reach", "Reach", 0.2f, 1.0f, 0.01f, 0.85f, "Length of the arcs.");
			add_param(t, "jaggedness", "Jaggedness", 0.0f, 1.0f, 0.01f, 0.6f, "Zig-zag amplitude.");
			add_param(t, "thickness", "Thickness", 0.001f, 0.03f, 0.001f, 0.005f, "Width of the arc core.");
			add_param(t, "glow", "Glow", 0.0f, 2.0f, 0.01f, 1.0f, "Size of the glow.");
			add_param(t, "intensity", "Intensity", 0.2f, 6.0f, 0.01f, 2.0f, "Overall brightness.");
			add_param(t, "flicker_rate", "Change rate", 0.0f, 30.0f, 0.5f, 14.0f, "Shape changes per second.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_FLASH, "Muzzle Flash / Flare", "Energy & Light", "Bright flash with star rays, an anamorphic streak and halo.", 1.0f, 0.55f, 0.16f, 1.0f, 0.95f, 0.82f, "Glow color", "Core color", false, false);
			add_param(t, "core", "Core size", 0.01f, 0.4f, 0.005f, 0.09f, "Size of the bright core.");
			add_param(t, "rays", "Rays", 0.0f, 16.0f, 1.0f, 6.0f, "Number of star rays.");
			add_param(t, "ray_length", "Ray length", 0.0f, 1.0f, 0.01f, 0.75f, "Length of the rays.");
			add_param(t, "ray_sharpness", "Ray sharpness", 1.0f, 64.0f, 0.5f, 18.0f, "Narrowness of the rays.");
			add_param(t, "streak", "Streak", 0.0f, 2.0f, 0.01f, 0.6f, "Horizontal anamorphic streak.");
			add_param(t, "halo", "Halo", 0.0f, 1.0f, 0.01f, 0.25f, "Brightness of the halo ring.");
			add_param(t, "intensity", "Intensity", 0.2f, 6.0f, 0.01f, 2.0f, "Overall brightness.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_RING, "Energy Ring", "Energy & Light", "Glowing ring with turbulent energy.", 0.24f, 0.36f, 1.0f, 0.62f, 0.94f, 1.0f, "Glow color", "Core color", false, false);
			add_param(t, "radius", "Radius", 0.1f, 0.9f, 0.01f, 0.6f, "Ring radius.");
			add_param(t, "thickness", "Thickness", 0.005f, 0.3f, 0.005f, 0.05f, "Ring thickness.");
			add_param(t, "distortion", "Distortion", 0.0f, 1.0f, 0.01f, 0.35f, "Turbulent wobble of the ring.");
			add_param(t, "noise_scale", "Noise scale", 1.0f, 12.0f, 0.1f, 5.0f, "Frequency of the turbulence.");
			add_param(t, "glow", "Glow", 0.0f, 2.0f, 0.01f, 0.8f, "Size of the glow.");
			add_param(t, "intensity", "Intensity", 0.2f, 6.0f, 0.01f, 1.6f, "Overall brightness.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_WAVE_RING, "Shockwave", "Energy & Light", "Expanding shockwave ring with a sharp front and fading wake.", 0.5f, 0.66f, 1.0f, 0.92f, 0.97f, 1.0f, "Wake color", "Front color", false, false);
			add_param(t, "progress", "Progress", 0.0f, 1.0f, 0.01f, 0.6f, "Expansion of the wave. Sprite sheets animate it over the life cycle.");
			add_param(t, "thickness", "Thickness", 0.01f, 0.5f, 0.005f, 0.16f, "Width of the wake behind the front.");
			add_param(t, "sharpness", "Front sharpness", 0.0f, 1.0f, 0.01f, 0.75f, "Sharpness of the leading edge.");
			add_param(t, "distortion", "Distortion", 0.0f, 1.0f, 0.01f, 0.25f, "Irregularity of the front.");
			add_param(t, "intensity", "Intensity", 0.2f, 6.0f, 0.01f, 1.4f, "Overall brightness.");
			add_param(t, "animate_progress", "Animate progress", 0.0f, 1.0f, 1.0f, 1.0f, "Drive progress from the sprite sheet life cycle.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_MAGIC_CIRCLE, "Magic Circle", "Shapes", "Rotating glyph circle with rings, spokes and a polygram.", 0.3f, 0.2f, 1.0f, 0.6f, 0.95f, 1.0f, "Glow color", "Line color", false, false);
			add_param(t, "line_width", "Line width", 0.002f, 0.03f, 0.001f, 0.008f, "Width of the lines.");
			add_param(t, "points", "Star points", 3.0f, 12.0f, 1.0f, 6.0f, "Points of the inner polygram.");
			add_param(t, "spokes", "Spokes", 0.0f, 24.0f, 1.0f, 8.0f, "Radial spokes between the rings.");
			add_param(t, "runes", "Runes", 0.0f, 1.0f, 0.01f, 0.7f, "Density of the rune band.");
			add_param(t, "glow", "Glow", 0.0f, 2.0f, 0.01f, 0.7f, "Size of the glow.");
			add_param(t, "intensity", "Intensity", 0.2f, 6.0f, 0.01f, 1.5f, "Overall brightness.");
			add_param(t, "spin", "Spin", -2.0f, 2.0f, 0.01f, 0.25f, "Rotation speed in turns per second.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_BUBBLES, "Bokeh / Bubbles", "Shapes", "Soft out-of-focus disks with bright rims.", 0.15f, 0.5f, 1.0f, 0.75f, 0.95f, 1.0f, "Body color", "Rim color", false, false);
			add_param(t, "count", "Count", 1.0f, 64.0f, 1.0f, 18.0f, "Number of disks.");
			add_param(t, "size", "Size", 0.02f, 0.4f, 0.005f, 0.12f, "Average radius.");
			add_param(t, "size_variation", "Size variation", 0.0f, 1.0f, 0.01f, 0.6f, "Random size spread.");
			add_param(t, "rim", "Rim", 0.0f, 1.0f, 0.01f, 0.6f, "Brightness of the rim.");
			add_param(t, "softness", "Softness", 0.0f, 1.0f, 0.01f, 0.3f, "Edge blur.");
			add_param(t, "intensity", "Intensity", 0.2f, 4.0f, 0.01f, 1.0f, "Overall brightness.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_CAUSTICS, "Caustics", "Noise & Patterns", "Animated water caustic network. Supports seamless tiling.", 0.05f, 0.35f, 0.55f, 0.75f, 1.0f, 0.95f, "Water color", "Light color", false, true);
			add_param(t, "scale", "Scale", 1.0f, 16.0f, 1.0f, 5.0f, "Number of cells across the texture.");
			add_param(t, "sharpness", "Sharpness", 1.0f, 12.0f, 0.1f, 4.0f, "Thinness of the light lines.");
			add_param(t, "distortion", "Distortion", 0.0f, 1.0f, 0.01f, 0.4f, "Wavy distortion of the network.");
			add_param(t, "layers", "Second layer", 0.0f, 1.0f, 0.01f, 0.5f, "Blend of a second, finer layer.");
			add_param(t, "intensity", "Intensity", 0.2f, 4.0f, 0.01f, 1.5f, "Brightness of the light lines.");
			add_param(t, "background", "Background", 0.0f, 1.0f, 0.01f, 0.0f, "Opacity of the water color between lines.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_NOISE, "Noise", "Noise & Patterns", "Fractal noise mask. Supports seamless tiling.", 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, "Low color", "High color", false, true);
			add_param(t, "scale", "Scale", 1.0f, 16.0f, 1.0f, 4.0f, "Base frequency (cells across the texture).");
			add_param(t, "octaves", "Octaves", 1.0f, 8.0f, 1.0f, 5.0f, "Number of fractal octaves.");
			add_param(t, "gain", "Roughness", 0.2f, 0.8f, 0.01f, 0.5f, "Strength of the higher octaves.");
			add_param(t, "mode", "Mode", 0.0f, 2.0f, 1.0f, 0.0f, "0 = smooth fBm, 1 = billow, 2 = ridged.");
			add_param(t, "contrast", "Contrast", 0.2f, 4.0f, 0.01f, 1.0f, "Output contrast.");
			add_param(t, "threshold", "Alpha threshold", 0.0f, 1.0f, 0.01f, 0.0f, "Values below become transparent.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_BULLET_HOLE, "Bullet Hole", "Decals", "Bullet hole decal: dark hole, chipped surface, cracks and soot. Surface: concrete, metal (bent rim) or wood (splinters).", 0.04f, 0.035f, 0.03f, 0.62f, 0.6f, 0.56f, "Hole / soot color", "Chipped surface color", false, false);
			add_param(t, "hole_size", "Hole size", 0.02f, 0.3f, 0.005f, 0.09f, "Radius of the dark hole.");
			add_param(t, "chip_size", "Chipped area", 0.0f, 0.9f, 0.01f, 0.4f, "Radius of the chipped, freshly exposed surface around the hole.");
			add_param(t, "cracks", "Cracks", 0.0f, 16.0f, 1.0f, 7.0f, "Number of radial cracks.");
			add_param(t, "crack_length", "Crack length", 0.0f, 1.0f, 0.01f, 0.65f, "How far the cracks reach.");
			add_param(t, "roughness", "Roughness", 0.0f, 1.0f, 0.01f, 0.6f, "Irregularity of the hole and the chipped edge.");
			add_param(t, "soot", "Soot", 0.0f, 1.0f, 0.01f, 0.45f, "Dark powder burn around the hole.");
			add_param(t, "surface", "Surface", 0.0f, 2.0f, 1.0f, 0.0f, "0 = concrete / plaster, 1 = metal (bent bright rim), 2 = wood (splinters along the grain).");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_SCORCH, "Scorch / Burn Mark", "Decals", "Explosion scorch with radial blast streaks, or a burn mark (streaks 0). Dark soot core fading to a burnt edge.", 0.03f, 0.025f, 0.02f, 0.26f, 0.16f, 0.09f, "Soot color", "Burnt edge color", false, false);
			add_param(t, "radius", "Radius", 0.2f, 0.95f, 0.01f, 0.72f, "Size of the mark.");
			add_param(t, "irregularity", "Irregularity", 0.0f, 1.0f, 0.01f, 0.5f, "Ragged outline.");
			add_param(t, "darkness", "Darkness", 0.0f, 1.0f, 0.01f, 0.85f, "Opacity of the soot core.");
			add_param(t, "streaks", "Blast streaks", 0.0f, 48.0f, 1.0f, 18.0f, "Radial streaks from the blast. 0 gives a burn mark.");
			add_param(t, "streak_strength", "Streak strength", 0.0f, 1.0f, 0.01f, 0.6f, "Length and opacity of the streaks.");
			add_param(t, "char_ring", "Char ring", 0.0f, 1.0f, 0.01f, 0.35f, "Darker charred ring inside the mark.");
			add_param(t, "detail", "Detail", 1.0f, 12.0f, 0.1f, 5.0f, "Scale of the soot noise.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_GLASS_CRACK, "Glass Crack", "Decals", "Bullet hit in glass: spider-web of radial and ring cracks around a crushed hole.", 0.92f, 0.96f, 1.0f, 0.8f, 0.86f, 0.92f, "Crack color", "Crushed glass color", false, false);
			add_param(t, "hole_size", "Hole size", 0.0f, 0.2f, 0.005f, 0.035f, "Radius of the hole (0 = only cracks).");
			add_param(t, "radial", "Radial cracks", 3.0f, 32.0f, 1.0f, 12.0f, "Number of cracks from the centre.");
			add_param(t, "rings", "Ring cracks", 0.0f, 8.0f, 1.0f, 4.0f, "Concentric cracks between the radial ones.");
			add_param(t, "reach", "Reach", 0.3f, 1.0f, 0.01f, 0.9f, "Length of the radial cracks.");
			add_param(t, "line_width", "Line width", 0.001f, 0.02f, 0.0005f, 0.004f, "Width of the crack lines.");
			add_param(t, "crushed", "Crushed zone", 0.0f, 1.0f, 0.01f, 0.5f, "Frosted crushed glass around the hole.");
			add_param(t, "jaggedness", "Jaggedness", 0.0f, 1.0f, 0.01f, 0.5f, "Wobble of the cracks.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_SPLATTER, "Splatter", "Decals", "Blood, oil or paint splatter: a ragged blob, droplets and elongated drops thrown in one direction (+X).", 0.18f, 0.0f, 0.0f, 0.45f, 0.02f, 0.02f, "Thick color", "Thin color", false, false);
			add_param(t, "size", "Size", 0.1f, 0.6f, 0.01f, 0.32f, "Radius of the main blob.");
			add_param(t, "droplets", "Droplets", 0.0f, 64.0f, 1.0f, 24.0f, "Number of droplets around the blob.");
			add_param(t, "spread", "Spread", 0.0f, 1.0f, 0.01f, 0.7f, "How far droplets fly.");
			add_param(t, "direction", "Direction", 0.0f, 1.0f, 0.01f, 0.45f, "Bias of the droplets and streaks towards +X (use Rotation to aim).");
			add_param(t, "raggedness", "Raggedness", 0.0f, 1.0f, 0.01f, 0.6f, "Irregular edge of the blob.");
			add_param(t, "variation", "Thickness variation", 0.0f, 1.0f, 0.01f, 0.5f, "Darker thick areas inside the splatter.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_CELL_NOISE, "Cell Noise", "Noise & Patterns", "Worley (Voronoi) cell noise: distance to the nearest cell, glowing cell edges or flat colored cells. For cracked ground, scales, energy grids and tiles. Supports seamless tiling.", 0.02f, 0.03f, 0.05f, 0.35f, 0.8f, 1.0f, "Low color", "High color", false, true);
			add_param(t, "scale", "Scale", 1.0f, 32.0f, 1.0f, 6.0f, "Number of cells across the texture.");
			add_param(t, "mode", "Mode", 0.0f, 2.0f, 1.0f, 1.0f, "0 = distance to the cell center (F1), 1 = cell edges (F2 - F1), 2 = flat cells.");
			add_param(t, "jitter", "Jitter", 0.0f, 1.0f, 0.01f, 0.9f, "Randomness of the cell centers (0 = square grid).");
			add_param(t, "edge_width", "Edge width", 0.01f, 0.5f, 0.01f, 0.08f, "Width of the cell edges (Edges mode).");
			add_param(t, "octaves", "Octaves", 1.0f, 4.0f, 1.0f, 1.0f, "Finer cell layers added on top.");
			add_param(t, "contrast", "Contrast", 0.2f, 4.0f, 0.01f, 1.0f, "Output contrast.");
			add_param(t, "threshold", "Alpha threshold", 0.0f, 1.0f, 0.01f, 0.0f, "Values below become transparent.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_MARBLE_NOISE, "Marble Noise", "Noise & Patterns", "Marble veins: a sine pattern bent by turbulent fractal noise. For stone, polished floors and flowing energy. Supports seamless tiling.", 0.85f, 0.84f, 0.82f, 0.25f, 0.24f, 0.26f, "Base color", "Vein color", false, true);
			add_param(t, "frequency", "Vein frequency", 1.0f, 16.0f, 1.0f, 4.0f, "Number of veins across the texture.");
			add_param(t, "turbulence", "Turbulence", 0.0f, 8.0f, 0.01f, 3.0f, "How strongly noise bends the veins.");
			add_param(t, "sharpness", "Sharpness", 0.2f, 8.0f, 0.01f, 2.0f, "Thinness of the veins.");
			add_param(t, "angle", "Direction", 0.0f, 180.0f, 1.0f, 30.0f, "Direction of the veins in degrees (snapped to a repeating direction when tiling).");
			add_param(t, "octaves", "Octaves", 1.0f, 8.0f, 1.0f, 5.0f, "Detail of the turbulence.");
			add_param(t, "contrast", "Contrast", 0.2f, 4.0f, 0.01f, 1.0f, "Output contrast.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_DEBRIS, "Debris Chunk", "Shapes", "Rock, concrete or brick chip for impact debris: an irregular faceted chunk with bevelled, lit edges and grainy surface. In a sheet every frame is a different chip (use a 4x4 sheet with a random frame per particle).", 0.2f, 0.19f, 0.18f, 0.74f, 0.72f, 0.68f, "Shadow color", "Lit color", false, false);
			add_param(t, "size", "Size", 0.3f, 0.95f, 0.01f, 0.82f, "Size of the chip. Stays inside the frame.");
			add_param(t, "corners", "Corners", 4.0f, 9.0f, 1.0f, 6.0f, "Average number of corners of the outline.");
			add_param(t, "irregularity", "Irregularity", 0.0f, 1.0f, 0.01f, 0.75f, "How uneven the corners and the outline are.");
			add_param(t, "elongation", "Elongation", 0.0f, 1.0f, 0.01f, 0.45f, "How long and thin the chips can be.");
			add_param(t, "bevel", "Bevel", 0.0f, 1.0f, 0.01f, 0.5f, "Width of the lit and shadowed edge facets.");
			add_param(t, "grain", "Grain", 0.0f, 1.0f, 0.01f, 0.5f, "Grainy, pitted surface detail.");
			add_param(t, "light_angle", "Light angle", -180.0f, 180.0f, 1.0f, 120.0f, "Direction of the key light.");
			add_param(t, "shading", "Shading", 0.0f, 1.0f, 0.01f, 0.75f, "Contrast between lit and shadowed facets.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_WATER_SPLASH, "Water Splash", "Shapes", "Splash of a drop hitting water or a wet floor, seen from the side: a thin crown with a lip and fingers, droplets flying off, an optional jet in the middle and a spreading puddle. In a sheet each row is one splash animation and the columns are its frames (8 columns x 4 rows: 4 random variants). Alpha is the water, colour its glint.", 0.62f, 0.74f, 0.86f, 1.0f, 1.0f, 1.0f, "Water color", "Glint color", false, false);
			add_param(t, "frames", "Frames per splash", 2.0f, 16.0f, 1.0f, 8.0f, "Sheet columns: frames of one splash animation (the life cycle).");
			add_param(t, "variants", "Variants", 1.0f, 16.0f, 1.0f, 4.0f, "Sheet rows: different random splashes.");
			add_param(t, "crown_height", "Crown height", 0.1f, 1.0f, 0.01f, 0.6f, "Height of the crown sheet.");
			add_param(t, "crown_width", "Crown width", 0.1f, 0.9f, 0.01f, 0.5f, "Radius of the crown lip.");
			add_param(t, "fingers", "Fingers", 0.0f, 16.0f, 1.0f, 9.0f, "Thin fingers on the crown lip that throw droplets.");
			add_param(t, "droplets", "Droplets", 0.0f, 24.0f, 1.0f, 12.0f, "Droplets flying away from the splash.");
			add_param(t, "jet", "Centre jet", 0.0f, 1.0f, 0.01f, 0.5f, "Share of variants with a column shooting up in the middle.");
			add_param(t, "sheet", "Sheet opacity", 0.0f, 1.0f, 0.01f, 0.25f, "Opacity of the thin crown wall between its edges.");
			add_param(t, "puddle", "Puddle", 0.0f, 1.0f, 0.01f, 0.6f, "Spreading flat ring of water on the ground.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_WATER_PLUME, "Water Plume", "Shapes", "Column of water thrown up by an explosion, seen from the side: dense frothy water with vertical streaks opening at the top into a crown of lumps and spray. A still image: particles grow it, lift it and let it fall. Alpha is the water, colour its light from the shadowed to the lit side.", 0.55f, 0.64f, 0.72f, 1.0f, 1.0f, 1.0f, "Shadow color", "Lit color", false, false);
			add_param(t, "width", "Column width", 0.08f, 0.6f, 0.01f, 0.26f, "Half width of the column at its base.");
			add_param(t, "flare", "Top flare", 0.0f, 1.5f, 0.01f, 0.75f, "How much the column opens out at the top.");
			add_param(t, "height", "Height", 0.3f, 1.0f, 0.01f, 0.88f, "Height of the column in the image.");
			add_param(t, "lumps", "Lumps", 0.0f, 1.0f, 0.01f, 0.6f, "Lumpy, uneven outline and crown.");
			add_param(t, "streaks", "Streaks", 0.0f, 1.0f, 0.01f, 0.55f, "Vertical streaks of water running up the column.");
			add_param(t, "spray", "Spray", 0.0f, 1.0f, 0.01f, 0.6f, "Droplets and spray around the crown.");
			add_param(t, "density", "Density", 0.2f, 1.0f, 0.01f, 0.9f, "Opacity of the water body.");
			add_param(t, "holes", "Holes", 0.0f, 1.0f, 0.01f, 0.45f, "Gaps tearing open in the upper part.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_FOAM, "Foam", "Shapes", "Foam on water seen from above: a patch or ring of bubbles with a torn, uneven edge and holes. Lay it flat on the surface and let it spread and fade. Alpha is the foam, colour its brightness.", 0.7f, 0.78f, 0.84f, 1.0f, 1.0f, 1.0f, "Thin foam color", "Thick foam color", false, false);
			add_param(t, "radius", "Radius", 0.2f, 1.0f, 0.01f, 0.85f, "Size of the patch.");
			add_param(t, "ring", "Ring", 0.0f, 1.0f, 0.01f, 0.0f, "0 = full patch, 1 = thin ring with an open middle.");
			add_param(t, "bubbles", "Bubble size", 0.0f, 1.0f, 0.01f, 0.5f, "Size of the foam bubbles (small to large).");
			add_param(t, "breakup", "Breakup", 0.0f, 1.0f, 0.01f, 0.6f, "Holes and a torn, uneven edge.");
			add_param(t, "softness", "Edge softness", 0.0f, 1.0f, 0.01f, 0.5f, "How softly the foam thins out at the edge.");
			add_param(t, "streaks", "Streaks", 0.0f, 1.0f, 0.01f, 0.35f, "Foam stretched outward in radial streaks.");
			push(t);
		}
		{
			TypeInfo t = make_type(TYPE_UNDERWATER_BUBBLE, "Underwater Bubble", "Shapes", "One air bubble under water: a clear sphere with a bright rim, a sharp highlight on top, light coming through from below and a slight wobble. In a sheet every frame is a differently squashed bubble, so particles can pick a random one.", 0.55f, 0.7f, 0.8f, 1.0f, 1.0f, 1.0f, "Rim color", "Highlight color", false, false);
			add_param(t, "rim", "Rim", 0.0f, 1.0f, 0.01f, 0.6f, "Brightness of the bright edge.");
			add_param(t, "rim_width", "Rim width", 0.02f, 0.4f, 0.01f, 0.14f, "Width of the edge.");
			add_param(t, "highlight", "Highlight", 0.0f, 1.0f, 0.01f, 1.0f, "Sharp reflection of the light above.");
			add_param(t, "highlight_size", "Highlight size", 0.05f, 0.5f, 0.01f, 0.22f, "Size of the reflection.");
			add_param(t, "inner_light", "Inner light", 0.0f, 1.0f, 0.01f, 0.4f, "Light coming through the bubble from below.");
			add_param(t, "body", "Body", 0.0f, 1.0f, 0.01f, 0.25f, "Opacity of the clear middle.");
			add_param(t, "wobble", "Wobble", 0.0f, 1.0f, 0.01f, 0.25f, "Squashed, uneven shape of rising bubbles.");
			push(t);
		}
		if (i != TYPE_COUNT) {
			std::abort(); // TYPE_COUNT is larger than the number of registered types.
		}
	}
};

const TypeTable &type_table() {
	static TypeTable table;
	return table;
}

/* Frame setup ------------------------------------------------------------ */

struct Segment {
	float ax, ay, bx, by;
	float width; // Relative width (1 = main bolt).
};

constexpr int MAX_SEGMENTS = 4096; // 16 arcs x (128 main + 32 branch) segments, with headroom.

struct FrameContext {
	const Settings *s = nullptr;
	const TypeInfo *info = nullptr;
	V3 color_a;
	V3 color_b;
	float t = 0.0f; // time * speed
	V3 light_dir;
	// Precomputed bolt geometry for lightning and arcs.
	Segment segments[MAX_SEGMENTS];
	int segment_count = 0;
	// Bokeh disks.
	float disks[64][4];
	int disk_count = 0;
};

inline float param(const FrameContext &c, int p_index) {
	return c.s->params[p_index];
}

struct Rng {
	uint32_t state;
	explicit Rng(uint32_t p_seed) :
			state(mix_u32(p_seed ^ 0xa511e9b3u)) {}
	float next() {
		state = mix_u32(state + 0x9e3779b9u);
		return hash01(state);
	}
	float range(float a, float b) { return a + (b - a) * next(); }
};

void add_segment(FrameContext &c, float ax, float ay, float bx, float by, float w) {
	if (c.segment_count >= MAX_SEGMENTS) {
		return;
	}
	c.segments[c.segment_count++] = { ax, ay, bx, by, w };
}

// Midpoint displacement bolt between two points. Returns the polyline in r_points.
int build_bolt(Rng &rng, float ax, float ay, float bx, float by, float jag, int levels, float *r_points, int max_points) {
	int count = 2;
	r_points[0] = ax;
	r_points[1] = ay;
	r_points[2] = bx;
	r_points[3] = by;
	float amp = jag * len2(bx - ax, by - ay) * 0.22f;
	static thread_local float tmp[2 * 1100];
	for (int level = 0; level < levels && count * 2 - 1 <= max_points && count * 2 - 1 <= 1024; level++) {
		int out = 0;
		for (int i = 0; i < count - 1; i++) {
			const float x0 = r_points[i * 2];
			const float y0 = r_points[i * 2 + 1];
			const float x1 = r_points[i * 2 + 2];
			const float y1 = r_points[i * 2 + 3];
			const float dx = x1 - x0;
			const float dy = y1 - y0;
			const float l = maxf(len2(dx, dy), 1e-5f);
			const float off = rng.range(-1.0f, 1.0f) * amp;
			tmp[out * 2] = x0;
			tmp[out * 2 + 1] = y0;
			out++;
			tmp[out * 2] = (x0 + x1) * 0.5f - dy / l * off;
			tmp[out * 2 + 1] = (y0 + y1) * 0.5f + dx / l * off;
			out++;
		}
		tmp[out * 2] = r_points[(count - 1) * 2];
		tmp[out * 2 + 1] = r_points[(count - 1) * 2 + 1];
		out++;
		std::memcpy(r_points, tmp, sizeof(float) * 2 * out);
		count = out;
		amp *= 0.55f;
	}
	return count;
}

void add_bolt_with_branches(FrameContext &c, Rng &rng, float ax, float ay, float bx, float by, float jag, int branches, float width) {
	static thread_local float points[2 * 1100];
	const int count = build_bolt(rng, ax, ay, bx, by, jag, 7, points, 1024);
	for (int i = 0; i < count - 1; i++) {
		const float t = float(i) / float(count - 1);
		add_segment(c, points[i * 2], points[i * 2 + 1], points[i * 2 + 2], points[i * 2 + 3], width * (1.0f - t * 0.45f));
	}
	// Branches fork from random points on the main bolt.
	static thread_local float branch_points[2 * 1100];
	for (int b = 0; b < branches; b++) {
		const int start = int(rng.range(0.15f, 0.8f) * float(count - 1));
		const float sx = points[start * 2];
		const float sy = points[start * 2 + 1];
		const float dir_x = bx - ax;
		const float dir_y = by - ay;
		const float main_len = len2(dir_x, dir_y);
		const float angle = std::atan2(dir_y, dir_x) + rng.range(0.35f, 0.9f) * (rng.next() > 0.5f ? 1.0f : -1.0f);
		const float blen = main_len * rng.range(0.15f, 0.38f);
		const float ex = sx + std::cos(angle) * blen;
		const float ey = sy + std::sin(angle) * blen;
		const int bcount = build_bolt(rng, sx, sy, ex, ey, jag * 1.1f, 5, branch_points, 1024);
		for (int i = 0; i < bcount - 1; i++) {
			const float t = float(i) / float(bcount - 1);
			add_segment(c, branch_points[i * 2], branch_points[i * 2 + 1], branch_points[i * 2 + 2], branch_points[i * 2 + 3], width * 0.5f * (1.0f - t * 0.8f));
		}
	}
}

void setup_frame(FrameContext &c, const Settings &s) {
	c.s = &s;
	c.info = get_type_info(s.type);
	c.color_a = to_v3(s.color_a);
	c.color_b = to_v3(s.color_b);
	c.t = s.time * s.speed;
	c.segment_count = 0;
	c.disk_count = 0;
	if (!c.info) {
		return;
	}
	const int light_index = find_param(c.info, "light_angle");
	const float light_angle = light_index >= 0 ? s.params[light_index] * PI / 180.0f : 2.0f;
	c.light_dir = normalize3(V3(std::cos(light_angle), std::sin(light_angle), 0.2f));

	if (s.type == TYPE_LIGHTNING) {
		const float rate = param(c, 5);
		const uint32_t step = rate > 0.0f ? uint32_t(std::floor(c.t * rate)) : 0u;
		Rng rng(s.seed * 7919u + step * 104729u);
		const float jag = param(c, 0);
		const int branches = int(param(c, 1) + 0.5f);
		const float x0 = rng.range(-0.15f, 0.15f);
		const float x1 = rng.range(-0.25f, 0.25f);
		add_bolt_with_branches(c, rng, x0, 0.95f, x1, -0.95f, jag, branches, 1.0f);
	} else if (s.type == TYPE_ELECTRIC_ARC) {
		const float rate = param(c, 6);
		const uint32_t step = rate > 0.0f ? uint32_t(std::floor(c.t * rate)) : 0u;
		Rng rng(s.seed * 6151u + step * 92821u);
		const int arcs = int(param(c, 0) + 0.5f);
		const float reach = param(c, 1);
		const float jag = param(c, 2);
		for (int a = 0; a < arcs; a++) {
			const float angle = (float(a) + rng.range(-0.35f, 0.35f)) / float(arcs) * TAU;
			const float len = reach * rng.range(0.6f, 1.0f);
			add_bolt_with_branches(c, rng, 0.0f, 0.0f, std::cos(angle) * len, std::sin(angle) * len, jag, 1, 0.85f);
		}
	} else if (s.type == TYPE_BUBBLES) {
		Rng rng(s.seed * 3571u + 17u);
		const int count = int(clampf(param(c, 0), 1.0f, 64.0f));
		const float size = param(c, 1);
		const float var = param(c, 2);
		for (int i = 0; i < count; i++) {
			const float r = size * (1.0f - var * 0.75f * rng.next());
			const float a = rng.next() * TAU;
			const float d = std::sqrt(rng.next()) * (0.92f - r);
			const float drift = c.t * 0.05f * rng.range(-1.0f, 1.0f);
			c.disks[i][0] = std::cos(a + drift) * d;
			c.disks[i][1] = std::sin(a + drift) * d;
			c.disks[i][2] = r;
			c.disks[i][3] = rng.range(0.55f, 1.0f);
		}
		c.disk_count = count;
	} else if (s.type == TYPE_SPLATTER) {
		Rng rng(s.seed * 2903u + 5u);
		const float size = param(c, 0);
		const int count = int(clampf(param(c, 1), 0.0f, 64.0f));
		const float spread = param(c, 2);
		const float bias = param(c, 3);
		for (int i = 0; i < count; i++) {
			// Biased towards +X: blend a random angle towards 0.
			float a = rng.range(-PI, PI) * (1.0f - bias * 0.7f);
			const float dist = size * (0.9f + spread * rng.next() * 1.8f);
			const float far = saturate((dist - size) / maxf(spread * size * 1.8f, 1e-3f));
			const float radius = size * (0.03f + 0.17f * rng.next()) * (1.0f - 0.6f * far);
			c.disks[i][0] = std::cos(a) * dist;
			c.disks[i][1] = std::sin(a) * dist;
			c.disks[i][2] = radius;
			// Far drops are stretched along their flight direction.
			c.disks[i][3] = 1.0f + far * (1.0f + 2.5f * bias) * rng.next();
			if (std::fabs(c.disks[i][0]) > 0.97f || std::fabs(c.disks[i][1]) > 0.97f) {
				c.disks[i][2] = 0.0f;
			}
		}
		c.disk_count = count;
	}
}

/* Volumetric media ------------------------------------------------------- */

struct MediumSample {
	float density = 0.0f;
	float heat = 0.0f; // 0..1 temperature (emission).
	float soot = 0.0f; // 0..1 darkening of the scattering albedo.
};

V3 seed_offset(uint32_t seed) {
	return V3(hash01(mix_u32(seed * 3u + 1u)) * 97.0f, hash01(mix_u32(seed * 3u + 2u)) * 97.0f, hash01(mix_u32(seed * 3u + 3u)) * 97.0f);
}

struct ExplosionShape {
	float radius;
	float billows;
	float scale;
	float gain;
	float fire;
	float smoke;
	float density;
	float life_fade;
	float rise;
	float ring_thickness; // > 0 for the ring variant.
	V3 offset;
	float t;
	int octaves;
};

ExplosionShape make_explosion_shape(const FrameContext &c, bool p_ring) {
	const Settings &s = *c.s;
	ExplosionShape e;
	e.t = c.t;
	e.offset = seed_offset(s.seed);
	e.octaves = s.quality > 0 ? 6 : 4;
	float lifecycle;
	if (p_ring) {
		e.radius = param(c, 0);
		e.ring_thickness = param(c, 1);
		e.billows = param(c, 2);
		e.scale = param(c, 3);
		e.gain = 0.52f;
		e.fire = param(c, 4);
		e.smoke = param(c, 5);
		e.density = param(c, 6);
		lifecycle = param(c, 8);
	} else {
		e.radius = param(c, 0);
		e.ring_thickness = 0.0f;
		e.billows = param(c, 1);
		e.scale = param(c, 2);
		e.gain = param(c, 3);
		e.fire = param(c, 4);
		e.smoke = param(c, 5);
		e.density = param(c, 6);
		lifecycle = param(c, 8);
	}
	e.life_fade = 1.0f;
	e.rise = 0.0f;
	if (lifecycle > 0.0f) {
		const float life = saturate(s.life);
		const float expand = 1.0f - std::pow(1.0f - life, 2.6f);
		e.radius *= mixf(1.0f, 0.3f + 0.7f * expand, lifecycle);
		if (p_ring) {
			e.ring_thickness *= mixf(1.0f, 0.6f + 0.6f * expand, lifecycle);
		}
		// Burn for most of the first half, then cool into smoke.
		const float cooling = std::pow(1.0f - life, 0.8f);
		e.fire *= mixf(1.0f, minf(1.0f, cooling * 1.6f), lifecycle);
		e.smoke = mixf(e.smoke, minf(1.0f, e.smoke + std::pow(life, 1.5f) * 0.75f), lifecycle);
		e.life_fade = mixf(1.0f, 1.0f - smoothstepf(0.7f, 1.0f, life), lifecycle);
		e.rise = life * 0.12f * lifecycle;
	}
	return e;
}

inline float explosion_bound(const ExplosionShape &e) {
	if (e.ring_thickness > 0.0f) {
		return e.radius + e.ring_thickness * (1.0f + e.billows) + 0.05f;
	}
	return e.radius * (1.0f + e.billows * 0.55f) + 0.05f;
}

MediumSample sample_explosion(const ExplosionShape &e, V3 p, bool p_cheap) {
	MediumSample m;
	p.y -= e.rise;
	float r;
	float base_dist; // Signed distance to the undisplaced shape, in radius units.
	V3 dir;
	float size;
	if (e.ring_thickness > 0.0f) {
		const float rxy = len2(p.x, p.y);
		const float tube_x = rxy - e.radius;
		r = len2(tube_x, p.z);
		size = e.ring_thickness;
		base_dist = r - size;
		const float inv = rxy > 1e-5f ? 1.0f / rxy : 0.0f;
		dir = normalize3(V3(p.x * inv * tube_x, p.y * inv * tube_x, p.z));
	} else {
		r = length3(p);
		size = e.radius;
		base_dist = r - size;
		dir = r > 1e-5f ? p * (1.0f / r) : V3(0.0f, 1.0f, 0.0f);
	}
	if (base_dist > size * e.billows * 0.6f + 0.04f) {
		return m;
	}
	// Billows roll outwards over time: the noise domain flows along the normal.
	// Rotate the domain off the lattice axes so low octaves never read as boxy.
	const V3 q = octave_rotate(p * e.scale - dir * (e.t * 0.35f) + V3(0.0f, -e.t * 0.08f, 0.0f)) * (1.0f / 2.03f) + e.offset;
	const int octaves = p_cheap ? 3 : e.octaves;
	V3 qw = q;
	if (!p_cheap) {
		const float wx = fbm(q * 0.5f + V3(11.3f, 0.0f, 0.0f), 2, 0.5f, 7u);
		const float wy = fbm(q * 0.5f + V3(0.0f, 23.7f, 0.0f), 2, 0.5f, 8u);
		const float wz = fbm(q * 0.5f + V3(0.0f, 0.0f, 31.1f), 2, 0.5f, 9u);
		qw = q + V3(wx, wy, wz) * 0.45f;
	}
	const float b = billow(qw, octaves, e.gain * 0.9f, 21u);
	// Displace the surface outwards by the billows -> rounded cauliflower lumps.
	const float displaced = base_dist - size * e.billows * (b - 0.18f) * 1.1f;
	const float edge = size * 0.035f + 0.008f;
	float density = smoothstepf(edge, -edge, displaced);
	if (density <= 0.0f) {
		return m;
	}
	// Interior density variation keeps the medium from looking like a solid.
	density *= 0.7f + 0.6f * b;
	m.density = density * e.density * e.life_fade;
	// Temperature: hottest in the core, breaking up along noise.
	const float depth = saturate(-displaced / (size * 0.6f));
	if (p_cheap) {
		return m;
	}
	// Burning pockets: low frequency noise decides where the surface still burns
	// and where it has cooled into soot.
	const float pockets = fbm(q * 0.55f + V3(5.1f, 9.2f, 1.7f), 3, 0.5f, 33u);
	const float fire = minf(e.fire, 1.0f);
	// Crevices between billows (low billow values) are cooler and darker.
	float heat = 0.22f + depth * 0.75f + pockets * 1.2f + (b - 0.32f) * 1.6f;
	heat = heat * fire + maxf(e.fire - 1.0f, 0.0f) * 0.5f - (1.0f - fire) * 0.35f;
	m.heat = saturate(heat);
	// Soot gathers in cooled pockets on the outside and on top.
	const float up = saturate(dir.y * 0.5f + 0.5f);
	m.soot = saturate((0.55f - pockets * 1.4f - depth * 0.8f + up * 0.35f) * e.smoke * 2.0f) * (1.0f - m.heat * 0.8f);
	return m;
}

struct FireShape {
	float base_y;
	float top_y;
	float width;
	float turbulence;
	float scale;
	float rise;
	float intensity;
	float core;
	float smoke;
	V3 offset;
	float t;
	int octaves;
};

FireShape make_fire_shape(const FrameContext &c) {
	FireShape f;
	f.base_y = -0.88f;
	f.top_y = f.base_y + 1.5f * param(c, 0);
	f.width = param(c, 1);
	f.turbulence = param(c, 2);
	f.scale = param(c, 3);
	f.rise = param(c, 4);
	f.intensity = param(c, 5);
	f.core = param(c, 6);
	f.smoke = param(c, 7);
	f.offset = seed_offset(c.s->seed);
	f.t = c.t;
	f.octaves = c.s->quality > 0 ? 6 : 4;
	return f;
}

MediumSample sample_fire(const FireShape &f, const V3 &p) {
	MediumSample m;
	const float span = f.top_y - f.base_y;
	const float h = (p.y - f.base_y) / span;
	if (h < -0.15f || h > 1.3f) {
		return m;
	}
	const float hc = saturate(h);
	// Buoyant flow: the noise rises over time and is stretched vertically, so
	// structures read as licking tongues rather than round blobs.
	const V3 q = V3(p.x * f.scale, (p.y - f.t * f.rise * 0.5f) * f.scale * 0.55f, p.z * f.scale) + f.offset;
	const float sway = fbm(V3(q.x * 0.3f, q.y * 0.35f, f.t * 0.25f + f.offset.z), 2, 0.5f, 3u);
	const float px = p.x - sway * f.turbulence * 0.3f * hc * hc;
	// Column profile with a rounded base; the taper comes from the height term.
	const float base_round = std::sqrt(saturate(h / 0.12f + 0.25f));
	const float profile = f.width * base_round * (1.0f - hc * 0.35f) + 0.004f;
	const float radial = len2(px, p.z) / profile;
	const float n = fbm(q, f.octaves, 0.55f, 11u);
	const float tear = f.turbulence * (0.35f + 1.3f * hc);
	// Rounded, slightly ragged bottom instead of a straight cut.
	const float base_h = h + minf(radial, 1.5f) * minf(radial, 1.5f) * 0.09f + n * 0.05f;
	const float base_fade = smoothstepf(0.12f, -0.1f, base_h) * 1.4f;
	const float field = 1.0f - radial * 0.75f - hc * 0.8f + n * tear - base_fade;
	if (field <= -0.15f) {
		if (f.smoke > 0.0f && h > 0.5f) {
			// Thin soot above the tips.
			const float smoke_field = 1.0f - len2(px, p.z) / (f.width * 1.4f) + n * 0.8f;
			m.density = saturate(smoke_field) * f.smoke * smoothstepf(0.5f, 1.0f, h) * 0.6f;
			m.soot = 1.0f;
		}
		return m;
	}
	// Never touch the frame border: particle sprites must fade out before it.
	m.density = smoothstepf(-0.15f, 0.3f, field) * smoothstepf(0.99f, 0.8f, p.y);
	// Temperature: hottest in a small core near the base, cooling outwards and up.
	const float core_size = 0.5f + (1.0f - f.core) * 0.8f;
	m.heat = saturate(field / core_size) * (0.92f - hc * 0.4f);
	m.soot = f.smoke * smoothstepf(0.5f, 1.0f, h) * (1.0f - m.heat);
	return m;
}

struct SmokeShape {
	float radius;
	float billows;
	float scale;
	float gain;
	float density;
	float softness;
	float flat_base; // Cloud only.
	bool cloud;
	V3 offset;
	float t;
	int octaves;
	V3 lobes[6];
	float lobe_radius[6];
	int lobe_count;
};

SmokeShape make_smoke_shape(const FrameContext &c, bool p_cloud) {
	SmokeShape sm;
	sm.cloud = p_cloud;
	sm.offset = seed_offset(c.s->seed);
	sm.t = c.t;
	sm.octaves = c.s->quality > 0 ? 6 : 4;
	sm.lobe_count = 0;
	if (p_cloud) {
		sm.radius = param(c, 0);
		sm.billows = param(c, 1);
		sm.scale = param(c, 2);
		sm.gain = 0.52f;
		sm.density = param(c, 3);
		sm.flat_base = param(c, 4);
		sm.softness = 0.35f;
		Rng rng(c.s->seed * 1543u + 5u);
		sm.lobe_count = 5;
		for (int i = 0; i < sm.lobe_count; i++) {
			const float x = (float(i) / float(sm.lobe_count - 1) - 0.5f) * 1.1f * sm.radius + rng.range(-0.06f, 0.06f);
			const float center = 1.0f - std::fabs(float(i) / float(sm.lobe_count - 1) - 0.5f) * 2.0f;
			const float r = sm.radius * (0.32f + 0.22f * center) * rng.range(0.85f, 1.1f);
			sm.lobes[i] = V3(x, -0.15f * sm.radius + r * 0.55f, rng.range(-0.08f, 0.08f));
			sm.lobe_radius[i] = r;
		}
	} else {
		sm.radius = param(c, 0);
		sm.billows = param(c, 1);
		sm.scale = param(c, 2);
		sm.gain = param(c, 3);
		sm.density = param(c, 4);
		sm.softness = param(c, 5);
		sm.flat_base = 0.0f;
	}
	return sm;
}

inline float smoke_bound(const SmokeShape &sm) {
	return sm.radius * (1.0f + sm.billows * 0.6f) + 0.08f;
}

MediumSample sample_smoke(const SmokeShape &sm, const V3 &p, bool p_cheap) {
	MediumSample m;
	float base_dist;
	float size;
	if (sm.cloud) {
		// Smooth union of lobes.
		float acc = 0.0f;
		for (int i = 0; i < sm.lobe_count; i++) {
			const V3 d = p - sm.lobes[i];
			const float rr = sm.lobe_radius[i];
			acc += std::exp(-dot3(d, d) / (rr * rr) * 1.6f);
		}
		size = sm.radius * 0.4f;
		base_dist = (0.45f - acc) * size * 1.6f;
	} else {
		size = sm.radius;
		base_dist = length3(p) - size;
	}
	if (base_dist > size * sm.billows * 0.7f + 0.06f) {
		return m;
	}
	const V3 q = octave_rotate(p * sm.scale + V3(0.0f, -sm.t * 0.12f, sm.t * 0.05f)) * (1.0f / 2.03f) + sm.offset;
	V3 qw = q;
	if (!p_cheap) {
		const float wx = fbm(q * 0.6f + V3(3.3f, 0.0f, 0.0f), 2, 0.5f, 41u);
		const float wy = fbm(q * 0.6f + V3(0.0f, 7.7f, 0.0f), 2, 0.5f, 42u);
		const float wz = fbm(q * 0.6f + V3(0.0f, 0.0f, 5.5f), 2, 0.5f, 43u);
		qw = q + V3(wx, wy, wz) * 0.5f;
	}
	const float b = billow(qw, p_cheap ? 3 : sm.octaves - 1, sm.gain * 0.85f, 51u);
	const float displaced = base_dist - size * sm.billows * (b - 0.2f) * 1.1f;
	const float edge = size * (0.05f + 0.3f * sm.softness) + 0.01f;
	float density = smoothstepf(edge, -edge * 0.6f, displaced);
	if (density <= 0.0f) {
		return m;
	}
	// Wispy interior: erode low density with fine noise.
	density *= saturate(0.35f + 1.2f * b);
	if (sm.cloud && sm.flat_base > 0.0f) {
		const float base_y = -0.15f * sm.radius;
		density *= mixf(1.0f, smoothstepf(base_y - 0.02f, base_y + 0.12f, p.y), sm.flat_base);
	}
	m.density = density * sm.density;
	return m;
}

// Ray marches a medium through the pixel (orthographic, viewer at +Z).
template <typename SampleFunc>
void march_medium(float *r_px, float x, float y, float bound, int steps, const FrameContext &c, SampleFunc sample, bool emissive, float shadow_strength, float ambient, float sigma_scale, const V3 &shadow_color, const V3 &lit_color) {
	const float r2 = x * x + y * y;
	if (r2 >= bound * bound) {
		r_px[0] = r_px[1] = r_px[2] = r_px[3] = 0.0f;
		return;
	}
	const float half = std::sqrt(bound * bound - r2);
	const float dt = 2.0f * bound / float(steps);
	// Sample on depth slices shared by every pixel (no per-pixel jitter). Random
	// jitter hides banding but shows up as flickering grain in the preview.
	float z = bound - dt * (std::floor((bound - half) / dt) + 0.5f);
	float transmittance = 1.0f;
	V3 radiance;
	const float shadow_step = 0.06f;
	const int shadow_steps = c.s->quality > 0 ? 5 : 3;
	const V3 &L = c.light_dir;
	// Accumulates one sample covering p_len along the ray. Returns true once opaque.
	auto integrate = [&](const V3 &p, const MediumSample &m, float p_len) -> bool {
		const float sigma = m.density * sigma_scale;
		const float step_t = std::exp(-sigma * p_len);
		// Light reaching this point from the key light.
		float light = 1.0f;
		if (shadow_strength > 0.0f) {
			float od = 0.0f;
			for (int i = 1; i <= shadow_steps; i++) {
				const MediumSample sm = sample(p + L * (shadow_step * float(i)), true);
				od += sm.density;
			}
			light = std::exp(-od * sigma_scale * shadow_step * shadow_strength);
		}
		const float scatter_light = ambient + (1.0f - ambient) * light;
		V3 albedo = mix3(shadow_color, lit_color, scatter_light);
		if (emissive) {
			albedo = albedo * (1.0f - m.soot * 0.85f);
		}
		V3 contribution = albedo * (1.0f - step_t);
		if (emissive && m.heat > 0.0f) {
			// Emission rises steeply with temperature for a punchy blackbody falloff.
			const float h = m.heat;
			const float h2 = h * h;
			const V3 e = heat_ramp(h, c.color_a, c.color_b) * (h * 1.2f + h2 * h2 * 4.0f);
			contribution += e * (1.0f - step_t) * (1.0f - m.soot * 0.7f);
		}
		radiance += contribution * transmittance;
		transmittance *= step_t;
		if (transmittance < 0.004f) {
			transmittance = 0.0f;
			return true;
		}
		return false;
	};
	float previous_density = 0.0f;
	bool opaque = false;
	while (z > -half && !opaque) {
		const V3 p(x, y, z);
		const MediumSample m = sample(p, false);
		// Where the density changes sharply (entering a billow surface) the
		// shared depth slices would show as contour bands. Refine that interval
		// with four sub-steps instead.
		// In dense media keep each sub-step's optical depth small, otherwise the
		// nearest slice dominates and its cut through the field reads as rings.
		int substeps = std::fabs(m.density - previous_density) > 0.2f ? 4 : 1;
		if (m.density > 0.001f) {
			const int optical = int(std::ceil(m.density * sigma_scale * dt / 0.25f));
			substeps = optical > substeps ? optical : substeps;
		}
		substeps = substeps > 8 ? 8 : substeps;
		if (substeps > 1) {
			const float sub = dt / float(substeps);
			for (int k = 0; k < substeps && !opaque; k++) {
				const V3 pk(x, y, z + dt * 0.5f - sub * (float(k) + 0.5f));
				const MediumSample mk = sample(pk, false);
				if (mk.density > 0.001f) {
					opaque = integrate(pk, mk, sub);
				}
			}
		} else if (m.density > 0.001f) {
			opaque = integrate(p, m, dt);
		}
		previous_density = m.density;
		z -= dt;
	}
	write_pixel(r_px, radiance, 1.0f - transmittance, *c.s);
}

void render_explosion(const FrameContext &c, float x, float y, float *r_px, bool p_ring) {
	const ExplosionShape e = make_explosion_shape(c, p_ring);
	const int steps = c.s->quality > 0 ? 96 : 48;
	// Smoke is lit by the key light and by the fire inside it.
	// Smoke is lit mostly by the fire inside it, so its lit colour is warm.
	const V3 smoke_dark(0.015f, 0.012f, 0.011f);
	// Once the fire cools down the smoke is lit by the neutral key light instead.
	const V3 smoke_lit = mix3(V3(0.5f, 0.49f, 0.47f), c.color_a * 0.22f + V3(0.03f, 0.03f, 0.03f), saturate(e.fire));
	march_medium(r_px, x, y, explosion_bound(e) + e.rise, steps, c, [&](const V3 &p, bool cheap) { return sample_explosion(e, p, cheap); }, true, 1.0f, 0.3f, 22.0f, smoke_dark, smoke_lit);
}

void render_fire(const FrameContext &c, float x, float y, float *r_px) {
	const FireShape f = make_fire_shape(c);
	const float half_w = f.width * 1.6f + 0.1f;
	if (std::fabs(x) > half_w + 0.3f) {
		r_px[0] = r_px[1] = r_px[2] = r_px[3] = 0.0f;
		return;
	}
	const int steps = c.s->quality > 0 ? 64 : 32;
	const float dt = 2.0f * half_w / float(steps);
	float z = half_w - dt * 0.5f;
	float transmittance = 1.0f;
	V3 radiance;
	const V3 soot_color(0.03f, 0.028f, 0.026f);
	while (z > -half_w) {
		const MediumSample m = sample_fire(f, V3(x, y, z));
		if (m.density > 0.001f) {
			// Flames are mostly emissive; only soot absorbs noticeably.
			const float sigma = m.density * (0.6f + m.soot * 14.0f);
			const float step_t = std::exp(-sigma * dt);
			const float h = m.heat;
			const V3 e = heat_ramp(h, c.color_a, c.color_b) * ((h * h * h * 2.2f + h * 0.5f + 0.06f) * f.intensity * m.density * (1.0f - m.soot));
			radiance += (e * dt * 7.0f + soot_color * (1.0f - step_t)) * transmittance;
			transmittance *= step_t;
		}
		z -= dt;
	}
	write_pixel(r_px, radiance, 1.0f - transmittance, *c.s);
}

void render_smoke(const FrameContext &c, float x, float y, float *r_px, bool p_cloud) {
	const SmokeShape sm = make_smoke_shape(c, p_cloud);
	const int steps = c.s->quality > 0 ? 80 : 40;
	const float shadow = p_cloud ? param(c, 6) : param(c, 7);
	const float ambient = p_cloud ? param(c, 7) : param(c, 8);
	march_medium(r_px, x, y, smoke_bound(sm) + (p_cloud ? 0.25f : 0.0f), steps, c, [&](const V3 &p, bool cheap) { return sample_smoke(sm, p, cheap); }, false, shadow * (p_cloud ? 0.55f : 0.9f), ambient, 28.0f, c.color_a, c.color_b);
}

/* 2D emissive types ------------------------------------------------------ */

inline float seg_dist(float px, float py, const Segment &s, float &r_t) {
	const float vx = s.bx - s.ax;
	const float vy = s.by - s.ay;
	const float wx = px - s.ax;
	const float wy = py - s.ay;
	const float l2 = vx * vx + vy * vy;
	const float t = l2 > 0.0f ? saturate((wx * vx + wy * vy) / l2) : 0.0f;
	r_t = t;
	return len2(wx - vx * t, wy - vy * t);
}

// Emission for a thin hot filament at distance d.
inline V3 filament(float d, float width, float glow, const V3 &core_col, const V3 &glow_col) {
	const float core = std::exp(-(d * d) / (width * width));
	const float g = glow > 0.0f ? (width * 6.0f * glow) / (d + width * 6.0f * glow) : 0.0f;
	const float halo = std::exp(-d / maxf(0.02f * glow + 0.001f, 0.001f)) * 0.5f;
	return core_col * (core * 3.0f) + glow_col * (g * g * 0.6f + halo * glow);
}

void render_bolts(const FrameContext &c, float x, float y, float *r_px, float width, float glow, float intensity) {
	float best = 1e9f;
	float best_w = 1.0f;
	float glow_acc = 0.0f;
	for (int i = 0; i < c.segment_count; i++) {
		const Segment &s = c.segments[i];
		float t;
		const float d = seg_dist(x, y, s, t);
		if (d / s.width < best / best_w) {
			best = d;
			best_w = s.width;
		}
		const float gw = 0.035f * glow * s.width + 0.0005f;
		// exp(-14) < 1e-6: the glow of far segments is invisible, skip the exp.
		if (d < gw * 14.0f) {
			glow_acc += s.width * std::exp(-d / gw) * 0.08f;
		}
	}
	const float w = width * best_w;
	const float d = best;
	const V3 core_col = mix3(c.color_b, V3(1.0f, 1.0f, 1.0f), 0.5f);
	const float core = std::exp(-(d * d) / (w * w));
	const float near_glow = std::exp(-d / (w * 4.0f + 0.004f));
	V3 e = core_col * (core * 4.0f) + c.color_b * (near_glow * 1.2f) + c.color_a * (minf(glow_acc, 3.0f) * glow);
	write_pixel(r_px, e * intensity, 0.0f, *c.s);
}

void render_2d(const FrameContext &c, float x, float y, float *r_px) {
	const Settings &s = *c.s;
	const float r = len2(x, y);
	const float angle = std::atan2(y, x);
	const V3 &ca = c.color_a;
	const V3 &cb = c.color_b;
	const V3 white(1.0f, 1.0f, 1.0f);
	switch (s.type) {
		case TYPE_PLASMA: {
			const float radius = param(c, 0);
			const float gran = param(c, 1);
			const float cells = param(c, 2);
			const float corona = param(c, 3);
			const float corona_size = param(c, 4);
			const float intensity = param(c, 5);
			V3 e;
			float coverage = 0.0f;
			if (r < radius) {
				// Sphere surface: project to sphere for convection cells.
				const float z = std::sqrt(maxf(radius * radius - r * r, 0.0f));
				const float sx = x / radius;
				const float sy = y / radius;
				const float sz = z / radius;
				float f1, f2, cell;
				const float rot = c.t * 0.05f;
				const float u = std::atan2(sx, sz) + rot;
				voronoi2(u * cells * 0.5f, sy * cells * 0.5f + c.t * 0.02f, s.seed, 0.9f, 0, f1, f2, cell);
				const float edges = saturate((f2 - f1) * 3.0f);
				const float n = fbm(V3(sx * 6.0f, sy * 6.0f, sz * 6.0f + c.t * 0.2f), 4, 0.5f, s.seed);
				const float limb = std::pow(sz, 0.45f); // Limb darkening.
				const float temp = saturate(0.55f + (edges - 0.5f) * gran * 0.7f + n * 0.25f) * limb;
				e = heat_ramp(0.45f + temp * 0.55f, ca, cb) * (1.2f + temp * 1.5f);
				coverage = smoothstepf(radius, radius - 0.01f, r);
			}
			if (corona > 0.0f) {
				const float h = maxf(r - radius, 0.0f);
				const float n = fbm(V3(std::cos(angle) * 3.0f, std::sin(angle) * 3.0f, h * 4.0f - c.t * 0.4f) + seed_offset(s.seed), 5, 0.55f, s.seed + 9u);
				const float reach = corona_size * (0.6f + 0.8f * (n * 0.5f + 0.5f));
				const float falloff = std::exp(-h / maxf(reach * 0.35f, 0.005f));
				e += heat_ramp(0.35f + falloff * 0.4f, ca, cb) * (falloff * corona * 1.4f);
			}
			write_pixel(r_px, e * intensity, coverage, s);
			return;
		}
		case TYPE_SPARK: {
			const float length = param(c, 0);
			const float thick = param(c, 1);
			const float glow = param(c, 2);
			const float head = param(c, 3);
			const float flicker = param(c, 4);
			const float intensity = param(c, 5);
			const float head_y = length * 0.5f;
			const float tail_y = -length * 0.5f;
			const float t = saturate((y - tail_y) / maxf(head_y - tail_y, 1e-4f)); // 0 tail .. 1 head
			const float cy = clampf(y, tail_y, head_y);
			const float d = len2(x, y - cy);
			const float w = thick * (0.25f + 0.75f * t);
			const float n = gnoise3(y * 18.0f, c.t * 6.0f, 0.0f, s.seed) * 0.5f + 0.5f;
			const float brightness = (0.15f + 0.85f * t * t) * (1.0f - flicker + flicker * n * 1.6f);
			V3 e = filament(d, w, glow * (0.4f + 0.6f * t), mix3(cb, white, 0.4f), ca) * brightness;
			const float hd = len2(x, y - head_y);
			const float hr = thick * 1.6f * head + 1e-4f;
			e += mix3(cb, white, 0.6f) * (std::exp(-(hd * hd) / (hr * hr)) * 3.0f * head);
			e += ca * (std::exp(-hd / (0.05f * head * glow + 0.002f)) * 0.8f * head);
			write_pixel(r_px, e * intensity, 0.0f, s);
			return;
		}
		case TYPE_LIGHTNING:
			render_bolts(c, x, y, r_px, param(c, 2), param(c, 3), param(c, 4));
			return;
		case TYPE_ELECTRIC_ARC: {
			render_bolts(c, x, y, r_px, param(c, 3), param(c, 4), param(c, 5));
			// Bright node in the centre.
			const float node = std::exp(-r * r / 0.0015f) * 4.0f + std::exp(-r / 0.06f) * 0.8f;
			V3 extra = mix3(cb, white, 0.5f) * (node * param(c, 5));
			const float e = maxf(s.exposure, 0.0f);
			// Combine by re-tonemapping the approximate radiance.
			for (int i = 0; i < 3; i++) {
				const float existing = r_px[i] * r_px[3];
				const float add = tonemap((i == 0 ? extra.x : (i == 1 ? extra.y : extra.z)) * e);
				r_px[i] = existing + add - existing * add;
			}
			const float a = maxf(r_px[3], maxf(r_px[0], maxf(r_px[1], r_px[2])));
			if (a > 1e-5f) {
				r_px[0] = saturate(r_px[0] / a);
				r_px[1] = saturate(r_px[1] / a);
				r_px[2] = saturate(r_px[2] / a);
			}
			r_px[3] = saturate(a * s.opacity);
			return;
		}
		case TYPE_FLASH: {
			const float core = param(c, 0);
			const int rays = int(param(c, 1) + 0.5f);
			const float ray_len = param(c, 2);
			const float sharp = param(c, 3);
			const float streak = param(c, 4);
			const float halo = param(c, 5);
			const float intensity = param(c, 6);
			V3 e = mix3(cb, white, 0.7f) * (std::exp(-(r * r) / (core * core)) * 4.0f);
			e += cb * (core * 0.6f / (r + core * 0.6f)) * 0.9f;
			if (rays > 0 && ray_len > 0.0f) {
				const float jitter = gnoise3(angle * 3.0f, c.t * 2.0f, 0.0f, s.seed) * 0.15f;
				const float a = std::fabs(std::cos(angle * float(rays) * 0.5f + jitter));
				const float ray = std::pow(a, sharp) * std::exp(-r / (ray_len * 0.35f + 1e-3f));
				const float lenvar = 0.7f + 0.3f * gnoise3(std::floor((angle + PI) / TAU * float(rays) + 0.5f) * 7.0f, 0.0f, 0.0f, s.seed + 3u);
				e += mix3(ca, cb, 0.5f) * (ray * 2.0f * lenvar);
			}
			if (streak > 0.0f) {
				const float st = std::exp(-std::fabs(y) / 0.006f) * std::exp(-std::fabs(x) / (0.5f * streak + 1e-3f));
				e += mix3(cb, white, 0.3f) * (st * 1.6f * streak);
			}
			if (halo > 0.0f) {
				const float hr = 0.55f;
				const float hd = (r - hr) / 0.05f;
				e += ca * (std::exp(-hd * hd) * halo * 0.6f);
			}
			e = e * smoothstepf(1.0f, 0.85f, r);
			write_pixel(r_px, e * intensity, 0.0f, s);
			return;
		}
		case TYPE_RING: {
			const float radius = param(c, 0);
			const float thick = param(c, 1);
			const float distortion = param(c, 2);
			const float scale = param(c, 3);
			const float glow = param(c, 4);
			const float intensity = param(c, 5);
			const float n = fbm(V3(std::cos(angle) * scale * 0.5f, std::sin(angle) * scale * 0.5f, c.t * 0.5f) + seed_offset(s.seed), 4, 0.5f, s.seed);
			const float rr = radius + n * distortion * 0.12f;
			const float d = std::fabs(r - rr);
			const float energy = 0.7f + 0.6f * (fbm(V3(x * scale, y * scale, c.t), 3, 0.5f, s.seed + 5u) * 0.5f + 0.5f);
			V3 e = filament(d, thick * 0.5f, glow, mix3(cb, white, 0.3f), ca) * energy;
			write_pixel(r_px, e * intensity, 0.0f, s);
			return;
		}
		case TYPE_WAVE_RING: {
			float progress = param(c, 0);
			if (param(c, 5) > 0.5f && s.life > 0.0f) {
				progress = s.life;
			}
			const float thick = param(c, 1);
			const float sharp = param(c, 2);
			const float distortion = param(c, 3);
			const float intensity = param(c, 4);
			const float front = 0.05f + progress * 0.85f;
			const float n = fbm(V3(std::cos(angle) * 3.0f, std::sin(angle) * 3.0f, progress * 2.0f) + seed_offset(s.seed), 4, 0.5f, s.seed);
			const float d = r - (front + n * distortion * 0.05f);
			const float edge_w = mixf(0.04f, 0.004f, sharp);
			float v;
			if (d > 0.0f) {
				v = std::exp(-(d * d) / (edge_w * edge_w));
			} else {
				const float wake = thick * (0.4f + progress);
				v = std::exp(d / maxf(wake, 1e-3f)) * (0.35f + 0.65f * std::exp(-(d * d) / (edge_w * edge_w * 4.0f)));
			}
			const float fade = 1.0f - progress * progress;
			V3 e = mix3(ca, cb, saturate(v * v)) * (v * fade * 1.8f);
			write_pixel(r_px, e * intensity, 0.0f, s);
			return;
		}
		case TYPE_MAGIC_CIRCLE: {
			const float lw = param(c, 0);
			const int points = int(param(c, 1) + 0.5f);
			const int spokes = int(param(c, 2) + 0.5f);
			const float runes = param(c, 3);
			const float glow = param(c, 4);
			const float intensity = param(c, 5);
			const float spin = param(c, 6);
			const float a = angle + c.t * spin * TAU;
			float d = 1e9f;
			d = minf(d, std::fabs(r - 0.86f));
			d = minf(d, std::fabs(r - 0.8f));
			d = minf(d, std::fabs(r - 0.56f));
			d = minf(d, std::fabs(r - 0.5f));
			if (spokes > 0 && r > 0.56f && r < 0.8f) {
				const float seg = TAU / float(spokes);
				const float local = std::fabs(fractf(a / seg + 0.5f) - 0.5f) * seg * r;
				d = minf(d, local);
			}
			// Polygram inscribed in the inner circle.
			if (points >= 3) {
				const float seg = TAU / float(points);
				for (int k = 0; k < 2; k++) {
					const float off = k == 0 ? 0.0f : seg * 0.5f;
					const float la = fractf((a + off) / seg) * seg - seg * 0.5f;
					const float apothem = 0.5f * std::cos(seg * 0.5f);
					const float edge = std::fabs(r * std::cos(la) - apothem);
					d = minf(d, r < 0.52f ? edge : 1e9f);
				}
			}
			float rune = 0.0f;
			if (runes > 0.0f && r > 0.81f && r < 0.855f) {
				const float cellsn = 48.0f;
				const float cell = std::floor((a + PI) / TAU * cellsn);
				const float h = hash01(hash3i(int(cell), 0, 0, s.seed));
				if (h < runes) {
					const float local = fractf((a + PI) / TAU * cellsn);
					const float rv = (r - 0.81f) / 0.045f;
					const float hx = hash01(hash3i(int(cell), 1, 0, s.seed));
					const float bar = std::fabs(local - (0.3f + 0.4f * hx)) < 0.12f ? 1.0f : 0.0f;
					const float cross = std::fabs(rv - (0.3f + 0.4f * hash01(hash3i(int(cell), 2, 0, s.seed)))) < 0.15f && std::fabs(local - 0.5f) < 0.3f ? 1.0f : 0.0f;
					rune = maxf(bar, cross);
				}
			}
			V3 e = filament(d, lw, glow, mix3(cb, white, 0.25f), ca) + mix3(cb, white, 0.3f) * (rune * 1.5f);
			e = e * smoothstepf(0.98f, 0.9f, r);
			write_pixel(r_px, e * intensity, 0.0f, s);
			return;
		}
		case TYPE_BUBBLES: {
			const float rim = param(c, 3);
			const float soft = param(c, 4);
			const float intensity = param(c, 5);
			V3 e;
			for (int i = 0; i < c.disk_count; i++) {
				const float dx = x - c.disks[i][0];
				const float dy = y - c.disks[i][1];
				const float br = c.disks[i][2];
				const float d = len2(dx, dy);
				if (d > br * 1.3f) {
					continue;
				}
				const float edge = br * (0.03f + soft * 0.3f);
				const float body = smoothstepf(br + edge, br - edge, d);
				const float ring = std::exp(-((d - br * 0.94f) * (d - br * 0.94f)) / (edge * edge + br * br * 0.002f));
				e += (ca * (body * 0.35f) + cb * (ring * rim * 1.2f)) * c.disks[i][3];
			}
			write_pixel(r_px, e * intensity, 0.0f, s);
			return;
		}
		default:
			break;
	}
	write_pixel(r_px, V3(), 0.0f, s);
}


void render_soft_smoke(const FrameContext &c, float x, float y, float *r_px) {
	const Settings &s = *c.s;
	const float base_radius = param(c, 0);
	const float softness = param(c, 1);
	const float base_wisps = param(c, 2);
	const float scale = param(c, 3);
	const float gain = param(c, 4);
	const float base_density = param(c, 5);
	const float light_angle = param(c, 6) * PI / 180.0f;
	const float shading = param(c, 7);
	const float billows = param(c, 8);
	const float lifecycle = param(c, 9);
	// Life cycle (UE-style flipbook): grow, break up into wisps, thin out.
	const float life = lifecycle > 0.0f ? saturate(s.life) * lifecycle : 0.0f;
	// Modest growth: the particle Scale curve does the rest, and a small puff
	// in a big quad would only add transparent overdraw.
	const float grow = mixf(1.0f, mixf(0.8f, 1.0f, std::sqrt(life)), lifecycle);
	const float radius = base_radius * grow;
	const float wisps = minf(1.0f, mixf(base_wisps * mixf(1.0f, 0.6f, lifecycle), base_wisps + 0.45f, life));
	const float density = base_density * mixf(1.0f, 0.4f, life * life);
	const float r = len2(x, y) / radius;
	if (r >= 1.0f) {
		write_flat(r_px, c.color_b, 0.0f, s);
		return;
	}
	const int octaves = s.quality > 0 ? 5 : 3;
	const V3 offset = seed_offset(s.seed);
	// Noise in the puff's own (expanding) space, so it grows coherently.
	const float ux = x / grow;
	const float uy = y / grow;
	const float z = c.t * 0.15f + life * 0.8f;
	auto noise = [&](float px, float py) -> float {
		const V3 q = V3(px * scale, py * scale, z) + offset;
		const float wx = fbm(q * 0.5f + V3(1.7f, 9.2f, 0.0f), 2, 0.5f, s.seed + 11u);
		const float wy = fbm(q * 0.5f + V3(8.3f, 2.8f, 0.0f), 2, 0.5f, s.seed + 12u);
		return fbm(q + V3(wx, wy, 0.0f) * 0.8f, octaves, gain, s.seed) * 0.5f + 0.5f;
	};
	auto inner = [&](float px, float py) -> float {
		return saturate(billow(V3(px * scale * 0.95f, py * scale * 0.95f, z * 1.3f) + offset * 1.7f, 4, 0.45f, s.seed + 23u) * 0.9f + 0.1f);
	};
	const float n = noise(ux, uy);
	const float b = billows > 0.0f ? inner(ux, uy) : 0.5f;
	// Smooth radial profile: opaque-ish centre, long transparent fade.
	const float falloff = 1.0f - r;
	const float profile = std::pow(smoothstepf(0.0f, 1.0f, falloff), 0.25f + 0.9f * softness);
	// Wisps: contrasty noise that thins the puff, strongest towards the edge.
	const float w = smoothstepf(0.25f, 0.85f, n);
	float d = profile * mixf(1.0f, w * 1.6f, wisps * (0.55f + 0.45f * (1.0f - profile)));
	d *= 1.0f - billows * 0.35f * (1.0f - b);
	const float alpha = saturate(d * density * 1.9f);
	// Lighting: rounded puff normal, bent by the wisps and the inner billows.
	const float e = 0.02f;
	const float gx = (noise(ux + e, uy) - n) / e;
	const float gy = (noise(ux, uy + e) - n) / e;
	float bx = 0.0f;
	float by = 0.0f;
	if (billows > 0.0f) {
		bx = (inner(ux + e, uy) - b) / e;
		by = (inner(ux, uy + e) - b) / e;
	}
	const float nx = x / radius - gx * 0.04f * wisps - bx * 0.12f * billows;
	const float ny = y / radius - gy * 0.04f * wisps - by * 0.12f * billows;
	const float nz = std::sqrt(maxf(1.0f - minf(r * r, 1.0f), 0.0f)) + 0.35f;
	const V3 normal = normalize3(V3(nx, ny, nz));
	const V3 light = normalize3(V3(std::cos(light_angle), std::sin(light_angle), 0.8f));
	const float lambert = dot3(normal, light) * 0.5f + 0.5f;
	float lit = mixf(0.6f, lambert, shading) + (n - 0.5f) * 0.25f * shading;
	lit += (b - 0.5f) * billows * 0.55f;
	write_flat(r_px, mix3(c.color_a, c.color_b, saturate(lit)), alpha, s);
}

/* Water splash ----------------------------------------------------------- */

// Distance from p to the segment a-b and the position t (0..1) along it.
inline float segment_distance(float px, float py, float ax, float ay, float bx, float by, float &r_t) {
	const float ex = bx - ax;
	const float ey = by - ay;
	const float l2 = maxf(ex * ex + ey * ey, 1e-8f);
	r_t = clampf(((px - ax) * ex + (py - ay) * ey) / l2, 0.0f, 1.0f);
	return len2(px - ax - ex * r_t, py - ay - ey * r_t);
}

void render_water_plume(const FrameContext &c, float x, float y, float *r_px) {
	const Settings &s = *c.s;
	const float width = param(c, 0);
	const float flare = param(c, 1);
	const float height = param(c, 2);
	const float lumps = param(c, 3);
	const float streaks = param(c, 4);
	const float spray = param(c, 5);
	const float density = param(c, 6);
	const float holes_amount = param(c, 7);
	const uint32_t seed = s.seed * 7919u + 25u;
	const V3 noff = seed_offset(seed);
	const float base = -0.97f;
	const float top = base + 1.9f * height;
	const float h = saturate((y - base) / maxf(top - base, 1e-3f)); // 0 base .. 1 top.
	// Half width: widening a little going up, opening into a crown at the top.
	const float open = std::pow(saturate((h - 0.5f) / 0.5f), 1.6f);
	float w = width * (0.6f + 0.4f * h) + flare * width * open * 1.8f;
	const float edge_noise = fbm(V3(x * 3.2f + noff.x, y * 2.6f + noff.y, 0.4f), 4, 0.55f, seed);
	w *= 1.0f + lumps * 0.32f * edge_noise;
	// Crown top: a lumpy, rounded upper edge.
	const float cap = top + lumps * 0.14f * fbm(V3(x * 4.5f + noff.z, 1.3f, 2.1f), 3, 0.5f, seed + 5u) - 0.08f * (x / maxf(w, 0.05f)) * (x / maxf(w, 0.05f));
	const float aa = 0.015f;
	// Soft foot: the column rises out of the water, no hard bottom edge.
	float inside = smoothstepf(w + aa, w - w * 0.22f, absf(x)) * smoothstepf(cap + aa, cap - 0.1f, y) * smoothstepf(base, base + 0.04f, y);
	const float froth = billow(V3(x * 6.5f + noff.y, y * 5.5f, 1.7f), 4, 0.5f, seed + 9u);
	const float streak = gnoise3(x * 16.0f / maxf(width * 4.0f, 0.3f) + noff.x, y * 1.6f, 0.3f, seed + 7u) * 0.5f + 0.5f;
	const float core = smoothstepf(w, 0.0f, absf(x));
	float body = density * mixf(0.5f, 1.0f, core) * mixf(1.0f, 0.55f + 0.7f * streak, streaks) * (0.7f + 0.55f * froth);
	// The upper part tears open into separate lumps.
	const float tear = smoothstepf(0.45f, 0.95f, h) * smoothstepf(0.32f, 0.6f, froth + 0.25f * gnoise3(x * 9.0f, y * 9.0f, 3.3f, seed + 11u));
	// Light from the left: lit side, shadowed side, bright froth.
	float shade = 0.55f - 0.4f * (x / maxf(w, 0.05f)) + froth * 0.35f + 0.15f * h;
	float water = inside * saturate(body * 1.3f) * (1.0f - holes_amount * tear);
	// Crown: round lumps of water bulging out of the top and the upper sides,
	// so the column does not end in a flat cut.
	const float top_w = width + flare * width * 1.8f;
	const int crown_lumps = 5 + int(lumps * 12.0f + 0.5f);
	for (int i = 0; i < crown_lumps; i++) {
		const float r1 = hash01(hash3i(i, 11, 71, seed));
		const float r2 = hash01(hash3i(i, 13, 72, seed));
		const float r3 = hash01(hash3i(i, 17, 73, seed));
		const float side = r1 * 2.0f - 1.0f;
		const float lh = mixf(0.62f, 1.0f, r2); // Along the upper part.
		const float lw = width * (0.6f + 0.4f * lh) + flare * width * std::pow(saturate((lh - 0.5f) / 0.5f), 1.6f) * 1.8f;
		const float lx = side * lw * 0.85f;
		const float ly = base + (top - base) * lh + top_w * 0.08f * (1.0f - absf(side));
		const float lr = top_w * mixf(0.16f, 0.34f, r3) * (0.85f + 0.3f * lumps);
		const float lump_noise = fbm(V3((x - lx) * 7.0f, (y - ly) * 7.0f, float(i) * 1.7f), 3, 0.5f, seed + 13u);
		const float d = len2(x - lx, y - ly) / lr + lump_noise * 0.25f;
		const float a = smoothstepf(1.0f, 0.7f, d) * density * (0.85f + 0.3f * froth);
		if (a > water) {
			water = maxf(water, saturate(a));
			// Lit on the upper left of each lump.
			shade = 0.6f + 0.4f * saturate(((ly - y) * -1.0f + (lx - x)) / lr * 0.6f + 0.4f) + froth * 0.2f;
		}
	}
	// Spray: droplets and small lumps around the crown and the sides.
	const int drops = int(spray * 70.0f + 0.5f);
	for (int i = 0; i < drops; i++) {
		const float r1 = hash01(hash3i(i, 3, 91, seed));
		const float r2 = hash01(hash3i(i, 5, 92, seed));
		const float r3 = hash01(hash3i(i, 7, 93, seed));
		const float side = r1 * 2.0f - 1.0f;
		const float dh = mixf(0.35f, 1.08f, std::sqrt(r2));
		const float dy = base + (top - base) * dh;
		const float local_w = width * (0.6f + 0.4f * dh) + flare * width * std::pow(saturate((dh - 0.5f) / 0.5f), 1.6f) * 1.8f;
		const float dx = side * local_w * mixf(0.9f, 1.7f, r3);
		const float dr = (0.006f + 0.014f * r3 * r3) * (0.6f + 0.6f * dh);
		// Drops falling off are a little stretched vertically.
		const float d = len2((x - dx) / dr, (y - dy) / (dr * 1.6f));
		const float a = smoothstepf(1.0f, 0.6f, d);
		if (a > water) {
			water = a;
			shade = 0.75f + 0.25f * (1.0f - side);
		}
	}
	write_flat(r_px, mix3(c.color_a, c.color_b, saturate(shade)), water, s);
}

void render_foam(const FrameContext &c, float x, float y, float *r_px) {
	const Settings &s = *c.s;
	const float radius = param(c, 0);
	const float ring = param(c, 1);
	const float bubbles = param(c, 2);
	const float breakup = param(c, 3);
	const float softness = param(c, 4);
	const float streaks = param(c, 5);
	const uint32_t seed = s.seed * 7919u + 26u;
	const V3 noff = seed_offset(seed);
	const float ang = std::atan2(y, x);
	// Torn edge: the radius wobbles around the patch.
	const float wobble = fbm(V3(std::cos(ang) * 2.2f + noff.x, std::sin(ang) * 2.2f + noff.y, 0.7f), 4, 0.55f, seed);
	const float r = len2(x, y) / maxf(radius * (1.0f + breakup * 0.18f * wobble), 0.05f);
	float mask = smoothstepf(1.0f, mixf(0.92f, 0.45f, softness), r);
	// Ring: an open middle, softly filled toward the outer edge.
	mask *= mixf(1.0f, smoothstepf(0.35f, 0.75f, r), ring);
	// Bubbles: rounded cells from billowy noise, smaller or larger.
	const float cell_scale = mixf(26.0f, 8.0f, bubbles);
	// Streaks: the foam stretched outward along the radius.
	const float sx = x * mixf(1.0f, 0.35f, streaks * saturate(r));
	const float sy = y * mixf(1.0f, 0.35f, streaks * saturate(r));
	const float cells = billow(V3(sx * cell_scale + noff.z, sy * cell_scale, 2.3f), 3, 0.5f, seed + 3u);
	const float foam = smoothstepf(0.18f, 0.5f, cells);
	const float holes = smoothstepf(0.05f, -0.25f, fbm(V3(x * 4.0f + noff.y, y * 4.0f, 5.1f), 4, 0.55f, seed + 5u)) * breakup;
	const float alpha = mask * mixf(0.45f, 1.0f, foam) * (1.0f - holes * 0.9f);
	write_flat(r_px, mix3(c.color_a, c.color_b, saturate(foam * 0.8f + 0.2f)), alpha, s);
}

void render_underwater_bubble(const FrameContext &c, float x, float y, float *r_px) {
	// x, y are rotated per variant below.
	const Settings &s = *c.s;
	const float rim = param(c, 0);
	const float rim_width = param(c, 1);
	const float highlight = param(c, 2);
	const float highlight_size = param(c, 3);
	const float inner_light = param(c, 4);
	const float body = param(c, 5);
	const float wobble = param(c, 6);
	// Each sheet frame is another bubble: squashed wider than tall, uneven.
	const uint32_t seed = s.seed * 7919u + 27u + uint32_t(s.life * 977.0f);
	const V3 noff = seed_offset(seed);
	// Rising bubbles flatten and tilt: each variant its own squash and tilt.
	const float squash = 1.0f + wobble * 0.6f * (hash01(hash3i(1, 2, 3, seed)) - 0.25f);
	const float tilt = (hash01(hash3i(4, 5, 6, seed)) - 0.5f) * wobble * 0.9f;
	const float tx = x * std::cos(tilt) - y * std::sin(tilt);
	const float ty = x * std::sin(tilt) + y * std::cos(tilt);
	x = tx;
	y = ty;
	const float px = x / squash;
	const float py = y * squash;
	const float ang = std::atan2(py, px);
	const float radius = 0.86f * (1.0f + wobble * 0.16f * gnoise3(std::cos(ang) * 1.8f + noff.x, std::sin(ang) * 1.8f + noff.y, 0.5f, seed));
	const float r = len2(px, py) / radius;
	const float aa = 0.02f;
	const float inside = smoothstepf(1.0f + aa, 1.0f - aa, r);
	if (inside <= 0.0f) {
		write_flat(r_px, c.color_a, 0.0f, s);
		return;
	}
	// Bright edge (seen through more surface at a grazing angle), softer inwards.
	const float edge = std::pow(smoothstepf(1.0f - rim_width * 2.5f, 1.0f, r), 1.5f) * rim;
	// Light coming in from below, glowing on the lower inner side.
	const float lower = smoothstepf(0.9f, 0.0f, len2(px + 0.1f, py + 0.45f)) * inner_light;
	// Sharp highlight up and to the left, with a small second one below right.
	const float hl = smoothstepf(highlight_size, highlight_size * 0.55f, len2((px + 0.38f) * 1.2f, (py - 0.42f) * 1.6f)) * highlight;
	const float hl2 = smoothstepf(highlight_size * 0.45f, highlight_size * 0.2f, len2(px - 0.42f, py + 0.4f)) * highlight * 0.5f;
	const float alpha = inside * saturate(body + edge + lower * 0.6f + hl + hl2);
	const float bright = saturate(hl + hl2 + lower * 0.5f);
	write_flat(r_px, mix3(c.color_a, c.color_b, bright), alpha, s);
}

void render_water_splash(const FrameContext &c, float x, float y, float *r_px) {
	const Settings &s = *c.s;
	const int frames = maxi(int(param(c, 0) + 0.5f), 2);
	const int variants = maxi(int(param(c, 1) + 0.5f), 1);
	const float crown_height = param(c, 2);
	const float crown_width = param(c, 3);
	const int fingers = int(param(c, 4) + 0.5f);
	const int droplets = int(param(c, 5) + 0.5f);
	const float jet_share = param(c, 6);
	const float sheet = param(c, 7);
	const float puddle = param(c, 8);
	// Sheet frame -> variant (row) and time in its animation (column).
	const int total = frames * variants;
	const int index = int(std::floor(saturate(s.life) * float(total - 1) + 0.5f));
	const int variant = index / frames;
	const float t = float(index % frames) / float(frames - 1);
	const uint32_t seed = s.seed * 7919u + uint32_t(variant) * 104729u;
	auto rnd = [&](int p_index) -> float {
		return hash01(hash3i(p_index, variant, 57, seed));
	};

	const float ground = -0.82f;
	const float yy = y - ground; // Height above the surface.
	float water = 0.0f; // Alpha of the water.
	float glint = 0.0f; // Bright edges and highlights.
	auto add = [&](float a, float g) {
		water = maxf(water, a);
		glint = maxf(glint, g * a);
	};
	const float aa = 0.012f;
	const float scale_v = 0.85f + 0.3f * rnd(1); // Splashes differ in size.
	const float fade = 1.0f - smoothstepf(0.72f, 1.0f, t);

	// Incoming drop just above the surface on the first frame.
	if (t < 0.5f / float(frames - 1)) {
		const float dr = 0.09f;
		const float dx = x / dr;
		const float dy = (yy - 0.16f) / (dr * 1.35f);
		const float d = len2(dx, dy);
		add(smoothstepf(1.0f, 0.85f, d), smoothstepf(0.9f, 0.6f, len2(dx + 0.35f, dy - 0.35f)) * 0.9f + 0.2f);
		// A first small ring where it touches.
		const float rr = len2(x / 0.16f, yy / 0.035f);
		add(smoothstepf(0.25f, 0.0f, absf(rr - 1.0f)) * 0.6f, 0.6f);
		write_flat(r_px, mix3(c.color_a, c.color_b, saturate(glint)), water, s);
		return;
	}

	// Crown: rises fast, then sinks back while the lip spreads out.
	const V3 noff = seed_offset(seed);
	const float rise = smoothstepf(0.0f, 0.3f, t);
	const float collapse = smoothstepf(0.45f, 0.95f, t);
	const float height = crown_height * scale_v * (rise - collapse * 0.95f) * (0.85f + 0.3f * rnd(2));
	const float lip = crown_width * scale_v * mixf(0.5f, 1.0f, rise) * (1.0f + collapse * 0.45f);
	const float base_r = lip * 0.32f;
	if (height > 0.01f && yy >= -0.01f && yy <= height + 0.1f) {
		const float h = saturate(yy / maxf(height, 1e-3f));
		// Thin sheet flaring out like a tulip; its edge wobbles with noise.
		const float flare = std::pow(h, 1.6f);
		const float wobble_r = 1.0f + 0.12f * gnoise3(yy * 9.0f + noff.x, noff.y, float(variant), seed + 3u);
		const float wall_r = mixf(base_r, lip, flare) * wobble_r;
		const float ax = absf(x);
		if (yy <= height && ax <= wall_r + aa) {
			const float edge = smoothstepf(wall_r * 0.82f, wall_r, ax); // Seen edge-on: the bright outline.
			const float inside = smoothstepf(wall_r + aa, wall_r - aa, ax);
			// The sheet tears: holes and streaks running up the wall.
			const float tear = gnoise3(x * 14.0f + noff.z, yy * 5.0f, float(variant) * 3.0f, seed + 4u) * 0.5f + 0.5f;
			const float body = sheet * smoothstepf(0.25f, 0.75f, tear) * (0.4f + 0.6f * h);
			add(inside * mixf(body, 0.75f, edge) * fade, edge * 0.85f + tear * 0.15f);
		}
		// Lip: the rim, an uneven ellipse broken into pieces.
		const float ang = std::atan2((yy - height) / maxf(lip * 0.22f, 0.01f), x / maxf(lip, 0.01f));
		const float rim_r = 1.0f + 0.1f * gnoise3(std::cos(ang) * 3.0f + noff.x, std::sin(ang) * 3.0f + noff.y, float(variant), seed + 6u);
		const float lr = len2(x / maxf(lip, 0.01f), (yy - height) / maxf(lip * 0.22f, 0.01f));
		const float broken = smoothstepf(-0.2f, 0.25f, gnoise3(std::cos(ang) * 4.0f + noff.z, std::sin(ang) * 4.0f, float(variant) + 7.0f, seed + 8u));
		add(smoothstepf(0.11f, 0.0f, absf(lr - rim_r)) * mixf(0.35f, 0.9f, broken) * fade, 1.0f);
	}
	// Fingers grow from the lip, bending outwards; drops break off their tips.
	for (int i = 0; i < fingers; i++) {
		if (rnd(200 + i) < 0.25f) {
			continue; // Some places on the lip throw nothing.
		}
		const float phi = (float(i) + (rnd(10 + i) - 0.5f) * 0.8f) / float(maxi(fingers, 1)) * TAU;
		const float depth = std::sin(phi); // > 0 in front of the crown.
		const float fx = std::cos(phi) * lip;
		const float fy = height + depth * lip * 0.22f;
		const float len = (0.04f + 0.16f * rnd(30 + i) * rnd(31 + i) * 2.0f) * scale_v * (rise - collapse);
		if (len <= 0.005f || height <= 0.02f) {
			continue;
		}
		// Curved finger: up first, then bending out (quadratic curve in 3 parts).
		const float out = (fx >= 0.0f ? 1.0f : -1.0f) * (0.3f + 0.5f * rnd(40 + i)) * len;
		float px0 = fx;
		float py0 = fy;
		float best = 1e9f;
		float best_t = 0.0f;
		for (int k = 1; k <= 3; k++) {
			const float u = float(k) / 3.0f;
			const float px1 = fx + out * u * u;
			const float py1 = fy + len * u;
			float st = 0.0f;
			const float d = segment_distance(x, yy, px0, py0, px1, py1, st);
			if (d < best) {
				best = d;
				best_t = (float(k - 1) + st) / 3.0f;
			}
			px0 = px1;
			py0 = py1;
		}
		const float w = mixf(0.022f, 0.005f, best_t) * scale_v;
		add(smoothstepf(w + aa, w - aa * 0.5f, best) * 0.8f * fade, 0.75f);
		// The drop at the tip breaks off and flies on.
		const float detach = saturate((t - 0.28f) / 0.5f);
		const float bx = px0 + out * detach * 1.2f;
		const float by = py0 + len * detach * 1.1f - detach * detach * 0.45f;
		const float br = (0.012f + 0.014f * rnd(50 + i)) * scale_v;
		const float bd = len2(x - bx, yy - by);
		add(smoothstepf(br + aa, br - aa, bd) * fade, smoothstepf(br, br * 0.2f, len2(x - bx + br * 0.4f, yy - by - br * 0.4f)) * 0.9f + 0.3f);
	}
	// Droplets flying away on ballistic arcs.
	for (int i = 0; i < droplets; i++) {
		const float start = 0.05f + 0.25f * rnd(70 + i);
		const float age = t - start;
		if (age <= 0.0f) {
			continue;
		}
		const float side = rnd(90 + i) < 0.5f ? -1.0f : 1.0f;
		const float vx = side * (0.4f + 1.1f * rnd(110 + i)) * scale_v;
		const float vy = (1.1f + 1.4f * rnd(130 + i)) * scale_v * crown_height * 1.6f;
		const float gravity = 3.2f;
		const float px = side * base_r * 0.8f + vx * age;
		const float py = 0.05f + vy * age - 0.5f * gravity * age * age;
		if (py < -0.02f) {
			continue;
		}
		const float r = (0.01f + 0.018f * rnd(150 + i)) * scale_v;
		// Slightly stretched along the flight.
		const float vel_y = vy - gravity * age;
		const float stretch = 1.0f + minf(len2(vx, vel_y) * 0.15f, 0.8f);
		const float dx = x - px;
		const float dy = (yy - py) / stretch;
		const float d = len2(dx, dy);
		add(smoothstepf(r + aa, r - aa, d) * fade, smoothstepf(r, r * 0.2f, len2(dx + r * 0.4f, dy - r * 0.4f)) * 0.9f + 0.3f);
	}
	// Centre jet (Worthington jet) on some variants: rises after the crown.
	if (rnd(5) < jet_share) {
		const float jt = saturate((t - 0.35f) / 0.5f);
		const float jh = crown_height * scale_v * 0.9f * std::sin(jt * PI) * (0.8f + 0.4f * rnd(6));
		if (jh > 0.01f) {
			const float w = mixf(0.05f, 0.018f, saturate(yy / maxf(jh, 0.01f))) * scale_v;
			if (yy >= 0.0f && yy <= jh) {
				add(smoothstepf(w + aa, w - aa, absf(x)) * 0.8f * fade, smoothstepf(w * 0.2f, w, absf(x + w * 0.4f)) < 0.5f ? 0.9f : 0.4f);
			}
			const float tr = 0.035f * scale_v;
			add(smoothstepf(tr + aa, tr - aa, len2(x, yy - jh - tr * 0.6f)) * fade, 0.8f);
		}
	}
	// Puddle: a flat ring spreading on the surface.
	if (puddle > 0.0f) {
		const float pr = mixf(0.25f, 0.95f, std::sqrt(t)) * scale_v;
		const float ring = len2(x / pr, yy / maxf(pr * 0.09f, 0.01f));
		const float band = smoothstepf(0.18f, 0.0f, absf(ring - 1.0f));
		const float flat = smoothstepf(1.0f, 0.6f, ring) * 0.25f;
		add(maxf(band * 0.7f, flat) * puddle * (1.0f - t * 0.85f), band * 0.8f);
	}
	write_flat(r_px, mix3(c.color_a, c.color_b, saturate(glint)), water, s);
}

/* Debris ----------------------------------------------------------------- */

void render_debris(const FrameContext &c, float x, float y, float *r_px) {
	const Settings &s = *c.s;
	const float size = param(c, 0);
	const float corners = param(c, 1);
	const float irregularity = param(c, 2);
	const float elongation = param(c, 3);
	const float bevel = param(c, 4);
	const float grain = param(c, 5);
	const float light_angle = param(c, 6) * PI / 180.0f;
	const float shading = param(c, 7);
	// Every sheet frame (life 0..1 over 16 frames) is its own chip.
	const int variant = int(std::floor(saturate(s.life) * 15.0f + 0.5f));
	const uint32_t seed = s.seed * 7919u + uint32_t(variant) * 104729u;
	auto rnd = [&](int p_index) -> float {
		return hash01(hash3i(p_index, variant, 91, seed));
	};

	// Chip frame: random rotation and elongation, scaled to stay in the frame.
	const float rot = rnd(0) * TAU;
	const float stretch = 1.0f - elongation * 0.55f * rnd(1);
	const float cr = std::cos(rot);
	const float sr = std::sin(rot);
	const float px = (x * cr + y * sr);
	const float py = (-x * sr + y * cr) / stretch;

	// Star-shaped outline with jittered corners.
	constexpr int MAX_CORNERS = 12;
	const int n = int(clampf(float(int(corners + 0.5f) + int(rnd(2) * 3.0f) - 1), 3.0f, float(MAX_CORNERS)));
	float vx[MAX_CORNERS];
	float vy[MAX_CORNERS];
	for (int i = 0; i < n; i++) {
		const float a = (float(i) + (rnd(10 + i) - 0.5f) * 0.7f * irregularity) / float(n) * TAU;
		const float r = size * mixf(1.0f, 0.55f + 0.45f * rnd(30 + i), irregularity);
		vx[i] = std::cos(a) * r;
		vy[i] = std::sin(a) * r;
	}
	// Rough outline: noise pushes the edge in and out a little.
	const V3 offset = seed_offset(seed);
	const float rough = gnoise3(px * 7.0f + offset.x, py * 7.0f + offset.y, 0.0f, seed + 5u) * 0.035f * irregularity;

	// Signed distance to the polygon (https://iquilezles.org/articles/distfunctions2d/)
	// and the nearest edge, whose outward normal tilts the bevel facet.
	float d2 = (px - vx[0]) * (px - vx[0]) + (py - vy[0]) * (py - vy[0]);
	float sign = 1.0f;
	int nearest = 0;
	for (int i = 0, j = n - 1; i < n; j = i, i++) {
		const float ex = vx[j] - vx[i];
		const float ey = vy[j] - vy[i];
		const float wx = px - vx[i];
		const float wy = py - vy[i];
		const float t = clampf((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
		const float bx = wx - ex * t;
		const float by = wy - ey * t;
		const float bd = bx * bx + by * by;
		if (bd < d2) {
			d2 = bd;
			nearest = i;
		}
		const bool c1 = py >= vy[i];
		const bool c2 = py < vy[j];
		const bool c3 = ex * wy > ey * wx;
		if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) {
			sign = -sign;
		}
	}
	const float dist = sign * std::sqrt(d2) + rough; // < 0 inside.
	const float aa = 0.012f;
	const float alpha = smoothstepf(aa, -aa, dist);
	if (alpha <= 0.0f) {
		write_flat(r_px, c.color_b, 0.0f, s);
		return;
	}

	// Facets: a slightly tilted top face, and bevels sloping down to the edge.
	const int prev = (nearest + n - 1) % n;
	float ox = vy[prev] - vy[nearest];
	float oy = vx[nearest] - vx[prev];
	const float ol = maxf(len2(ox, oy), 1e-5f);
	ox /= ol;
	oy /= ol;
	if (ox * (vx[nearest] + vx[prev]) + oy * (vy[nearest] + vy[prev]) < 0.0f) {
		ox = -ox; // Keep the edge normal pointing out of the chip.
		oy = -oy;
	}
	const float bevel_width = maxf(bevel * 0.3f * size, 1e-4f);
	const float edge = smoothstepf(bevel_width, 0.0f, -dist); // 1 at the outline.
	// A ridge splits the top into two facets, so the chip reads as a chunk.
	const float ridge_angle = rnd(6) * TAU;
	const float rdx = std::cos(ridge_angle);
	const float rdy = std::sin(ridge_angle);
	const float ridge_side = smoothstepf(-0.02f, 0.02f, px * rdx + py * rdy + (rnd(7) - 0.5f) * size * 0.6f) * 2.0f - 1.0f;
	const float tilt_x = (rnd(3) - 0.5f) * 0.6f + rdx * ridge_side * 0.45f;
	const float tilt_y = (rnd(4) - 0.5f) * 0.6f + rdy * ridge_side * 0.45f;
	const float gx = gnoise3(px * 18.0f + offset.y, py * 18.0f + offset.z, 1.0f, seed + 9u);
	const float gy = gnoise3(px * 18.0f + offset.z, py * 18.0f + offset.x, 2.0f, seed + 10u);
	V3 normal = normalize3(V3(tilt_x + ox * edge * 1.6f + gx * grain * 0.35f, tilt_y + oy * edge * 1.6f + gy * grain * 0.35f, 1.0f));
	// The normal lives in the chip frame; rotate it back to the image.
	normal = V3(normal.x * cr - normal.y * sr, normal.x * sr + normal.y * cr, normal.z);
	const V3 light = normalize3(V3(std::cos(light_angle), std::sin(light_angle), 0.9f));
	const float lambert = saturate(dot3(normal, light));
	float lit = mixf(0.62f, lambert, shading);
	// Grain: speckles and small dark pits.
	const float speck = fbm(V3(px * 22.0f, py * 22.0f, 0.5f) + offset, 3, 0.5f, seed + 13u);
	float f1, f2, cell;
	voronoi2(px * 9.0f + offset.x, py * 9.0f + offset.y, seed + 17u, 0.9f, 0, f1, f2, cell);
	const float pit = smoothstepf(0.12f, 0.0f, f1) * (cell > 0.6f ? 1.0f : 0.0f);
	lit += speck * 0.35f * grain - pit * 0.35f * grain;
	// Each chip a little lighter or darker than the others.
	lit *= mixf(0.85f, 1.1f, rnd(5));
	write_flat(r_px, mix3(c.color_a, c.color_b, saturate(lit)), alpha, s);
}

/* Decals ----------------------------------------------------------------- */
// Non-emissive albedo + alpha for Decal nodes. Outside the mark alpha is 0.

inline float hash_angle(uint32_t p_seed, int p_index) {
	return hash01(hash3i(p_index, 17, 3, p_seed));
}

// Distance from (x, y) to a crack that leaves the centre at angle a0 and
// wobbles with radius.
inline float radial_crack_distance(float x, float y, float r, float a0, float wobble, uint32_t seed, int index) {
	const float a = a0 + wobble * gnoise3(r * 6.0f, float(index) * 3.7f, 0.0f, seed + 31u) * 0.35f;
	return std::fabs(-std::sin(a) * x + std::cos(a) * y);
}

// Height of the last render_decal sample (relative surface height: holes and
// cracks below 0, bent metal and wet splatter above). Used for normal maps.
thread_local float t_decal_height = 0.0f;

void render_decal(const FrameContext &c, float x, float y, float *r_px) {
	const Settings &s = *c.s;
	float &h = t_decal_height;
	h = 0.0f;
	const V3 &ca = c.color_a;
	const V3 &cb = c.color_b;
	const float r = len2(x, y);
	const float angle = std::atan2(y, x);
	const V3 dir(std::cos(angle), std::sin(angle), 0.0f);
	switch (s.type) {
		case TYPE_BULLET_HOLE: {
			const float hole = param(c, 0);
			const float chip = maxf(param(c, 1), hole);
			const int cracks = int(param(c, 2) + 0.5f);
			const float crack_len = param(c, 3);
			const float rough = param(c, 4);
			const float soot = param(c, 5);
			const int surface = int(param(c, 6) + 0.5f);
			// Wood: the chipped area stretches along the grain (Y).
			const float ex = surface == 2 ? x * 1.7f : x;
			const float er = len2(ex, y);
			const float edge_noise = fbm(V3(dir.x * 2.5f, dir.y * 2.5f, 0.0f) + seed_offset(s.seed), 4, 0.55f, s.seed);
			const float rr = er * (1.0f + rough * 0.45f * edge_noise);
			const float grain = fbm(V3(x * 3.0f, y * 28.0f, 0.0f) + seed_offset(s.seed + 5u), 3, 0.5f, s.seed + 5u);
			V3 color = cb;
			float alpha = 0.0f;
			// Chipped, freshly exposed surface.
			if (chip > hole && surface != 1) {
				const float chunk = fbm(V3(x * 9.0f, y * 9.0f, 0.0f) + seed_offset(s.seed + 2u), 4, 0.55f, s.seed + 2u);
				const float edge = chip * (0.85f + 0.3f * chunk);
				const float a_chip = smoothstepf(edge, edge - 0.02f, rr);
				const float shade = 0.85f + 0.3f * chunk + (surface == 2 ? 0.2f * grain : 0.0f);
				color = cb * shade;
				alpha = a_chip;
				// Chipped surface sits lower, rough with the chunks / grain.
				h = -a_chip * (0.25f + 0.15f * chunk + (surface == 2 ? 0.08f * grain : 0.0f));
			}
			// Soot / powder burn.
			if (soot > 0.0f) {
				const float sr = maxf(chip, hole * 2.0f) * 1.5f;
				const float n = fbm(V3(x * 6.0f, y * 6.0f, 0.0f) + seed_offset(s.seed + 9u), 4, 0.5f, s.seed + 9u) * 0.5f + 0.5f;
				const float k = saturate(1.0f - rr / sr) * soot * (0.6f + 0.6f * n);
				color = mix3(color, ca, saturate(k * 1.2f));
				alpha = maxf(alpha, saturate(k * 1.1f));
			}
			// Metal: bent, lit petals around the hole.
			if (surface == 1) {
				const float petal_n = gnoise3(angle * 2.0f, 0.0f, 0.0f, s.seed + 21u);
				const float petals = std::cos(angle * 7.0f + petal_n * 3.0f) * 0.5f + 0.5f;
				const float rim_r = hole * (1.9f + 0.6f * petals);
				const float rim = smoothstepf(rim_r, rim_r - hole * 0.25f, rr) * smoothstepf(hole * 0.95f, hole * 1.1f, rr);
				// Petals bend towards the viewer: lit on one side, shaded on the other.
				const float facing = std::cos(angle - 2.2f) * 0.5f + 0.5f;
				const float lit = saturate(0.25f + 0.9f * facing * (0.5f + 0.5f * petals));
				const V3 bright = mix3(cb, V3(1.0f, 1.0f, 1.0f), 0.45f);
				const V3 metal_color = mix3(ca * 2.5f, bright, lit);
				color = mix3(color, metal_color, rim);
				alpha = maxf(alpha, rim);
				h = maxf(h, rim * (0.3f + 0.45f * petals)); // Petals bent outwards.
			}
			// Radial cracks (concrete) / splinters (wood).
			if (cracks > 0 && surface != 1) {
				const float reach = chip + crack_len * (0.95f - chip);
				for (int k = 0; k < cracks; k++) {
					const float a0 = (float(k) + hash_angle(s.seed, k) * 0.8f) / float(cracks) * TAU;
					const float len = reach * (0.55f + 0.45f * hash_angle(s.seed + 7u, k));
					float d;
					float along;
					if (surface == 2) {
						// Splinters run along the grain, up or down.
						const float sx = (hash_angle(s.seed + 11u, k) - 0.5f) * chip * 1.2f;
						const float sign = (k & 1) ? 1.0f : -1.0f;
						d = std::fabs(x - sx - 0.04f * gnoise3(y * 8.0f, float(k), 0.0f, s.seed));
						along = y * sign;
						if (along < 0.0f) {
							continue;
						}
					} else {
						d = radial_crack_distance(x, y, r, a0, rough + 0.3f, s.seed, k);
						along = x * std::cos(a0) + y * std::sin(a0);
						if (along < 0.0f) {
							continue;
						}
					}
					if (along > len) {
						continue;
					}
					const float taper = 1.0f - along / len;
					const float w = 0.006f + 0.008f * taper;
					const float line = smoothstepf(w, w * 0.3f, d) * taper;
					if (line > 0.0f) {
						color = mix3(color, ca, line);
						alpha = maxf(alpha, line);
						h -= 0.45f * line; // Grooves.
					}
				}
			}
			// The hole itself with a shaded inner wall.
			const float hole_r = hole * (1.0f + rough * 0.3f * edge_noise);
			const float wall_r = hole_r * (surface == 1 ? 1.12f : 1.35f);
			if (r < wall_r) {
				const float wall = smoothstepf(wall_r, hole_r * 0.7f, r);
				color = mix3(color, ca * 0.6f, wall);
				h = h + (-1.2f - h) * smoothstepf(wall_r, hole_r * 0.4f, r); // The hole goes in.
				alpha = maxf(alpha, smoothstepf(wall_r, hole_r, r));
			}
			write_flat(r_px, color, alpha, s);
			return;
		}
		case TYPE_SCORCH: {
			const float radius = param(c, 0);
			const float irr = param(c, 1);
			const float darkness = param(c, 2);
			const int streaks = int(param(c, 3) + 0.5f);
			const float streak_strength = param(c, 4);
			const float char_ring = param(c, 5);
			const float detail = param(c, 6);
			const float outline = fbm(V3(dir.x * 1.8f, dir.y * 1.8f, 0.0f) + seed_offset(s.seed), 4, 0.55f, s.seed);
			const float edge = radius * (1.0f + irr * 0.4f * outline);
			const float t = r / maxf(edge, 1e-3f); // 0 centre .. 1 edge
			const float n = fbm(V3(x * detail, y * detail, 0.0f) + seed_offset(s.seed + 3u), 5, 0.55f, s.seed + 3u) * 0.5f + 0.5f;
			float density = saturate(1.0f - t);
			density = std::pow(density, 0.6f) * (0.65f + 0.55f * n);
			// Radial streaks reaching past the edge.
			if (streaks > 0 && streak_strength > 0.0f) {
				float streak = 0.0f;
				for (int k = 0; k < streaks; k++) {
					const float a0 = hash_angle(s.seed + 13u, k) * TAU;
					const float width = 0.02f + 0.06f * hash_angle(s.seed + 17u, k);
					float da = std::fabs(angle - a0);
					da = minf(da, TAU - da);
					const float len = edge * (1.0f + streak_strength * (0.4f + 0.8f * hash_angle(s.seed + 19u, k)));
					const float across = std::exp(-(da * da) / (width * width));
					const float fall = saturate(1.0f - r / len);
					streak = maxf(streak, across * fall * streak_strength);
				}
				density = maxf(density, streak * (0.55f + 0.45f * n));
			}
			// Charred ring.
			if (char_ring > 0.0f) {
				const float ring = std::exp(-std::pow((t - 0.45f) / 0.12f, 2.0f)) * char_ring * (0.7f + 0.5f * n);
				density = saturate(density + ring * 0.5f);
			}
			const float alpha = saturate(density * darkness * 1.25f);
			const V3 color = mix3(ca, cb, saturate(t * 1.1f - n * 0.25f));
			h = (n - 0.5f) * 0.35f * alpha; // Flaky char, nearly flat.
			write_flat(r_px, color, alpha, s);
			return;
		}
		case TYPE_GLASS_CRACK: {
			const float hole = param(c, 0);
			const int radial = int(param(c, 1) + 0.5f);
			const int rings = int(param(c, 2) + 0.5f);
			const float reach = param(c, 3);
			const float lw = param(c, 4);
			const float crushed = param(c, 5);
			const float jag = param(c, 6);
			float line = 0.0f;
			// Radial cracks.
			int sector = 0;
			float sector_a0 = 0.0f;
			float sector_a1 = TAU;
			for (int k = 0; k < radial; k++) {
				const float a0 = (float(k) + (hash_angle(s.seed, k) - 0.5f) * 0.7f) / float(radial) * TAU;
				const float len = reach * (0.6f + 0.4f * hash_angle(s.seed + 3u, k));
				const float along = x * std::cos(a0) + y * std::sin(a0);
				if (along > 0.0f && along < len) {
					const float d = radial_crack_distance(x, y, r, a0, jag, s.seed, k);
					const float w = lw * (1.0f + 1.5f * (1.0f - along / len));
					line = maxf(line, smoothstepf(w, w * 0.25f, d));
				}
			}
			// Ring cracks: chords between neighbouring radial cracks.
			if (rings > 0 && radial > 0) {
				float a = angle < 0.0f ? angle + TAU : angle;
				const float base = a / TAU * float(radial);
				sector = int(std::floor(base));
				for (int k = sector - 1; k <= sector + 1; k++) {
					const int k0 = ((k % radial) + radial) % radial;
					const int k1 = (k0 + 1) % radial;
					sector_a0 = (float(k) + (hash_angle(s.seed, k0) - 0.5f) * 0.7f) / float(radial) * TAU;
					sector_a1 = (float(k + 1) + (hash_angle(s.seed, k1) - 0.5f) * 0.7f) / float(radial) * TAU;
					for (int j = 1; j <= rings; j++) {
						const float base_r = reach * (float(j) / float(rings + 1));
						const float r0 = base_r * (0.8f + 0.4f * hash01(hash3i(j, k0, 1, s.seed)));
						const float r1 = base_r * (0.8f + 0.4f * hash01(hash3i(j, k1, 1, s.seed)));
						// A ring segment only joins cracks that reach that far.
						const float len0 = reach * (0.6f + 0.4f * hash_angle(s.seed + 3u, k0));
						const float len1 = reach * (0.6f + 0.4f * hash_angle(s.seed + 3u, k1));
						if (r0 > len0 * 0.95f || r1 > len1 * 0.95f) {
							continue;
						}
						if (hash01(hash3i(j, k0, 2, s.seed)) < 0.2f) {
							continue; // Some segments are missing.
						}
						const float px = std::cos(sector_a0) * r0, py = std::sin(sector_a0) * r0;
						const float qx = std::cos(sector_a1) * r1, qy = std::sin(sector_a1) * r1;
						const float vx = qx - px, vy = qy - py;
						const float h = saturate(((x - px) * vx + (y - py) * vy) / maxf(vx * vx + vy * vy, 1e-6f));
						const float d = len2(x - px - vx * h, y - py - vy * h);
						line = maxf(line, smoothstepf(lw, lw * 0.25f, d) * 0.85f);
					}
				}
			}
			float alpha = line * 0.9f;
			V3 color = ca;
			h = -0.5f * line;
			// Crushed frosted zone and hole.
			if (crushed > 0.0f) {
				const float cr = maxf(hole * 4.0f, 0.15f) * (0.7f + 0.8f * crushed);
				const float n = fbm(V3(x * 30.0f, y * 30.0f, 0.0f) + seed_offset(s.seed + 5u), 3, 0.5f, s.seed + 5u) * 0.5f + 0.5f;
				const float frost = saturate(std::pow(saturate(1.0f - r / cr), 0.7f) * crushed * (0.7f + 0.9f * n) * 1.3f);
				if (frost > alpha) {
					color = mix3(ca, cb, 0.6f);
				}
				h += (n - 0.5f) * 0.3f * frost; // Crushed, faceted glass.
				alpha = maxf(alpha, saturate(frost));
			}
			if (hole > 0.0f) {
				const float edge = hole * (1.0f + 0.3f * gnoise3(dir.x * 3.0f, dir.y * 3.0f, 0.0f, s.seed + 9u));
				if (r < edge) {
					alpha = 0.0f; // Real hole: see-through.
					h = 0.0f;
				} else if (r < edge + lw * 3.0f) {
					color = ca;
					alpha = 1.0f;
					h = -0.4f; // Broken edge.
				}
			}
			write_flat(r_px, color, alpha, s);
			return;
		}
		case TYPE_SPLATTER: {
			const float size = param(c, 0);
			const float spread = param(c, 2);
			const float bias = param(c, 3);
			const float ragged = param(c, 4);
			const float variation = param(c, 5);
			// Main blob with a ragged outline, pushed towards +X.
			const float bx = x + size * bias * 0.4f;
			const float br = len2(bx, y);
			const float outline = fbm(V3(std::cos(std::atan2(y, bx)) * 2.2f, std::sin(std::atan2(y, bx)) * 2.2f, 0.0f) + seed_offset(s.seed), 4, 0.6f, s.seed);
			float field = size * (1.0f + ragged * 0.5f * outline) - br;
			// Droplets and elongated drops (precomputed in setup_frame).
			for (int i = 0; i < c.disk_count; i++) {
				const float dx = x - c.disks[i][0];
				const float dy = y - c.disks[i][1];
				const float stretch = c.disks[i][3];
				const float da = std::atan2(c.disks[i][1], c.disks[i][0]);
				const float ux = dx * std::cos(da) + dy * std::sin(da);
				const float uy = -dx * std::sin(da) + dy * std::cos(da);
				field = maxf(field, c.disks[i][2] - len2(ux / stretch, uy));
			}
			(void)spread;
			const float alpha = smoothstepf(-0.004f, 0.004f, field);
			const float thick = saturate(field / maxf(size * 0.5f, 1e-3f));
			const float n = fbm(V3(x * 7.0f, y * 7.0f, 0.0f) + seed_offset(s.seed + 4u), 3, 0.5f, s.seed + 4u) * 0.5f + 0.5f;
			const V3 color = mix3(cb, ca, saturate(thick * variation * 1.5f + (n - 0.5f) * variation));
			h = alpha * (0.15f + 0.35f * std::sqrt(thick) + 0.05f * n); // A wet, raised film.
			write_flat(r_px, color, alpha, s);
			return;
		}
		default:
			break;
	}
	write_flat(r_px, V3(), 0.0f, s);
}

// Tileable 2D patterns. (u, v) in 0..1 texture space.
void render_pattern(const FrameContext &c, float u, float v, float *r_px) {
	const Settings &s = *c.s;
	const bool tile = s.tiling;
	if (s.type == TYPE_CAUSTICS) {
		const int scale = int(param(c, 0) + 0.5f);
		const float sharp = param(c, 1);
		const float distortion = param(c, 2);
		const float layers = param(c, 3);
		const float intensity = param(c, 4);
		const float background = param(c, 5);
		const int period = tile ? scale : 0;
		auto layer = [&](int p_scale, int p_period, float p_time, uint32_t p_seed) -> float {
			const float wx = fbm_periodic(u * 2.0f, v * 2.0f, p_time * 0.3f, 3, 0.5f, tile ? 2 : 0, p_seed + 1u);
			const float wy = fbm_periodic(u * 2.0f + 5.2f, v * 2.0f + 1.3f, p_time * 0.3f, 3, 0.5f, tile ? 2 : 0, p_seed + 2u);
			float f1, f2, cell;
			const float px = u * float(p_scale) + wx * distortion * 1.2f;
			const float py = v * float(p_scale) + wy * distortion * 1.2f;
			voronoi2(px + std::sin(p_time * 0.7f) * 0.15f, py + std::cos(p_time * 0.6f) * 0.15f, p_seed, 0.85f, p_period, f1, f2, cell);
			return std::pow(saturate(1.0f - (f2 - f1) * 1.4f), sharp);
		};
		float l = layer(scale, period, c.t, s.seed);
		if (layers > 0.0f) {
			l = l * (1.0f - layers * 0.5f) + layer(scale * 2, tile ? scale * 2 : 0, c.t * 1.3f + 3.0f, s.seed + 77u) * layers * 0.6f;
		}
		const V3 e = mix3(c.color_a, c.color_b, saturate(l)) * (l * 2.0f * intensity);
		float px[4];
		write_pixel(px, e, background, s);
		if (background > 0.0f) {
			// Blend over the water colour.
			const float a = px[3];
			for (int i = 0; i < 3; i++) {
				const float water = (i == 0 ? c.color_a.x : (i == 1 ? c.color_a.y : c.color_a.z));
				const float lit = px[i] * a;
				const float total_a = maxf(a, background);
				px[i] = saturate((lit + water * background * (1.0f - a)) / maxf(total_a, 1e-4f));
			}
			px[3] = saturate(maxf(a, background * s.opacity));
		}
		r_px[0] = px[0];
		r_px[1] = px[1];
		r_px[2] = px[2];
		r_px[3] = px[3];
		return;
	}
	if (s.type == TYPE_CELL_NOISE) {
		const int scale = std::max(1, int(param(c, 0) + 0.5f));
		const int mode = int(param(c, 1) + 0.5f);
		const float jitter = param(c, 2);
		const float edge_width = maxf(param(c, 3), 1e-3f);
		const int octaves = std::max(1, int(param(c, 4) + 0.5f));
		const float contrast = param(c, 5);
		const float threshold = param(c, 6);
		float value = 0.0f;
		float amp = 1.0f;
		float norm = 0.0f;
		int cells = scale;
		for (int i = 0; i < octaves; i++) {
			// Animation drifts each layer on a small circle; a uniform shift
			// keeps the pattern periodic.
			const float drift = c.t * (0.6f + 0.25f * float(i));
			const float px = u * float(cells) + std::sin(drift) * 0.25f;
			const float py = v * float(cells) + std::cos(drift * 0.9f) * 0.25f;
			float f1, f2, cell;
			voronoi2(px, py, s.seed + uint32_t(i) * 131u, jitter, tile ? cells : 0, f1, f2, cell);
			float layer;
			if (mode == 0) {
				layer = saturate(f1 / 0.75f);
			} else if (mode == 1) {
				layer = 1.0f - smoothstepf(0.0f, edge_width * 2.0f, f2 - f1);
			} else {
				layer = cell;
			}
			value += layer * amp;
			norm += amp;
			amp *= 0.5f;
			cells *= 2;
		}
		value = saturate((value / norm - 0.5f) * contrast + 0.5f);
		const float alpha = threshold > 0.0f ? smoothstepf(threshold, minf(threshold + 0.1f, 1.0f), value) : 1.0f;
		write_flat(r_px, mix3(c.color_a, c.color_b, value), alpha, s);
		return;
	}
	if (s.type == TYPE_MARBLE_NOISE) {
		const float frequency = param(c, 0);
		const float turbulence = param(c, 1);
		const float sharpness = maxf(param(c, 2), 0.05f);
		const float angle = param(c, 3) * PI / 180.0f;
		const int octaves = std::max(1, int(param(c, 4) + 0.5f));
		const float contrast = param(c, 5);
		float kx = std::cos(angle) * frequency;
		float ky = std::sin(angle) * frequency;
		if (tile) {
			// A seamless sine needs a whole number of cycles along U and V.
			kx = std::round(kx);
			ky = std::round(ky);
			if (kx == 0.0f && ky == 0.0f) {
				kx = 1.0f;
			}
		}
		const float base = 4.0f;
		const float n = fbm_periodic(u * base, v * base, c.t * 0.25f, octaves, 0.5f, tile ? int(base) : 0, s.seed + 11u);
		const float phase = TAU * (kx * u + ky * v) + turbulence * n * PI;
		float value = std::pow(saturate(1.0f - std::fabs(std::sin(phase * 0.5f))), sharpness);
		value = saturate((value - 0.5f) * contrast + 0.5f);
		write_flat(r_px, mix3(c.color_a, c.color_b, value), 1.0f, s);
		return;
	}
	// TYPE_NOISE
	const int scale = int(param(c, 0) + 0.5f);
	const int octaves = int(param(c, 1) + 0.5f);
	const float gain = param(c, 2);
	const int mode = int(param(c, 3) + 0.5f);
	const float contrast = param(c, 4);
	const float threshold = param(c, 5);
	const float z = c.t * 0.25f;
	float value = 0.0f;
	float amp = 1.0f;
	float norm = 0.0f;
	float freq = float(scale);
	for (int i = 0; i < octaves; i++) {
		float n = gnoise3(u * freq, v * freq, z * freq * 0.25f + float(i) * 13.1f, s.seed + uint32_t(i) * 101u, tile ? int(freq) : 0);
		if (mode == 1) {
			n = std::fabs(n) * 2.0f - 1.0f;
		} else if (mode == 2) {
			n = 1.0f - std::fabs(n);
			n = n * n * 2.0f - 1.0f;
		}
		value += n * amp;
		norm += amp;
		amp *= gain;
		freq *= 2.0f;
	}
	value = value / norm * 0.5f + 0.5f;
	value = saturate((value - 0.5f) * contrast + 0.5f);
	const float alpha = threshold > 0.0f ? smoothstepf(threshold, minf(threshold + 0.1f, 1.0f), value) : 1.0f;
	write_flat(r_px, mix3(c.color_a, c.color_b, value), alpha, s);
}

bool is_decal_type(int p_type) {
	return p_type == TYPE_BULLET_HOLE || p_type == TYPE_SCORCH || p_type == TYPE_GLASS_CRACK || p_type == TYPE_SPLATTER;
}

// Renders one sample at texture coordinates (u, v) in 0..1, v down.
void render_uv(const FrameContext &c, float u, float v, float *r_out) {
	const Settings &s = *c.s;
	if (c.info && c.info->supports_tiling) {
		render_pattern(c, u, v, r_out);
		return;
	}
	// Centered coordinates, Y up.
	float x = u * 2.0f - 1.0f;
	float y = 1.0f - v * 2.0f;
	if (s.rotation_degrees != 0.0f) {
		const float a = -s.rotation_degrees * PI / 180.0f;
		const float ca = std::cos(a);
		const float sa = std::sin(a);
		const float rx = x * ca - y * sa;
		const float ry = x * sa + y * ca;
		x = rx;
		y = ry;
	}
	if (s.polar) {
		// Unwrap: angle -> X, radius -> Y.
		const float r = len2(x, y);
		const float a = std::atan2(y, x);
		x = (a / TAU + 0.5f) * 2.0f - 1.0f;
		y = 1.0f - saturate(r) * 2.0f;
	}
	switch (s.type) {
		case TYPE_EXPLOSION:
			render_explosion(c, x, y, r_out, false);
			break;
		case TYPE_EXPLOSION_RING:
			render_explosion(c, x, y, r_out, true);
			break;
		case TYPE_FIRE:
			render_fire(c, x, y, r_out);
			break;
		case TYPE_SMOKE:
			render_smoke(c, x, y, r_out, false);
			break;
		case TYPE_CLOUD:
			render_smoke(c, x, y, r_out, true);
			break;
		case TYPE_SOFT_SMOKE:
			render_soft_smoke(c, x, y, r_out);
			break;
		case TYPE_DEBRIS:
			render_debris(c, x, y, r_out);
			break;
		case TYPE_WATER_SPLASH:
			render_water_splash(c, x, y, r_out);
			break;
		case TYPE_WATER_PLUME:
			render_water_plume(c, x, y, r_out);
			break;
		case TYPE_FOAM:
			render_foam(c, x, y, r_out);
			break;
		case TYPE_UNDERWATER_BUBBLE:
			render_underwater_bubble(c, x, y, r_out);
			break;
		case TYPE_BULLET_HOLE:
		case TYPE_SCORCH:
		case TYPE_GLASS_CRACK:
		case TYPE_SPLATTER:
			render_decal(c, x, y, r_out);
			break;
		default:
			render_2d(c, x, y, r_out);
			break;
	}
}

void render_pixel(const FrameContext &c, int px, int py, int size, float *r_out) {
	const float u = (float(px) + 0.5f) / float(size);
	const float v = (float(py) + 0.5f) / float(size);
	if (!c.s->output_normal || !is_decal_type(c.s->type)) {
		render_uv(c, u, v, r_out);
		return;
	}
	// Normal map from the decal height field: central differences over one
	// pixel, scaled so the strength looks the same at every resolution.
	float sample[4];
	const float step = 1.0f / float(size);
	render_uv(c, u, v, r_out);
	const float alpha = r_out[3];
	render_uv(c, u - step, v, sample);
	const float h_left = t_decal_height;
	render_uv(c, u + step, v, sample);
	const float h_right = t_decal_height;
	render_uv(c, u, v - step, sample);
	const float h_up = t_decal_height;
	render_uv(c, u, v + step, sample);
	const float h_down = t_decal_height;
	const float k = c.s->normal_strength * float(size) / 512.0f;
	V3 n(-(h_right - h_left) * k, (h_down - h_up) * k, 1.0f);
	n = normalize3(n);
	r_out[0] = n.x * 0.5f + 0.5f;
	r_out[1] = n.y * 0.5f + 0.5f;
	r_out[2] = n.z * 0.5f + 0.5f;
	r_out[3] = alpha;
}

} // namespace

/* Public API ------------------------------------------------------------- */

int get_type_count() {
	return TYPE_COUNT;
}

bool has_height_normal(int p_type) {
	return is_decal_type(p_type);
}

const TypeInfo *get_type_info_by_index(int p_index) {
	if (p_index < 0 || p_index >= TYPE_COUNT) {
		return nullptr;
	}
	return &type_table().types[p_index];
}

const TypeInfo *get_type_info(int p_id) {
	const TypeTable &table = type_table();
	for (int i = 0; i < TYPE_COUNT; i++) {
		if (table.types[i].id == p_id) {
			return &table.types[i];
		}
	}
	return nullptr;
}

int find_param(const TypeInfo *p_info, const char *p_key) {
	if (!p_info || !p_key) {
		return -1;
	}
	for (int i = 0; i < p_info->param_count; i++) {
		if (std::strcmp(p_info->params[i].key, p_key) == 0) {
			return i;
		}
	}
	return -1;
}

void apply_type_defaults(Settings &r_settings, bool p_colors) {
	const TypeInfo *info = get_type_info(r_settings.type);
	if (!info) {
		return;
	}
	for (int i = 0; i < MAX_PARAMS; i++) {
		r_settings.params[i] = i < info->param_count ? info->params[i].default_value : 0.0f;
	}
	if (p_colors) {
		for (int i = 0; i < 3; i++) {
			r_settings.color_a[i] = info->color_a[i];
			r_settings.color_b[i] = info->color_b[i];
		}
	}
}

void render_rows(const Settings &p_settings, int p_size, float *r_rgba, int p_row_begin, int p_row_end) {
	if (p_size <= 0 || !r_rgba) {
		return;
	}
	// The frame context holds per-frame geometry (bolts, disks). It is rebuilt
	// per call so rows can be rendered independently on worker threads.
	static thread_local FrameContext context;
	setup_frame(context, p_settings);
	for (int py = p_row_begin; py < p_row_end; py++) {
		for (int px = 0; px < p_size; px++) {
			render_pixel(context, px, py, p_size, r_rgba + (size_t(py) * size_t(p_size) + size_t(px)) * 4);
		}
	}
}

void render_rows_rgba8(const Settings &p_settings, int p_size, uint8_t *r_rgba8, int p_row_begin, int p_row_end) {
	if (p_size <= 0 || p_size > 4096 || !r_rgba8) {
		return;
	}
	static thread_local FrameContext context;
	static thread_local float row[4096 * 4];
	setup_frame(context, p_settings);
	for (int py = p_row_begin; py < p_row_end; py++) {
		for (int px = 0; px < p_size; px++) {
			render_pixel(context, px, py, p_size, row + px * 4);
		}
		uint8_t *out = r_rgba8 + size_t(py) * size_t(p_size) * 4;
		for (int i = 0; i < p_size * 4; i++) {
			const float v = row[i] * 255.0f + 0.5f;
			out[i] = uint8_t(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
		}
	}
}

} // namespace VFXTextureGenerator
