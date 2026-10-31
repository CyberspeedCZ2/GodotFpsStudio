@tool
class_name UBExportGLTF
extends RefCounted
## glTF exporter for Unibuilder meshes.

static func export_file(mesh_data: UBMeshData, object_name: String, path: String, xf: Transform3D) -> int:
	if mesh_data == null:
		return FAILED
	var mesh: ArrayMesh = UBMeshCompiler.compile(mesh_data, Transform3D.IDENTITY)
	if mesh == null:
		return FAILED

	# Use the exported file name as the imported scene root. This keeps the
	# level node name meaningful even when the UniBuilder object has a generic name.
	var root := MeshInstance3D.new()
	var root_name := path.get_file().get_basename().validate_node_name()
	root.name = root_name if not root_name.is_empty() else object_name
	root.mesh = mesh
	root.transform = xf

	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_scene(root, state)
	if err == OK:
		err = doc.write_to_filesystem(state, path)
	root.free()
	return err
