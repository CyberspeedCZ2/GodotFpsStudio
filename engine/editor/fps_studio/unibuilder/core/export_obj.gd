@tool
class_name UBExportOBJ
extends RefCounted
## Wavefront OBJ (+ MTL) exporter for Unibuilder meshes.

static func to_obj(mesh_data: UBMeshData, object_name: String, mtl_name: String, xf: Transform3D) -> String:
	if mesh_data == null:
		return ""

	var lines: PackedStringArray = []
	lines.append("# Wavefront OBJ exported by Unibuilder")
	if not mtl_name.is_empty():
		lines.append("mtllib " + mtl_name)
	lines.append("o " + object_name)

	# 1. Vertices
	for p in mesh_data.positions:
		var tp := xf * p
		lines.append("v %.6f %.6f %.6f" % [tp.x, tp.y, tp.z])

	var topo := UBTopology.build(mesh_data)
	var face_normals := PackedVector3Array()
	face_normals.resize(mesh_data.faces.size())
	for fi in mesh_data.faces.size():
		face_normals[fi] = mesh_data.face_normal(mesh_data.faces[fi])

	var normal_xf := xf.basis.inverse().transposed()

	# 2. UVs (vt) and Normals (vn) per face corner
	var corner_vt_map := {}
	var corner_vn_map := {}
	var next_vt := 1
	var next_vn := 1

	for fi in mesh_data.faces.size():
		var face: UBFace = mesh_data.faces[fi]
		var verts := face.vertices
		if verts.size() < 3:
			continue
		var corners := PackedVector3Array()
		for vi in verts:
			corners.append(mesh_data.positions[vi])

		var uvs := (
			face.uv_coords
			if face.uv_manual and face.uv_coords.size() == verts.size()
			else UBUVUnwrap.face_uvs(corners, face_normals[fi], face, xf)
		)

		for ci in verts.size():
			var uv := uvs[ci] if ci < uvs.size() else Vector2.ZERO
			lines.append("vt %.6f %.6f" % [uv.x, 1.0 - uv.y])
			corner_vt_map[Vector2i(fi, ci)] = next_vt
			next_vt += 1

			var norm := UBMeshCompiler.corner_normal(mesh_data, topo, face_normals, fi, verts[ci])
			var tnorm := (normal_xf * norm).normalized() if normal_xf.determinant() != 0.0 else norm
			lines.append("vn %.6f %.6f %.6f" % [tnorm.x, tnorm.y, tnorm.z])
			corner_vn_map[Vector2i(fi, ci)] = next_vn
			next_vn += 1

	# 3. Faces grouped by material slot
	var by_material := {}
	for fi in mesh_data.faces.size():
		var slot: int = mesh_data.faces[fi].material_index
		if not by_material.has(slot):
			by_material[slot] = []
		by_material[slot].append(fi)

	var slots: Array = by_material.keys()
	slots.sort()

	for slot in slots:
		var mat: Material = mesh_data.material_slots[slot] if slot >= 0 and slot < mesh_data.material_slots.size() else null
		var mat_name := _mat_name(mat, slot)
		lines.append("usemtl " + mat_name)
		for fi in by_material[slot]:
			var face: UBFace = mesh_data.faces[fi]
			var verts := face.vertices
			if verts.size() < 3:
				continue
			var face_tokens: PackedStringArray = []
			for ci in verts.size():
				var v_idx := verts[ci] + 1
				var vt_idx: int = corner_vt_map.get(Vector2i(fi, ci), 0)
				var vn_idx: int = corner_vn_map.get(Vector2i(fi, ci), 0)
				face_tokens.append("%d/%d/%d" % [v_idx, vt_idx, vn_idx])
			lines.append("f " + " ".join(face_tokens))

	return "\n".join(lines) + "\n"


static func to_mtl(mesh_data: UBMeshData) -> String:
	if mesh_data == null:
		return ""

	var lines: PackedStringArray = []
	lines.append("# Wavefront MTL exported by Unibuilder")

	var used_slots := {}
	for f in mesh_data.faces:
		used_slots[f.material_index] = true

	var slots: Array = used_slots.keys()
	slots.sort()

	for slot in slots:
		var mat: Material = mesh_data.material_slots[slot] if slot >= 0 and slot < mesh_data.material_slots.size() else null
		var mat_name := _mat_name(mat, slot)
		lines.append("")
		lines.append("newmtl " + mat_name)
		if mat is StandardMaterial3D or mat is ORMMaterial3D:
			var base_mat: BaseMaterial3D = mat
			lines.append("Kd %.4f %.4f %.4f" % [base_mat.albedo_color.r, base_mat.albedo_color.g, base_mat.albedo_color.b])
			lines.append("d %.4f" % base_mat.albedo_color.a)
			if base_mat.albedo_texture != null and not base_mat.albedo_texture.resource_path.is_empty():
				lines.append("map_Kd " + base_mat.albedo_texture.resource_path)
		else:
			lines.append("Kd 0.8 0.8 0.8")
			lines.append("d 1.0")

	return "\n".join(lines) + "\n"


static func _mat_name(mat: Material, slot_idx: int) -> String:
	if mat != null and not mat.resource_name.is_empty():
		return mat.resource_name.validate_node_name()
	if mat != null and not mat.resource_path.is_empty():
		return mat.resource_path.get_file().get_basename().validate_node_name()
	return "Material_%d" % slot_idx
