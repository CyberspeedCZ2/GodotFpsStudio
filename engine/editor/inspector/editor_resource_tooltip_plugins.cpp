/**************************************************************************/
/*  editor_resource_tooltip_plugins.cpp                                   */
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

#include "editor_resource_tooltip_plugins.h"

#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/inspector/editor_resource_preview.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/texture_rect.h"

static String _get_texture_preview_info(const Dictionary &p_metadata) {
	const Vector2 dimensions = p_metadata.get("dimensions", Vector2());
	if (dimensions.x <= 0.0 || dimensions.y <= 0.0) {
		return String();
	}

	String info = vformat(TTR(U"%d × %d"), int(dimensions.x), int(dimensions.y));
	const String format = p_metadata.get("format", String());
	if (!format.is_empty()) {
		info += vformat(TTR(U"  ·  %s"), format);
	}
	return info;
}

void EditorResourceTooltipPlugin::_thumbnail_ready(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small_preview, ObjectID p_trect_id, ObjectID p_metadata_label_id) {
	TextureRect *tr = ObjectDB::get_instance<TextureRect>(p_trect_id);
	if (tr) {
		tr->set_texture(p_preview);
	}
	if (p_metadata_label_id.is_valid()) {
		// The preview request may call this synchronously while holding the cache mutex.
		// Defer metadata lookup so it runs after that lock has been released.
		callable_mp(this, &EditorResourceTooltipPlugin::_update_texture_info_label).call_deferred(p_path, p_metadata_label_id);
	}
}

void EditorResourceTooltipPlugin::_update_texture_info_label(const String &p_path, ObjectID p_metadata_label_id) const {
	Label *metadata_label = ObjectDB::get_instance<Label>(p_metadata_label_id);
	if (!metadata_label) {
		return;
	}

	EditorResourcePreview *resource_preview = EditorResourcePreview::get_singleton();
	if (!resource_preview) {
		return;
	}
	const Dictionary metadata = resource_preview->get_preview_metadata(p_path);
	const String info = _get_texture_preview_info(metadata);
	if (!info.is_empty()) {
		metadata_label->set_text(info);
	}
}

void EditorResourceTooltipPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("request_thumbnail", "path", "control"), &EditorResourceTooltipPlugin::request_thumbnail);

	GDVIRTUAL_BIND(_handles, "type");
	GDVIRTUAL_BIND(_make_tooltip_for_path, "path", "metadata", "base");
}

VBoxContainer *EditorResourceTooltipPlugin::make_default_tooltip(const String &p_resource_path) {
	VBoxContainer *vb = memnew(VBoxContainer);
	vb->add_theme_constant_override("separation", -4 * EDSCALE);
	{
		Label *label = memnew(Label(p_resource_path.get_file()));
		vb->add_child(label);
	}

	ResourceUID::ID id = EditorFileSystem::get_singleton()->get_file_uid(p_resource_path);
	if (id != ResourceUID::INVALID_ID) {
		Label *label = memnew(Label(ResourceUID::get_singleton()->id_to_text(id)));
		vb->add_child(label);
	}

	{
		Ref<FileAccess> f = FileAccess::open(p_resource_path, FileAccess::READ);
		if (f.is_valid()) {
			Label *label = memnew(Label(vformat(TTR("Size: %s"), String::humanize_size(f->get_length()))));
			vb->add_child(label);
		} else {
			Label *label = memnew(Label(TTR("Invalid file or broken link.")));
			label->add_theme_color_override(SceneStringName(font_color), EditorNode::get_singleton()->get_gui_base()->get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			vb->add_child(label);
			return vb;
		}
	}

	if (ResourceLoader::exists(p_resource_path)) {
		String type = ResourceLoader::get_resource_type(p_resource_path);
		Label *label = memnew(Label(vformat(TTR("Type: %s"), type)));
		vb->add_child(label);
	}

	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da->is_link(p_resource_path)) {
		Label *link = memnew(Label(vformat(TTR("Link to: %s"), da->read_link(p_resource_path))));
		vb->add_child(link);
	}
	return vb;
}

void EditorResourceTooltipPlugin::_request_thumbnail(const String &p_path, TextureRect *p_for_control, Label *p_metadata_label, bool p_large_preview) const {
	ERR_FAIL_NULL(p_for_control);
	const ObjectID metadata_label_id = p_metadata_label ? p_metadata_label->get_instance_id() : ObjectID();
	EditorResourcePreview *resource_preview = EditorResourcePreview::get_singleton();
	const Callable callback = callable_mp(const_cast<EditorResourceTooltipPlugin *>(this), &EditorResourceTooltipPlugin::_thumbnail_ready).bind(p_for_control->get_instance_id(), metadata_label_id);
	if (p_large_preview) {
		resource_preview->queue_resource_preview_for_tooltip(p_path, callback);
	} else {
		resource_preview->queue_resource_preview(p_path, callback);
	}
}

void EditorResourceTooltipPlugin::request_thumbnail(const String &p_path, TextureRect *p_for_control) const {
	_request_thumbnail(p_path, p_for_control, nullptr, false);
}

bool EditorResourceTooltipPlugin::handles(const String &p_resource_type) const {
	bool ret = false;
	GDVIRTUAL_CALL(_handles, p_resource_type, ret);
	return ret;
}

Control *EditorResourceTooltipPlugin::make_tooltip_for_path(const String &p_resource_path, const Dictionary &p_metadata, Control *p_base) const {
	Control *ret = nullptr;
	GDVIRTUAL_CALL(_make_tooltip_for_path, p_resource_path, p_metadata, p_base, ret);
	return ret;
}

// EditorTextureTooltipPlugin

bool EditorTextureTooltipPlugin::handles(const String &p_resource_type) const {
	return ClassDB::is_parent_class(p_resource_type, "Texture2D") || ClassDB::is_parent_class(p_resource_type, "Image");
}

Control *EditorTextureTooltipPlugin::make_tooltip_for_path(const String &p_resource_path, const Dictionary &p_metadata, Control *p_base) const {
	VBoxContainer *vb = Object::cast_to<VBoxContainer>(p_base);
	DEV_ASSERT(vb);
	while (vb->get_child_count() > 0) {
		Node *child = vb->get_child(0);
		vb->remove_child(child);
		memdelete(child);
	}
	vb->add_theme_constant_override("separation", 4 * EDSCALE);
	vb->set_alignment(BoxContainer::ALIGNMENT_CENTER);

	TextureRect *tr = memnew(TextureRect);
	tr->set_custom_minimum_size(Size2(EditorResourcePreview::TOOLTIP_PREVIEW_SIZE, EditorResourcePreview::TOOLTIP_PREVIEW_SIZE * 5 / 6) * EDSCALE);
	tr->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tr->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	tr->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	vb->add_child(tr);

	Label *info_label = memnew(Label);
	info_label->set_text(_get_texture_preview_info(p_metadata));
	info_label->set_custom_minimum_size(Size2(0, 18) * EDSCALE);
	info_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	vb->add_child(info_label);

	_request_thumbnail(p_resource_path, tr, info_label, true);
	return vb;
}

// EditorAudioStreamTooltipPlugin

bool EditorAudioStreamTooltipPlugin::handles(const String &p_resource_type) const {
	return ClassDB::is_parent_class(p_resource_type, "AudioStream");
}

Control *EditorAudioStreamTooltipPlugin::make_tooltip_for_path(const String &p_resource_path, const Dictionary &p_metadata, Control *p_base) const {
	VBoxContainer *vb = Object::cast_to<VBoxContainer>(p_base);
	DEV_ASSERT(vb);

	double length = p_metadata.get("length", 0.0);
	if (length >= 60.0) {
		vb->add_child(memnew(Label(vformat(TTR("Length: %0dm %0ds"), int(length / 60.0), int(std::fmod(length, 60))))));
	} else if (length >= 1.0) {
		vb->add_child(memnew(Label(vformat(TTR("Length: %0.1fs"), length))));
	} else {
		vb->add_child(memnew(Label(vformat(TTR("Length: %0.3fs"), length))));
	}

	TextureRect *tr = memnew(TextureRect);
	vb->add_child(tr);
	request_thumbnail(p_resource_path, tr);

	return vb;
}
