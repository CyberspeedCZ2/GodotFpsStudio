/**************************************************************************/
/*  model_import_wizard.h                                                 */
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

#pragma once

#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/option_button.h"

#include <atomic>

class Label;
class Timer;
class VBoxContainer;

class ModelImportWizard : public ConfirmationDialog {
	GDCLASS(ModelImportWizard, ConfirmationDialog);

	String source_path;
	Vector<String> source_paths;
	String target_directory;
	bool source_is_project_asset = false;
	bool existing_asset_mode = false;
	bool showing_result = false;

	VBoxContainer *options_container = nullptr;
	VBoxContainer *settings_container = nullptr;
	Label *source_label = nullptr;
	CheckBox *convert_checkbox = nullptr;
	OptionButton *resolution_option = nullptr;
	CheckBox *delete_original_checkbox = nullptr;
	Timer *progress_timer = nullptr;

	struct ConversionResult {
		bool succeeded = false;
		String error;
		String output_path;
		Vector<String> extracted_texture_paths;
	};

	Thread import_thread;
	SafeFlag import_finished;
	Vector<String> background_source_paths;
	String background_target_directory;
	int background_resolution = 1024;
	Vector<ConversionResult> background_results;
	bool background_conversion_ready = false;
	bool background_import_running = false;
	bool waiting_for_filesystem = false;
	std::atomic<int> conversion_stage{ 0 };
	std::atomic<int> conversion_index{ 0 };
	Vector<String> pending_output_paths;
	uint64_t filesystem_wait_started = 0;
	String pending_result_message;
	bool pending_result_success = false;

	void _options_changed();
	void _confirmed();
	void _run_import();
	void _start_background_conversion();
	static void _conversion_thread(void *p_userdata);
	void _check_background_progress();
	void _notification(int p_what);
	void _show_progress(const String &p_message);
	void _show_result(const String &p_message, bool p_success);

protected:
	static void _bind_methods();

public:
	static bool supports_model_file(const String &p_path);
	void configure(const String &p_source_path, const String &p_target_directory, bool p_existing_asset);
	void configure_batch(const Vector<String> &p_source_paths, const String &p_target_directory);

	ModelImportWizard();
};
