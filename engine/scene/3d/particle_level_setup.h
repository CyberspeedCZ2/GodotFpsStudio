/**************************************************************************/
/*  particle_level_setup.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "core/variant/typed_array.h"
#include "scene/3d/particle_impact_table.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

// Drop into a level: when the game starts it bakes the level's particle
// collision and prewarms effect pools, so the first explosion or impact
// does not hitch. Does nothing in the editor.
class ParticleLevelSetup : public Node {
	GDCLASS(ParticleLevelSetup, Node);

	NodePath level_root;
	bool prepare_collision = true;
	uint32_t collision_mask = 1;
	TypedArray<PackedScene> prewarm_effects;
	int prewarm_count = 8;
	Ref<ParticleImpactTable> impact_table;
	int effects_per_frame = 2;
	Vector3 wind;

	LocalVector<Ref<PackedScene>> queue;
	ObjectID parent_id;
	bool finished = false;

	Node *_get_level_root() const;
	void _start();
	void _process_queue();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_level_root(const NodePath &p_path);
	NodePath get_level_root() const;
	void set_prepare_collision(bool p_enable);
	bool is_preparing_collision() const;
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;
	void set_prewarm_effects(const TypedArray<PackedScene> &p_effects);
	TypedArray<PackedScene> get_prewarm_effects() const;
	// Wind of this level for emitters with Wind Influence (ParticleEffect3D.set_wind()).
	void set_wind(const Vector3 &p_wind);
	Vector3 get_wind() const;
	void set_prewarm_count(int p_count);
	int get_prewarm_count() const;
	void set_impact_table(const Ref<ParticleImpactTable> &p_table);
	Ref<ParticleImpactTable> get_impact_table() const;
	void set_effects_per_frame(int p_count);
	int get_effects_per_frame() const;

	bool is_finished() const;
};
