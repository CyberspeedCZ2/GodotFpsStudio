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

constexpr int TYPE_COUNT = 21;

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

void render_decal(const FrameContext &c, float x, float y, float *r_px) {
	const Settings &s = *c.s;
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
					}
				}
			}
			// The hole itself with a shaded inner wall.
			const float hole_r = hole * (1.0f + rough * 0.3f * edge_noise);
			const float wall_r = hole_r * (surface == 1 ? 1.12f : 1.35f);
			if (r < wall_r) {
				const float wall = smoothstepf(wall_r, hole_r * 0.7f, r);
				color = mix3(color, ca * 0.6f, wall);
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
			// Crushed frosted zone and hole.
			if (crushed > 0.0f) {
				const float cr = maxf(hole * 4.0f, 0.15f) * (0.7f + 0.8f * crushed);
				const float n = fbm(V3(x * 30.0f, y * 30.0f, 0.0f) + seed_offset(s.seed + 5u), 3, 0.5f, s.seed + 5u) * 0.5f + 0.5f;
				const float frost = saturate(std::pow(saturate(1.0f - r / cr), 0.7f) * crushed * (0.7f + 0.9f * n) * 1.3f);
				if (frost > alpha) {
					color = mix3(ca, cb, 0.6f);
				}
				alpha = maxf(alpha, saturate(frost));
			}
			if (hole > 0.0f) {
				const float edge = hole * (1.0f + 0.3f * gnoise3(dir.x * 3.0f, dir.y * 3.0f, 0.0f, s.seed + 9u));
				if (r < edge) {
					alpha = 0.0f; // Real hole: see-through.
				} else if (r < edge + lw * 3.0f) {
					color = ca;
					alpha = 1.0f;
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

void render_pixel(const FrameContext &c, int px, int py, int size, float *r_out) {
	const Settings &s = *c.s;
	float u = (float(px) + 0.5f) / float(size);
	float v = (float(py) + 0.5f) / float(size);
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

} // namespace

/* Public API ------------------------------------------------------------- */

int get_type_count() {
	return TYPE_COUNT;
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
