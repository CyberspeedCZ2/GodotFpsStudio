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

	var root := Node3D.new()
	var mi := MeshInstance3D.new()
	mi.name = object_name
	mi.mesh = mesh
	mi.transform = xf
	root.add_child(mi)

	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_scene(root, state)
	if err == OK:
		err = doc.write_to_filesystem(state, path)
	root.free()
	return err
