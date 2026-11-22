/**************************************************************************/
/*  effect_spawner_3d.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "effect_spawner_3d.h"

#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/resources/packed_scene.h"

namespace {
// An effect that spawns itself at time 0 would never return.
int spawn_depth = 0;
constexpr int MAX_SPAWN_DEPTH = 4;

Vector3 random_unit_vector() {
	const real_t z = Math::randf() * 2.0 - 1.0;
	const real_t angle = Math::randf() * Math::TAU;
	const real_t r = Math::sqrt(MAX(real_t(0.0), 1 - z * z));
	return Vector3(r * Math::cos(angle), r * Math::sin(angle), z);
}
} // namespace

void EffectSpawner3D::set_scene(const Ref<PackedScene> &p_scene) {
	scene = p_scene;
}

Ref<PackedScene> EffectSpawner3D::get_scene() const {
	return scene;
}

void EffectSpawner3D::set_count(int p_count) {
	count = CLAMP(p_count, 1, 64);
}

int EffectSpawner3D::get_count() const {
	return count;
}

void EffectSpawner3D::set_scatter_radius(real_t p_radius) {
	scatter_radius = MAX(real_t(0.0), p_radius);
}

real_t EffectSpawner3D::get_scatter_radius() const {
	return scatter_radius;
}

void EffectSpawner3D::set_launch_speed(real_t p_speed) {
	launch_speed = MAX(real_t(0.0), p_speed);
}

real_t EffectSpawner3D::get_launch_speed() const {
	return launch_speed;
}

void EffectSpawner3D::set_launch_spread(real_t p_degrees) {
	launch_spread = CLAMP(p_degrees, real_t(0.0), real_t(180.0));
}

real_t EffectSpawner3D::get_launch_spread() const {
	return launch_spread;
}

void EffectSpawner3D::set_inherit_look(bool p_enable) {
	inherit_look = p_enable;
}

bool EffectSpawner3D::is_inheriting_look() const {
	return inherit_look;
}

void EffectSpawner3D::spawn() {
	Node *parent = get_parent();
	for (Node *node = get_parent(); node; node = node->get_parent()) {
		if (Object::cast_to<ParticleEffect3D>(node)) {
			parent = node->get_parent();
			break;
		}
	}
	spawn_from_effect(parent, Color(1, 1, 1, 1), 1.0, false);
}

void EffectSpawner3D::spawn_from_effect(Node *p_parent, const Color &p_tint, real_t p_scale, bool p_editor_level_preview) {
	if (scene.is_null() || !p_parent || !is_inside_tree() || spawn_depth >= MAX_SPAWN_DEPTH) {
		return;
	}
	const bool in_editor = Engine::get_singleton()->is_editor_hint();
	const Ref<SceneState> state = scene->get_state();
	const bool is_effect = state.is_valid() && state->get_node_count() > 0 && ClassDB::is_parent_class(state->get_node_type(0), SNAME("ParticleEffect3D"));
	if (in_editor && !is_effect) {
		return; // Props and gibs need the running game (scripts, physics).
	}
	const Transform3D origin = get_global_transform().orthonormalized();
	const Color tint = inherit_look ? p_tint : Color(1, 1, 1, 1);
	const real_t scale = inherit_look ? p_scale : real_t(1.0);
	spawn_depth++;
	for (int i = 0; i < count; i++) {
		Transform3D xform = origin;
		if (scatter_radius > 0.0) {
			xform.origin += random_unit_vector() * (scatter_radius * scale * Math::pow(Math::randf(), 1.0f / 3.0f));
		}
		if (is_effect && !in_editor) {
			ParticleEffect3D::spawn(scene, p_parent, xform, tint, scale);
			continue;
		}
		Node *instance = scene->instantiate();
		if (!instance) {
			break;
		}
		p_parent->add_child(instance);
		Node3D *node_3d = Object::cast_to<Node3D>(instance);
		if (node_3d) {
			node_3d->set_global_transform(xform);
			node_3d->reset_physics_interpolation();
		}
		if (in_editor) {
			// Not owned by the scene, so never saved; gone when it has played.
			ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(instance);
			editor_spawned.push_back(instance->get_instance_id());
			effect->connect(SceneStringName(finished), callable_mp(static_cast<Node *>(effect), &Node::queue_free), CONNECT_ONE_SHOT);
			if (p_editor_level_preview) {
				effect->editor_preview_play();
			} else {
				effect->play();
			}
			continue;
		}
		if (launch_speed > 0.0 && node_3d) {
			// Thrown along the spawner's +Y, inside a cone of launch_spread.
			Vector3 direction = origin.basis.get_column(1);
			if (launch_spread > 0.0) {
				const Vector3 side = random_unit_vector();
				direction = direction.slerp(side, CLAMP(Math::randf() * launch_spread / 180.0, 0.0, 1.0)).normalized();
			}
			const Vector3 velocity = direction * (launch_speed * scale * Math::random(0.7, 1.0));
			if (RigidBody3D *body = Object::cast_to<RigidBody3D>(node_3d)) {
				body->set_linear_velocity(velocity);
				body->set_angular_velocity(random_unit_vector() * Math::random(2.0, 9.0));
			} else {
				bool valid = false;
				node_3d->get("velocity", &valid); // CharacterBody3D or a script's own velocity.
				if (valid) {
					node_3d->set("velocity", velocity);
				}
			}
		}
	}
	spawn_depth--;
}

void EffectSpawner3D::clear_editor_spawned() {
	for (const ObjectID &id : editor_spawned) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(id));
		if (node && !node->is_queued_for_deletion()) {
			node->queue_free();
		}
	}
	editor_spawned.clear();
}

void EffectSpawner3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		clear_editor_spawned();
	}
}

void EffectSpawner3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_scene", "scene"), &EffectSpawner3D::set_scene);
	ClassDB::bind_method(D_METHOD("get_scene"), &EffectSpawner3D::get_scene);
	ClassDB::bind_method(D_METHOD("set_count", "count"), &EffectSpawner3D::set_count);
	ClassDB::bind_method(D_METHOD("get_count"), &EffectSpawner3D::get_count);
	ClassDB::bind_method(D_METHOD("set_scatter_radius", "radius"), &EffectSpawner3D::set_scatter_radius);
	ClassDB::bind_method(D_METHOD("get_scatter_radius"), &EffectSpawner3D::get_scatter_radius);
	ClassDB::bind_method(D_METHOD("set_launch_speed", "speed"), &EffectSpawner3D::set_launch_speed);
	ClassDB::bind_method(D_METHOD("get_launch_speed"), &EffectSpawner3D::get_launch_speed);
	ClassDB::bind_method(D_METHOD("set_launch_spread", "degrees"), &EffectSpawner3D::set_launch_spread);
	ClassDB::bind_method(D_METHOD("get_launch_spread"), &EffectSpawner3D::get_launch_spread);
	ClassDB::bind_method(D_METHOD("set_inherit_look", "enable"), &EffectSpawner3D::set_inherit_look);
	ClassDB::bind_method(D_METHOD("is_inheriting_look"), &EffectSpawner3D::is_inheriting_look);
	ClassDB::bind_method(D_METHOD("spawn"), &EffectSpawner3D::spawn);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_scene", "get_scene");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "count", PROPERTY_HINT_RANGE, "1,64,1"), "set_count", "get_count");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scatter_radius", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_scatter_radius", "get_scatter_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "launch_speed", PROPERTY_HINT_RANGE, "0,30,0.1,or_greater,suffix:m/s"), "set_launch_speed", "get_launch_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "launch_spread", PROPERTY_HINT_RANGE, "0,180,1,degrees"), "set_launch_spread", "get_launch_spread");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inherit_look"), "set_inherit_look", "is_inheriting_look");
}
