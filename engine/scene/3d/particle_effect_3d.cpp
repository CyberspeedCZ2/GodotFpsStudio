/**************************************************************************/
/*  particle_effect_3d.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_effect_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/resources/packed_scene.h"

bool ParticleEffect3D::is_emitter(const Node *p_node) {
	return Object::cast_to<GPUParticles3D>(p_node) || Object::cast_to<CPUParticles3D>(p_node);
}

void ParticleEffect3D::set_emitter_delay(Node *p_emitter, double p_delay) {
	ERR_FAIL_NULL(p_emitter);
	if (p_delay <= 0.0) {
		p_emitter->remove_meta(META_DELAY);
	} else {
		p_emitter->set_meta(META_DELAY, p_delay);
	}
}

double ParticleEffect3D::get_emitter_delay(const Node *p_emitter) {
	ERR_FAIL_NULL_V(p_emitter, 0.0);
	return MAX(0.0, double(p_emitter->get_meta(META_DELAY, 0.0)));
}

void ParticleEffect3D::set_emitter_duration(Node *p_emitter, double p_duration) {
	ERR_FAIL_NULL(p_emitter);
	if (p_duration <= 0.0) {
		p_emitter->remove_meta(META_DURATION);
	} else {
		p_emitter->set_meta(META_DURATION, p_duration);
	}
}

double ParticleEffect3D::get_emitter_duration(const Node *p_emitter) {
	ERR_FAIL_NULL_V(p_emitter, 0.0);
	return MAX(0.0, double(p_emitter->get_meta(META_DURATION, 0.0)));
}

void ParticleEffect3D::get_emitter_window(const Node *p_emitter, double &r_start, double &r_emission_end, double &r_visual_end) {
	r_start = get_emitter_delay(p_emitter);
	const double duration = get_emitter_duration(p_emitter);
	const double lifetime = MAX(0.001, double(p_emitter->get("lifetime")));
	const bool one_shot = bool(p_emitter->get("one_shot"));
	if (duration > 0.0) {
		r_emission_end = r_start + duration;
	} else if (one_shot) {
		// A one-shot emitter releases its particles over one lifetime cycle.
		r_emission_end = r_start + lifetime * (1.0 - CLAMP(double(p_emitter->get("explosiveness")), 0.0, 1.0)) + 0.001;
	} else {
		r_emission_end = -1.0;
	}
	r_visual_end = r_emission_end < 0.0 ? -1.0 : r_emission_end + lifetime;
}

void ParticleEffect3D::_set_emitter_emitting(Node *p_emitter, bool p_emitting, bool p_restart) {
	if (p_restart) {
		p_emitter->call("restart", false);
	}
	p_emitter->set("emitting", p_emitting);
}

void ParticleEffect3D::_collect_emitters(Node *p_node) {
	if (is_emitter(p_node)) {
		EmitterState state;
		state.id = p_node->get_instance_id();
		state.delay = get_emitter_delay(p_node);
		state.duration = get_emitter_duration(p_node);
		state.lifetime = MAX(0.001, double(p_node->get("lifetime")));
		state.one_shot = bool(p_node->get("one_shot"));
		emitters.push_back(state);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_emitters(p_node->get_child(i));
	}
}

double ParticleEffect3D::_get_max_lifetime() const {
	double result = 0.0;
	for (const EmitterState &state : emitters) {
		result = MAX(result, state.lifetime);
	}
	return result;
}

double ParticleEffect3D::get_total_duration() const {
	double total = 0.0;
	LocalVector<Node *> stack;
	stack.push_back(const_cast<ParticleEffect3D *>(this));
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		if (is_emitter(node)) {
			double start, emission_end, visual_end;
			get_emitter_window(node, start, emission_end, visual_end);
			if (visual_end < 0.0) {
				return -1.0;
			}
			total = MAX(total, visual_end);
		}
		for (int i = 0; i < node->get_child_count(); i++) {
			stack.push_back(node->get_child(i));
		}
	}
	return total;
}

void ParticleEffect3D::play() {
	if (!is_inside_tree()) {
		play_pending = true;
		return;
	}
	play_pending = false;
	emitters.clear();
	_collect_emitters(this);
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (emitter) {
			_set_emitter_emitting(emitter, false, true);
		}
	}
	elapsed = 0.0;
	stop_time = 0.0;
	stopping = false;
	playing = true;
	set_process_internal(true);
	_update(0.0);
}

void ParticleEffect3D::stop() {
	if (!playing) {
		return;
	}
	stopping = true;
	stop_time = elapsed;
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (emitter && !state.ended) {
			_set_emitter_emitting(emitter, false, false);
		}
		state.started = true;
		state.ended = true;
	}
}

void ParticleEffect3D::restart() {
	play();
}

void ParticleEffect3D::seek(double p_time) {
	play();
	if (p_time <= 0.0) {
		return;
	}
	elapsed = p_time;
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (!emitter || p_time < state.delay) {
			continue;
		}
		if (!state.started) {
			state.started = true;
			_set_emitter_emitting(emitter, true, true);
		}
		double run = p_time - state.delay;
		if (state.duration > 0.0 && run >= state.duration) {
			// Emission is over; simulate up to its end and let the tail age.
			state.ended = true;
			emitter->call("request_particles_process", run);
			_set_emitter_emitting(emitter, false, false);
		} else {
			emitter->call("request_particles_process", run);
		}
	}
}

bool ParticleEffect3D::is_playing() const {
	return playing;
}

double ParticleEffect3D::get_elapsed_time() const {
	return elapsed;
}

void ParticleEffect3D::_update(double p_delta) {
	if (!playing) {
		return;
	}
	elapsed += p_delta * speed_scale;
	double total = 0.0;
	bool infinite = false;
	for (EmitterState &state : emitters) {
		Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id));
		if (!emitter) {
			continue;
		}
		if (!state.started && elapsed >= state.delay) {
			state.started = true;
			_set_emitter_emitting(emitter, true, true);
		}
		if (state.started && !state.ended && state.duration > 0.0 && elapsed >= state.delay + state.duration) {
			state.ended = true;
			_set_emitter_emitting(emitter, false, false);
		}
		double start, emission_end, visual_end;
		get_emitter_window(emitter, start, emission_end, visual_end);
		if (visual_end < 0.0) {
			infinite = true;
		} else {
			total = MAX(total, visual_end);
		}
	}
	if (emitters.is_empty()) {
		// Nothing to play; never loop an empty effect (that would restart forever).
		_finish();
		return;
	}
	if (stopping) {
		if (elapsed >= stop_time + _get_max_lifetime()) {
			_finish();
		}
	} else if (!infinite && elapsed >= total) {
		// Loop only effects with a real duration, and never restart from within
		// play() itself (p_delta == 0), which would recurse.
		if (looping && total > 0.0 && p_delta > 0.0) {
			play();
		} else if (!looping || total <= 0.0) {
			_finish();
		}
	}
}

void ParticleEffect3D::_finish() {
	playing = false;
	stopping = false;
	set_process_internal(false);
	emit_signal(SceneStringName(finished));
	if (auto_free && !Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		queue_free();
	}
}

void ParticleEffect3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (play_pending || (autoplay && !Engine::get_singleton()->is_editor_hint())) {
				play();
			} else if (!Engine::get_singleton()->is_editor_hint()) {
				// Without autoplay the effect stays silent until play() is called.
				emitters.clear();
				_collect_emitters(this);
				for (const EmitterState &state : emitters) {
					if (Node *emitter = Object::cast_to<Node>(ObjectDB::get_instance(state.id))) {
						_set_emitter_emitting(emitter, false, false);
					}
				}
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update(get_process_delta_time());
		} break;
	}
}

void ParticleEffect3D::set_autoplay(bool p_autoplay) {
	autoplay = p_autoplay;
}

bool ParticleEffect3D::is_autoplay() const {
	return autoplay;
}

void ParticleEffect3D::set_looping(bool p_looping) {
	looping = p_looping;
}

bool ParticleEffect3D::is_looping() const {
	return looping;
}

void ParticleEffect3D::set_auto_free(bool p_auto_free) {
	auto_free = p_auto_free;
}

bool ParticleEffect3D::is_auto_free() const {
	return auto_free;
}

void ParticleEffect3D::set_speed_scale(double p_speed_scale) {
	speed_scale = MAX(0.0, p_speed_scale);
}

double ParticleEffect3D::get_speed_scale() const {
	return speed_scale;
}

ParticleEffect3D *ParticleEffect3D::spawn(const Ref<PackedScene> &p_scene, Node *p_parent, const Transform3D &p_transform) {
	ERR_FAIL_COND_V(p_scene.is_null(), nullptr);
	ERR_FAIL_NULL_V(p_parent, nullptr);
	Node *instance = p_scene->instantiate();
	ERR_FAIL_NULL_V(instance, nullptr);
	ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(instance);
	if (!effect) {
		memdelete(instance);
		ERR_FAIL_V_MSG(nullptr, "The scene root is not a ParticleEffect3D. Save the effect from the Particle workspace.");
	}
	effect->set_auto_free(true);
	effect->set_autoplay(false);
	p_parent->add_child(effect);
	if (effect->is_inside_tree()) {
		effect->set_global_transform(p_transform);
	} else {
		effect->set_transform(p_transform);
	}
	effect->play();
	return effect;
}

void ParticleEffect3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_autoplay", "enable"), &ParticleEffect3D::set_autoplay);
	ClassDB::bind_method(D_METHOD("is_autoplay"), &ParticleEffect3D::is_autoplay);
	ClassDB::bind_method(D_METHOD("set_looping", "enable"), &ParticleEffect3D::set_looping);
	ClassDB::bind_method(D_METHOD("is_looping"), &ParticleEffect3D::is_looping);
	ClassDB::bind_method(D_METHOD("set_auto_free", "enable"), &ParticleEffect3D::set_auto_free);
	ClassDB::bind_method(D_METHOD("is_auto_free"), &ParticleEffect3D::is_auto_free);
	ClassDB::bind_method(D_METHOD("set_speed_scale", "scale"), &ParticleEffect3D::set_speed_scale);
	ClassDB::bind_method(D_METHOD("get_speed_scale"), &ParticleEffect3D::get_speed_scale);
	ClassDB::bind_method(D_METHOD("play"), &ParticleEffect3D::play);
	ClassDB::bind_method(D_METHOD("stop"), &ParticleEffect3D::stop);
	ClassDB::bind_method(D_METHOD("restart"), &ParticleEffect3D::restart);
	ClassDB::bind_method(D_METHOD("seek", "time"), &ParticleEffect3D::seek);
	ClassDB::bind_method(D_METHOD("is_playing"), &ParticleEffect3D::is_playing);
	ClassDB::bind_method(D_METHOD("get_elapsed_time"), &ParticleEffect3D::get_elapsed_time);
	ClassDB::bind_method(D_METHOD("get_total_duration"), &ParticleEffect3D::get_total_duration);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("set_emitter_delay", "emitter", "delay"), &ParticleEffect3D::set_emitter_delay);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("get_emitter_delay", "emitter"), &ParticleEffect3D::get_emitter_delay);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("set_emitter_duration", "emitter", "duration"), &ParticleEffect3D::set_emitter_duration);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("get_emitter_duration", "emitter"), &ParticleEffect3D::get_emitter_duration);
	ClassDB::bind_static_method("ParticleEffect3D", D_METHOD("spawn", "scene", "parent", "transform"), &ParticleEffect3D::spawn);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autoplay"), "set_autoplay", "is_autoplay");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "looping"), "set_looping", "is_looping");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_free"), "set_auto_free", "is_auto_free");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_scale", PROPERTY_HINT_RANGE, "0,8,0.01,or_greater"), "set_speed_scale", "get_speed_scale");

	ADD_SIGNAL(MethodInfo("finished"));
}

ParticleEffect3D::ParticleEffect3D() {
}
