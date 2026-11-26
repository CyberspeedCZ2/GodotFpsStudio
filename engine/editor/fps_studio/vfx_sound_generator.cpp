/**************************************************************************/
/*  vfx_sound_generator.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "vfx_sound_generator.h"

#include "core/io/file_access.h"
#include "core/math/math_funcs.h"
#include "core/templates/local_vector.h"
#include "scene/resources/audio/audio_stream_wav.h"

namespace {

constexpr float SR = float(VFXSoundGenerator::MIX_RATE);
constexpr float TAU_F = 6.28318530718f;

// Small deterministic RNG (xorshift32).
struct Rng {
	uint32_t state;
	explicit Rng(uint32_t p_seed) :
			state(p_seed * 2654435761u + 0x9E3779B9u) {
		if (state == 0) {
			state = 0x1234567u;
		}
		for (int i = 0; i < 4; i++) {
			next();
		}
	}
	uint32_t next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	float randf() { return float(next() >> 8) / 16777216.0f; } // [0, 1)
	float bipolar() { return randf() * 2.0f - 1.0f; }
	float range(float p_a, float p_b) { return p_a + (p_b - p_a) * randf(); }
};

// One-pole low-pass.
struct OnePole {
	float y = 0.0f;
	float process(float p_x, float p_cutoff) {
		const float a = 1.0f - Math::exp(-TAU_F * MIN(p_cutoff, SR * 0.45f) / SR);
		y += a * (p_x - y);
		return y;
	}
};

// Topology-preserving state variable filter (stable under modulation).
struct SVF {
	float ic1 = 0.0f;
	float ic2 = 0.0f;
	float lp = 0.0f, bp = 0.0f, hp = 0.0f;
	void process(float p_x, float p_cutoff, float p_q) {
		const float g = Math::tan(Math::PI * CLAMP(p_cutoff, 10.0f, SR * 0.45f) / SR);
		const float k = 1.0f / MAX(p_q, 0.05f);
		const float a1 = 1.0f / (1.0f + g * (g + k));
		const float a2 = g * a1;
		const float a3 = g * a2;
		const float v3 = p_x - ic2;
		const float v1 = a1 * ic1 + a2 * v3;
		const float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.0f * v1 - ic1;
		ic2 = 2.0f * v2 - ic2;
		lp = v2;
		bp = v1;
		hp = p_x - k * v1 - v2;
	}
};

class Buffer {
public:
	LocalVector<float> data;

	explicit Buffer(float p_seconds) {
		data.resize(uint32_t(MAX(1.0f, p_seconds * SR)));
		for (float &v : data) {
			v = 0.0f;
		}
	}
	int size() const { return int(data.size()); }
	float seconds() const { return float(data.size()) / SR; }
	void add(int p_index, float p_value) {
		if (p_index >= 0 && p_index < int(data.size())) {
			data[p_index] += p_value;
		}
	}
};

inline float env_exp(float p_t, float p_rate) {
	return p_t < 0.0f ? 0.0f : Math::exp(-p_t * p_rate);
}

// Attack then exponential decay.
inline float env_ad(float p_t, float p_attack, float p_rate) {
	if (p_t < 0.0f) {
		return 0.0f;
	}
	if (p_t < p_attack) {
		return p_t / MAX(p_attack, 1e-5f);
	}
	return Math::exp(-(p_t - p_attack) * p_rate);
}

// A struck resonant body: decaying sine modes with inharmonic ratios.
void add_modal_hit(Buffer &r_buf, Rng &r_rng, float p_time, float p_base_hz, const float *p_ratios, const float *p_decays, int p_modes, float p_gain) {
	const int start = int(p_time * SR);
	float longest = 0.0f;
	for (int m = 0; m < p_modes; m++) {
		longest = MAX(longest, p_decays[m]);
	}
	const int length = int(MIN(longest * 7.0f, 3.0f) * SR);
	for (int m = 0; m < p_modes; m++) {
		const float freq = p_base_hz * p_ratios[m] * r_rng.range(0.985f, 1.015f);
		if (freq >= SR * 0.45f) {
			continue;
		}
		const float amp = p_gain / (1.0f + m * 0.6f) * r_rng.range(0.7f, 1.0f);
		const float rate = 1.0f / MAX(p_decays[m], 1e-3f);
		const float phase = r_rng.randf() * TAU_F;
		const float w = TAU_F * freq / SR;
		for (int i = 0; i < length; i++) {
			const float t = float(i) / SR;
			r_buf.add(start + i, Math::sin(w * float(i) + phase) * amp * Math::exp(-t * rate) * MIN(1.0f, t * 4000.0f));
		}
	}
}

// A short filtered noise burst (click, crack, grain).
void add_noise_burst(Buffer &r_buf, Rng &r_rng, float p_time, float p_length, float p_cutoff, float p_q, float p_gain, bool p_highpass = false) {
	SVF svf;
	const int start = int(p_time * SR);
	const int length = int(p_length * SR);
	const float rate = 6.0f / MAX(p_length, 1e-4f);
	for (int i = 0; i < length; i++) {
		const float t = float(i) / SR;
		svf.process(r_rng.bipolar(), p_cutoff, p_q);
		const float s = p_highpass ? svf.hp : svf.bp;
		r_buf.add(start + i, s * p_gain * env_ad(t, 0.0008f, rate));
	}
}

// Crackle grains spread over a time range (sparks, fire, debris).
void add_crackle(Buffer &r_buf, Rng &r_rng, float p_from, float p_to, int p_count, float p_low_hz, float p_high_hz, float p_gain, float p_early_bias = 1.0f) {
	for (int g = 0; g < p_count; g++) {
		const float u = Math::pow(r_rng.randf(), p_early_bias);
		const float t = p_from + (p_to - p_from) * u;
		const float fade = p_early_bias > 1.0f ? (1.0f - 0.7f * u) : 1.0f;
		add_noise_burst(r_buf, r_rng, t, r_rng.range(0.002f, 0.009f), r_rng.range(p_low_hz, p_high_hz), r_rng.range(1.5f, 4.0f), p_gain * fade * r_rng.range(0.3f, 1.0f));
	}
}

// Continuous noise through a band-pass, multiplied by an envelope callback.
template <typename Env>
void add_band_noise(Buffer &r_buf, Rng &r_rng, float p_from, float p_length, float p_cutoff_start, float p_cutoff_end, float p_q, float p_gain, Env p_env, int p_mode = 1) {
	SVF svf;
	const int start = int(p_from * SR);
	const int length = int(p_length * SR);
	for (int i = 0; i < length; i++) {
		const float u = float(i) / MAX(1, length - 1);
		const float t = float(i) / SR;
		const float cutoff = p_cutoff_start * Math::pow(p_cutoff_end / p_cutoff_start, u);
		svf.process(r_rng.bipolar(), cutoff, p_q);
		const float s = p_mode == 0 ? svf.lp : (p_mode == 1 ? svf.bp : svf.hp);
		r_buf.add(start + i, s * p_gain * p_env(t));
	}
}

// Sine with an exponential pitch glide (booms, thumps, chirps).
template <typename Env>
void add_glide(Buffer &r_buf, float p_from, float p_length, float p_hz_start, float p_hz_end, float p_gain, Env p_env) {
	const int start = int(p_from * SR);
	const int length = int(p_length * SR);
	float phase = 0.0f;
	for (int i = 0; i < length; i++) {
		const float u = float(i) / MAX(1, length - 1);
		const float t = float(i) / SR;
		const float hz = p_hz_start * Math::pow(p_hz_end / p_hz_start, u);
		phase += TAU_F * hz / SR;
		r_buf.add(start + i, Math::sin(phase) * p_gain * p_env(t));
	}
}

void finalize(Buffer &r_buf, float p_drive) {
	// Gentle DC removal, soft clip, normalize to about -1 dBFS.
	float x_prev = 0.0f;
	float y_prev = 0.0f;
	float peak = 0.0f;
	for (float &v : r_buf.data) {
		const float y = v - x_prev + 0.9995f * y_prev;
		x_prev = v;
		y_prev = y;
		v = Math::tanh(y * p_drive);
		peak = MAX(peak, Math::abs(v));
	}
	if (peak > 1e-6f) {
		const float gain = 0.891f / peak;
		for (float &v : r_buf.data) {
			v *= gain;
		}
	}
}

// Short fades so one-shots start and end without clicks.
void fade_edges(Buffer &r_buf, float p_in, float p_out) {
	const int n = r_buf.size();
	const int in = MIN(n, int(p_in * SR));
	const int out = MIN(n, int(p_out * SR));
	for (int i = 0; i < in; i++) {
		r_buf.data[i] *= float(i) / MAX(1, in);
	}
	for (int i = 0; i < out; i++) {
		r_buf.data[n - 1 - i] *= float(i) / MAX(1, out);
	}
}

// Folds the extra tail over the head with an equal-power crossfade so the
// result loops seamlessly; returns the loop length in frames.
int make_loop(Buffer &r_buf, float p_crossfade) {
	const int xf = int(p_crossfade * SR);
	const int loop = r_buf.size() - xf;
	for (int i = 0; i < xf; i++) {
		const float u = float(i) / float(xf);
		const float a = Math::sin(u * Math::PI * 0.5f);
		const float b = Math::cos(u * Math::PI * 0.5f);
		r_buf.data[i] = r_buf.data[i] * a + r_buf.data[loop + i] * b;
	}
	r_buf.data.resize(loop);
	return loop;
}

// --- Sounds --------------------------------------------------------------

void synth_explosion(Buffer &b, Rng &rng) {
	// Crack.
	add_noise_burst(b, rng, 0.0f, 0.03f, 4500.0f, 0.7f, 1.4f, true);
	// Body: low-passed noise whose cutoff falls from bright to dull.
	add_band_noise(b, rng, 0.0f, b.seconds(), 2600.0f, 160.0f, 0.6f, 2.2f, [](float t) { return env_ad(t, 0.004f, 2.4f); }, 0);
	// Sub boom with a pitch drop.
	add_glide(b, 0.0f, 1.6f, 70.0f, 32.0f, 1.3f, [](float t) { return env_ad(t, 0.006f, 3.0f); });
	add_glide(b, 0.0f, 0.6f, 140.0f, 55.0f, 0.5f, [](float t) { return env_ad(t, 0.003f, 7.0f); });
	// Long rumble tail.
	add_band_noise(b, rng, 0.05f, b.seconds() - 0.05f, 300.0f, 90.0f, 0.5f, 1.6f, [](float t) { return env_ad(t, 0.15f, 1.3f); }, 0);
	// Falling debris.
	add_crackle(b, rng, 0.25f, 1.8f, 60, 1500.0f, 6000.0f, 0.25f, 1.6f);
	finalize(b, 1.6f);
	fade_edges(b, 0.0005f, 0.25f);
}

void synth_shell_clink(Buffer &b, Rng &rng) {
	static const float ratios[] = { 1.0f, 2.76f, 5.40f, 8.93f, 13.3f };
	static const float decays[] = { 0.16f, 0.11f, 0.07f, 0.045f, 0.03f };
	const float base = rng.range(2900.0f, 3500.0f);
	float t = 0.0f;
	float gain = 1.0f;
	for (int bounce = 0; bounce < 4; bounce++) {
		add_noise_burst(b, rng, t, 0.003f, 6000.0f, 1.0f, 0.6f * gain, true);
		add_modal_hit(b, rng, t, base * rng.range(0.97f, 1.03f), ratios, decays, 5, gain);
		t += rng.range(0.08f, 0.13f) * Math::pow(0.72f, float(bounce));
		gain *= rng.range(0.4f, 0.6f);
	}
	finalize(b, 1.2f);
	fade_edges(b, 0.0005f, 0.05f);
}

void synth_sparks(Buffer &b, Rng &rng) {
	add_crackle(b, rng, 0.0f, b.seconds() * 0.85f, 140, 2500.0f, 9000.0f, 0.7f, 1.8f);
	add_band_noise(b, rng, 0.0f, b.seconds(), 6500.0f, 5000.0f, 1.2f, 0.35f, [](float t) { return env_ad(t, 0.005f, 4.5f); }, 1);
	add_noise_burst(b, rng, 0.0f, 0.02f, 5000.0f, 0.8f, 0.9f, true);
	finalize(b, 1.4f);
	fade_edges(b, 0.0005f, 0.08f);
}

void synth_glass_shatter(Buffer &b, Rng &rng) {
	add_noise_burst(b, rng, 0.0f, 0.06f, 3200.0f, 0.8f, 1.6f, false);
	add_noise_burst(b, rng, 0.0f, 0.02f, 7000.0f, 0.7f, 0.9f, true);
	static const float ratio[] = { 1.0f, 2.41f };
	for (int p = 0; p < 55; p++) {
		const float u = Math::pow(rng.randf(), 1.7f);
		const float t = u * 0.9f;
		const float decay[] = { rng.range(0.04f, 0.22f), rng.range(0.02f, 0.08f) };
		add_modal_hit(b, rng, t, rng.range(2200.0f, 8500.0f), ratio, decay, 2, rng.range(0.15f, 0.45f) * (1.0f - 0.6f * u));
	}
	add_crackle(b, rng, 0.05f, 1.0f, 50, 3000.0f, 9000.0f, 0.3f, 1.5f);
	finalize(b, 1.3f);
	fade_edges(b, 0.0005f, 0.1f);
}

void synth_impact_concrete(Buffer &b, Rng &rng) {
	add_noise_burst(b, rng, 0.0f, 0.05f, 2600.0f, 0.6f, 1.6f, false);
	add_noise_burst(b, rng, 0.0f, 0.012f, 6000.0f, 0.7f, 0.8f, true);
	add_glide(b, 0.0f, 0.12f, 180.0f, 90.0f, 0.7f, [](float t) { return env_ad(t, 0.001f, 35.0f); });
	add_crackle(b, rng, 0.02f, 0.3f, 14, 1800.0f, 5000.0f, 0.35f, 1.5f);
	finalize(b, 1.5f);
	fade_edges(b, 0.0003f, 0.05f);
}

void synth_impact_metal(Buffer &b, Rng &rng) {
	static const float ratios[] = { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f };
	static const float decays[] = { 0.38f, 0.25f, 0.16f, 0.1f, 0.06f };
	add_noise_burst(b, rng, 0.0f, 0.008f, 7000.0f, 0.8f, 1.1f, true);
	add_modal_hit(b, rng, 0.0f, rng.range(1500.0f, 2100.0f), ratios, decays, 5, 1.0f);
	add_glide(b, 0.0f, 0.08f, 300.0f, 160.0f, 0.4f, [](float t) { return env_ad(t, 0.001f, 40.0f); });
	finalize(b, 1.3f);
	fade_edges(b, 0.0003f, 0.08f);
}

void synth_impact_wood(Buffer &b, Rng &rng) {
	static const float ratios[] = { 1.0f, 2.61f, 4.12f };
	static const float decays[] = { 0.07f, 0.04f, 0.025f };
	add_noise_burst(b, rng, 0.0f, 0.04f, 900.0f, 1.2f, 1.5f, false);
	add_noise_burst(b, rng, 0.0f, 0.01f, 4000.0f, 0.8f, 0.6f, true);
	add_modal_hit(b, rng, 0.0f, rng.range(200.0f, 260.0f), ratios, decays, 3, 1.0f);
	add_crackle(b, rng, 0.01f, 0.2f, 8, 1200.0f, 3500.0f, 0.3f, 1.5f);
	finalize(b, 1.4f);
	fade_edges(b, 0.0003f, 0.05f);
}

void synth_flesh_hit(Buffer &b, Rng &rng, float p_time, float p_gain) {
	add_glide(b, p_time, 0.18f, rng.range(90.0f, 120.0f), 50.0f, 0.9f * p_gain, [](float t) { return env_ad(t, 0.002f, 25.0f); });
	add_band_noise(b, rng, p_time, 0.25f, 700.0f, 350.0f, 0.8f, 1.6f * p_gain, [](float t) { return env_ad(t, 0.002f, 16.0f); }, 0);
	// Wet squish: band noise with a fast tremolo.
	add_band_noise(b, rng, p_time + 0.01f, 0.2f, 1600.0f, 900.0f, 2.0f, 0.8f * p_gain, [](float t) { return env_ad(t, 0.01f, 14.0f) * (0.6f + 0.4f * Math::sin(t * TAU_F * 38.0f)); }, 1);
}

void synth_flesh_impact(Buffer &b, Rng &rng) {
	synth_flesh_hit(b, rng, 0.0f, 1.0f);
	finalize(b, 1.4f);
	fade_edges(b, 0.0005f, 0.05f);
}

void synth_gibs(Buffer &b, Rng &rng) {
	synth_flesh_hit(b, rng, 0.0f, 1.0f);
	float t = 0.0f;
	for (int i = 0; i < 4; i++) {
		t += rng.range(0.07f, 0.16f);
		synth_flesh_hit(b, rng, t, rng.range(0.3f, 0.6f));
	}
	add_band_noise(b, rng, 0.0f, 0.6f, 1100.0f, 600.0f, 1.5f, 0.6f, [](float x) { return env_ad(x, 0.02f, 5.0f) * (0.5f + 0.5f * Math::sin(x * TAU_F * 23.0f)); }, 1);
	finalize(b, 1.5f);
	fade_edges(b, 0.0005f, 0.08f);
}

void synth_water_splash(Buffer &b, Rng &rng) {
	add_band_noise(b, rng, 0.0f, 0.9f, 700.0f, 2800.0f, 0.9f, 1.8f, [](float t) { return env_ad(t, 0.012f, 4.2f); }, 1);
	add_glide(b, 0.0f, 0.1f, 160.0f, 80.0f, 0.5f, [](float t) { return env_ad(t, 0.002f, 30.0f); });
	for (int i = 0; i < 26; i++) {
		const float t = 0.05f + Math::pow(rng.randf(), 1.4f) * 0.8f;
		const float f0 = rng.range(400.0f, 1200.0f);
		add_glide(b, t, 0.04f, f0, f0 * rng.range(1.6f, 2.4f), rng.range(0.08f, 0.25f), [](float x) { return env_ad(x, 0.002f, 70.0f); });
	}
	finalize(b, 1.3f);
	fade_edges(b, 0.001f, 0.08f);
}

void synth_electric_zap(Buffer &b, Rng &rng) {
	const int n = b.size();
	SVF svf;
	float phase = 0.0f;
	float gate = 1.0f;
	int gate_left = 0;
	for (int i = 0; i < n; i++) {
		const float t = float(i) / SR;
		if (--gate_left <= 0) {
			gate = rng.randf() < 0.65f ? rng.range(0.5f, 1.0f) : 0.0f;
			gate_left = int(rng.range(0.002f, 0.012f) * SR);
		}
		phase += 120.0f / SR;
		const float saw = (phase - Math::floor(phase)) * 2.0f - 1.0f;
		svf.process(rng.bipolar() * 0.7f + saw * 0.6f, 3200.0f, 1.4f);
		b.data[i] += (svf.bp * 1.6f + saw * 0.25f) * gate * env_ad(t, 0.002f, 5.5f);
	}
	add_noise_burst(b, rng, 0.0f, 0.015f, 6000.0f, 0.8f, 1.0f, true);
	finalize(b, 1.8f);
	fade_edges(b, 0.0005f, 0.05f);
}

// Loops: the generators fill the buffer (loop length + crossfade).

void synth_steam_hiss(Buffer &b, Rng &rng) {
	add_band_noise(b, rng, 0.0f, b.seconds(), 4800.0f, 4800.0f, 0.7f, 1.0f, [](float t) { return 0.85f + 0.15f * Math::sin(t * TAU_F * 5.3f); }, 1);
	add_band_noise(b, rng, 0.0f, b.seconds(), 2200.0f, 2200.0f, 0.5f, 0.4f, [](float) { return 1.0f; }, 2);
}

void synth_fire_loop(Buffer &b, Rng &rng) {
	add_band_noise(b, rng, 0.0f, b.seconds(), 380.0f, 380.0f, 0.5f, 1.6f, [](float t) { return 0.8f + 0.2f * Math::sin(t * TAU_F * 1.7f) * Math::sin(t * TAU_F * 0.6f); }, 0);
	add_band_noise(b, rng, 0.0f, b.seconds(), 1500.0f, 1500.0f, 0.6f, 0.3f, [](float) { return 1.0f; }, 1);
	add_crackle(b, rng, 0.0f, b.seconds() - 0.01f, int(b.seconds() * 28.0f), 1500.0f, 6500.0f, 0.55f);
}

void synth_rocket_loop(Buffer &b, Rng &rng) {
	add_band_noise(b, rng, 0.0f, b.seconds(), 650.0f, 650.0f, 0.5f, 2.2f, [](float t) { return 0.85f + 0.15f * Math::sin(t * TAU_F * 29.0f); }, 0);
	add_band_noise(b, rng, 0.0f, b.seconds(), 2400.0f, 2400.0f, 0.8f, 0.6f, [](float) { return 1.0f; }, 1);
	add_crackle(b, rng, 0.0f, b.seconds() - 0.01f, int(b.seconds() * 20.0f), 1000.0f, 4000.0f, 0.3f);
}

void synth_energy_hum(Buffer &b, Rng &rng, float p_loop_seconds) {
	// Whole cycles per loop so the tones line up at the seam.
	auto cycles = [&](float p_hz) { return Math::round(p_hz * p_loop_seconds) / p_loop_seconds; };
	const float f1 = cycles(110.0f);
	const float vib = cycles(3.0f);
	const int n = b.size();
	for (int i = 0; i < n; i++) {
		const float t = float(i) / SR;
		const float wobble = 1.0f + 0.004f * Math::sin(TAU_F * vib * t);
		const float s = Math::sin(TAU_F * f1 * t * wobble) * 0.6f + Math::sin(TAU_F * f1 * 2.0f * t) * 0.3f + Math::sin(TAU_F * f1 * 3.01f * t) * 0.18f + Math::sin(TAU_F * f1 * 5.0f * t) * 0.06f;
		b.data[i] += s * (0.85f + 0.15f * Math::sin(TAU_F * cycles(0.5f) * t));
	}
	add_band_noise(b, rng, 0.0f, b.seconds(), 3500.0f, 3500.0f, 2.0f, 0.12f, [](float) { return 1.0f; }, 1);
	add_crackle(b, rng, 0.0f, b.seconds() - 0.01f, int(b.seconds() * 6.0f), 3000.0f, 8000.0f, 0.12f);
}

void synth_flare_burn(Buffer &b, Rng &rng) {
	add_band_noise(b, rng, 0.0f, b.seconds(), 3000.0f, 3000.0f, 0.8f, 1.2f, [](float t) { return 0.9f + 0.1f * Math::sin(t * TAU_F * 7.0f); }, 1);
	add_band_noise(b, rng, 0.0f, b.seconds(), 500.0f, 500.0f, 0.5f, 0.6f, [](float) { return 1.0f; }, 0);
	add_crackle(b, rng, 0.0f, b.seconds() - 0.01f, int(b.seconds() * 40.0f), 2500.0f, 8000.0f, 0.4f);
}

void synth_smoke_grenade_hiss(Buffer &b, Rng &rng) {
	add_band_noise(b, rng, 0.0f, b.seconds(), 2600.0f, 2600.0f, 0.45f, 1.0f, [](float t) { return 0.8f + 0.2f * Math::sin(t * TAU_F * 2.2f); }, 1);
	add_band_noise(b, rng, 0.0f, b.seconds(), 900.0f, 900.0f, 0.5f, 0.35f, [](float) { return 1.0f; }, 0);
}

} // namespace

String VFXSoundGenerator::get_sound_name(int p_sound) {
	switch (p_sound) {
		case SOUND_EXPLOSION:
			return "Explosion";
		case SOUND_SHELL_CLINK:
			return "Shell Clink";
		case SOUND_SPARKS:
			return "Sparks";
		case SOUND_GLASS_SHATTER:
			return "Glass Shatter";
		case SOUND_BULLET_IMPACT_CONCRETE:
			return "Bullet Impact (Concrete)";
		case SOUND_BULLET_IMPACT_METAL:
			return "Bullet Impact (Metal)";
		case SOUND_BULLET_IMPACT_WOOD:
			return "Bullet Impact (Wood)";
		case SOUND_FLESH_IMPACT:
			return "Flesh Impact";
		case SOUND_WATER_SPLASH:
			return "Water Splash";
		case SOUND_ELECTRIC_ZAP:
			return "Electric Zap";
		case SOUND_STEAM_HISS:
			return "Steam Hiss (Loop)";
		case SOUND_FIRE_LOOP:
			return "Fire (Loop)";
		case SOUND_ROCKET_LOOP:
			return "Rocket (Loop)";
		case SOUND_ENERGY_HUM:
			return "Energy Hum (Loop)";
		case SOUND_FLARE_BURN:
			return "Flare Burn (Loop)";
		case SOUND_SMOKE_GRENADE_HISS:
			return "Smoke Grenade Hiss (Loop)";
		case SOUND_GIBS:
			return "Gibs";
	}
	return "Unknown";
}

bool VFXSoundGenerator::is_loop(int p_sound) {
	switch (p_sound) {
		case SOUND_STEAM_HISS:
		case SOUND_FIRE_LOOP:
		case SOUND_ROCKET_LOOP:
		case SOUND_ENERGY_HUM:
		case SOUND_FLARE_BURN:
		case SOUND_SMOKE_GRENADE_HISS:
			return true;
	}
	return false;
}

Ref<AudioStreamWAV> VFXSoundGenerator::generate(int p_sound, uint32_t p_seed) {
	ERR_FAIL_INDEX_V(p_sound, SOUND_COUNT, Ref<AudioStreamWAV>());
	Rng rng(p_seed * 977u + uint32_t(p_sound) * 7919u + 1u);

	static const float LOOP_SECONDS = 3.0f;
	static const float CROSSFADE = 0.35f;
	const bool loop = is_loop(p_sound);
	float seconds = 1.0f;
	switch (p_sound) {
		case SOUND_EXPLOSION:
			seconds = 2.8f;
			break;
		case SOUND_SHELL_CLINK:
			seconds = 0.7f;
			break;
		case SOUND_SPARKS:
			seconds = 0.9f;
			break;
		case SOUND_GLASS_SHATTER:
			seconds = 1.4f;
			break;
		case SOUND_BULLET_IMPACT_CONCRETE:
		case SOUND_BULLET_IMPACT_WOOD:
			seconds = 0.4f;
			break;
		case SOUND_BULLET_IMPACT_METAL:
			seconds = 0.9f;
			break;
		case SOUND_FLESH_IMPACT:
			seconds = 0.45f;
			break;
		case SOUND_WATER_SPLASH:
			seconds = 1.1f;
			break;
		case SOUND_ELECTRIC_ZAP:
			seconds = 0.6f;
			break;
		case SOUND_GIBS:
			seconds = 1.0f;
			break;
		default:
			seconds = LOOP_SECONDS + CROSSFADE;
			break;
	}

	Buffer buf(seconds);
	switch (p_sound) {
		case SOUND_EXPLOSION:
			synth_explosion(buf, rng);
			break;
		case SOUND_SHELL_CLINK:
			synth_shell_clink(buf, rng);
			break;
		case SOUND_SPARKS:
			synth_sparks(buf, rng);
			break;
		case SOUND_GLASS_SHATTER:
			synth_glass_shatter(buf, rng);
			break;
		case SOUND_BULLET_IMPACT_CONCRETE:
			synth_impact_concrete(buf, rng);
			break;
		case SOUND_BULLET_IMPACT_METAL:
			synth_impact_metal(buf, rng);
			break;
		case SOUND_BULLET_IMPACT_WOOD:
			synth_impact_wood(buf, rng);
			break;
		case SOUND_FLESH_IMPACT:
			synth_flesh_impact(buf, rng);
			break;
		case SOUND_WATER_SPLASH:
			synth_water_splash(buf, rng);
			break;
		case SOUND_ELECTRIC_ZAP:
			synth_electric_zap(buf, rng);
			break;
		case SOUND_STEAM_HISS:
			synth_steam_hiss(buf, rng);
			break;
		case SOUND_FIRE_LOOP:
			synth_fire_loop(buf, rng);
			break;
		case SOUND_ROCKET_LOOP:
			synth_rocket_loop(buf, rng);
			break;
		case SOUND_ENERGY_HUM:
			synth_energy_hum(buf, rng, LOOP_SECONDS);
			break;
		case SOUND_FLARE_BURN:
			synth_flare_burn(buf, rng);
			break;
		case SOUND_SMOKE_GRENADE_HISS:
			synth_smoke_grenade_hiss(buf, rng);
			break;
		case SOUND_GIBS:
			synth_gibs(buf, rng);
			break;
	}

	int loop_end = 0;
	if (loop) {
		loop_end = make_loop(buf, CROSSFADE);
		finalize(buf, 1.2f);
	}

	Vector<uint8_t> bytes;
	bytes.resize(buf.size() * 2);
	uint8_t *w = bytes.ptrw();
	for (int i = 0; i < buf.size(); i++) {
		const int16_t s = int16_t(CLAMP(buf.data[i], -1.0f, 1.0f) * 32767.0f);
		w[i * 2 + 0] = uint8_t(s & 0xFF);
		w[i * 2 + 1] = uint8_t((uint16_t(s) >> 8) & 0xFF);
	}

	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	stream->set_format(AudioStreamWAV::FORMAT_16_BITS);
	stream->set_mix_rate(MIX_RATE);
	stream->set_stereo(false);
	stream->set_data(bytes);
	if (loop) {
		stream->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
		stream->set_loop_begin(0);
		stream->set_loop_end(loop_end);
	}
	return stream;
}

Error VFXSoundGenerator::save_wav(const Ref<AudioStreamWAV> &p_stream, const String &p_path) {
	// AudioStreamWAV::save_to_wav() drops loop points, so write the file here
	// with a 'smpl' chunk that the WAV importer reads in "Detect" loop mode.
	ERR_FAIL_COND_V(p_stream.is_null(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(p_stream->get_format() != AudioStreamWAV::FORMAT_16_BITS, ERR_INVALID_PARAMETER, "Only 16-bit generated sounds can be saved.");
	const Vector<uint8_t> data = p_stream->get_data();
	const int channels = p_stream->is_stereo() ? 2 : 1;
	const uint32_t rate = uint32_t(p_stream->get_mix_rate());
	const bool loop = p_stream->get_loop_mode() != AudioStreamWAV::LOOP_DISABLED;

	Error err;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Can't write sound file: " + p_path);

	const uint32_t smpl_size = loop ? 60 : 0; // 9 header words + 1 loop of 6 words.
	const uint32_t riff_size = 4 + (8 + 16) + (loop ? 8 + smpl_size : 0) + 8 + uint32_t(data.size());
	file->store_buffer((const uint8_t *)"RIFF", 4);
	file->store_32(riff_size);
	file->store_buffer((const uint8_t *)"WAVE", 4);

	file->store_buffer((const uint8_t *)"fmt ", 4);
	file->store_32(16);
	file->store_16(1); // PCM.
	file->store_16(channels);
	file->store_32(rate);
	file->store_32(rate * channels * 2);
	file->store_16(channels * 2);
	file->store_16(16);

	if (loop) {
		file->store_buffer((const uint8_t *)"smpl", 4);
		file->store_32(smpl_size);
		file->store_32(0); // Manufacturer.
		file->store_32(0); // Product.
		file->store_32(1000000000u / MAX(1u, rate)); // Sample period (ns).
		file->store_32(60); // MIDI unity note.
		file->store_32(0); // Pitch fraction.
		file->store_32(0); // SMPTE format.
		file->store_32(0); // SMPTE offset.
		file->store_32(1); // Loop count.
		file->store_32(0); // Sampler data.
		file->store_32(0); // Cue point ID.
		file->store_32(0); // Loop type: forward.
		file->store_32(uint32_t(p_stream->get_loop_begin()));
		file->store_32(uint32_t(p_stream->get_loop_end()));
		file->store_32(0); // Fraction.
		file->store_32(0); // Play count (infinite).
	}

	file->store_buffer((const uint8_t *)"data", 4);
	file->store_32(uint32_t(data.size()));
	file->store_buffer(data.ptr(), data.size());
	return OK;
}
