/**************************************************************************/
/*  scene_import_settings.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/


// FPS Studio Model View: hitbox editor (presets, bone attachment, .hitboxcfg, preview).
// Part of SceneImportSettingsScreen (Model View); split out of
// scene_import_settings.cpp, which keeps the Godot import dialog itself.

#include "scene_import_settings.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/file_access.h"
#include "core/io/config_file.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_saver.h"
#include "core/math/projection.h"
#include "core/object/callable_mp.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/3d/skeleton_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/3d/bone_attachment_3d.h"
#include "scene/3d/label_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/check_button.h"
#include "scene/gui/button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/panel.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/separator.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/sky.h"
#include "scene/resources/shader.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/surface_tool.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"
#include "servers/text/text_server.h"
#include "scene_import_settings_internal.h"

static constexpr int MODEL_HITBOX_CONFIG_VERSION = 3;
static constexpr int MODEL_HITBOX_ATTACHMENT_VERSION = 2;

static Ref<Shape3D> create_model_hitbox_shape(const Vector3 &p_minimum, const Vector3 &p_maximum) {
	Ref<BoxShape3D> shape;
	shape.instantiate();
	const Vector3 dimensions = p_maximum - p_minimum;
	shape->set_size(Vector3(MAX(dimensions.x, 0.01), MAX(dimensions.y, 0.01), MAX(dimensions.z, 0.01)));
	return shape;
}

static real_t get_default_hitbox_damage(int p_hitgroup) {
	// Lethal head hits, stronger torso hits, and reduced limb damage are useful
	// starter values for a 100 HP NPC. Each value remains editable per hitbox.
	switch (CLAMP(p_hitgroup, 0, 7)) {
		case 1: // Head.
			return 100.0;
		case 2: // Chest.
			return 40.0;
		case 3: // Stomach.
			return 30.0;
		default: // Generic and limbs.
			return 15.0;
	}
}

// Frame a hitbox's corners are stored in, as a transform into the model
// root: the bone's rest frame, or the root itself (Model Root / no bone).
static Transform3D get_hitbox_frame_to_root(Node *p_scene, const String &p_skeleton_path, const String &p_bone_name) {
	Node3D *scene_root = Object::cast_to<Node3D>(p_scene);
	if (!scene_root || p_skeleton_path.is_empty() || p_bone_name.is_empty()) {
		return Transform3D();
	}
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_scene->get_node_or_null(NodePath(p_skeleton_path)));
	const int bone = skeleton ? skeleton->find_bone(p_bone_name) : -1;
	if (bone < 0) {
		return Transform3D();
	}
	return scene_root->get_global_transform().affine_inverse() * skeleton->get_global_transform() * skeleton->get_bone_global_rest(bone);
}

// Axis-aligned bounds of a box after a transform (all 8 corners).
static void transform_hitbox_bounds(const Vector3 &p_minimum, const Vector3 &p_maximum, const Transform3D &p_transform, Vector3 &r_minimum, Vector3 &r_maximum) {
	for (int i = 0; i < 8; i++) {
		const Vector3 corner((i & 1) ? p_maximum.x : p_minimum.x, (i & 2) ? p_maximum.y : p_minimum.y, (i & 4) ? p_maximum.z : p_minimum.z);
		const Vector3 point = p_transform.xform(corner);
		if (i == 0) {
			r_minimum = point;
			r_maximum = point;
		} else {
			r_minimum = r_minimum.min(point);
			r_maximum = r_maximum.max(point);
		}
	}
}

static String normalize_hitbox_bone_name(const String &p_name) {
	String normalized = p_name.to_lower();
	normalized = normalized.replace("_", "").replace("-", "").replace(".", "").replace(":", "").replace(" ", "");
	return normalized;
}

static int find_hitbox_preset_bone(Skeleton3D *p_skeleton, const Vector<String> &p_aliases, int p_parent_bone = -1, int *r_score = nullptr) {
	if (p_skeleton == nullptr) {
		if (r_score) {
			*r_score = -1;
		}
		return -1;
	}
	int best_bone = -1;
	int best_score = -1;
	for (int bone = 0; bone < p_skeleton->get_bone_count(); bone++) {
		if (p_parent_bone >= 0) {
			int ancestor = p_skeleton->get_bone_parent(bone);
			while (ancestor >= 0 && ancestor != p_parent_bone) {
				ancestor = p_skeleton->get_bone_parent(ancestor);
			}
			if (ancestor != p_parent_bone) {
				continue;
			}
		}
		const String normalized_bone = normalize_hitbox_bone_name(p_skeleton->get_bone_name(bone));
		const bool is_control_bone = normalized_bone.begins_with("ctrl") || normalized_bone.begins_with("control");
		const bool is_mixamo_bone = normalized_bone.begins_with("mixamorig") || normalized_bone.begins_with("mixamo");
		for (const String &alias : p_aliases) {
			const String normalized_alias = normalize_hitbox_bone_name(alias);
			if (normalized_alias.is_empty()) {
				continue;
			}
			int match_score = -1;
			if (normalized_bone == normalized_alias) {
				match_score = 1000 + normalized_alias.length();
			} else if (normalized_bone.ends_with(normalized_alias)) {
				match_score = normalized_alias.length();
			}
			// Mixamo animations drive the deform skeleton. Always prefer those
			// bones over a parallel Ctrl rig, even when the Ctrl name is an exact
			// match and the Mixamo name needs a suffix match.
			const int rig_priority = is_mixamo_bone ? 3 : (is_control_bone ? 1 : 2);
			const int score = match_score < 0 ? -1 : rig_priority * 10000 + match_score;
			if (score > best_score) {
				best_score = score;
				best_bone = bone;
			}
		}
	}
	if (r_score) {
		*r_score = best_score;
	}
	return best_bone;
}

static bool is_hitbox_digit_bone_name(const String &p_name) {
	const String normalized = normalize_hitbox_bone_name(p_name);
	return normalized.contains("thumb") || normalized.contains("finger") || normalized.contains("index") || normalized.contains("middle") || normalized.contains("ring") || normalized.contains("pinky") || normalized.contains("pinkie") || normalized.contains("little") || normalized.contains("toe");
}

static Color get_hitbox_hitgroup_color(int p_hitgroup) {
	static const Color hitgroup_colors[] = {
		Color(0.30, 0.76, 1.00), // Generic.
		Color(1.00, 0.24, 0.20), // Head.
		Color(1.00, 0.53, 0.12), // Chest.
		Color(1.00, 0.84, 0.18), // Stomach.
		Color(0.25, 0.86, 0.38), // Left arm.
		Color(0.12, 0.82, 0.83), // Right arm.
		Color(0.34, 0.47, 1.00), // Left leg.
		Color(0.78, 0.36, 1.00), // Right leg.
	};
	return hitgroup_colors[CLAMP(p_hitgroup, 0, 7)];
}

void SceneImportSettingsScreen::_build_hitbox_editor(VBoxContainer *p_tab) {
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	p_tab->add_child(scroll);
	VBoxContainer *content = memnew(VBoxContainer);
	content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(content);

	Label *hint = memnew(Label);
	hint->set_text(TTR("Body presets create approximate boxes from model bounds and use matching humanoid bones when available. Adjust each preset to fit the model."));
	hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	content->add_child(hint);
	Button *auto_generate_button = memnew(Button);
	auto_generate_button->set_text(TTR("Auto Generate Hitboxes"));
	auto_generate_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Skeleton3D"), EditorStringName(EditorIcons)));
	auto_generate_button->set_tooltip_text(TTR("Generate missing humanoid hitboxes from the model bounds and skeleton"));
	auto_generate_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	auto_generate_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_auto_generate_hitboxes));
	content->add_child(auto_generate_button);

	HBoxContainer *list_toolbar = memnew(HBoxContainer);
	content->add_child(list_toolbar);
	Label *list_title = memnew(Label);
	list_title->set_text(TTR("Hitbox List"));
	list_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	list_toolbar->add_child(list_title);
	hitbox_count_label = memnew(Label);
	list_toolbar->add_child(hitbox_count_label);
	Button *add_button = memnew(Button);
	add_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Add"), EditorStringName(EditorIcons)));
	add_button->set_tooltip_text(TTR("Add Hitbox"));
	add_button->set_flat(true);
	add_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_add_hitbox));
	list_toolbar->add_child(add_button);
	MenuButton *preset_button = memnew(MenuButton);
	preset_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Skeleton3D"), EditorStringName(EditorIcons)));
	preset_button->set_tooltip_text(TTR("Add Body-Part Presets"));
	preset_button->set_flat(true);
	list_toolbar->add_child(preset_button);
	PopupMenu *preset_menu = preset_button->get_popup();
	preset_menu->add_item(TTR("Add Humanoid Set"), HITBOX_PRESET_HUMANOID_SET);
	preset_menu->add_separator();
	preset_menu->add_item(TTR("Head"), HITBOX_PRESET_HEAD);
	preset_menu->add_item(TTR("Chest"), HITBOX_PRESET_CHEST);
	preset_menu->add_item(TTR("Stomach"), HITBOX_PRESET_STOMACH);
	preset_menu->add_item(TTR("Left Arm"), HITBOX_PRESET_LEFT_ARM);
	preset_menu->add_item(TTR("Right Arm"), HITBOX_PRESET_RIGHT_ARM);
	preset_menu->add_item(TTR("Left Leg"), HITBOX_PRESET_LEFT_LEG);
	preset_menu->add_item(TTR("Right Leg"), HITBOX_PRESET_RIGHT_LEG);
	preset_menu->connect(SceneStringName(id_pressed), callable_mp(this, &SceneImportSettingsScreen::_hitbox_preset_selected));
	hitbox_duplicate_button = memnew(Button);
	hitbox_duplicate_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Duplicate"), EditorStringName(EditorIcons)));
	hitbox_duplicate_button->set_tooltip_text(TTR("Duplicate Hitbox"));
	hitbox_duplicate_button->set_flat(true);
	hitbox_duplicate_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_duplicate_hitbox));
	list_toolbar->add_child(hitbox_duplicate_button);
	hitbox_remove_button = memnew(Button);
	hitbox_remove_button->set_button_icon(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("Remove"), EditorStringName(EditorIcons)));
	hitbox_remove_button->set_tooltip_text(TTR("Remove Hitbox"));
	hitbox_remove_button->set_flat(true);
	hitbox_remove_button->connect(SceneStringName(pressed), callable_mp(this, &SceneImportSettingsScreen::_remove_hitbox));
	list_toolbar->add_child(hitbox_remove_button);

	hitbox_tree = memnew(Tree);
	hitbox_tree->set_columns(2);
	hitbox_tree->set_hide_root(true);
	hitbox_tree->set_column_titles_visible(true);
	hitbox_tree->set_column_title(0, TTR("Name"));
	hitbox_tree->set_column_title(1, TTR("Hitgroup"));
	hitbox_tree->set_column_expand(0, true);
	hitbox_tree->set_column_custom_minimum_width(0, 120 * EDSCALE);
	hitbox_tree->set_column_expand(1, false);
	hitbox_tree->set_column_custom_minimum_width(1, 105 * EDSCALE);
	hitbox_tree->set_custom_minimum_size(Size2(0, 150 * EDSCALE));
	hitbox_tree->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	// Keep the list at its natural full height so every hitbox is visible at
	// once. The surrounding ScrollContainer remains available for the inspector
	// fields below the list when the panel is short.
	hitbox_tree->set_v_size_flags(Control::SIZE_FILL);
	hitbox_tree->connect(SceneStringName(item_selected), callable_mp(this, &SceneImportSettingsScreen::_hitbox_tree_selected));
	content->add_child(hitbox_tree);

	Label *selected_hitbox_label = memnew(Label);
	selected_hitbox_label->set_text(TTR("Selected Hitbox"));
	content->add_child(selected_hitbox_label);

	HBoxContainer *name_row = memnew(HBoxContainer);
	content->add_child(name_row);
	Label *name_label = memnew(Label);
	name_label->set_text(TTR("Name"));
	name_label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
	name_row->add_child(name_label);
	hitbox_name_input = memnew(LineEdit);
	hitbox_name_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	hitbox_name_input->connect(SceneStringName(text_changed), callable_mp(this, &SceneImportSettingsScreen::_hitbox_name_changed));
	hitbox_name_input->connect(SceneStringName(text_submitted), callable_mp(this, &SceneImportSettingsScreen::_hitbox_name_committed).unbind(1));
	hitbox_name_input->connect(SceneStringName(focus_exited), callable_mp(this, &SceneImportSettingsScreen::_hitbox_name_committed));
	name_row->add_child(hitbox_name_input);

	HBoxContainer *group_row = memnew(HBoxContainer);
	content->add_child(group_row);
	Label *group_label = memnew(Label);
	group_label->set_text(TTR("Hitgroup"));
	group_label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
	group_row->add_child(group_label);
	hitbox_group_option = memnew(OptionButton);
	hitbox_group_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	const String hitgroup_names[] = { TTR("Generic"), TTR("Head"), TTR("Chest"), TTR("Stomach"), TTR("Left Arm"), TTR("Right Arm"), TTR("Left Leg"), TTR("Right Leg") };
	for (int i = 0; i < 8; i++) {
		hitbox_group_option->add_item(vformat("%d: %s", i, hitgroup_names[i]), i);
	}
	hitbox_group_option->connect(SceneStringName(item_selected), callable_mp(this, &SceneImportSettingsScreen::_hitbox_group_changed));
	group_row->add_child(hitbox_group_option);

	HBoxContainer *damage_row = memnew(HBoxContainer);
	content->add_child(damage_row);
	Label *damage_label = memnew(Label);
	damage_label->set_text(TTR("Damage (HP)"));
	damage_label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
	damage_row->add_child(damage_label);
	hitbox_damage_input = memnew(SpinBox);
	hitbox_damage_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	hitbox_damage_input->set_min(0.0);
	hitbox_damage_input->set_max(10000.0);
	hitbox_damage_input->set_step(1.0);
	hitbox_damage_input->set_suffix(" HP");
	hitbox_damage_input->set_tooltip_text(TTR("Fixed health damage dealt when this hitbox is hit. It can be overridden for a placed model in the Level Inspector."));
	hitbox_damage_input->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_hitbox_damage_changed));
	damage_row->add_child(hitbox_damage_input);

	HBoxContainer *attachment_row = memnew(HBoxContainer);
	content->add_child(attachment_row);
	Label *attachment_label = memnew(Label);
	attachment_label->set_text(TTR("Bone"));
	attachment_label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
	attachment_row->add_child(attachment_label);
	hitbox_attachment_option = memnew(OptionButton);
	hitbox_attachment_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	hitbox_attachment_option->connect(SceneStringName(item_selected), callable_mp(this, &SceneImportSettingsScreen::_hitbox_attachment_changed));
	attachment_row->add_child(hitbox_attachment_option);

	Label *bounds_label = memnew(Label);
	bounds_label->set_text(TTR("Bounds"));
	content->add_child(bounds_label);
	const String axis_names[3] = { TTR("X"), TTR("Y"), TTR("Z") };
	auto add_vector_row = [&](const String &p_title, SpinBox **r_inputs, int p_field) {
		HBoxContainer *row = memnew(HBoxContainer);
		content->add_child(row);
		Label *label = memnew(Label);
		label->set_text(p_title);
		label->set_custom_minimum_size(Size2(90 * EDSCALE, 0));
		row->add_child(label);
		for (int axis = 0; axis < 3; axis++) {
			HBoxContainer *axis_row = memnew(HBoxContainer);
			axis_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			Label *axis_label = memnew(Label);
			axis_label->set_text(axis_names[axis]);
			axis_row->add_child(axis_label);
			SpinBox *input = memnew(SpinBox);
			input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			input->set_min(-10000.0);
			input->set_max(10000.0);
			input->set_step(0.01);
			input->set_allow_greater(true);
			input->set_allow_lesser(true);
			input->connect(SceneStringName(value_changed), callable_mp(this, &SceneImportSettingsScreen::_hitbox_transform_changed).bind(p_field, axis));
			axis_row->add_child(input);
			row->add_child(axis_row);
			r_inputs[axis] = input;
		}
	};
	add_vector_row(TTR("Minimum"), hitbox_minimum_inputs, 0);
	add_vector_row(TTR("Maximum"), hitbox_maximum_inputs, 1);

	for (int hitgroup = 0; hitgroup < 8; hitgroup++) {
		Ref<StandardMaterial3D> normal_material;
		normal_material.instantiate();
		normal_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		normal_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		normal_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
		normal_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		normal_material->set_render_priority(1);
		Color normal_color = get_hitbox_hitgroup_color(hitgroup);
		normal_color.a = 0.28;
		normal_material->set_albedo(normal_color);
		hitbox_preview_materials[hitgroup] = normal_material;

		Ref<StandardMaterial3D> selected_material;
		selected_material.instantiate();
		selected_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		selected_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		selected_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
		selected_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		selected_material->set_render_priority(1);
		Color selected_color = get_hitbox_hitgroup_color(hitgroup).lightened(0.18);
		selected_color.a = 0.48;
		selected_material->set_albedo(selected_color);
		hitbox_selected_preview_materials[hitgroup] = selected_material;
	}

	_populate_hitbox_tree();
	_select_hitbox(-1);
}

void SceneImportSettingsScreen::_populate_hitbox_tree() {
	if (!hitbox_tree) {
		return;
	}
	const String hitgroup_names[] = { TTR("Generic"), TTR("Head"), TTR("Chest"), TTR("Stomach"), TTR("Left Arm"), TTR("Right Arm"), TTR("Left Leg"), TTR("Right Leg") };
	hitbox_ui_updating = true;
	hitbox_tree->clear();
	if (hitbox_count_label) {
		hitbox_count_label->set_text(vformat(TTR("%d hitboxes"), model_hitboxes.size()));
	}
	TreeItem *root = hitbox_tree->create_item();
	for (int i = 0; i < model_hitboxes.size(); i++) {
		const ModelHitbox &hitbox = model_hitboxes[i];
		TreeItem *item = hitbox_tree->create_item(root);
		item->set_text(0, hitbox.name);
		item->set_text(1, vformat("%d: %s", CLAMP(hitbox.hitgroup, 0, 7), hitgroup_names[CLAMP(hitbox.hitgroup, 0, 7)]));
		item->set_custom_color(1, get_hitbox_hitgroup_color(hitbox.hitgroup));
		item->set_meta("hitbox_index", i);
		item->set_selectable(0, true);
		item->set_selectable(1, false);
		if (i == selected_hitbox) {
			item->select(0);
		}
	}
	const real_t tree_row_height = (hitbox_tree->get_theme_default_font_size() + 10) * EDSCALE;
	hitbox_tree->set_custom_minimum_size(Size2(0, MAX(150 * EDSCALE, (model_hitboxes.size() + 1) * tree_row_height + 8 * EDSCALE)));
	hitbox_ui_updating = false;
}

void SceneImportSettingsScreen::_populate_hitbox_attachments() {
	if (!hitbox_attachment_option) {
		return;
	}
	hitbox_ui_updating = true;
	hitbox_attachment_option->clear();
	hitbox_attachment_option->add_item(TTR("Model Root"));
	hitbox_attachment_option->set_item_metadata(0, Dictionary());
	int selected = 0;
	if (selected_hitbox >= 0 && selected_hitbox < model_hitboxes.size()) {
		const ModelHitbox &hitbox = model_hitboxes[selected_hitbox];
		for (Skeleton3D *skeleton : skeletons) {
			if (skeleton == nullptr) {
				continue;
			}
			const String skeleton_path = scene ? String(scene->get_path_to(skeleton)) : String();
			for (int bone = 0; bone < skeleton->get_bone_count(); bone++) {
				const String bone_name = skeleton->get_bone_name(bone);
				Dictionary attachment;
				attachment["skeleton_path"] = skeleton_path;
				attachment["bone_name"] = bone_name;
				const int item_index = hitbox_attachment_option->get_item_count();
				hitbox_attachment_option->add_item(vformat(TTR("%s / %s"), skeleton->get_name(), bone_name));
				hitbox_attachment_option->set_item_metadata(item_index, attachment);
				if (skeleton_path == hitbox.skeleton_path && bone_name == hitbox.bone_name) {
					selected = item_index;
				}
			}
		}
	}
	hitbox_attachment_option->select(selected);
	hitbox_attachment_option->set_disabled(selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size());
	hitbox_ui_updating = false;
}

void SceneImportSettingsScreen::_select_hitbox(int p_index, bool p_refresh_tree) {
	selected_hitbox = (p_index >= 0 && p_index < model_hitboxes.size()) ? p_index : -1;
	if (hitbox_name_input) {
		hitbox_ui_updating = true;
		const bool valid = selected_hitbox >= 0;
		hitbox_name_input->set_text(valid ? model_hitboxes[selected_hitbox].name : String());
		hitbox_name_input->set_editable(valid);
		hitbox_group_option->set_disabled(!valid);
		for (int axis = 0; axis < 3; axis++) {
			hitbox_minimum_inputs[axis]->set_editable(valid);
			hitbox_maximum_inputs[axis]->set_editable(valid);
			if (valid) {
				hitbox_minimum_inputs[axis]->set_value_no_signal(model_hitboxes[selected_hitbox].minimum[axis]);
				hitbox_maximum_inputs[axis]->set_value_no_signal(model_hitboxes[selected_hitbox].maximum[axis]);
			} else {
				hitbox_minimum_inputs[axis]->set_value_no_signal(-0.5);
				hitbox_maximum_inputs[axis]->set_value_no_signal(0.5);
			}
		}
		if (valid) {
			hitbox_group_option->select(model_hitboxes[selected_hitbox].hitgroup);
			hitbox_damage_input->set_value_no_signal(model_hitboxes[selected_hitbox].damage);
		}
		hitbox_damage_input->set_editable(valid);
		if (!valid) {
			hitbox_damage_input->set_value_no_signal(15.0);
		}
		hitbox_ui_updating = false;
	}
	if (hitbox_duplicate_button) {
		hitbox_duplicate_button->set_disabled(selected_hitbox < 0);
	}
	if (hitbox_remove_button) {
		hitbox_remove_button->set_disabled(selected_hitbox < 0);
	}
	_populate_hitbox_attachments();
	if (p_refresh_tree) {
		_populate_hitbox_tree();
	}
	_update_hitbox_preview();
}

void SceneImportSettingsScreen::_add_hitbox() {
	ModelHitbox hitbox;
	hitbox.name = vformat(TTR("Hitbox %d"), model_hitboxes.size() + 1);
	if (scene) {
		const Vector3 half_size = Vector3(MAX(contents_aabb.size.x * 0.4, 0.05), MAX(contents_aabb.size.y * 0.4, 0.05), MAX(contents_aabb.size.z * 0.4, 0.05));
		hitbox.minimum = contents_aabb.get_center() - half_size;
		hitbox.maximum = contents_aabb.get_center() + half_size;
	}
	model_hitboxes.push_back(hitbox);
	selected_hitbox = model_hitboxes.size() - 1;
	_select_hitbox(selected_hitbox);
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_duplicate_hitbox() {
	if (selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	ModelHitbox duplicate = model_hitboxes[selected_hitbox];
	const String base_name = duplicate.name + TTR(" Copy");
	duplicate.name = base_name;
	int copy_number = 2;
	bool name_in_use = true;
	while (name_in_use) {
		name_in_use = false;
		for (const ModelHitbox &existing : model_hitboxes) {
			if (existing.name == duplicate.name) {
				duplicate.name = vformat(TTR("%s %d"), base_name, copy_number++);
				name_in_use = true;
				break;
			}
		}
	}
	model_hitboxes.push_back(duplicate);
	selected_hitbox = model_hitboxes.size() - 1;
	_select_hitbox(selected_hitbox);
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_auto_generate_hitboxes() {
	_generate_hitbox_preset(HITBOX_PRESET_HUMANOID_SET, true);
}

void SceneImportSettingsScreen::_hitbox_preset_selected(int p_preset) {
	_generate_hitbox_preset(p_preset, false);
}

void SceneImportSettingsScreen::_generate_hitbox_preset(int p_preset, bool p_only_missing) {
	if (p_preset < HITBOX_PRESET_HUMANOID_SET || p_preset > HITBOX_PRESET_RIGHT_LEG) {
		return;
	}

	static const char *preset_names[] = { "Head", "Chest", "Stomach", "Left Arm", "Right Arm", "Left Leg", "Right Leg" };
	static const int preset_hitgroups[] = { 1, 2, 3, 4, 5, 6, 7 };
	static const Vector3 preset_minimum_fractions[] = {
		Vector3(0.34, 0.82, 0.25),
		Vector3(0.26, 0.59, 0.25),
		Vector3(0.31, 0.40, 0.30),
		Vector3(0.02, 0.38, 0.36),
		Vector3(0.52, 0.38, 0.36),
		Vector3(0.35, 0.00, 0.28),
		Vector3(0.51, 0.00, 0.28),
	};
	static const Vector3 preset_maximum_fractions[] = {
		Vector3(0.66, 1.00, 0.75),
		Vector3(0.74, 0.84, 0.75),
		Vector3(0.69, 0.64, 0.70),
		Vector3(0.48, 0.82, 0.64),
		Vector3(0.98, 0.82, 0.64),
		Vector3(0.49, 0.54, 0.72),
		Vector3(0.65, 0.54, 0.72),
	};
	static const char *bone_aliases[][10] = {
		{ "head", nullptr },
		{ "chest", "upperchest", "spine3", "spine03", "spine2", nullptr },
		{ "abdomen", "spine1", "spine02", "spine", "pelvis", "hips", nullptr },
		{ "leftupperarm", "upperarmleft", "upperarml", "lupperarm", "leftarm", "armleft", "arml", "larm", nullptr },
		{ "rightupperarm", "upperarmright", "upperarmr", "rupperarm", "rightarm", "armright", "armr", "rarm", nullptr },
		{ "leftupleg", "uplegleft", "uplegl", "lupleg", "leftthigh", "thighleft", "thighl", "lthigh", "leftleg", "legl" },
		{ "rightupleg", "uplegright", "uplegr", "rupleg", "rightthigh", "thighright", "thighr", "rthigh", "rightleg", "legr" },
	};
	static const char *forearm_bone_aliases[][9] = {
		{ "leftforearm", "forearmleft", "forearml", "lforearm", "leftlowerarm", "lowerarmleft", "lowerarml", "llowerarm", nullptr },
		{ "rightforearm", "forearmright", "forearmr", "rforearm", "rightlowerarm", "lowerarmright", "lowerarmr", "rlowerarm", nullptr },
	};
	static const char *lower_leg_bone_aliases[][14] = {
		{ "leftlowerleg", "lowerlegleft", "lowerlegl", "llowerleg", "leftcalf", "calfleft", "calfl", "lcalf", "leftshin", "shinl", "leftleg", "legleft", "legl", "lleg" },
		{ "rightlowerleg", "lowerlegright", "lowerlegr", "rlowerleg", "rightcalf", "calfright", "calfr", "rcalf", "rightshin", "shinr", "rightleg", "legright", "legr", "rleg" },
	};
	const String segment_names[4][2] = {
		{ TTR("Left Upper Arm"), TTR("Left Forearm") },
		{ TTR("Right Upper Arm"), TTR("Right Forearm") },
		{ TTR("Left Thigh"), TTR("Left Lower Leg") },
		{ TTR("Right Thigh"), TTR("Right Lower Leg") },
	};
	const int first_part = p_preset == HITBOX_PRESET_HUMANOID_SET ? 0 : p_preset - HITBOX_PRESET_HEAD;
	const int last_part = p_preset == HITBOX_PRESET_HUMANOID_SET ? 6 : first_part;
	Node3D *scene_root = Object::cast_to<Node3D>(scene);
	int first_added = -1;
	auto make_aliases = [](const char *const *p_aliases, int p_max_count) {
		Vector<String> aliases;
		for (int i = 0; i < p_max_count && p_aliases[i] != nullptr; i++) {
			aliases.push_back(String(p_aliases[i]));
		}
		return aliases;
	};
	auto include_point = [](const Vector3 &p_point, Vector3 &r_minimum, Vector3 &r_maximum, bool &r_first) {
		if (r_first) {
			r_minimum = p_point;
			r_maximum = p_point;
			r_first = false;
			return;
		}
		for (int axis = 0; axis < 3; axis++) {
			r_minimum[axis] = MIN(r_minimum[axis], p_point[axis]);
			r_maximum[axis] = MAX(r_maximum[axis], p_point[axis]);
		}
	};
	auto transform_bounds = [](const Vector3 &p_minimum, const Vector3 &p_maximum, const Transform3D &p_transform, Vector3 &r_minimum, Vector3 &r_maximum) {
		bool first_corner = true;
		for (int x = 0; x < 2; x++) {
			for (int y = 0; y < 2; y++) {
				for (int z = 0; z < 2; z++) {
					const Vector3 corner(x == 0 ? p_minimum.x : p_maximum.x, y == 0 ? p_minimum.y : p_maximum.y, z == 0 ? p_minimum.z : p_maximum.z);
					const Vector3 transformed_corner = p_transform.xform(corner);
					if (first_corner) {
						r_minimum = transformed_corner;
						r_maximum = transformed_corner;
						first_corner = false;
					} else {
						for (int axis = 0; axis < 3; axis++) {
							r_minimum[axis] = MIN(r_minimum[axis], transformed_corner[axis]);
							r_maximum[axis] = MAX(r_maximum[axis], transformed_corner[axis]);
						}
					}
				}
			}
		}
	};
	auto unique_hitbox_name = [&](const String &p_base_name) {
		String name = p_base_name;
		int suffix = 2;
		while (true) {
			bool in_use = false;
			for (const ModelHitbox &existing : model_hitboxes) {
				if (existing.name == name) {
					in_use = true;
					break;
				}
			}
			if (!in_use) {
				return name;
			}
			name = vformat(TTR("%s %d"), p_base_name, suffix++);
		}
	};
	auto convert_root_bounds_to_bone = [&](const Vector3 &p_root_minimum, const Vector3 &p_root_maximum, Skeleton3D *p_skeleton, int p_bone, Vector3 &r_minimum, Vector3 &r_maximum) {
		if (!scene_root || !p_skeleton || p_bone < 0) {
			r_minimum = p_root_minimum;
			r_maximum = p_root_maximum;
			return;
		}
		const Transform3D root_to_bone = p_skeleton->get_bone_global_rest(p_bone).affine_inverse() * p_skeleton->get_global_transform().affine_inverse() * scene_root->get_global_transform();
		transform_bounds(p_root_minimum, p_root_maximum, root_to_bone, r_minimum, r_maximum);
	};

	for (int part = first_part; part <= last_part; part++) {
		const bool is_arm_part = part == 3 || part == 4;
		const bool is_leg_part = part == 5 || part == 6;
		const bool is_limb_part = is_arm_part || is_leg_part;
		const int side = part == 3 || part == 5 ? 0 : 1;
		Vector<String> limb_names;
		if (is_limb_part) {
			limb_names.push_back(segment_names[part - 3][0]);
			limb_names.push_back(segment_names[part - 3][1]);
		}
		bool migrate_legacy_hitbox = false;
		int legacy_hitbox_index = -1;
		Vector3 legacy_root_minimum;
		Vector3 legacy_root_maximum;
		if (p_only_missing) {
			int group_count = 0;
			bool segment_exists = false;
			for (int i = 0; i < model_hitboxes.size(); i++) {
				const ModelHitbox &existing = model_hitboxes[i];
				if (existing.hitgroup == preset_hitgroups[part]) {
					group_count++;
					if (is_limb_part && (existing.name == limb_names[0] || existing.name == limb_names[1])) {
						segment_exists = true;
					}
					if (existing.name == TTR(preset_names[part])) {
						legacy_hitbox_index = i;
					}
				}
			}
			if (group_count > 0) {
				// Earlier versions generated one default-named box for each limb.
				// Split that box on the next auto-generation while retaining its
				// edited bounds. Leave custom and already-segmented hitboxes intact.
				migrate_legacy_hitbox = is_limb_part && group_count == 1 && !segment_exists && legacy_hitbox_index >= 0;
				if (!migrate_legacy_hitbox) {
					continue;
				}
				const ModelHitbox &legacy = model_hitboxes[legacy_hitbox_index];
				Transform3D old_bone_to_root;
				if (scene_root && !legacy.skeleton_path.is_empty() && !legacy.bone_name.is_empty()) {
					Skeleton3D *old_skeleton = Object::cast_to<Skeleton3D>(scene_root->get_node_or_null(NodePath(legacy.skeleton_path)));
					const int old_bone = old_skeleton ? old_skeleton->find_bone(legacy.bone_name) : -1;
					if (old_skeleton && old_bone >= 0) {
						old_bone_to_root = scene_root->get_global_transform().affine_inverse() * old_skeleton->get_global_transform() * old_skeleton->get_bone_global_rest(old_bone);
					}
				}
				transform_bounds(legacy.minimum, legacy.maximum, old_bone_to_root, legacy_root_minimum, legacy_root_maximum);
			}
		}

		Vector<String> aliases = make_aliases(bone_aliases[part], 10);

		Skeleton3D *matched_skeleton = nullptr;
		int matched_bone = -1;
		int matched_score = -1;
		if (scene_root) {
			for (Skeleton3D *skeleton : skeletons) {
				int candidate_score = -1;
				const int candidate_bone = find_hitbox_preset_bone(skeleton, aliases, -1, &candidate_score);
				if (candidate_bone >= 0 && candidate_score > matched_score) {
					matched_skeleton = skeleton;
					matched_bone = candidate_bone;
					matched_score = candidate_score;
				}
			}
		}

		Vector3 minimum_fraction = preset_minimum_fractions[part];
		Vector3 maximum_fraction = preset_maximum_fractions[part];
		const bool is_left_side = part == 3 || part == 5;
		const bool is_right_side = part == 4 || part == 6;
		if ((is_left_side || is_right_side) && matched_skeleton && matched_bone >= 0 && contents_aabb.size.x > 0.0) {
			const Transform3D skeleton_to_scene = scene_root->get_global_transform().affine_inverse() * matched_skeleton->get_global_transform();
			const Vector3 bone_position = skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(matched_bone).origin);
			const real_t side_offset = bone_position.x - contents_aabb.get_center().x;
			if (Math::abs(side_offset) > contents_aabb.size.x * 0.025) {
				const int detected_side = side_offset < 0.0 ? -1 : 1;
				const int preset_side = is_left_side ? -1 : 1;
				if (detected_side != preset_side) {
					const real_t old_minimum_x = minimum_fraction.x;
					minimum_fraction.x = 1.0 - maximum_fraction.x;
					maximum_fraction.x = 1.0 - old_minimum_x;
				}
			}
		}

		Vector3 fallback_minimum = contents_aabb.position + contents_aabb.size * minimum_fraction;
		Vector3 fallback_maximum = contents_aabb.position + contents_aabb.size * maximum_fraction;
		const Vector3 fallback_center = (fallback_minimum + fallback_maximum) * 0.5;
		for (int axis = 0; axis < 3; axis++) {
			if (fallback_maximum[axis] - fallback_minimum[axis] < 0.02) {
				fallback_minimum[axis] = fallback_center[axis] - 0.01;
				fallback_maximum[axis] = fallback_center[axis] + 0.01;
			}
		}

		if (migrate_legacy_hitbox) {
			model_hitboxes.remove_at(legacy_hitbox_index);
			if (selected_hitbox == legacy_hitbox_index) {
				selected_hitbox = -1;
			} else if (selected_hitbox > legacy_hitbox_index) {
				selected_hitbox--;
			}
			if (first_added > legacy_hitbox_index) {
				first_added--;
			}
		}

		Vector3 split_minimum = migrate_legacy_hitbox ? legacy_root_minimum : fallback_minimum;
		Vector3 split_maximum = migrate_legacy_hitbox ? legacy_root_maximum : fallback_maximum;
		int split_axis = 1;
		real_t split_direction = 1.0;
		if (is_arm_part) {
			split_axis = split_maximum.x - split_minimum.x >= split_maximum.z - split_minimum.z ? 0 : 2;
			split_direction = is_left_side ? -1.0 : 1.0;
		} else if (is_leg_part) {
			split_axis = 1;
			split_direction = 1.0; // Thigh is the upper section of the leg.
		}
		Vector3 segment_joint = Vector3();
		int lower_bone = -1;
		if (matched_skeleton && matched_bone >= 0 && is_limb_part) {
			const Vector<String> lower_aliases = is_arm_part ? make_aliases(forearm_bone_aliases[side], 9) : make_aliases(lower_leg_bone_aliases[side], 14);
			// The lower segment must be a descendant of the upper segment. This
			// prevents a partial rig's fallback alias from binding both boxes to
			// the same bone (which makes the lower box drift during animation).
			lower_bone = find_hitbox_preset_bone(matched_skeleton, lower_aliases, matched_bone);
			if (lower_bone >= 0) {
				const Transform3D skeleton_to_scene = scene_root->get_global_transform().affine_inverse() * matched_skeleton->get_global_transform();
				segment_joint = skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(lower_bone).origin);
				const Vector3 upper_joint = skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(matched_bone).origin);
				Vector3 direction = segment_joint - upper_joint;
				if (is_arm_part) {
					const real_t horizontal_length = MAX(Math::abs(direction.x), Math::abs(direction.z));
					split_axis = Math::abs(direction.x) >= Math::abs(direction.z) ? 0 : 2;
					if (horizontal_length < 0.001) {
						split_axis = split_maximum.x - split_minimum.x >= split_maximum.z - split_minimum.z ? 0 : 2;
					}
				} else {
					split_axis = 1;
				}
				split_direction = direction[split_axis] < 0.0 ? -1.0 : 1.0;
			}
		}
		const real_t split_extent = split_maximum[split_axis] - split_minimum[split_axis];
		real_t split_value = (split_minimum[split_axis] + split_maximum[split_axis]) * 0.5;
		if (migrate_legacy_hitbox && lower_bone >= 0) {
			const real_t joint_fraction = (segment_joint[split_axis] - split_minimum[split_axis]) / MAX(split_extent, 0.001);
			if (joint_fraction > 0.2 && joint_fraction < 0.8) {
				split_value = segment_joint[split_axis];
			}
		}
		Vector3 segment_minimum[2] = { split_minimum, split_minimum };
		Vector3 segment_maximum[2] = { split_maximum, split_maximum };
		const real_t overlap = MAX(contents_aabb.size.y * 0.0125, 0.01);
		if (split_direction >= 0.0) {
			segment_maximum[0][split_axis] = MIN(split_maximum[split_axis], split_value + overlap);
			segment_minimum[1][split_axis] = MAX(split_minimum[split_axis], split_value - overlap);
		} else {
			segment_minimum[0][split_axis] = MAX(split_minimum[split_axis], split_value - overlap);
			segment_maximum[1][split_axis] = MIN(split_maximum[split_axis], split_value + overlap);
		}

		const int segment_count = is_limb_part ? 2 : 1;
		for (int segment = 0; segment < segment_count; segment++) {
			Vector3 root_minimum = is_limb_part ? segment_minimum[segment] : fallback_minimum;
			Vector3 root_maximum = is_limb_part ? segment_maximum[segment] : fallback_maximum;
			int attachment_bone = matched_bone;
			if (is_limb_part && segment == 1 && lower_bone >= 0) {
				attachment_bone = lower_bone;
			}

			// For rigged models, derive each segment from its own joint chain.
			// The lower box follows the wrist/hand or ankle/foot root, while
			// excluding fingers and toes so they cannot pull the box off the limb.
			if (is_limb_part && matched_skeleton && matched_bone >= 0 && lower_bone >= 0 && !migrate_legacy_hitbox) {
				Vector3 points_minimum;
				Vector3 points_maximum;
				bool first_point = true;
				const Transform3D skeleton_to_scene = scene_root->get_global_transform().affine_inverse() * matched_skeleton->get_global_transform();
				if (segment == 0) {
					include_point(skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(matched_bone).origin), points_minimum, points_maximum, first_point);
					include_point(skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(lower_bone).origin), points_minimum, points_maximum, first_point);
				} else {
					for (int bone = 0; bone < matched_skeleton->get_bone_count(); bone++) {
						int ancestor = bone;
						while (ancestor >= 0 && ancestor != lower_bone) {
							ancestor = matched_skeleton->get_bone_parent(ancestor);
						}
						if (ancestor == lower_bone) {
							if (is_hitbox_digit_bone_name(matched_skeleton->get_bone_name(bone))) {
								continue;
							}
							include_point(skeleton_to_scene.xform(matched_skeleton->get_bone_global_rest(bone).origin), points_minimum, points_maximum, first_point);
						}
					}
				}
				if (!first_point) {
					const real_t padding = MAX(contents_aabb.size.y * 0.025, 0.025);
					for (int axis = 0; axis < 3; axis++) {
						root_minimum[axis] = points_minimum[axis] - padding;
						root_maximum[axis] = points_maximum[axis] + padding;
					}
				}
			}

			ModelHitbox hitbox;
			hitbox.name = unique_hitbox_name(is_limb_part ? limb_names[segment] : TTR(preset_names[part]));
			hitbox.hitgroup = preset_hitgroups[part];
			hitbox.damage = get_default_hitbox_damage(hitbox.hitgroup);
			if (matched_skeleton && matched_bone >= 0 && scene_root) {
				convert_root_bounds_to_bone(root_minimum, root_maximum, matched_skeleton, attachment_bone, hitbox.minimum, hitbox.maximum);
				hitbox.skeleton_path = String(scene->get_path_to(matched_skeleton));
				hitbox.bone_name = matched_skeleton->get_bone_name(attachment_bone >= 0 ? attachment_bone : matched_bone);
			} else {
				hitbox.minimum = root_minimum;
				hitbox.maximum = root_maximum;
			}

			model_hitboxes.push_back(hitbox);
			if (first_added < 0) {
				first_added = model_hitboxes.size() - 1;
			}
		}
	}

	if (first_added >= 0) {
		_select_hitbox(first_added);
		_save_hitbox_config();
	}
}

void SceneImportSettingsScreen::_remove_hitbox() {
	if (selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	model_hitboxes.remove_at(selected_hitbox);
	selected_hitbox = MIN(selected_hitbox, model_hitboxes.size() - 1);
	_select_hitbox(selected_hitbox);
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_tree_selected() {
	if (hitbox_ui_updating || !hitbox_tree) {
		return;
	}
	TreeItem *item = hitbox_tree->get_selected();
	if (item) {
		// The Tree already has the clicked item selected. Rebuilding it here would
		// delete that item while Tree::select_single_item() is still emitting its
		// selection signal.
		_select_hitbox((int)item->get_meta("hitbox_index", -1), false);
	}
}

void SceneImportSettingsScreen::_hitbox_name_changed(const String &p_name) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	model_hitboxes.write[selected_hitbox].name = p_name;
	_populate_hitbox_tree();
	_save_hitbox_config();
}

// On Enter or leaving the field: a hitbox name becomes a node name in the
// imported scene and level overrides of its damage are stored by that path,
// so it must be unique and not empty (duplicates got order-based suffixes
// that moved when a box was removed).
void SceneImportSettingsScreen::_hitbox_name_committed() {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	String base = model_hitboxes[selected_hitbox].name.strip_edges().validate_node_name();
	if (base.is_empty()) {
		base = TTR("Hitbox");
	}
	String name = base;
	for (int suffix = 2;; suffix++) {
		bool in_use = false;
		for (int i = 0; i < model_hitboxes.size(); i++) {
			if (i != selected_hitbox && model_hitboxes[i].name == name) {
				in_use = true;
				break;
			}
		}
		if (!in_use) {
			break;
		}
		name = vformat("%s %d", base, suffix);
	}
	if (name == model_hitboxes[selected_hitbox].name) {
		return;
	}
	model_hitboxes.write[selected_hitbox].name = name;
	if (hitbox_name_input) {
		hitbox_name_input->set_text(name);
	}
	_populate_hitbox_tree();
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_group_changed(int p_group) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	model_hitboxes.write[selected_hitbox].hitgroup = CLAMP(p_group, 0, 7);
	_populate_hitbox_tree();
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_damage_changed(double p_value) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size()) {
		return;
	}
	model_hitboxes.write[selected_hitbox].damage = CLAMP((real_t)p_value, 0.0, 10000.0);
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_attachment_changed(int p_attachment) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size() || !hitbox_attachment_option || p_attachment < 0) {
		return;
	}
	Dictionary attachment = hitbox_attachment_option->get_item_metadata(p_attachment);
	ModelHitbox &hitbox = model_hitboxes.write[selected_hitbox];
	const String new_skeleton_path = attachment.get("skeleton_path", String());
	const String new_bone_name = attachment.get("bone_name", String());
	// The corners are in the attached bone's frame: re-express them in the new
	// one so the box stays where it is instead of jumping with the numbers.
	const Transform3D old_to_root = get_hitbox_frame_to_root(scene, hitbox.skeleton_path, hitbox.bone_name);
	const Transform3D new_to_root = get_hitbox_frame_to_root(scene, new_skeleton_path, new_bone_name);
	Vector3 minimum;
	Vector3 maximum;
	transform_hitbox_bounds(hitbox.minimum, hitbox.maximum, new_to_root.affine_inverse() * old_to_root, minimum, maximum);
	hitbox.minimum = minimum;
	hitbox.maximum = maximum;
	hitbox.skeleton_path = new_skeleton_path;
	hitbox.bone_name = new_bone_name;
	_select_hitbox(selected_hitbox); // Shows the converted corners.
	_update_hitbox_preview();
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_hitbox_transform_changed(double p_value, int p_field, int p_axis) {
	if (hitbox_ui_updating || selected_hitbox < 0 || selected_hitbox >= model_hitboxes.size() || p_axis < 0 || p_axis > 2) {
		return;
	}
	ModelHitbox &hitbox = model_hitboxes.write[selected_hitbox];
	if (p_field == 0) {
		hitbox.minimum[p_axis] = p_value;
		if (hitbox.minimum[p_axis] > hitbox.maximum[p_axis] - 0.01) {
			hitbox.maximum[p_axis] = hitbox.minimum[p_axis] + 0.01;
			hitbox_maximum_inputs[p_axis]->set_value_no_signal(hitbox.maximum[p_axis]);
		}
	} else if (p_field == 1) {
		hitbox.maximum[p_axis] = p_value;
		if (hitbox.maximum[p_axis] < hitbox.minimum[p_axis] + 0.01) {
			hitbox.minimum[p_axis] = hitbox.maximum[p_axis] - 0.01;
			hitbox_minimum_inputs[p_axis]->set_value_no_signal(hitbox.minimum[p_axis]);
		}
	}
	_update_hitbox_preview();
	_save_hitbox_config();
}

void SceneImportSettingsScreen::_save_hitbox_config() {
	if (base_path.is_empty()) {
		return;
	}
	// Opening a model without hitboxes must not create an empty sidecar: it
	// would win over hitboxes added to the .import later and wipe them.
	if (model_hitboxes.is_empty() && !FileAccess::exists(base_path + ".hitboxcfg")) {
		return;
	}
	Array saved_hitboxes;
	for (const ModelHitbox &hitbox : model_hitboxes) {
		Dictionary item;
		item["name"] = hitbox.name;
		item["minimum"] = hitbox.minimum;
		item["maximum"] = hitbox.maximum;
		item["hitgroup"] = hitbox.hitgroup;
		item["damage"] = hitbox.damage;
		item["skeleton_path"] = hitbox.skeleton_path;
		item["bone_name"] = hitbox.bone_name;
		saved_hitboxes.push_back(item);
	}
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("hitboxes", "version", MODEL_HITBOX_CONFIG_VERSION);
	config->set_value("hitboxes", "items", saved_hitboxes);
	if (config->save(base_path + ".hitboxcfg") != OK) {
		ERR_PRINT("Unable to save model hitbox settings: " + base_path + ".hitboxcfg");
	}
}

void SceneImportSettingsScreen::_load_hitbox_config() {
	model_hitboxes.clear();
	selected_hitbox = -1;
	hitbox_config_version = MODEL_HITBOX_CONFIG_VERSION;
	if (base_path.is_empty()) {
		return;
	}
	Array saved_hitboxes;
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(base_path + ".hitboxcfg") == OK) {
		hitbox_config_version = MAX(0, (int)config->get_value("hitboxes", "version", 1));
		saved_hitboxes = config->get_value("hitboxes", "items", Array());
	} else if (defaults.has("fps_studio/hitboxes")) {
		hitbox_config_version = 1;
		saved_hitboxes = defaults["fps_studio/hitboxes"];
	}
	for (const Variant &value : saved_hitboxes) {
		if (value.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary item = value;
		ModelHitbox hitbox;
		hitbox.name = item.get("name", vformat(TTR("Hitbox %d"), model_hitboxes.size() + 1));
		hitbox.minimum = item.get("minimum", Vector3(-0.5, -0.5, -0.5));
		hitbox.maximum = item.get("maximum", Vector3(0.5, 0.5, 0.5));
		hitbox.hitgroup = CLAMP((int)item.get("hitgroup", 0), 0, 7);
		hitbox.damage = CLAMP((real_t)item.get("damage", get_default_hitbox_damage(hitbox.hitgroup)), 0.0, 10000.0);
		hitbox.skeleton_path = item.get("skeleton_path", String());
		hitbox.bone_name = item.get("bone_name", String());
		model_hitboxes.push_back(hitbox);
	}
	selected_hitbox = model_hitboxes.is_empty() ? -1 : 0;
	_populate_hitbox_tree();
	_select_hitbox(selected_hitbox);
}

bool SceneImportSettingsScreen::_repair_hitbox_attachments() {
	Node3D *scene_root = Object::cast_to<Node3D>(scene);
	if (!scene_root || hitbox_config_version >= MODEL_HITBOX_ATTACHMENT_VERSION) {
		return false;
	}

	static const char *preset_names[] = { "Head", "Chest", "Stomach", "Left Upper Arm", "Left Forearm", "Right Upper Arm", "Right Forearm", "Left Thigh", "Left Lower Leg", "Right Thigh", "Right Lower Leg" };
	static const int preset_hitgroups[] = { 1, 2, 3, 4, 4, 5, 5, 6, 6, 7, 7 };
	static const int parent_slots[] = { -1, -1, -1, -1, 3, -1, 5, -1, 7, -1, 9 };
	static const char *preset_aliases[][15] = {
		{ "head", nullptr },
		{ "chest", "upperchest", "spine3", "spine03", "spine2", nullptr },
		{ "abdomen", "spine1", "spine02", "spine", "pelvis", "hips", nullptr },
		{ "leftupperarm", "upperarmleft", "upperarml", "lupperarm", "leftarm", "armleft", "arml", "larm", nullptr },
		{ "leftforearm", "forearmleft", "forearml", "lforearm", "leftlowerarm", "lowerarmleft", "lowerarml", "llowerarm", nullptr },
		{ "rightupperarm", "upperarmright", "upperarmr", "rupperarm", "rightarm", "armright", "armr", "rarm", nullptr },
		{ "rightforearm", "forearmright", "forearmr", "rforearm", "rightlowerarm", "lowerarmright", "lowerarmr", "rlowerarm", nullptr },
		{ "leftupleg", "uplegleft", "uplegl", "lupleg", "leftthigh", "thighleft", "thighl", "lthigh", "leftleg", "legl", nullptr },
		{ "leftlowerleg", "lowerlegleft", "lowerlegl", "llowerleg", "leftcalf", "calfleft", "calfl", "lcalf", "leftshin", "shinl", "leftleg", "legleft", "legl", "lleg", nullptr },
		{ "rightupleg", "uplegright", "uplegr", "rupleg", "rightthigh", "thighright", "thighr", "rthigh", "rightleg", "legr", nullptr },
		{ "rightlowerleg", "lowerlegright", "lowerlegr", "rlowerleg", "rightcalf", "calfright", "calfr", "rcalf", "rightshin", "shinr", "rightleg", "legright", "legr", "rleg", nullptr },
	};
	auto make_aliases = [](const char *const *p_aliases, int p_max_count) {
		Vector<String> aliases;
		for (int i = 0; i < p_max_count && p_aliases[i] != nullptr; i++) {
			aliases.push_back(String(p_aliases[i]));
		}
		return aliases;
	};
	auto transform_bounds = [](const Vector3 &p_minimum, const Vector3 &p_maximum, const Transform3D &p_transform, Vector3 &r_minimum, Vector3 &r_maximum) {
		bool first_corner = true;
		for (int x = 0; x < 2; x++) {
			for (int y = 0; y < 2; y++) {
				for (int z = 0; z < 2; z++) {
					const Vector3 corner(x == 0 ? p_minimum.x : p_maximum.x, y == 0 ? p_minimum.y : p_maximum.y, z == 0 ? p_minimum.z : p_maximum.z);
					const Vector3 transformed_corner = p_transform.xform(corner);
					if (first_corner) {
						r_minimum = transformed_corner;
						r_maximum = transformed_corner;
						first_corner = false;
					} else {
						for (int axis = 0; axis < 3; axis++) {
							r_minimum[axis] = MIN(r_minimum[axis], transformed_corner[axis]);
							r_maximum[axis] = MAX(r_maximum[axis], transformed_corner[axis]);
						}
					}
				}
			}
		}
	};

	for (ModelHitbox &hitbox : model_hitboxes) {
		int slot = -1;
		for (int candidate = 0; candidate < 11; candidate++) {
			if (hitbox.hitgroup == preset_hitgroups[candidate] && hitbox.name == TTR(preset_names[candidate])) {
				slot = candidate;
				break;
			}
		}
		if (slot < 0) {
			continue; // Keep custom hitboxes and names untouched.
		}
		// Version 1 could select a parallel control rig instead of the deform
		// skeleton. Only control-rig attachments are eligible for migration; an
		// empty attachment means Model Root, and any other valid bone is preserved.
		Skeleton3D *old_skeleton = hitbox.skeleton_path.is_empty() ? nullptr : Object::cast_to<Skeleton3D>(scene_root->get_node_or_null(NodePath(hitbox.skeleton_path)));
		const int old_bone = old_skeleton ? old_skeleton->find_bone(hitbox.bone_name) : -1;
		if (!old_skeleton || old_bone < 0) {
			continue;
		}
		const String old_bone_normalized = normalize_hitbox_bone_name(old_skeleton->get_bone_name(old_bone));
		if (!old_bone_normalized.begins_with("ctrl") && !old_bone_normalized.begins_with("control")) {
			continue;
		}

		const Vector<String> aliases = make_aliases(preset_aliases[slot], 15);
		Skeleton3D *target_skeleton = nullptr;
		int target_bone = -1;
		int target_score = -1;
		for (Skeleton3D *candidate_skeleton : skeletons) {
			if (!candidate_skeleton) {
				continue;
			}
			int parent_bone = -1;
			if (parent_slots[slot] >= 0) {
				const Vector<String> parent_aliases = make_aliases(preset_aliases[parent_slots[slot]], 15);
				parent_bone = find_hitbox_preset_bone(candidate_skeleton, parent_aliases);
				if (parent_bone < 0) {
					continue;
				}
			}
			int candidate_score = -1;
			const int candidate_bone = find_hitbox_preset_bone(candidate_skeleton, aliases, parent_bone, &candidate_score);
			if (candidate_bone >= 0 && candidate_score > target_score) {
				target_skeleton = candidate_skeleton;
				target_bone = candidate_bone;
				target_score = candidate_score;
			}
		}
		if (!target_skeleton || target_bone < 0) {
			continue;
		}
		const String target_bone_normalized = normalize_hitbox_bone_name(target_skeleton->get_bone_name(target_bone));
		if (!target_bone_normalized.begins_with("mixamorig") && !target_bone_normalized.begins_with("mixamo")) {
			continue;
		}
		const String target_skeleton_path = String(scene->get_path_to(target_skeleton));
		const String target_bone_name = target_skeleton->get_bone_name(target_bone);
		if (old_skeleton == target_skeleton && old_bone == target_bone && hitbox.skeleton_path == target_skeleton_path) {
			continue;
		}

		Vector3 root_minimum = hitbox.minimum;
		Vector3 root_maximum = hitbox.maximum;
		if (old_skeleton && old_bone >= 0) {
			const Transform3D old_bone_to_root = scene_root->get_global_transform().affine_inverse() * old_skeleton->get_global_transform() * old_skeleton->get_bone_global_rest(old_bone);
			transform_bounds(hitbox.minimum, hitbox.maximum, old_bone_to_root, root_minimum, root_maximum);
		}
		const Transform3D root_to_new_bone = target_skeleton->get_bone_global_rest(target_bone).affine_inverse() * target_skeleton->get_global_transform().affine_inverse() * scene_root->get_global_transform();
		transform_bounds(root_minimum, root_maximum, root_to_new_bone, hitbox.minimum, hitbox.maximum);
		hitbox.skeleton_path = target_skeleton_path;
		hitbox.bone_name = target_bone_name;
	}
	// Persist the migration marker even if no automatic attachment needed repair.
	hitbox_config_version = MODEL_HITBOX_CONFIG_VERSION;
	return true;
}

void SceneImportSettingsScreen::_clear_hitbox_preview() {
	for (int i = hitbox_preview_nodes.size() - 1; i >= 0; i--) {
		Node3D *node = hitbox_preview_nodes[i];
		if (node && node->get_parent()) {
			node->get_parent()->remove_child(node);
			memdelete(node);
		}
	}
	hitbox_preview_nodes.clear();
}

void SceneImportSettingsScreen::_update_hitbox_preview() {
	_clear_hitbox_preview();
	if (!scene) {
		return;
	}
	for (int i = 0; i < model_hitboxes.size(); i++) {
		const ModelHitbox &hitbox = model_hitboxes[i];
		const int hitgroup = CLAMP(hitbox.hitgroup, 0, 7);
		if (!hitbox_preview_materials[hitgroup].is_valid() || !hitbox_selected_preview_materials[hitgroup].is_valid()) {
			continue;
		}
		Ref<Shape3D> shape = create_model_hitbox_shape(hitbox.minimum, hitbox.maximum);
		Ref<ArrayMesh> debug_mesh = shape->get_debug_mesh();
		if (debug_mesh.is_null()) {
			continue;
		}
		Node *parent = scene;
		if (!hitbox.skeleton_path.is_empty() && !hitbox.bone_name.is_empty()) {
			Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(scene->get_node_or_null(NodePath(hitbox.skeleton_path)));
			if (skeleton && skeleton->find_bone(hitbox.bone_name) >= 0) {
				BoneAttachment3D *attachment = memnew(BoneAttachment3D);
				attachment->set_name(String("HitboxPreviewAttachment_") + itos(i));
				attachment->set_bone_name(hitbox.bone_name);
				skeleton->add_child(attachment, true);
				attachment->set_owner(scene);
				hitbox_preview_nodes.push_back(attachment);
				parent = attachment;
			}
		}
		MeshInstance3D *preview = memnew(MeshInstance3D);
		preview->set_name(String("HitboxPreview_") + itos(i));
		preview->set_mesh(debug_mesh);
		preview->set_material_override(i == selected_hitbox ? hitbox_selected_preview_materials[hitgroup] : hitbox_preview_materials[hitgroup]);
		preview->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		preview->set_gi_mode(GeometryInstance3D::GI_MODE_DISABLED);
		preview->set_transform(Transform3D(Basis(), (hitbox.minimum + hitbox.maximum) * 0.5));
		parent->add_child(preview, true);
		preview->set_owner(scene);
		if (parent == scene) {
			hitbox_preview_nodes.push_back(preview);
		}
	}
}
