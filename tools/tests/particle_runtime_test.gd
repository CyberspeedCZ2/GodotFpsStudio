# Runtime checks of the effect classes. Run headless on a COPY of the game project:
#   HOME=<scratch>/home godot --headless --path <game copy> -s <this file>
# Prints "RT OK n/n" and exits with 0, or lists the failed checks and exits with 1.
extends SceneTree

const FX := "res://Scenes/Effects/Particles/"
var passed := 0
var failed: Array[String] = []
var damage_taken := 0.0


func check(ok: bool, what: String) -> void:
	if ok:
		passed += 1
	else:
		failed.append(what)
		print("RT FAIL: ", what)


func frames(n: int) -> void:
	for i in n:
		await process_frame


func physics(n: int) -> void:
	for i in n:
		await physics_frame


func _init() -> void:
	_run.call_deferred()


func _run() -> void:
	var world := Node3D.new()
	world.name = "World"
	root.add_child(world)
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.position = Vector3(0, 1, 6)
	camera.make_current()

	# Floor with a surface name, for the impact table and radial damage cover.
	var floor_body := StaticBody3D.new()
	floor_body.set_meta("surface", "metal")
	var floor_shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(20, 0.2, 20)
	floor_shape.shape = box
	floor_body.add_child(floor_shape)
	floor_body.position = Vector3(0, -0.1, 0)
	world.add_child(floor_body)
	await physics(3)

	# --- All effect scenes of the game load and play -------------------------
	var scenes: Array[PackedScene] = []
	var dir := DirAccess.open(FX)
	check(dir != null, "effect folder exists")
	if dir:
		for file in dir.get_files():
			if not file.ends_with(".tscn"):
				continue
			var scene: PackedScene = load(FX + file)
			check(scene != null, "load " + file)
			if scene == null:
				continue
			var instance := scene.instantiate()
			if instance is ParticleEffect3D:
				scenes.append(scene)
				check(instance.get_total_duration() > 0.0, file + " has a duration")
			instance.free()
	check(scenes.size() >= 5, "at least five ParticleEffect3D scenes (%d)" % scenes.size())

	# --- play / seek / stop / finished ---------------------------------------
	var explosion: PackedScene = load(FX + "explosion.tscn")
	var effect: ParticleEffect3D = explosion.instantiate()
	effect.autoplay = false
	effect.auto_free = false
	world.add_child(effect)
	check(not effect.is_playing(), "not playing before play()")
	effect.play()
	await frames(5)
	check(effect.is_playing(), "playing after play()")
	check(effect.get_elapsed_time() > 0.0, "elapsed time advances")
	effect.seek(0.5)
	await frames(2)
	check(effect.get_elapsed_time() >= 0.5, "seek moves the time")
	effect.paused = true
	var paused_at := effect.get_elapsed_time()
	await frames(5)
	check(is_equal_approx(effect.get_elapsed_time(), paused_at), "paused holds the time")
	effect.paused = false
	effect.stop()
	var finished := [false]
	effect.finished.connect(func(): finished[0] = true)
	effect.seek(effect.get_total_duration() + 5.0)
	await frames(10)
	check(finished[0] or not effect.is_playing(), "effect ends after its duration")
	effect.restart()
	await frames(3)
	check(effect.is_playing(), "restart plays again")
	effect.tint = Color(1, 0.5, 0.5)
	effect.effect_scale = 2.0
	effect.speed_scale = 2.0
	effect.set_emission_direction(Vector3(1, 0, 0))
	effect.set_inherited_velocity(Vector3(0, 0, 3))
	effect.set_target_position(Vector3(0, 0, -5))
	effect.set_parameter("power", 0.5)
	check(is_equal_approx(effect.get_parameter("power"), 0.5), "parameter round trip")
	effect.restart()
	await frames(5)
	check(effect.is_playing(), "plays with tint, scale, direction and parameters")
	effect.queue_free()
	await frames(2)

	# --- spawn, pool, prewarm ------------------------------------------------
	ParticleEffect3D.clear_pool()
	ParticleEffect3D.prewarm(explosion, world, 2)
	await frames(3)
	check(ParticleEffect3D.get_pooled_count(explosion) == 2, "prewarm fills the pool (%d)" % ParticleEffect3D.get_pooled_count(explosion))
	var spawned: ParticleEffect3D = ParticleEffect3D.spawn(explosion, world, Transform3D(Basis(), Vector3(2, 0, 0)), Color(1, 0, 0), 1.5)
	check(spawned != null and spawned.is_playing(), "spawn() returns a playing effect")
	check(ParticleEffect3D.get_pooled_count(explosion) == 1, "spawn() takes from the pool")
	check(ParticleEffect3D.get_active_effect_count() >= 1, "active effect count")
	var holder := Node3D.new()
	world.add_child(holder)
	var attached: ParticleEffect3D = ParticleEffect3D.spawn_attached(load(FX + "muzzle_flash.tscn") if ResourceLoader.exists(FX + "muzzle_flash.tscn") else explosion, holder)
	check(attached != null and attached.get_parent() == holder, "spawn_attached() parents to the node")
	if spawned:
		spawned.seek(spawned.get_total_duration() + 5.0)
	await frames(15)
	check(ParticleEffect3D.get_pooled_count(explosion) >= 2, "finished effect returns to the pool (%d)" % ParticleEffect3D.get_pooled_count(explosion))

	# --- impact table --------------------------------------------------------
	var table: ParticleImpactTable = load(FX + "impact_table.tres") if ResourceLoader.exists(FX + "impact_table.tres") else null
	check(table != null, "impact table loads")
	if table:
		check(table.get_surface(floor_body) == &"metal", "surface from collider meta (%s)" % table.get_surface(floor_body))
		check(table.get_effect(&"metal") != null, "metal effect assigned")
		var hit_effect = table.spawn(world, Vector3(0, 0, 0), Vector3.UP, floor_body, Color.WHITE, 1.0, Vector3(0.5, -1, 0).normalized())
		check(hit_effect != null, "impact table spawns an effect")
		var space := world.get_world_3d().direct_space_state
		var query := PhysicsRayQueryParameters3D.create(Vector3(1, 3, 1), Vector3(1, -3, 1))
		var hit := space.intersect_ray(query)
		check(not hit.is_empty(), "ray hits the floor")
		if not hit.is_empty():
			check(table.spawn_from_hit(world, hit) != null, "spawn_from_hit")

	# --- radial damage -------------------------------------------------------
	var target := StaticBody3D.new()
	var target_script := GDScript.new()
	target_script.source_code = "extends StaticBody3D\nvar got := 0.0\nfunc take_damage(amount):\n\tgot += amount\n"
	target_script.reload()
	target.set_script(target_script)
	var target_shape := CollisionShape3D.new()
	target_shape.shape = SphereShape3D.new()
	target.add_child(target_shape)
	target.position = Vector3(2, 1, 0)
	world.add_child(target)
	await physics(3)
	var damage := RadialDamage3D.new()
	damage.damage = 100.0
	damage.radius = 6.0
	damage.inner_radius = 1.0
	world.add_child(damage)
	damage.position = Vector3(0, 1, 0)
	check(damage.get_damage_at_distance(0.5) >= 99.0, "full damage inside the inner radius")
	check(damage.get_damage_at_distance(10.0) == 0.0, "no damage outside the radius")
	damage.apply_damage()
	await physics(2)
	check(target.got > 0.0, "radial damage reaches the target (%.1f)" % target.got)

	# --- camera shake, screen flash, beam, splatter, spawner ----------------
	var shake := CameraShake3D.new()
	shake.radius = 30.0
	shake.duration = 0.3
	world.add_child(shake)
	shake.shake()
	await frames(2)
	check(shake.is_shaking(), "camera shake runs")
	shake.stop()
	check(not shake.is_shaking(), "camera shake stops")

	var flash := ScreenFlash3D.new()
	flash.radius = 30.0
	world.add_child(flash)
	check(flash.get_strength_at_camera() > 0.0, "screen flash sees the camera")
	flash.flash()
	await frames(2)

	var beam := EffectBeam3D.new()
	world.add_child(beam)
	beam.set_global_end_position(Vector3(0, 2, -4))
	beam.intensity = 1.0
	await frames(2)
	check(beam.end_position.length() > 1.0, "beam end position")

	var splatter := SplatterDecals3D.new()
	splatter.count = 4
	splatter.direction = Vector3.DOWN
	splatter.max_distance = 5.0
	world.add_child(splatter)
	splatter.position = Vector3(-2, 1, 0)
	splatter.splatter()
	await physics(3)
	await frames(2)
	check(splatter.is_inside_tree(), "splatter decals run")

	var spawner := EffectSpawner3D.new()
	spawner.scene = explosion
	spawner.count = 2
	world.add_child(spawner)
	var before := ParticleEffect3D.get_active_effect_count()
	spawner.spawn()
	await frames(2)
	check(ParticleEffect3D.get_active_effect_count() > before, "effect spawner starts effects")

	# --- wind and level setup ------------------------------------------------
	ParticleEffect3D.set_wind(Vector3(1, 0, 0))
	check(ParticleEffect3D.get_wind() == Vector3(1, 0, 0), "wind round trip")
	ParticleEffect3D.set_wind(Vector3())
	var setup := ParticleLevelSetup.new()
	setup.prewarm_effects = [explosion] as Array[PackedScene]
	setup.prewarm_count = 1
	world.add_child(setup)
	for i in 240:
		if setup.is_finished():
			break
		await process_frame
	check(setup.is_finished(), "level setup finishes")

	world.queue_free()
	await frames(3)
	ParticleEffect3D.clear_pool()
	print("RT %s %d/%d" % ["OK" if failed.is_empty() else "FAILED", passed, passed + failed.size()])
	quit(0 if failed.is_empty() else 1)
