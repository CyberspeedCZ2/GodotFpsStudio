/**************************************************************************/
/*  particle_level_preview_plugin.h                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#pragma once

#include "editor/plugins/editor_plugin.h"

class Button;
class HBoxContainer;
class ParticleEffect3D;

// Preview a ParticleEffect3D selected in the Level editor: plays it in the
// viewport with collision against the level (shared level tiles), without
// changing the saved scene.
class ParticleEffectLevelPreviewPlugin : public EditorPlugin {
	GDCLASS(ParticleEffectLevelPreviewPlugin, EditorPlugin);

	HBoxContainer *toolbar = nullptr;
	Button *play_button = nullptr;
	Button *stop_button = nullptr;
	ObjectID effect_id;

	ParticleEffect3D *_get_effect() const;
	void _on_play_pressed();
	void _on_stop_pressed();
	void _update_icons();

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "ParticleEffectLevelPreview"; }
	virtual bool has_main_screen() const override { return false; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;

	ParticleEffectLevelPreviewPlugin();
};
