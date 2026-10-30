/**************************************************************************/
/*  model_import_wizard.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* copyright notice and this permission notice shall be included in all    */
/* copies or substantial portions of the Software.                        */
/**************************************************************************/

#include "model_import_wizard.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "editor/editor_string_names.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/import/3d/resource_importer_scene.h"
#include "modules/fbx/editor/editor_scene_importer_ufbx.h"
#include "modules/gltf/gltf_document.h"
#include "modules/gltf/extensions/gltf_document_extension_convert_importer_mesh.h"
#include "modules/gltf/gltf_state.h"
#include "modules/gltf/structures/gltf_buffer_view.h"
#include "modules/gltf/structures/gltf_mesh.h"
#include "modules/gltf/structures/gltf_skin.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/image_texture.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/main/timer.h"

#include <algorithm>

static const int MODEL_TEXTURE_LIMITS[] = { 4096, 2048, 1024, 512, 256 };

static String _model_source_absolute_path(const String &p_path) {
	return p_path.begins_with("res://") ? ProjectSettings::get_singleton()->globalize_path(p_path) : p_path;
}

static int _gltf_state_bone_count(const Ref<GLTFState> &p_state) {
	int count = 0;
	for (const Ref<GLTFSkin> &skin : p_state->get_skins()) {
		if (skin.is_valid()) {
			count += skin->get_joints().size();
		}
	}
	return count;
}

static Node *_convert_importer_mesh_instances(Node *p_root) {
	if (p_root == nullptr) {
		return nullptr;
	}
	Vector<ImporterMeshInstance3D *> converted_nodes;
	Node *root = p_root;
	if (ImporterMeshInstance3D *importer_mesh = Object::cast_to<ImporterMeshInstance3D>(root)) {
		root = GLTFDocumentExtensionConvertImporterMesh::convert_importer_mesh_instance_3d(importer_mesh);
		converted_nodes.push_back(importer_mesh);
	}
	Vector<Node *> pending_nodes = { root };
	for (int node_index = 0; node_index < pending_nodes.size(); node_index++) {
		Node *node = pending_nodes[node_index];
		if (ImporterMeshInstance3D *importer_mesh = Object::cast_to<ImporterMeshInstance3D>(node)) {
			node = GLTFDocumentExtensionConvertImporterMesh::convert_importer_mesh_instance_3d(importer_mesh);
			converted_nodes.push_back(importer_mesh);
		}
		for (int child_index = 0; child_index < node->get_child_count(); child_index++) {
			pending_nodes.push_back(node->get_child(child_index));
		}
	}
	for (ImporterMeshInstance3D *converted_node : converted_nodes) {
		memdelete(converted_node);
	}
	return root;
}

static bool _gltf_state_has_renderable_mesh(const Ref<GLTFState> &p_state) {
	for (const Ref<GLTFMesh> &mesh : p_state->get_meshes()) {
		if (mesh.is_valid()) {
			Ref<ImporterMesh> import_mesh = mesh->get_mesh();
			if (import_mesh.is_valid() && import_mesh->get_surface_count() > 0) {
				return true;
			}
		}
	}
	return false;
}

static void _remove_directory_recursive(const String &p_path) {
	{
		Ref<DirAccess> directory = DirAccess::open(p_path);
		if (directory.is_valid()) {
			directory->list_dir_begin();
			String entry = directory->get_next();
			while (!entry.is_empty()) {
				if (entry != "." && entry != "..") {
					const String child_path = p_path.path_join(entry);
					if (directory->current_is_dir()) {
						_remove_directory_recursive(child_path);
					} else {
						directory->remove(entry);
					}
				}
				entry = directory->get_next();
			}
			directory->list_dir_end();
		}
	}
	DirAccess::remove_absolute(p_path);
}

static bool _resize_texture_resource(const Ref<Texture2D> &p_texture, int p_max_dimension, Ref<Texture2D> &r_texture, String &r_error) {
	if (p_texture.is_null()) {
		r_texture = p_texture;
		return true;
	}
	Ref<Image> image = p_texture->get_image();
	if (image.is_null() || image->is_empty()) {
		r_error = vformat("Texture '%s' could not be decoded.", p_texture->get_name());
		return false;
	}
	if (image->is_compressed()) {
		image = image->duplicate();
		image->decompress();
		if (image->is_compressed()) {
			r_error = vformat("Texture '%s' could not be decompressed for resizing.", p_texture->get_name());
			return false;
		}
	}
	const int width = image->get_width();
	const int height = image->get_height();
	const int longest_side = MAX(width, height);
	if (longest_side <= p_max_dimension) {
		r_texture = p_texture;
		return true;
	}
	const double scale = double(p_max_dimension) / double(longest_side);
	const int resized_width = MAX(1, int(Math::round(width * scale)));
	const int resized_height = MAX(1, int(Math::round(height * scale)));
	image = image->duplicate();
	image->resize(resized_width, resized_height, Image::INTERPOLATE_LANCZOS);
	Ref<ImageTexture> resized_texture = ImageTexture::create_from_image(image);
	if (resized_texture.is_null()) {
		r_error = vformat("Texture '%s' could not be resized.", p_texture->get_name());
		return false;
	}
	resized_texture->set_name(p_texture->get_name());
	r_texture = resized_texture;
	return true;
}

static bool _resize_gltf_images(Ref<GLTFState> p_state, int p_max_dimension, String &r_error) {
	HashMap<Object *, Ref<Texture2D>> resized_textures;
	Vector<Ref<Texture2D>> images = p_state->get_images();
	for (int i = 0; i < images.size(); i++) {
		if (images[i].is_null()) {
			continue;
		}
		Object *source_texture = images[i].ptr();
		if (resized_textures.has(source_texture)) {
			images.write[i] = resized_textures[source_texture];
			continue;
		}
		Ref<Texture2D> resized_texture;
		if (!_resize_texture_resource(images[i], p_max_dimension, resized_texture, r_error)) {
			return false;
		}
		resized_textures[source_texture] = resized_texture;
		images.write[i] = resized_texture;
	}
	p_state->set_images(images);

	// Parsed GLTF materials keep references to the original textures. Update those
	// properties too, or export would append unresized 4K copies beside the resized images.
	for (const Ref<Material> &material : p_state->get_materials()) {
		if (material.is_null()) {
			continue;
		}
		List<PropertyInfo> properties;
		material->get_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (property.type != Variant::OBJECT) {
				continue;
			}
			const Variant value = material->get(property.name);
			if (value.get_type() != Variant::OBJECT) {
				continue;
			}
			Object *object = value;
			Texture2D *source_texture = Object::cast_to<Texture2D>(object);
			if (source_texture == nullptr) {
				continue;
			}
			Ref<Texture2D> resized_texture;
			if (resized_textures.has(source_texture)) {
				resized_texture = resized_textures[source_texture];
			} else {
				Ref<Texture2D> original_texture = source_texture;
				if (!_resize_texture_resource(original_texture, p_max_dimension, resized_texture, r_error)) {
					return false;
				}
				resized_textures[source_texture] = resized_texture;
			}
			if (resized_texture.ptr() != source_texture) {
				material->set(property.name, resized_texture);
			}
		}
	}
	return true;
}

struct ImageBufferSpan {
	int buffer_index = 0;
	int64_t start = 0;
	int64_t end = 0;
	Vector<int> view_indices;
};

static void _collect_gltf_buffer_view_references(const Variant &p_value, HashSet<int> &r_references) {
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		for (const Variant *key = dictionary.next(nullptr); key != nullptr; key = dictionary.next(key)) {
			const String key_name = String(*key);
			const Variant value = dictionary[*key];
			if (key_name == "bufferView" && value.get_type() == Variant::INT) {
				r_references.insert(int(value));
			} else {
				_collect_gltf_buffer_view_references(value, r_references);
			}
		}
	} else if (p_value.get_type() == Variant::ARRAY) {
		const Array array = p_value;
		for (int i = 0; i < array.size(); i++) {
			_collect_gltf_buffer_view_references(array[i], r_references);
		}
	}
}

static bool _compact_embedded_image_data(Ref<GLTFState> p_state, String &r_error) {
	Dictionary source_json = p_state->get_json();
	Array source_images = source_json.get("images", Array());
	Vector<Ref<GLTFBufferView>> buffer_views = p_state->get_buffer_views();
	HashSet<int> image_view_indices;
	Vector<ImageBufferSpan> spans;
	for (int i = 0; i < source_images.size(); i++) {
		const Dictionary image = source_images[i];
		if (!image.has("bufferView")) {
			continue;
		}
		const int view_index = int(image["bufferView"]);
		if (view_index < 0 || view_index >= buffer_views.size() || buffer_views[view_index].is_null()) {
			r_error = "The source model contains an invalid embedded image buffer view.";
			return false;
		}
		if (image_view_indices.has(view_index)) {
			continue;
		}
		image_view_indices.insert(view_index);
		const Ref<GLTFBufferView> view = buffer_views[view_index];
		ImageBufferSpan span;
		span.buffer_index = view->get_buffer();
		span.start = view->get_byte_offset();
		span.end = span.start + view->get_byte_length();
		span.view_indices.push_back(view_index);
		spans.push_back(span);
	}
	if (spans.is_empty()) {
		return true;
	}

	Dictionary references_json = source_json;
	references_json.erase("images");
	HashSet<int> other_buffer_view_references;
	_collect_gltf_buffer_view_references(references_json, other_buffer_view_references);
	for (int view_index : image_view_indices) {
		if (other_buffer_view_references.has(view_index)) {
			r_error = "An embedded image data block is also referenced by model geometry or an extension. The original file was kept.";
			return false;
		}
	}

	if (spans.size() > 1) {
		std::sort(spans.ptrw(), spans.ptrw() + spans.size(), [](const ImageBufferSpan &a, const ImageBufferSpan &b) {
			if (a.buffer_index != b.buffer_index) {
				return a.buffer_index < b.buffer_index;
			}
			return a.start < b.start;
		});
	}
	Vector<ImageBufferSpan> merged_spans;
	for (const ImageBufferSpan &span : spans) {
		if (!merged_spans.is_empty() && merged_spans[merged_spans.size() - 1].buffer_index == span.buffer_index && span.start <= merged_spans[merged_spans.size() - 1].end) {
			ImageBufferSpan &previous = merged_spans.write[merged_spans.size() - 1];
			previous.end = MAX(previous.end, span.end);
			previous.view_indices.append_array(span.view_indices);
		} else {
			merged_spans.push_back(span);
		}
	}

	Vector<PackedByteArray> buffers = p_state->get_buffers();
	for (const ImageBufferSpan &span : merged_spans) {
		if (span.buffer_index < 0 || span.buffer_index >= buffers.size() || span.start < 0 || span.end <= span.start || span.end > buffers[span.buffer_index].size()) {
			r_error = "The source model contains invalid embedded image data. The original file was kept.";
			return false;
		}
	}

	for (int i = 0; i < buffer_views.size(); i++) {
		if (image_view_indices.has(i) || buffer_views[i].is_null()) {
			continue;
		}
		const Ref<GLTFBufferView> view = buffer_views[i];
		const int buffer_index = view->get_buffer();
		const int64_t view_start = view->get_byte_offset();
		const int64_t view_end = view_start + view->get_byte_length();
		int64_t shift = 0;
		for (const ImageBufferSpan &span : merged_spans) {
			if (span.buffer_index != buffer_index) {
				continue;
			}
			if (view_end <= span.start) {
				break;
			}
			if (span.end <= view_start) {
				const int64_t replacement_length = 4 + ((span.end - span.start) % 4);
				shift += (span.end - span.start) - replacement_length;
				continue;
			}
			r_error = "Embedded image data overlaps a geometry buffer. The original file was kept.";
			return false;
		}
		view->set_byte_offset(view_start - shift);
	}

	for (int buffer_index = 0; buffer_index < buffers.size(); buffer_index++) {
		PackedByteArray original_buffer = buffers[buffer_index];
		PackedByteArray compacted_buffer;
		int64_t cursor = 0;
		bool has_image_spans = false;
		for (const ImageBufferSpan &span : merged_spans) {
			if (span.buffer_index != buffer_index) {
				continue;
			}
			has_image_spans = true;
			compacted_buffer.append_array(original_buffer.slice(cursor, span.start));
			const int64_t placeholder_length = 4 + ((span.end - span.start) % 4);
			const int old_size = compacted_buffer.size();
			compacted_buffer.resize(old_size + placeholder_length);
			for (int64_t zero_i = old_size; zero_i < old_size + placeholder_length; zero_i++) {
				compacted_buffer.write[zero_i] = 0;
			}
			for (int view_index : span.view_indices) {
				buffer_views.write[view_index]->set_byte_offset(0);
				buffer_views.write[view_index]->set_byte_length(4);
			}
			cursor = span.end;
		}
		if (has_image_spans) {
			compacted_buffer.append_array(original_buffer.slice(cursor, original_buffer.size()));
			buffers.write[buffer_index] = compacted_buffer;
		}
	}
	p_state->set_buffers(buffers);
	p_state->set_buffer_views(buffer_views);
	return true;
}

static bool _validate_gltf_external_files(const String &p_gltf_path, const String &p_base_path, String &r_error) {
	Ref<FileAccess> file = FileAccess::open(p_gltf_path, FileAccess::READ);
	if (file.is_null()) {
		r_error = "The converted glTF file could not be opened for validation.";
		return false;
	}
	JSON json;
	const Error parse_error = json.parse(file->get_as_utf8_string());
	if (parse_error != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		r_error = "The converted glTF file contains invalid JSON.";
		return false;
	}
	const Dictionary root = json.get_data();
	for (const String &array_name : { String("buffers"), String("images") }) {
		const Array assets = root.get(array_name, Array());
		for (int i = 0; i < assets.size(); i++) {
			const Dictionary asset = assets[i];
			if (!asset.has("uri")) {
				continue;
			}
			String uri = asset["uri"];
			if (uri.begins_with("data:")) {
				continue;
			}
			uri = uri.uri_file_decode();
			const String asset_path = uri.is_absolute_path() ? uri.simplify_path() : p_base_path.path_join(uri).simplify_path();
			if (!FileAccess::exists(asset_path)) {
				r_error = vformat("The converted glTF references a missing %s file: %s", array_name == "images" ? "texture" : "binary model data", asset_path);
				return false;
			}
		}
	}
	return true;
}

static bool _resize_gltf_texture_files(const String &p_gltf_path, const String &p_base_path, int p_max_dimension, String &r_error) {
	Ref<FileAccess> file = FileAccess::open(p_gltf_path, FileAccess::READ);
	if (file.is_null()) {
		r_error = "The converted glTF file could not be opened to resize its textures.";
		return false;
	}
	JSON json;
	if (json.parse(file->get_as_utf8_string()) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		r_error = "The converted glTF file contains invalid JSON.";
		return false;
	}
	const Array images = Dictionary(json.get_data()).get("images", Array());
	HashSet<String> processed_paths;
	for (int i = 0; i < images.size(); i++) {
		const Dictionary image_info = images[i];
		if (!image_info.has("uri")) {
			continue;
		}
		String uri = image_info["uri"];
		if (uri.begins_with("data:")) {
			continue;
		}
		uri = uri.uri_file_decode();
		const String image_path = (uri.is_absolute_path() ? uri : p_base_path.path_join(uri)).simplify_path();
		if (processed_paths.has(image_path)) {
			continue;
		}
		processed_paths.insert(image_path);
		if (!FileAccess::exists(image_path)) {
			r_error = vformat("The converted glTF references a missing texture file: %s", image_path);
			return false;
		}
		Ref<Image> texture_image;
		texture_image.instantiate();
		if (texture_image->load(image_path) != OK || texture_image->is_empty()) {
			r_error = vformat("The converted texture could not be decoded: %s", image_path);
			return false;
		}
		const int longest_side = MAX(texture_image->get_width(), texture_image->get_height());
		if (longest_side <= p_max_dimension) {
			continue;
		}
		const double scale = double(p_max_dimension) / double(longest_side);
		const int width = MAX(1, int(Math::round(texture_image->get_width() * scale)));
		const int height = MAX(1, int(Math::round(texture_image->get_height() * scale)));
		texture_image->resize(width, height, Image::INTERPOLATE_LANCZOS);
		if (texture_image->save_png(image_path) != OK) {
			r_error = vformat("The resized texture could not be saved: %s", image_path);
			return false;
		}
	}
	return true;
}

static void _collect_model_references(EditorFileSystemDirectory *p_directory, const String &p_target_path, HashSet<String> &r_references, bool &r_scan_complete, const String &p_ignored_reference = String()) {
	if (p_directory == nullptr) {
		r_scan_complete = false;
		return;
	}
	const ResourceUID::ID target_uid = ResourceLoader::get_resource_uid(p_target_path);
	const String target_uid_text = target_uid == ResourceUID::INVALID_ID ? String() : ResourceUID::get_singleton()->id_to_text(target_uid);
	for (int i = 0; i < p_directory->get_file_count(); i++) {
		const String file_path = p_directory->get_file_path(i);
		if (file_path == p_target_path || file_path == p_target_path + ".import" || file_path == p_ignored_reference || file_path == p_ignored_reference + ".import") {
			continue;
		}
		for (const String &dependency : p_directory->get_file_deps(i)) {
			const String dependency_path = dependency.get_slice("::", 0);
			if (dependency_path == p_target_path) {
				r_references.insert(file_path);
			}
		}
		const String extension = file_path.get_extension().to_lower();
		if (extension != "tscn" && extension != "tres" && extension != "gd" && extension != "cs" && extension != "godot" && extension != "cfg" && extension != "json" && extension != "import") {
			continue;
		}
		Ref<FileAccess> file = FileAccess::open(ProjectSettings::get_singleton()->globalize_path(file_path), FileAccess::READ);
		if (file.is_null()) {
			r_scan_complete = false;
			continue;
		}
		if (file->get_length() > 16 * 1024 * 1024) {
			r_scan_complete = false;
			continue;
		}
		const String contents = file->get_as_utf8_string();
		if (contents.contains(p_target_path) || (!target_uid_text.is_empty() && contents.contains(target_uid_text))) {
			r_references.insert(file_path);
		}
	}
	for (int i = 0; i < p_directory->get_subdir_count(); i++) {
		_collect_model_references(p_directory->get_subdir(i), p_target_path, r_references, r_scan_complete, p_ignored_reference);
	}
}

static bool _validate_gltf_round_trip(const Ref<GLTFState> &p_source, const Ref<GLTFState> &p_round_trip, String &r_error) {
	if (p_round_trip->get_nodes().is_empty()) {
		r_error = "The converted glTF file did not contain any model nodes.";
		return false;
	}
	if (!_gltf_state_has_renderable_mesh(p_source) || !_gltf_state_has_renderable_mesh(p_round_trip)) {
		r_error = "The source or converted file contains no renderable mesh geometry. No optimized model was created.";
		return false;
	}
	if (p_source->get_meshes().size() > p_round_trip->get_meshes().size()) {
		r_error = "Validation found a missing mesh in the converted file.";
		return false;
	}
	if (p_source->get_materials().size() > p_round_trip->get_materials().size()) {
		r_error = "Validation found a missing material in the converted file.";
		return false;
	}
	if (p_source->get_skins().size() > p_round_trip->get_skins().size() || _gltf_state_bone_count(p_source) > _gltf_state_bone_count(p_round_trip)) {
		r_error = "Validation found a missing skeleton or bone in the converted file.";
		return false;
	}
	if (p_source->get_animations().size() > p_round_trip->get_animations().size()) {
		r_error = "Validation found a missing animation in the converted file.";
		return false;
	}
	return true;
}

static void _collect_imported_model_textures(const String &p_source_path, const Ref<GLTFState> &p_state, Vector<String> &r_texture_paths) {
	r_texture_paths.clear();
	if (p_source_path.get_extension().to_lower() != "glb") {
		return;
	}
	const String localized_source = ProjectSettings::get_singleton()->localize_path(p_source_path);
	if (!localized_source.begins_with("res://") || localized_source.begins_with("res://..")) {
		return;
	}
	Ref<ConfigFile> import_settings;
	import_settings.instantiate();
	if (import_settings->load(p_source_path + ".import") != OK || int(import_settings->get_value("params", "gltf/embedded_image_handling", 0)) != GLTFState::HANDLE_BINARY_IMAGE_MODE_EXTRACT_TEXTURES) {
		return;
	}
	const Array images = p_state->get_json().get("images", Array());
	HashSet<String> unique_paths;
	const Vector<String> extensions = { ".png", ".jpg", ".jpeg", ".webp", ".ktx2" };
	for (int i = 0; i < images.size(); i++) {
		const Dictionary image = images[i];
		if (!image.has("bufferView") || image.has("uri")) {
			continue;
		}
		String image_name = image.get("name", String());
		if (image_name.is_empty()) {
			image_name = itos(i);
		}
		const String candidate_base = localized_source.get_base_dir().path_join(localized_source.get_file().get_basename() + "_" + image_name);
		for (const String &extension : extensions) {
			const String candidate = candidate_base + extension;
			if (FileAccess::exists(_model_source_absolute_path(candidate)) && !unique_paths.has(candidate)) {
				unique_paths.insert(candidate);
				r_texture_paths.push_back(candidate);
			}
		}
	}
}

static bool _convert_model_to_gltf(const String &p_source_path, const String &p_output_dir, int p_max_dimension, Vector<String> &r_extracted_texture_paths, String &r_error, String &r_output_path, std::atomic<int> *p_stage) {
	r_extracted_texture_paths.clear();
	r_output_path = String();
	if (p_stage) { p_stage->store(1); }
	r_error = String();
	String texture_warning;
	const String source_absolute_path = _model_source_absolute_path(p_source_path);
	if (!FileAccess::exists(source_absolute_path)) {
		r_error = vformat("The source file could not be found: %s", p_source_path);
		return false;
	}

	Ref<GLTFDocument> gltf_document;
	gltf_document.instantiate();
	Ref<GLTFState> gltf_state;
	gltf_state.instantiate();
	gltf_state->set_handle_binary_image_mode(GLTFState::HANDLE_BINARY_IMAGE_MODE_EMBED_AS_UNCOMPRESSED);
	Error err = OK;
	Node *fbx_scene = nullptr;

	if (p_source_path.get_extension().to_lower() == "fbx") {
		Ref<EditorSceneFormatImporterUFBX> fbx_importer;
		fbx_importer.instantiate();
		HashMap<StringName, Variant> import_options;
		ResourceImporterScene scene_importer;
		List<ResourceImporter::ImportOption> default_options;
		scene_importer.get_import_options(p_source_path, &default_options);
		for (const ResourceImporter::ImportOption &option : default_options) {
			import_options[option.option.name] = option.default_value;
		}
		List<ResourceImporter::ImportOption> fbx_options;
		fbx_importer->get_import_options(p_source_path, &fbx_options);
		for (const ResourceImporter::ImportOption &option : fbx_options) {
			import_options[option.option.name] = option.default_value;
		}
		// External FBX files can live outside the project. The default extract
		// mode asks EditorFileSystem to reimport sibling PNGs as project assets,
		// which fails for files in locations such as Downloads. Keep them in
		// memory here; the optimized glTF writer will create the resized copies.
		import_options["fbx/embedded_image_handling"] = GLTFState::HANDLE_BINARY_IMAGE_MODE_EMBED_AS_UNCOMPRESSED;
		List<String> missing_dependencies;
		Error import_error = OK;
		fbx_scene = fbx_importer->import_scene(source_absolute_path, EditorSceneFormatImporter::IMPORT_USE_NAMED_SKIN_BINDS, import_options, &missing_dependencies, &import_error);
		if (fbx_scene == nullptr || import_error != OK) {
			r_error = "Godot could not import this FBX file. Check that its geometry, materials, and animations are supported.";
			if (!missing_dependencies.is_empty()) {
				r_error += " One or more external dependencies are missing.";
			}
			return false;
		}
		if (!missing_dependencies.is_empty()) {
			memdelete(fbx_scene);
			r_error = "The FBX file refers to missing external dependencies. The original file was left untouched.";
			return false;
		}
		if (!fbx_importer->get_missing_texture_paths().is_empty()) {
			texture_warning = vformat("Some FBX textures could not be found and were omitted:\n%s", String("\n").join(fbx_importer->get_missing_texture_paths()));
		}
		fbx_scene = _convert_importer_mesh_instances(fbx_scene);
		for (const String &texture_path : fbx_importer->get_source_texture_paths()) {
			const String localized_texture_path = ProjectSettings::get_singleton()->localize_path(texture_path);
			if (localized_texture_path.begins_with("res://") && !localized_texture_path.begins_with("res://..")) {
				r_extracted_texture_paths.push_back(localized_texture_path);
			} else {
				r_extracted_texture_paths.push_back(texture_path);
			}
		}
		err = gltf_document->append_from_scene(fbx_scene, gltf_state, GLTFDocument::IMPORT_FLAG_USE_NAMED_SKIN_BINDS);
		memdelete(fbx_scene);
		if (err != OK) {
			r_error = "Godot could not convert the imported FBX scene to glTF.";
			return false;
		}
	} else {
		gltf_state->set_extract_path(String());
		err = gltf_document->append_from_file(source_absolute_path, gltf_state, GLTFDocument::IMPORT_FLAG_USE_NAMED_SKIN_BINDS, source_absolute_path.get_base_dir());
		if (err != OK) {
			r_error = "Godot could not read this GLB/glTF file.";
			return false;
		}
		_collect_imported_model_textures(source_absolute_path, gltf_state, r_extracted_texture_paths);
	}
	if (!_gltf_state_has_renderable_mesh(gltf_state)) {
		r_error = "The source model contains no renderable mesh geometry. The source file was left untouched.";
		return false;
	}

	if (p_stage) { p_stage->store(2); }
	if (!_compact_embedded_image_data(gltf_state, r_error)) {
		return false;
	}
	if (!_resize_gltf_images(gltf_state, p_max_dimension, r_error)) {
		return false;
	}
	gltf_document->set_image_format("PNG");

	const String source_name = p_source_path.get_file().get_basename().validate_filename();
	const String target_absolute_path = _model_source_absolute_path(p_output_dir);
	if (DirAccess::make_dir_recursive_absolute(target_absolute_path) != OK) {
		r_error = vformat("Could not create the destination folder: %s", p_output_dir);
		return false;
	}
	const String final_folder_name_base = source_name + "_optimized";
	String final_folder_name = final_folder_name_base;
	int suffix = 2;
	while (DirAccess::dir_exists_absolute(target_absolute_path.path_join(final_folder_name))) {
		final_folder_name = final_folder_name_base + "_" + itos(suffix++);
	}
	const String project_root = ProjectSettings::get_singleton()->get_resource_path().simplify_path();
	const bool destination_is_in_project = target_absolute_path == project_root || target_absolute_path.begins_with(project_root.path_join(""));
	const String stage_root = destination_is_in_project ? project_root.path_join(".godot") : target_absolute_path;
	if (DirAccess::make_dir_recursive_absolute(stage_root) != OK) {
		r_error = "Could not create a temporary folder for the conversion.";
		return false;
	}
	const String stage_path = stage_root.path_join("model_import_" + itos(OS::get_singleton()->get_ticks_usec()));
	if (DirAccess::make_dir_absolute(stage_path) != OK) {
		r_error = "Could not create a temporary folder for the conversion.";
		return false;
	}

	const String staged_gltf = stage_path.path_join(source_name + ".gltf");
	gltf_document->set_max_image_dimension(p_max_dimension);
	if (p_stage) { p_stage->store(3); }
	err = gltf_document->write_to_filesystem(gltf_state, staged_gltf);
	if (err != OK) {
		_remove_directory_recursive(stage_path);
		r_error = "Godot could not write the optimized glTF source and its separate textures.";
		return false;
	}
	if (!_resize_gltf_texture_files(staged_gltf, stage_path, p_max_dimension, r_error) || !_validate_gltf_external_files(staged_gltf, stage_path, r_error)) {
		_remove_directory_recursive(stage_path);
		return false;
	}

	if (p_stage) { p_stage->store(4); }
	Ref<GLTFDocument> validation_document;
	validation_document.instantiate();
	Ref<GLTFState> validation_state;
	validation_state.instantiate();
	// Validation should read the model structure without importing temporary image paths.
	validation_state->set_handle_binary_image_mode(GLTFState::HANDLE_BINARY_IMAGE_MODE_DISCARD_TEXTURES);
	err = validation_document->append_from_file(staged_gltf, validation_state, GLTFDocument::IMPORT_FLAG_USE_NAMED_SKIN_BINDS, stage_path);
	if (err != OK || !_validate_gltf_round_trip(gltf_state, validation_state, r_error)) {
		_remove_directory_recursive(stage_path);
		if (r_error.is_empty()) {
			r_error = "The converted model could not be loaded again for validation.";
		}
		return false;
	}

	const String final_path = target_absolute_path.path_join(final_folder_name);
	err = DirAccess::rename_absolute(stage_path, final_path);
	if (err != OK) {
		_remove_directory_recursive(stage_path);
		r_error = "The conversion succeeded, but Godot could not move the verified files into the destination folder.";
		return false;
	}
	r_output_path = final_path.path_join(source_name + ".gltf");
	if (!texture_warning.is_empty()) {
		r_error = texture_warning;
	}
	return true;
}

static bool _delete_original_source(const String &p_source_path, bool p_source_is_project_asset, const Vector<String> &p_extracted_texture_paths, String &r_result) {
	const String source_absolute_path = _model_source_absolute_path(p_source_path);
	HashSet<String> references;
	bool scan_complete = true;
	_collect_model_references(EditorFileSystem::get_singleton()->get_filesystem(), p_source_path, references, scan_complete);
	if (!scan_complete) {
		r_result = "The conversion succeeded. The original was kept because Godot could not verify every project dependency.";
		return false;
	}
	if (!references.is_empty()) {
		Vector<String> paths;
		for (const String &path : references) {
			paths.push_back(path);
		}
		paths.sort();
		r_result = vformat("The conversion succeeded. The original was kept because %d project file(s) still depend on it:\n%s", paths.size(), String("\n").join(paths));
		return false;
	}
	Error err = DirAccess::remove_absolute(source_absolute_path);
	if (err != OK) {
		r_result = "The conversion succeeded, but Godot could not remove the original source file.";
		return false;
	}
	if (p_source_is_project_asset) {
		DirAccess::remove_absolute(source_absolute_path + ".import");
		DirAccess::remove_absolute(source_absolute_path + ".uid");
	}
	r_result = "The original source model was removed after conversion and dependency checks.";
	int removed_texture_count = 0;
	Vector<String> kept_texture_paths;
	HashSet<String> unique_texture_paths;
	for (const String &texture_path : p_extracted_texture_paths) {
		const String texture_absolute_path = _model_source_absolute_path(texture_path);
		if (texture_absolute_path == source_absolute_path || unique_texture_paths.has(texture_absolute_path)) {
			continue;
		}
		unique_texture_paths.insert(texture_absolute_path);
		const String localized_texture_path = ProjectSettings::get_singleton()->localize_path(texture_absolute_path);
		const bool texture_is_project_asset = localized_texture_path.begins_with("res://") && !localized_texture_path.begins_with("res://..");
		HashSet<String> references;
		bool scan_complete = true;
		const String dependency_path = texture_is_project_asset ? localized_texture_path : texture_absolute_path;
		_collect_model_references(EditorFileSystem::get_singleton()->get_filesystem(), dependency_path, references, scan_complete, p_source_path);
		if (!scan_complete || !references.is_empty()) {
			kept_texture_paths.push_back(texture_is_project_asset ? localized_texture_path : texture_path);
			continue;
		}
		if (DirAccess::remove_absolute(texture_absolute_path) != OK) {
			kept_texture_paths.push_back(texture_path);
			continue;
		}
		if (texture_is_project_asset) {
			DirAccess::remove_absolute(texture_absolute_path + ".import");
			DirAccess::remove_absolute(texture_absolute_path + ".uid");
		}
		removed_texture_count++;
	}
	if (removed_texture_count > 0) {
		r_result += vformat("\nRemoved %d unused original texture file(s).", removed_texture_count);
	}
	if (!kept_texture_paths.is_empty()) {
		r_result += vformat("\nKept %d texture file(s) because they are referenced, could not be verified, or could not be removed:\n%s", kept_texture_paths.size(), String("\n").join(kept_texture_paths));
	}
	return true;
}

bool ModelImportWizard::supports_model_file(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	return extension == "glb" || extension == "gltf" || extension == "fbx";
}

void ModelImportWizard::configure(const String &p_source_path, const String &p_target_directory, bool p_existing_asset) {
	if (background_import_running || waiting_for_filesystem) {
		popup_centered(Size2(520, 160));
		return;
	}
	showing_result = false;
	settings_container->show();
	get_cancel_button()->show();
	set_ok_button_text(TTRC("Import"));
	set_min_size(Size2(520, 0));
	source_path = p_source_path;
	source_paths.clear();
	source_paths.push_back(p_source_path);
	target_directory = p_target_directory;
	existing_asset_mode = p_existing_asset;
	const String localized_source = ProjectSettings::get_singleton()->localize_path(_model_source_absolute_path(source_path));
	source_is_project_asset = localized_source.begins_with("res://") && !localized_source.begins_with("res://..");
	if (source_is_project_asset) {
		source_path = localized_source;
	}
	set_title(TTR(existing_asset_mode ? "Optimize Existing Model" : "Import 3D Model"));
	source_label->set_text(source_path + "\n\n" + (existing_asset_mode ? TTR("Create a Godot-ready glTF source with separate resized textures. Existing scene references are checked before an optional source removal.") : TTR("Import GLB, glTF, or FBX. The optimized option creates a reusable glTF source, external binary data, and separate textures while preserving model materials, skeletons, and animations.")));
	convert_checkbox->set_disabled(existing_asset_mode);
	convert_checkbox->set_pressed(true);
	delete_original_checkbox->set_pressed(false);
	delete_original_checkbox->set_text(source_is_project_asset ? TTR("Remove the original model and its unused source textures after conversion") : TTR("Remove the original model and its linked source textures after conversion"));
	_options_changed();
	popup_centered(Size2(560, 0));
}

void ModelImportWizard::configure_batch(const Vector<String> &p_source_paths, const String &p_target_directory) {
	if (background_import_running || waiting_for_filesystem) {
		popup_centered(Size2(520, 160));
		return;
	}
	if (p_source_paths.is_empty()) {
		return;
	}
	showing_result = false;
	settings_container->show();
	get_cancel_button()->show();
	set_ok_button_text(TTRC("Import"));
	set_min_size(Size2(520, 0));
	source_paths = p_source_paths;
	source_path = source_paths[0];
	target_directory = p_target_directory;
	existing_asset_mode = false;
	String source_list;
	for (int i = 0; i < source_paths.size(); i++) {
		if (i > 0) {
			source_list += "\n";
		}
		source_list += source_paths[i];
	}
	source_label->set_text(vformat(TTR("%d model files:\n%s\n\n%s"), source_paths.size(), source_list, TTR("Use the same conversion and texture settings for every selected model. Each output is verified before it is added to the project.")));
	set_title(TTR("Import 3D Models"));
	convert_checkbox->set_disabled(false);
	convert_checkbox->set_pressed(true);
	delete_original_checkbox->set_pressed(false);
	delete_original_checkbox->set_text(TTR("Remove original models and their unused source textures after conversion"));
	_options_changed();
	popup_centered(Size2(600, 0));
}

void ModelImportWizard::_options_changed() {
	const bool converting = convert_checkbox->is_pressed();
	resolution_option->set_disabled(!converting);
	delete_original_checkbox->set_disabled(!converting);
}

void ModelImportWizard::_confirmed() {
	if (background_import_running || waiting_for_filesystem) {
		hide();
		return;
	}
	if (showing_result) {
		hide();
		return;
	}
	hide();
	call_deferred("_run_import");
}

void ModelImportWizard::_show_progress(const String &p_message) {
	showing_result = false;
	set_title(TTR("Optimizing Model"));
	source_label->set_text(p_message);
	settings_container->hide();
	get_cancel_button()->hide();
	set_ok_button_text(TTRC("Continue in Background"));
	set_min_size(Size2(520, 160));
	popup_centered(Size2(520, 160));
}

void ModelImportWizard::_show_result(const String &p_message, bool p_success) {
	showing_result = true;
	set_title(TTR(p_success ? "Model Import Complete" : "Model Import Failed"));
	source_label->set_text(p_message);
	settings_container->hide();
	get_cancel_button()->hide();
	set_ok_button_text(TTRC("Close"));
	set_min_size(Size2(560, 220));
	popup_centered(Size2(560, 220));
}

void ModelImportWizard::_start_background_conversion() {
	background_source_paths.clear();
	background_results.clear();
	for (const String &path : source_paths) {
		background_source_paths.push_back(_model_source_absolute_path(path));
	}
	background_target_directory = _model_source_absolute_path(target_directory);
	background_resolution = resolution_option->get_selected_id();
	background_conversion_ready = false;
	conversion_stage.store(1);
	conversion_index.store(0);
	import_finished.clear();
	_show_progress(TTR("Converting the model and resizing its textures. You can continue working while this runs."));
	if (import_thread.start(_conversion_thread, this) == Thread::UNASSIGNED_ID) {
		_show_result(TTR("Godot could not start the background model conversion."), false);
		return;
	}
	background_import_running = true;
	progress_timer->start();
}

void ModelImportWizard::_conversion_thread(void *p_userdata) {
	ModelImportWizard *wizard = static_cast<ModelImportWizard *>(p_userdata);
	Vector<ConversionResult> results;
	for (int i = 0; i < wizard->background_source_paths.size(); i++) {
		wizard->conversion_index.store(i);
		const String &path = wizard->background_source_paths[i];
		ConversionResult result;
		result.succeeded = _convert_model_to_gltf(path, wizard->background_target_directory, wizard->background_resolution, result.extracted_texture_paths, result.error, result.output_path, &wizard->conversion_stage);
		results.push_back(result);
	}
	wizard->background_results = results;
	wizard->import_finished.set();
}

void ModelImportWizard::_check_background_progress() {
	if (background_import_running && !import_finished.is_set()) {
		const char *stages[] = { "Preparing", "Reading the model and its textures", "Resizing textures", "Writing the optimized model and textures", "Checking the converted model" };
		const int stage = CLAMP(conversion_stage.load(), 0, 4);
		const int index = CLAMP(conversion_index.load(), 0, background_source_paths.size() - 1);
		source_label->set_text(vformat("%s\n%s (%d/%d)\n\n%s", TTR(stages[stage]), background_source_paths[index].get_file(), index + 1, background_source_paths.size(), TTR("The conversion is running. You can continue working in the background.")));
	}
	if (background_import_running && import_finished.is_set()) {
		import_thread.wait_to_finish();
		background_import_running = false;
		background_conversion_ready = true;
		_show_progress(TTR("Conversion finished. Godot is adding the model and textures to the project."));
		_run_import();
	}
	if (waiting_for_filesystem && !EditorFileSystem::get_singleton()->is_scanning() && !EditorFileSystem::get_singleton()->is_importing()) {
		bool ready = true;
		bool valid = true;
		for (const String &path : pending_output_paths) {
			int index = -1;
			EditorFileSystemDirectory *directory = EditorFileSystem::get_singleton()->find_file(path, &index);
			if (directory == nullptr || index < 0) { ready = false; break; }
			if (!directory->get_file_import_is_valid(index)) { valid = false; }
		}
		const bool timed_out = OS::get_singleton()->get_ticks_msec() - filesystem_wait_started > 120000;
		if (!ready && !timed_out) { return; }
		waiting_for_filesystem = false;
		progress_timer->stop();
		if (!ready || !valid) {
			pending_result_message += "\nGodot has not finished importing the generated model. Check the Output panel.";
			pending_result_success = false;
		} else if (pending_output_paths.size() == 1) {
			FileSystemDock::get_singleton()->select_file(pending_output_paths[0]);
		}
		_show_result(pending_result_message, pending_result_success);
		pending_result_message = String();
		pending_output_paths.clear();
	}
}

void ModelImportWizard::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE && import_thread.is_started()) {
		progress_timer->stop();
		import_thread.wait_to_finish();
		background_import_running = false;
	}
}

void ModelImportWizard::_run_import() {
	if (background_import_running) {
		return;
	}
	if (convert_checkbox->is_pressed() && !background_conversion_ready) {
		_start_background_conversion();
		return;
	}
	const bool use_background_results = background_conversion_ready;
	String result_message;
	bool all_succeeded = true;
	bool any_succeeded = false;
	pending_output_paths.clear();
	const int resolution = resolution_option->get_selected_id();
	for (int source_index = 0; source_index < source_paths.size(); source_index++) {
		const String &path = source_paths[source_index];
		const String source_absolute_path = _model_source_absolute_path(path);
		if (!FileAccess::exists(source_absolute_path)) {
			all_succeeded = false;
			result_message += vformat("Could not find %s.\n", path);
			continue;
		}
		if (!convert_checkbox->is_pressed()) {
			const String target_absolute_path = _model_source_absolute_path(target_directory).path_join(path.get_file());
			if (target_absolute_path == source_absolute_path) {
				result_message += vformat("%s is already in the selected project folder.\n", path.get_file());
				any_succeeded = true;
				continue;
			}
			if (FileAccess::exists(target_absolute_path)) {
				all_succeeded = false;
				result_message += vformat("A file named %s already exists in the destination.\n", path.get_file());
				continue;
			}
			Ref<DirAccess> target_dir = DirAccess::open(_model_source_absolute_path(target_directory));
			if (target_dir.is_null() || target_dir->copy(source_absolute_path, target_absolute_path) != OK) {
				all_succeeded = false;
				result_message += vformat("Could not copy %s into the project.\n", path.get_file());
				continue;
			}
			pending_output_paths.push_back(ProjectSettings::get_singleton()->localize_path(target_absolute_path));
			result_message += vformat("Copied %s into the project.\n", path.get_file());
			any_succeeded = true;
		} else {
			String conversion_error;
			String output_path;
			Vector<String> extracted_texture_paths;
			bool converted = false;
			if (use_background_results && source_index < background_results.size()) {
				converted = background_results[source_index].succeeded;
				conversion_error = background_results[source_index].error;
				output_path = background_results[source_index].output_path;
				extracted_texture_paths = background_results[source_index].extracted_texture_paths;
			} else if (use_background_results) {
				conversion_error = "The background converter did not return a result for this file.";
			} else {
				converted = _convert_model_to_gltf(path, target_directory, resolution, extracted_texture_paths, conversion_error, output_path, nullptr);
			}
			if (!converted) {
				all_succeeded = false;
				result_message += vformat("%s: %s\n", path.get_file(), conversion_error);
				continue;
			}
			const String result_path = ProjectSettings::get_singleton()->localize_path(output_path);
			pending_output_paths.push_back(result_path);
			result_message += vformat("Converted %s.\nModel: %s\n", path.get_file(), result_path);
			if (!conversion_error.is_empty()) {
				result_message += vformat("Warning: %s\n", conversion_error);
			}
			any_succeeded = true;
			if (delete_original_checkbox->is_pressed()) {
				if (!conversion_error.is_empty()) {
					result_message += "The original was kept because the optimized model is missing one or more source textures.\n";
				} else {
					const String localized = ProjectSettings::get_singleton()->localize_path(source_absolute_path);
					const bool in_project = localized.begins_with("res://") && !localized.begins_with("res://..");
					String delete_path = in_project ? localized : path;
					String delete_result;
					_delete_original_source(delete_path, in_project, extracted_texture_paths, delete_result);
					result_message += delete_result + "\n";
				}
			}
		}
	}
	if (any_succeeded) {
		EditorFileSystem::get_singleton()->scan_changes();
	}
	if (use_background_results) {
		background_conversion_ready = false;
		background_results.clear();
		background_source_paths.clear();
	}
	if (result_message.is_empty()) {
		result_message = "No model files were selected.";
		all_succeeded = false;
	}
	if (any_succeeded && !pending_output_paths.is_empty()) {
		pending_result_message = result_message.strip_edges();
		pending_result_success = all_succeeded;
		waiting_for_filesystem = true;
		filesystem_wait_started = OS::get_singleton()->get_ticks_msec();
		_show_progress(TTR("Godot is importing the optimized model and registering its texture files."));
		progress_timer->start();
	} else {
		progress_timer->stop();
		_show_result(result_message.strip_edges(), all_succeeded);
	}
}

void ModelImportWizard::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_options_changed"), &ModelImportWizard::_options_changed);
	ClassDB::bind_method(D_METHOD("_confirmed"), &ModelImportWizard::_confirmed);
	ClassDB::bind_method(D_METHOD("_run_import"), &ModelImportWizard::_run_import);
	ClassDB::bind_method(D_METHOD("_check_background_progress"), &ModelImportWizard::_check_background_progress);
}

ModelImportWizard::ModelImportWizard() {
	set_title(TTRC("Import 3D Model"));
	set_text(String());
	set_ok_button_text(TTRC("Import"));
	set_min_size(Size2(520, 0));
	connect(SceneStringName(confirmed), callable_mp(this, &ModelImportWizard::_confirmed));

	options_container = memnew(VBoxContainer);
	add_child(options_container);

	source_label = memnew(Label);
	source_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	options_container->add_child(source_label);
	settings_container = memnew(VBoxContainer);
	options_container->add_child(settings_container);

	convert_checkbox = memnew(CheckBox);
	convert_checkbox->set_text(TTRC("Create optimized .gltf source with separate textures"));
	convert_checkbox->set_pressed(true);
	convert_checkbox->connect(SceneStringName(toggled), callable_mp(this, &ModelImportWizard::_options_changed).unbind(1));
	settings_container->add_child(convert_checkbox);

	HBoxContainer *resolution_row = memnew(HBoxContainer);
	settings_container->add_child(resolution_row);
	Label *resolution_label = memnew(Label);
	resolution_label->set_text(TTRC("Maximum texture dimension"));
	resolution_row->add_child(resolution_label);
	resolution_option = memnew(OptionButton);
	for (int limit : MODEL_TEXTURE_LIMITS) {
		resolution_option->add_item(itos(limit) + " px", limit);
	}
	resolution_option->select(1); // 2048 px.
	resolution_row->add_child(resolution_option);

	delete_original_checkbox = memnew(CheckBox);
	delete_original_checkbox->set_text(TTRC("Remove the original model and its unused source textures after conversion"));
	delete_original_checkbox->set_tooltip_text(TTRC("The source model is removed only after the generated glTF passes validation. Project references are checked before removing the model or any linked texture, even when the source is outside the project."));
	settings_container->add_child(delete_original_checkbox);
	delete_original_checkbox->connect(SceneStringName(toggled), callable_mp(this, &ModelImportWizard::_options_changed).unbind(1));

	progress_timer = memnew(Timer);
	progress_timer->set_wait_time(0.1);
	progress_timer->connect("timeout", callable_mp(this, &ModelImportWizard::_check_background_progress));
	add_child(progress_timer);

}
