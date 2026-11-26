# Editor checks of the workspaces. Copy this folder to addons/particle_editor_test in a COPY
# of the game project, enable it in project.godot and run:
#   HOME=<scratch>/home godot --headless --editor --language en --path <game copy>
# Prints "ED OK n/n" and quits, or lists the failed checks.
@tool
extends EditorPlugin

const OUT := "res://_particle_editor_test/"
# FPSParticlePresets::PRESET_MAX in engine/editor/fps_studio/particle_presets.h.
const PRESET_COUNT := 25
var passed := 0
var failed: Array[String] = []
var screens: Array = []


func check(ok: bool, what: String) -> void:
	if ok:
		passed += 1
	else:
		failed.append(what)
		print("ED FAIL: ", what)


func frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _enter_tree() -> void:
	main_screen_changed.connect(func(n): screens.append(n))
	_run.call_deferred()


func _find(type: String) -> Array:
	return get_tree().root.find_children("*", type, true, false)


func _main_tabs() -> Dictionary:
	var out := {}
	var main: TabContainer = _find("EditorMainScreen")[0]
	for i in main.get_tab_count():
		out[String(main.get_tab_control(i).name)] = main.get_tab_title(i)
	out["_current"] = String(main.get_current_tab_control().name)
	return out


func _run() -> void:
	# The editor loads its layout (and the saved workspace tab) after the first file scan.
	await frames(10)
	for i in 1200:
		if not EditorInterface.get_resource_filesystem().is_scanning():
			break
		await get_tree().process_frame
	await frames(120)
	# --- workspaces ---------------------------------------------------------
	check(_find("EditorMainScreen").size() == 1, "main screen exists")
	check(_main_tabs()["_current"] == "3D", "the editor starts in Level (is %s)" % _main_tabs()["_current"])
	var tabs := _main_tabs()
	check(tabs.get("3D") == "Level", "3D tab is labelled Level (%s)" % tabs.get("3D"))
	check(tabs.get("Script") == "Code", "Script tab is labelled Code (%s)" % tabs.get("Script"))
	check(tabs.has("Model") and tabs.has("Particle"), "Model and Particle tabs exist")
	check(not tabs.has("Game"), "Game tab is hidden (the game runs in Level)")
	screens.clear()
	for screen in ["Particle", "Model", "Script", "3D"]:
		EditorInterface.set_main_screen_editor(screen)
		await frames(5)
		check(_main_tabs()["_current"] == screen, "switch to %s (is %s)" % [screen, _main_tabs()["_current"]])
	check(screens == ["Particle", "Model", "Script", "3D"], "main_screen_changed reports the names %s" % str(screens))
	var model_screens := _find("SceneImportSettingsScreen")
	check(model_screens.size() == 1 and not model_screens[0].is_visible_in_tree(), "Model screen hidden outside its tab")

	# --- every preset bakes, opens and plays in the preview -------------------
	var plugins := _find("FPSStudioParticleEditorPlugin")
	check(plugins.size() == 1, "particle plugin found")
	if plugins.is_empty():
		_finish()
		return
	var plugin = plugins[0]
	EditorInterface.set_main_screen_editor("Particle")
	await frames(5)
	DirAccess.make_dir_recursive_absolute(OUT)
	var baked: Array[String] = []
	for preset in PRESET_COUNT:
		var path := OUT + "preset_%02d.tscn" % preset
		if plugin.bake_preset_scene(preset, path) == OK:
			baked.append(path)
	check(baked.size() == PRESET_COUNT, "all presets bake (%d)" % baked.size())
	for path in baked:
		var scene: PackedScene = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)
		var instance = scene.instantiate() if scene else null
		check(instance is ParticleEffect3D and instance.get_child_count() > 0, path.get_file() + " is an effect with tracks")
		if instance:
			instance.free()
	for path in [baked[0], baked[baked.size() / 2], baked[baked.size() - 1]] if baked.size() > 2 else baked:
		plugin.open_effect(path)
		await frames(30)
		check(_main_tabs()["_current"] == "Particle", "stays in Particle after opening " + path.get_file())

	# --- Logic tab: node graph and shader code editors ------------------------
	var vs_path := OUT + "logic_graph.tscn"
	var code_path := OUT + "logic_code.tscn"
	_make_logic_scene(vs_path, true)
	_make_logic_scene(code_path, false)
	for step in [[vs_path, true, false], [code_path, false, true], [vs_path, true, false], [baked[0] if not baked.is_empty() else code_path, false, baked.is_empty()]]:
		plugin.open_effect(step[0])
		await frames(40)
		var graph := _find("VisualShaderEditor")
		var code := _find("ShaderTextEditor")
		check((not graph.is_empty() and graph[0].visible) == step[1], "graph editor shown=%s for %s" % [step[1], String(step[0]).get_file()])
		check((not code.is_empty() and code[0].visible) == step[2], "code editor shown=%s for %s" % [step[2], String(step[0]).get_file()])
	var code_edits: Array = _find("ShaderTextEditor")[0].find_children("*", "CodeEdit", true, false) if not _find("ShaderTextEditor").is_empty() else []
	check(not code_edits.is_empty() and code_edits[0].text.contains("shader_type particles"), "code editor holds the shader source")

	# --- back to Level with a scene --------------------------------------------
	EditorInterface.set_main_screen_editor("3D")
	await frames(10)
	check(_main_tabs()["_current"] == "3D", "back in Level")
	check(_find("Node3DEditorViewport").size() >= 1, "Level viewports exist")
	_finish()


func _make_logic_scene(path: String, visual: bool) -> void:
	var root := ParticleEffect3D.new()
	root.name = "Logic"
	var emitter := GPUParticles3D.new()
	emitter.name = "Emitter"
	var material := ShaderMaterial.new()
	if visual:
		var shader := VisualShader.new()
		shader.set_mode(Shader.MODE_PARTICLES)
		material.shader = shader
	else:
		var shader := Shader.new()
		shader.code = "shader_type particles;\nvoid start() {\n\tVELOCITY = vec3(0.0, 1.0, 0.0);\n}\nvoid process() {\n}\n"
		material.shader = shader
	emitter.process_material = material
	emitter.draw_pass_1 = QuadMesh.new()
	root.add_child(emitter)
	emitter.owner = root
	var scene := PackedScene.new()
	scene.pack(root)
	check(ResourceSaver.save(scene, path) == OK, "save " + path.get_file())
	root.free()


func _finish() -> void:
	print("ED %s %d/%d" % ["OK" if failed.is_empty() else "FAILED", passed, passed + failed.size()])
	get_tree().quit(0 if failed.is_empty() else 1)
