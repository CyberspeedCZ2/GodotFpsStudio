/**************************************************************************/
/*  model_editor_plugin.cpp                                               */
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
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,          */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                  */
/**************************************************************************/

#include "model_editor_plugin.h"

#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/import/3d/scene_import_settings.h"
#include "scene/gui/control.h"

const Ref<Texture2D> ModelEditorPlugin::get_plugin_icon() const {
	return EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("MeshInstance3D"), EditorStringName(EditorIcons));
}

void ModelEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		model_screen->show();
	} else {
		model_screen->hide();
	}
}

ModelEditorPlugin::ModelEditorPlugin() {
	model_screen = memnew(SceneImportSettingsScreen);
	model_screen->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(model_screen);
	model_screen->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	model_screen->hide();
}
