extends SceneTree

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		push_error("Usage: pack_fps_studio_unibuilder.gd -- <source directory> <output PCK>")
		quit(1)
		return

	var source_root := args[0].simplify_path()
	var output_path := args[1].simplify_path()
	var packer := PCKPacker.new()
	var error := packer.pck_start(output_path)
	if error != OK:
		push_error("Could not start Unibuilder resource pack: %s" % error_string(error))
		quit(1)
		return

	error = _add_directory(packer, source_root, source_root)
	if error != OK:
		push_error("Could not add Unibuilder resources to the pack: %s" % error_string(error))
		quit(1)
		return

	error = packer.flush()
	if error != OK:
		push_error("Could not finish Unibuilder resource pack: %s" % error_string(error))
		quit(1)
		return

	print("Packed built-in Unibuilder resources to: ", output_path)
	quit()


func _add_directory(packer: PCKPacker, source_root: String, current_dir: String) -> Error:
	for file_name in DirAccess.get_files_at(current_dir):
		if file_name == ".DS_Store":
			continue
		var source_path := current_dir.path_join(file_name)
		var relative_path := source_path.trim_prefix(source_root).trim_prefix("/")
		var target_path := "addons/unibuilder".path_join(relative_path)
		var error := packer.add_file(target_path, source_path)
		if error != OK:
			push_error("Could not pack %s: %s" % [source_path, error_string(error)])
			return error

	for dir_name in DirAccess.get_directories_at(current_dir):
		var error := _add_directory(packer, source_root, current_dir.path_join(dir_name))
		if error != OK:
			return error

	return OK
