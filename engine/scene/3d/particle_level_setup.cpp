/**************************************************************************/
/*  particle_level_setup.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_level_setup.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/main/scene_tree.h"

Node *ParticleLevelSetup::_get_level_root() const {
	if (!level_root.is_empty()) {
		return get_node_or_null(level_root);
	}
	// The scene this node was saved in, else its parent.
	if (get_owner()) {
		return get_owner();
	}
	return get_parent();
}

void ParticleLevelSetup::_start() {
	// Runs deferred after READY: the level root may still have been busy
	// adding children, and pooled effects are added to it.
	Node *root = _get_level_root();
	if (!root || !root->is_inside_tree()) {
		finished = true;
		emit_signal(SNAME("setup_finished"));
		return;
	}
	ParticleEffect3D::set_wind(wind); // Also zero: the last level's wind ends here.
	if (prepare_collision) {
		ParticleEffect3D::prepare_level_collision(root, collision_mask);
	}
	queue.clear();
	for (int i = 0; i < prewarm_effects.size(); i++) {
		const Ref<PackedScene> scene = prewarm_effects[i];
		if (scene.is_valid() && !queue.has(scene)) {
			queue.push_back(scene);
		}
	}
	if (impact_table.is_valid()) {
		const TypedArray<PackedScene> table_effects = impact_table->get_all_effects();
		for (int i = 0; i < table_effects.size(); i++) {
			const Ref<PackedScene> scene = table_effects[i];
			if (scene.is_valid() && !queue.has(scene)) {
				queue.push_back(scene);
			}
		}
	}
	parent_id = root->get_instance_id();
	set_process_internal(true);
	_process_queue();
}

void ParticleLevelSetup::_process_queue() {
	Node *parent = Object::cast_to<Node>(ObjectDB::get_instance(parent_id));
	// A few scenes per frame keep level start from stalling on instancing.
	for (int i = 0; parent && i < MAX(1, effects_per_frame) && !queue.is_empty(); i++) {
		const Ref<PackedScene> scene = queue[0];
		queue.remove_at(0);
		ParticleEffect3D::prewarm(scene, parent, prewarm_count);
	}
	if (!parent || queue.is_empty()) {
		queue.clear();
		set_process_internal(false);
		finished = true;
		emit_signal(SNAME("setup_finished"));
	}
}

void ParticleLevelSetup::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				callable_mp(this, &ParticleLevelSetup::_start).call_deferred();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_process_queue();
		} break;
	}
}

void ParticleLevelSetup::set_level_root(const NodePath &p_path) {
	level_root = p_path;
}

NodePath ParticleLevelSetup::get_level_root() const {
	return level_root;
}

void ParticleLevelSetup::set_prepare_collision(bool p_enable) {
	prepare_collision = p_enable;
}

bool ParticleLevelSetup::is_preparing_collision() const {
	return prepare_collision;
}

void ParticleLevelSetup::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
}

uint32_t ParticleLevelSetup::get_collision_mask() const {
	return collision_mask;
}

void ParticleLevelSetup::set_prewarm_effects(const TypedArray<PackedScene> &p_effects) {
	prewarm_effects = p_effects;
}

TypedArray<PackedScene> ParticleLevelSetup::get_prewarm_effects() const {
	return prewarm_effects;
}

void ParticleLevelSetup::set_wind(const Vector3 &p_wind) {
	wind = p_wind;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		ParticleEffect3D::set_wind(wind);
	}
}

Vector3 ParticleLevelSetup::get_wind() const {
	return wind;
}

void ParticleLevelSetup::set_prewarm_count(int p_count) {
	prewarm_count = CLAMP(p_count, 0, 64);
}

int ParticleLevelSetup::get_prewarm_count() const {
	return prewarm_count;
}

void ParticleLevelSetup::set_impact_table(const Ref<ParticleImpactTable> &p_table) {
	impact_table = p_table;
}

Ref<ParticleImpactTable> ParticleLevelSetup::get_impact_table() const {
	return impact_table;
}

void ParticleLevelSetup::set_effects_per_frame(int p_count) {
	effects_per_frame = MAX(1, p_count);
}

int ParticleLevelSetup::get_effects_per_frame() const {
	return effects_per_frame;
}

bool ParticleLevelSetup::is_finished() const {
	return finished;
}

void ParticleLevelSetup::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_level_root", "path"), &ParticleLevelSetup::set_level_root);
	ClassDB::bind_method(D_METHOD("get_level_root"), &ParticleLevelSetup::get_level_root);
	ClassDB::bind_method(D_METHOD("set_prepare_collision", "enable"), &ParticleLevelSetup::set_prepare_collision);
	ClassDB::bind_method(D_METHOD("is_preparing_collision"), &ParticleLevelSetup::is_preparing_collision);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &ParticleLevelSetup::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &ParticleLevelSetup::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_prewarm_effects", "effects"), &ParticleLevelSetup::set_prewarm_effects);
	ClassDB::bind_method(D_METHOD("get_prewarm_effects"), &ParticleLevelSetup::get_prewarm_effects);
	ClassDB::bind_method(D_METHOD("set_wind", "wind"), &ParticleLevelSetup::set_wind);
	ClassDB::bind_method(D_METHOD("get_wind"), &ParticleLevelSetup::get_wind);
	ClassDB::bind_method(D_METHOD("set_prewarm_count", "count"), &ParticleLevelSetup::set_prewarm_count);
	ClassDB::bind_method(D_METHOD("get_prewarm_count"), &ParticleLevelSetup::get_prewarm_count);
	ClassDB::bind_method(D_METHOD("set_impact_table", "table"), &ParticleLevelSetup::set_impact_table);
	ClassDB::bind_method(D_METHOD("get_impact_table"), &ParticleLevelSetup::get_impact_table);
	ClassDB::bind_method(D_METHOD("set_effects_per_frame", "count"), &ParticleLevelSetup::set_effects_per_frame);
	ClassDB::bind_method(D_METHOD("get_effects_per_frame"), &ParticleLevelSetup::get_effects_per_frame);
	ClassDB::bind_method(D_METHOD("is_finished"), &ParticleLevelSetup::is_finished);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "level_root"), "set_level_root", "get_level_root");
	ADD_GROUP("Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "prepare_collision"), "set_prepare_collision", "is_preparing_collision");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_GROUP("Prewarm", "");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "prewarm_effects", PROPERTY_HINT_TYPE_STRING, MAKE_RESOURCE_TYPE_HINT("PackedScene")), "set_prewarm_effects", "get_prewarm_effects");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "prewarm_count", PROPERTY_HINT_RANGE, "0,64,1"), "set_prewarm_count", "get_prewarm_count");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "impact_table", PROPERTY_HINT_RESOURCE_TYPE, "ParticleImpactTable"), "set_impact_table", "get_impact_table");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "effects_per_frame", PROPERTY_HINT_RANGE, "1,16,1"), "set_effects_per_frame", "get_effects_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind", PROPERTY_HINT_NONE, ""), "set_wind", "get_wind");

	ADD_SIGNAL(MethodInfo("setup_finished"));
}
