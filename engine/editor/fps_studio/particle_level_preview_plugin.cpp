/**************************************************************************/
/*  particle_level_preview_plugin.cpp                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/

#include "particle_level_preview_plugin.h"

#include "core/object/callable_mp.h"
#include "editor/editor_string_names.h"
#include "scene/3d/particle_effect_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/separator.h"

ParticleEffect3D *ParticleEffectLevelPreviewPlugin::_get_effect() const {
	return Object::cast_to<ParticleEffect3D>(ObjectDB::get_instance(effect_id));
}

void ParticleEffectLevelPreviewPlugin::_on_play_pressed() {
	if (ParticleEffect3D *effect = _get_effect()) {
		effect->editor_preview_play();
	}
}

void ParticleEffectLevelPreviewPlugin::_on_stop_pressed() {
	if (ParticleEffect3D *effect = _get_effect()) {
		effect->editor_preview_stop();
	}
}

void ParticleEffectLevelPreviewPlugin::_update_icons() {
	play_button->set_button_icon(play_button->get_editor_theme_icon(SNAME("MainPlay")));
	stop_button->set_button_icon(stop_button->get_editor_theme_icon(SNAME("Stop")));
}

void ParticleEffectLevelPreviewPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		toolbar->connect(SceneStringName(theme_changed), callable_mp(this, &ParticleEffectLevelPreviewPlugin::_update_icons));
	}
}

void ParticleEffectLevelPreviewPlugin::edit(Object *p_object) {
	ParticleEffect3D *previous = _get_effect();
	ParticleEffect3D *effect = Object::cast_to<ParticleEffect3D>(p_object);
	if (previous && previous != effect) {
		previous->editor_preview_stop();
	}
	effect_id = effect ? effect->get_instance_id() : ObjectID();
}

bool ParticleEffectLevelPreviewPlugin::handles(Object *p_object) const {
	return Object::cast_to<ParticleEffect3D>(p_object) != nullptr;
}

void ParticleEffectLevelPreviewPlugin::make_visible(bool p_visible) {
	if (toolbar) {
		toolbar->set_visible(p_visible);
	}
	if (!p_visible) {
		if (ParticleEffect3D *effect = _get_effect()) {
			effect->editor_preview_stop();
		}
	}
}

ParticleEffectLevelPreviewPlugin::ParticleEffectLevelPreviewPlugin() {
	toolbar = memnew(HBoxContainer);
	toolbar->add_child(memnew(VSeparator));
	play_button = memnew(Button);
	play_button->set_flat(true);
	play_button->set_text(TTR("Preview Effect"));
	play_button->set_tooltip_text(TTR("Play the selected particle effect here in the level, with particles colliding with the level geometry. The scene is not modified."));
	play_button->connect(SceneStringName(pressed), callable_mp(this, &ParticleEffectLevelPreviewPlugin::_on_play_pressed));
	toolbar->add_child(play_button);
	stop_button = memnew(Button);
	stop_button->set_flat(true);
	stop_button->set_tooltip_text(TTR("Stop the effect preview."));
	stop_button->connect(SceneStringName(pressed), callable_mp(this, &ParticleEffectLevelPreviewPlugin::_on_stop_pressed));
	toolbar->add_child(stop_button);
	toolbar->hide();
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, toolbar);
}
