/**************************************************************************/
/*  fps_studio_workspace.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                      GODOT FPS STUDIO FORK                             */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to   */
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

#include "fps_studio_workspace.h"

#include "core/io/config_file.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/groups_dock.h"
#include "editor/docks/history_dock.h"
#include "editor/docks/import_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/docks/signals_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_main_screen.h"
#include "editor/gui/editor_bottom_panel.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/main/node.h"
#include "scene/gui/split_container.h"

static constexpr int FPS_STUDIO_LAYOUT_VERSION = 1;

void FPSStudioWorkspace::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_scene_tab_changed", "tab_index"), &FPSStudioWorkspace::_on_scene_tab_changed);
	ClassDB::bind_method(D_METHOD("_on_scene_tab_closed", "tab_index"), &FPSStudioWorkspace::_on_scene_tab_closed);
	ClassDB::bind_method(D_METHOD("_on_center_split_drag_ended"), &FPSStudioWorkspace::_on_center_split_drag_ended);
	ADD_SIGNAL(MethodInfo("scene_tab_changed", PropertyInfo(Variant::INT, "tab_index")));
	ADD_SIGNAL(MethodInfo("scene_tab_closed", PropertyInfo(Variant::INT, "tab_index")));
}

void FPSStudioWorkspace::configure_dock_manager(EditorDockManager *p_dock_manager) {
	dock_manager = p_dock_manager;
	p_dock_manager->add_vsplit(left_l_vsplit);
	p_dock_manager->add_vsplit(left_r_vsplit);
	p_dock_manager->add_vsplit(right_l_vsplit);
	p_dock_manager->add_vsplit(right_r_vsplit);

	p_dock_manager->set_main_vsplit(main_vsplit);
	p_dock_manager->set_main_hsplit(main_dock_split);
	p_dock_manager->set_bottom_hsplit(bottom_hsplit);
	p_dock_manager->set_right_tab_strip(right_dock_tab_strip);

	for (DockTabContainer *dock_container : dock_slots) {
		p_dock_manager->register_dock_slot(dock_container);
	}
}

void FPSStudioWorkspace::build_central_editor_area() {
	top_split = memnew(VSplitContainer);
	top_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	top_split->set_collapsed(true);
	center_split->add_child(top_split);

	VBoxContainer *screen_container = memnew(VBoxContainer);
	screen_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	screen_container->add_theme_constant_override("separation", 0);
	top_split->add_child(screen_container);

	scene_tabs = memnew(EditorSceneTabs);
	screen_container->add_child(scene_tabs);
	scene_tabs->connect("tab_changed", callable_mp(this, &FPSStudioWorkspace::_on_scene_tab_changed));
	scene_tabs->connect("tab_closed", callable_mp(this, &FPSStudioWorkspace::_on_scene_tab_closed));

	editor_main_screen = memnew(EditorMainScreen);
	editor_main_screen->set_custom_minimum_size(Size2(0, 80) * EDSCALE);
	editor_main_screen->set_draw_behind_parent(true);
	screen_container->add_child(editor_main_screen);
	editor_main_screen->set_v_size_flags(Control::SIZE_EXPAND_FILL);
}

EditorBottomPanel *FPSStudioWorkspace::create_bottom_panel(EditorDockManager *p_dock_manager) {
	bottom_panel = memnew(EditorBottomPanel);
	p_dock_manager->register_dock_slot(bottom_panel);
	center_split->connect("drag_ended", callable_mp(this, &FPSStudioWorkspace::_on_center_split_drag_ended));
	center_split->add_child(bottom_panel);
	center_split->set_dragger_visibility(SplitContainer::DRAGGER_HIDDEN);
	return bottom_panel;
}

void FPSStudioWorkspace::_on_scene_tab_changed(int p_tab_index) {
	emit_signal("scene_tab_changed", p_tab_index);
}

void FPSStudioWorkspace::_on_scene_tab_closed(int p_tab_index) {
	emit_signal("scene_tab_closed", p_tab_index);
}

void FPSStudioWorkspace::_on_center_split_drag_ended() {
	if (bottom_panel && center_split) {
		bottom_panel->set_bottom_panel_offset(center_split->get_split_offset());
	}
}

HistoryDock *FPSStudioWorkspace::create_builtin_docks(
		EditorDockManager *p_dock_manager, Node *p_scene_root, EditorSelection *p_editor_selection,
		EditorData &p_editor_data, Ref<ConfigFile> &p_default_layout, const String &p_editor_section,
		const Callable &p_inherit_request, const Callable &p_instantiate_request,
		const Callable &p_save_layout, const Callable &p_connect_filesystem_signals) {
	memnew(SceneTreeDock(p_scene_root, p_editor_selection, p_editor_data));
	p_dock_manager->add_dock(SceneTreeDock::get_singleton());

	memnew(ImportDock);
	p_dock_manager->add_dock(ImportDock::get_singleton());

	create_bottom_panel(p_dock_manager);

	FileSystemDock *filesystem_dock = memnew(FileSystemDock);
	filesystem_dock->set_title(TTRC("Content Browser"));
	filesystem_dock->set_layout_key("fps_content_browser");
	filesystem_dock->set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	filesystem_dock->connect("inherit", p_inherit_request);
	filesystem_dock->connect("instantiate", p_instantiate_request);
	filesystem_dock->connect("display_mode_changed", p_save_layout);
	p_connect_filesystem_signals.call(filesystem_dock);
	p_dock_manager->add_dock(filesystem_dock);

	memnew(InspectorDock(p_editor_data));
	p_dock_manager->add_dock(InspectorDock::get_singleton());

	memnew(SignalsDock);
	p_dock_manager->add_dock(SignalsDock::get_singleton());

	memnew(GroupsDock);
	p_dock_manager->add_dock(GroupsDock::get_singleton());

	HistoryDock *history_dock = memnew(HistoryDock);
	p_dock_manager->add_dock(history_dock);

	p_dock_manager->set_dock_in_right_tab_bar(SceneTreeDock::get_singleton(), true);
	p_dock_manager->set_dock_in_right_tab_bar(ImportDock::get_singleton(), true);
	p_dock_manager->set_dock_in_right_tab_bar(InspectorDock::get_singleton(), true);
	p_dock_manager->set_dock_in_right_tab_bar(SignalsDock::get_singleton(), true);
	p_dock_manager->set_dock_in_right_tab_bar(GroupsDock::get_singleton(), true);
	p_dock_manager->set_dock_in_right_tab_bar(history_dock, true);

	const int dock_hsize = 360;
	const String docks_section = "docks";
	p_default_layout.instantiate();
	{
		const String scene_key = SceneTreeDock::get_singleton()->get_effective_layout_key();
		const String import_key = ImportDock::get_singleton()->get_effective_layout_key();
		p_default_layout->set_value(docks_section, "dock_5", vformat("%s,%s", scene_key, import_key));
		p_default_layout->set_value(docks_section, "dock_5_selected_tab_idx", 0);
	}
	{
		const String history_key = history_dock->get_effective_layout_key();
		const String inspector_key = InspectorDock::get_singleton()->get_effective_layout_key();
		const String signals_key = SignalsDock::get_singleton()->get_effective_layout_key();
		const String groups_key = GroupsDock::get_singleton()->get_effective_layout_key();
		p_default_layout->set_value(docks_section, "dock_6", vformat("%s,%s,%s,%s", inspector_key, signals_key, groups_key, history_key));
		p_default_layout->set_value(docks_section, "dock_6_selected_tab_idx", 0);
	}
	{
		PackedStringArray right_tabs;
		right_tabs.push_back(SceneTreeDock::get_singleton()->get_effective_layout_key());
		right_tabs.push_back(ImportDock::get_singleton()->get_effective_layout_key());
		right_tabs.push_back(InspectorDock::get_singleton()->get_effective_layout_key());
		right_tabs.push_back(SignalsDock::get_singleton()->get_effective_layout_key());
		right_tabs.push_back(GroupsDock::get_singleton()->get_effective_layout_key());
		right_tabs.push_back(history_dock->get_effective_layout_key());
		p_default_layout->set_value(docks_section, "right_dock_tabs", right_tabs);
	}
	{
		const String filesystem_key = filesystem_dock->get_effective_layout_key();
		PackedStringArray bottom_docks;
		for (int i = 0; i < bottom_panel->get_tab_count(); i++) {
			EditorDock *dock = Object::cast_to<EditorDock>(bottom_panel->get_tab_control(i));
			bottom_docks.append(dock->get_effective_layout_key());
		}
		p_default_layout->set_value(docks_section, "dock_9", String(",").join(bottom_docks));
		p_default_layout->set_value(docks_section, "dock_9_selected_tab_idx", bottom_docks.find(filesystem_key));
	}

	int hsplits[] = { 0, dock_hsize, -dock_hsize, 0 };
	for (int i = 0; i < (int)std_size(hsplits); i++) {
		p_default_layout->set_value(docks_section, "dock_hsplit_" + itos(i + 1), hsplits[i]);
	}
	for (int i = 0; i < p_dock_manager->get_vsplit_count(); i++) {
		p_default_layout->set_value(docks_section, "dock_split_" + itos(i + 1), 0);
	}

	Dictionary offsets;
	offsets["Audio"] = -450;
	offsets["Output"] = -270;
	p_default_layout->set_value(p_editor_section, "bottom_panel_offsets", offsets);

	return history_dock;
}

void FPSStudioWorkspace::save_layout_to_config(
		Ref<ConfigFile> p_config_file, const String &p_docks_section, const String &p_editor_section) const {
	p_config_file->set_value("fps_workspace", "layout_version", FPS_STUDIO_LAYOUT_VERSION);
	if (dock_manager) {
		dock_manager->save_docks_to_config(p_config_file, p_docks_section);
	}
	if (bottom_panel) {
		bottom_panel->save_layout_to_config(p_config_file, p_editor_section);
	}
	if (editor_main_screen) {
		editor_main_screen->save_layout_to_config(p_config_file, p_editor_section);
	}
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (debugger_node && debugger_node->get_default_debugger()) {
		p_config_file->set_value(p_editor_section, "selected_default_debugger_tab_idx",
				debugger_node->get_default_debugger()->get_current_debugger_tab());
	}
}

bool FPSStudioWorkspace::load_project_docks_from_config(
		Ref<ConfigFile> p_config_file, Ref<ConfigFile> p_default_layout, bool p_first_load) {
	const bool needs_migration = p_config_file.is_null() ||
			int(p_config_file->get_value("fps_workspace", "layout_version", 0)) < FPS_STUDIO_LAYOUT_VERSION;
	load_docks_from_config(needs_migration ? p_default_layout : p_config_file, "docks", p_first_load);
	return needs_migration;
}

void FPSStudioWorkspace::load_project_central_layout(
		Ref<ConfigFile> p_config_file, Ref<ConfigFile> p_default_layout,
		const String &p_editor_section, bool p_migrate_legacy_layout) {
	if (p_config_file.is_null()) {
		return;
	}

	load_bottom_panel_layout_from_config(p_config_file, p_editor_section);
	if (p_config_file->has_section_key(p_editor_section, "selected_default_debugger_tab_idx")) {
		EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
		if (debugger_node && debugger_node->get_default_debugger()) {
			int selected_default_debugger_tab_idx = p_config_file->get_value(p_editor_section, "selected_default_debugger_tab_idx");
			debugger_node->get_default_debugger()->switch_to_debugger(selected_default_debugger_tab_idx);
		}
	}
	load_main_screen_layout_from_config(p_config_file, p_editor_section);

	if (p_migrate_legacy_layout) {
		load_bottom_panel_layout_from_config(p_default_layout, p_editor_section);
		if (editor_main_screen) {
			editor_main_screen->select(EditorMainScreen::EDITOR_3D);
		}
	}
}

void FPSStudioWorkspace::load_docks_from_config(Ref<ConfigFile> p_config_file, const String &p_section, bool p_first_load) {
	if (dock_manager) {
		dock_manager->load_docks_from_config(p_config_file, p_section, p_first_load);
	}
}

void FPSStudioWorkspace::load_bottom_panel_layout_from_config(Ref<ConfigFile> p_config_file, const String &p_section) {
	if (bottom_panel) {
		bottom_panel->load_layout_from_config(p_config_file, p_section);
	}
}

void FPSStudioWorkspace::load_main_screen_layout_from_config(Ref<ConfigFile> p_config_file, const String &p_section) {
	if (editor_main_screen) {
		editor_main_screen->load_layout_from_config(p_config_file, p_section);
	}
}

Vector<String> FPSStudioWorkspace::get_saved_layout_names(bool &r_default_overridden) const {
	r_default_overridden = false;
	Vector<String> layout_names;

	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(EditorSettings::get_singleton()->get_editor_layouts_config()) != OK) {
		return layout_names;
	}

	r_default_overridden = config->has_section("Default");
	for (const String &layout_name : config->get_sections()) {
		if (layout_name != "Default" && !layout_name.contains_char('/')) {
			layout_names.push_back(layout_name);
		}
	}
	return layout_names;
}

Error FPSStudioWorkspace::save_layout_preset(const String &p_name) {
	if (!dock_manager) {
		return FAILED;
	}

	const String layouts_path = EditorSettings::get_singleton()->get_editor_layouts_config();
	Ref<ConfigFile> config;
	config.instantiate();
	const Error load_error = config->load(layouts_path);
	if (load_error != OK && load_error != ERR_FILE_CANT_OPEN && load_error != ERR_FILE_NOT_FOUND) {
		return load_error;
	}

	dock_manager->save_docks_to_config(config, p_name);
	return config->save(layouts_path);
}

Error FPSStudioWorkspace::delete_layout_preset(const String &p_name) {
	Ref<ConfigFile> config;
	config.instantiate();
	const Error load_error = config->load(EditorSettings::get_singleton()->get_editor_layouts_config());
	if (load_error != OK || !config->has_section(p_name)) {
		return ERR_DOES_NOT_EXIST;
	}

	for (const String &section : config->get_sections()) {
		if (section == p_name || section.begins_with(p_name + "/")) {
			config->erase_section(section);
		}
	}
	return config->save(EditorSettings::get_singleton()->get_editor_layouts_config());
}

Error FPSStudioWorkspace::apply_layout_preset(const String &p_name, Ref<ConfigFile> p_default_layout) {
	if (!dock_manager) {
		return FAILED;
	}

	Ref<ConfigFile> config;
	config.instantiate();
	const Error load_error = config->load(EditorSettings::get_singleton()->get_editor_layouts_config());
	if (load_error == OK && config->has_section(p_name)) {
		dock_manager->load_docks_from_config(config, p_name);
		return OK;
	}

	if (p_name == "Default") {
		dock_manager->load_docks_from_config(p_default_layout, "docks");
		return OK;
	}

	return load_error == OK ? ERR_DOES_NOT_EXIST : load_error;
}

FPSStudioWorkspace::FPSStudioWorkspace() {
	set_name("FPSStudioWorkspace");
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_theme_constant_override("separation", 0);

	main_vsplit = memnew(DockSplitContainer);
	main_vsplit->set_name("DockVSplitMain");
	main_vsplit->set_vertical(true);
	main_vsplit->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(main_vsplit);

	HBoxContainer *main_row = memnew(HBoxContainer);
	main_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_row->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_vsplit->add_child(main_row);

	main_dock_split = memnew(DockSplitContainer);
	main_dock_split->set_name("DockHSplitMain");
	main_dock_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_dock_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_row->add_child(main_dock_split);

	left_l_vsplit = memnew(DockSplitContainer);
	left_l_vsplit->set_name("DockVSplitLeftL");
	left_l_vsplit->set_vertical(true);
	main_dock_split->add_child(left_l_vsplit);
	{
		DockTabContainer *dock_container = memnew(SideDockTabContainer(EditorDock::DOCK_SLOT_LEFT_UL, Rect2i(0, 0, 1, 3)));
		dock_container->set_name("DockSlotLeftUL");
		left_l_vsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}
	{
		DockTabContainer *dock_container = memnew(SideDockTabContainer(EditorDock::DOCK_SLOT_LEFT_BL, Rect2i(0, 3, 1, 3)));
		dock_container->set_name("DockSlotLeftBL");
		left_l_vsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}

	left_r_vsplit = memnew(DockSplitContainer);
	left_r_vsplit->set_name("DockVSplitLeftR");
	left_r_vsplit->set_vertical(true);
	main_dock_split->add_child(left_r_vsplit);
	{
		DockTabContainer *dock_container = memnew(SideDockTabContainer(EditorDock::DOCK_SLOT_LEFT_UR, Rect2i(1, 0, 1, 3)));
		dock_container->set_name("DockSlotLeftUR");
		left_r_vsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}
	{
		DockTabContainer *dock_container = memnew(SideDockTabContainer(EditorDock::DOCK_SLOT_LEFT_BR, Rect2i(1, 3, 1, 3)));
		dock_container->set_name("DockSlotLeftBR");
		left_r_vsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}

	VBoxContainer *center_vb = memnew(VBoxContainer);
	center_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_dock_split->add_child(center_vb);

	center_split = memnew(DockSplitContainer);
	center_split->set_name("DockVSplitCenter");
	center_split->set_vertical(true);
	center_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	center_split->set_collapsed(true);
	center_vb->add_child(center_split);

	right_l_vsplit = memnew(DockSplitContainer);
	right_l_vsplit->set_name("DockVSplitRightL");
	right_l_vsplit->set_vertical(true);
	main_dock_split->add_child(right_l_vsplit);
	{
		DockTabContainer *dock_container = memnew(SideDockTabContainer(EditorDock::DOCK_SLOT_RIGHT_UL, Rect2i(6, 0, 1, 3)));
		dock_container->set_name("DockSlotRightUL");
		right_l_vsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}
	{
		DockTabContainer *dock_container = memnew(SideDockTabContainer(EditorDock::DOCK_SLOT_RIGHT_BL, Rect2i(6, 3, 1, 3)));
		dock_container->set_name("DockSlotRightBL");
		right_l_vsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}

	right_r_vsplit = memnew(DockSplitContainer);
	right_r_vsplit->set_name("DockVSplitRightR");
	right_r_vsplit->set_vertical(true);
	main_dock_split->add_child(right_r_vsplit);
	{
		DockTabContainer *dock_container = memnew(SideDockTabContainer(EditorDock::DOCK_SLOT_RIGHT_UR, Rect2i(7, 0, 1, 3)));
		dock_container->set_name("DockSlotRightUR");
		right_r_vsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}
	{
		DockTabContainer *dock_container = memnew(SideDockTabContainer(EditorDock::DOCK_SLOT_RIGHT_BR, Rect2i(7, 3, 1, 3)));
		dock_container->set_name("DockSlotRightBR");
		right_r_vsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}

	right_dock_tab_strip = memnew(VBoxContainer);
	right_dock_tab_strip->set_name("RightDockTabs");
	right_dock_tab_strip->set_custom_minimum_size(Size2(40, 0) * EDSCALE);
	right_dock_tab_strip->add_theme_constant_override("separation", 2 * EDSCALE);
	main_row->add_child(right_dock_tab_strip);

	bottom_hsplit = memnew(DockSplitContainer);
	bottom_hsplit->set_name("DockHSplitBottom");
	main_vsplit->add_child(bottom_hsplit);
	{
		DockTabContainer *dock_container = memnew(BottomSideDockTabContainer(EditorDock::DOCK_SLOT_BOTTOM_L, Rect2i(0, 6, 4, 2)));
		dock_container->set_name("DockSlotBottomL");
		bottom_hsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}
	{
		DockTabContainer *dock_container = memnew(BottomSideDockTabContainer(EditorDock::DOCK_SLOT_BOTTOM_R, Rect2i(4, 6, 4, 2)));
		dock_container->set_name("DockSlotBottomR");
		bottom_hsplit->add_child(dock_container);
		dock_slots.push_back(dock_container);
	}

	const int dock_hsize_scaled = 360 * EDSCALE;
	main_dock_split->set_split_offsets({ dock_hsize_scaled, -dock_hsize_scaled });
}

