/**************************************************************************/
/*  fps_studio_workspace.h                                                */
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

#pragma once

#include "core/templates/vector.h"
#include "core/variant/callable.h"
#include "editor/docks/dock_tab_container.h"
#include "editor/docks/editor_dock_manager.h"
#include "scene/gui/box_container.h"

class EditorMainScreen;
class EditorBottomPanel;
class ConfigFile;
class EditorData;
class EditorSelection;
class EditorSceneTabs;
class HistoryDock;
class Node;
class VSplitContainer;

class FPSStudioWorkspace : public VBoxContainer {
	GDCLASS(FPSStudioWorkspace, VBoxContainer);

private:
	DockSplitContainer *main_vsplit = nullptr;
	DockSplitContainer *bottom_hsplit = nullptr;
	DockSplitContainer *main_dock_split = nullptr;
	DockSplitContainer *left_l_vsplit = nullptr;
	DockSplitContainer *left_r_vsplit = nullptr;
	DockSplitContainer *right_l_vsplit = nullptr;
	DockSplitContainer *right_r_vsplit = nullptr;
	DockSplitContainer *center_split = nullptr;
	EditorDockManager *dock_manager = nullptr;
	VSplitContainer *top_split = nullptr;
	EditorSceneTabs *scene_tabs = nullptr;
	EditorMainScreen *editor_main_screen = nullptr;
	EditorBottomPanel *bottom_panel = nullptr;
	Vector<DockTabContainer *> dock_slots;
	VBoxContainer *right_dock_tab_strip = nullptr;

	void _on_scene_tab_changed(int p_tab_index);
	void _on_scene_tab_closed(int p_tab_index);
	void _on_center_split_drag_ended();

protected:
	static void _bind_methods();

public:
	void configure_dock_manager(EditorDockManager *p_dock_manager);
	void build_central_editor_area();
	EditorBottomPanel *create_bottom_panel(EditorDockManager *p_dock_manager);
	HistoryDock *create_builtin_docks(EditorDockManager *p_dock_manager, Node *p_scene_root, EditorSelection *p_editor_selection,
			EditorData &p_editor_data, Ref<ConfigFile> &p_default_layout, const String &p_editor_section,
			const Callable &p_inherit_request, const Callable &p_instantiate_request,
			const Callable &p_save_layout, const Callable &p_connect_filesystem_signals);
	void save_layout_to_config(Ref<ConfigFile> p_config_file, const String &p_docks_section, const String &p_editor_section) const;
	bool load_project_docks_from_config(
			Ref<ConfigFile> p_config_file, Ref<ConfigFile> p_default_layout, bool p_first_load = false);
	void load_project_central_layout(
			Ref<ConfigFile> p_config_file, Ref<ConfigFile> p_default_layout, const String &p_editor_section, bool p_migrate_legacy_layout);
	void load_docks_from_config(Ref<ConfigFile> p_config_file, const String &p_section, bool p_first_load = false);
	void load_bottom_panel_layout_from_config(Ref<ConfigFile> p_config_file, const String &p_section);
	Vector<String> get_saved_layout_names(bool &r_default_overridden) const;
	Error save_layout_preset(const String &p_name);
	Error delete_layout_preset(const String &p_name);
	Error apply_layout_preset(const String &p_name, Ref<ConfigFile> p_default_layout);

	DockSplitContainer *get_center_split() const { return center_split; }
	VSplitContainer *get_top_split() const { return top_split; }
	EditorSceneTabs *get_scene_tabs() const { return scene_tabs; }
	EditorMainScreen *get_editor_main_screen() const { return editor_main_screen; }
	EditorBottomPanel *get_bottom_panel() const { return bottom_panel; }

	FPSStudioWorkspace();
};
