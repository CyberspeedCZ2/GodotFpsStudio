class_name NPCController
extends CharacterBody3D

## Základní ovladač NPC pro kombinaci s NavigationAgent3D a Beehave Behavior Tree.
## Tento skript se stará čistě o fyziku pohybu a navigaci:
## - Výpočet trasy přes NavigationAgent3D
## - Gravitace a otáčení těla
## - Poskytuje čisté API pro Beehave akce (move_to, stop, is_navigation_finished)

const MOVEMENT_MODE_RANDOM_WANDER := 0
const MOVEMENT_MODE_ASSIGNED_PATH := 1
const LOCOMOTION_WALK := 0
const LOCOMOTION_RUN := 1
const COOP_ROLE_NONE := 0
const COOP_ROLE_CALLER := 1
const COOP_ROLE_HELPER := 2
const NPC_PATH_POINT_ACTIONS_META := "_fps_npc_point_actions"
const LEGACY_NPC_PATH_POINT_ACTIONS_META := "fps_npc_point_actions"

@export_group("Debug Visuals")
@export var debug_visuals: bool = false
@export var show_carrot: bool = true
@export var show_avoidance_probes: bool = true
@export var show_steer_vector: bool = true
@export var show_route_curve: bool = true
@export var show_nav_path: bool = true

@export_group("Movement")
@export_enum("Random Wander", "Assigned NPC Path") var movement_mode: int = MOVEMENT_MODE_RANDOM_WANDER
@export var npc_path: Path3D
## Join the assigned route near the placed NPC, or explicitly start at point 1.
@export_enum("Nearest Waypoint", "First Waypoint") var route_start_mode: int = 1
@export_enum("Walk", "Run") var locomotion_style: int = LOCOMOTION_WALK
@export var walk_speed: float = 2.8
@export var run_speed: float = 4.8
@export var turn_speed: float = 8.0
@export_range(0.1, 40.0, 0.1) var acceleration: float = 12.0
@export var debug_navigation: bool = false
@export_range(1.0, 45.0, 1.0) var move_alignment_angle_degrees: float = 12.0
@export_range(30.0, 150.0, 1.0) var turn_in_place_angle_degrees: float = 120.0

@export_group("Health")
@export_range(1.0, 1000.0, 1.0) var max_health: float = 100.0

@export_group("Fear")
@export var fear_enabled: bool = true
@export_range(1.0, 100.0, 1.0) var fear_threshold: float = 30.0
@export_range(0.0, 99.0, 1.0) var fear_recovery_threshold: float = 12.0
@export_range(0.0, 100.0, 1.0) var fear_gain_per_second: float = 3.0
@export_range(0.0, 99.0, 1.0) var fear_proximity_limit: float = 20.0
@export_range(0.0, 100.0, 1.0) var fear_decay_per_second: float = 8.0
@export_range(0.5, 30.0, 0.5) var fear_trigger_distance: float = 5.0
@export_range(10.0, 180.0, 1.0) var fear_vision_angle_degrees: float = 120.0
@export_range(0.0, 100.0, 1.0) var fear_from_damage: float = 55.0
@export_range(1.0, 30.0, 0.5) var fear_flee_distance: float = 8.0

@export_group("Avoidance & Physics")
@export var avoidance_enabled: bool = true
@export var push_force: float = 3.5
@export var push_mass_limit: float = 5.0
## Solo obstacle pushing
@export var can_push_obstacles: bool = true
@export var obstacle_push_force: float = 360.0
@export var obstacle_push_mass_limit: float = 65.0
@export var obstacle_push_duration_max: float = 2.5
## Fallback reverse patrol when route is permanently blocked
@export var enable_fallback_patrol: bool = true

## Co-op assistance: call nearby companions to help push heavy obstacles
@export_group("Co-op Assistance")
@export var can_call_for_coop: bool = true
@export var coop_call_radius: float = 24.0
@export var coop_push_mass_limit: float = 140.0
@export var coop_push_duration_max: float = 3.5

@export_group("Reactions")
@export var react_to_bumping: bool = true
@export_range(0.1, 8.0, 0.1) var bump_min_speed: float = 1.5
@export_range(0.0, 100.0, 1.0) var bump_fear_base: float = 8.0
@export_range(0.0, 20.0, 0.5) var bump_fear_per_speed: float = 6.0
@export_range(0.1, 5.0, 0.1) var bump_cooldown: float = 1.0
@export var react_to_flashlight: bool = true
@export_range(0.0, 100.0, 0.5) var flashlight_fear_per_second: float = 0.5
@export_range(0.0, 20.0, 0.5) var flashlight_fear_limit: float = 4.0

@export_group("Overhead Status")
@export var show_health_overhead: bool = true
@export var show_fear_overhead: bool = true

@export_group("Idle Look")
@export_range(0.0, 120.0, 1.0) var idle_look_angle_degrees: float = 55.0
@export_range(0.2, 10.0, 0.1) var idle_look_interval_min: float = 1.2
@export_range(0.2, 10.0, 0.1) var idle_look_interval_max: float = 2.8

@onready var nav_agent: NavigationAgent3D = $NavigationAgent3D
@onready var health_label: Label3D = get_node_or_null("HealthLabel")
@onready var fear_label: Label3D = get_node_or_null("FearLabel")
@onready var bump_sensor: Area3D = get_node_or_null("BumpSensor")

var current_speed: float = 0.0
var is_moving: bool = false
var is_turning_to_move: bool = false
var _turning_in_place: bool = false
var _requested_navigation_target := Vector3.ZERO
var _navigation_target_is_route: bool = false
var current_health: float = 100.0
var fear_level: float = 0.0
var is_dead: bool = false
var _fear_response_active: bool = false
var _fear_threat: Node3D = null
var _avoidance_velocity_sent: bool = false
var _avoidance_side: float = 0.0 # -1.0 = right, +1.0 = left
var _avoidance_clear_time: float = 0.0
var _current_avoidance_angle: float = 0.0

var _is_detouring: bool = false
var _is_path_blocked: bool = false
var _stuck_timer: float = 0.0
var _stuck_recovery_timer: float = 0.0
var _stuck_recovery_dir := Vector3.ZERO
var _forward_blocked: bool = false
var _blocked_retry_timer: float = 0.0
var _stuck_last_pos := Vector3.ZERO
var _stuck_sample_timer: float = 0.0
var _is_pushing_obstacle: bool = false
var _push_attempt_timer: float = 0.0
var _current_push_target_body: RigidBody3D = null
var _is_reversing_patrol: bool = false
var _blocked_waypoint_index: int = -1

var _coop_role: int = COOP_ROLE_NONE
var _coop_partner: Node3D = null
var _coop_target_obstacle: RigidBody3D = null
var _coop_push_timer: float = 0.0
var _saved_pre_coop_waypoint: int = -1
var _saved_pre_coop_target := Vector3.ZERO
var _saved_pre_coop_speed: float = 0.0

var _debug_lines_mesh: MeshInstance3D = null
var _carrot_node: Node3D = null
var _last_carrot_world_pos := Vector3.ZERO
var _debug_probe_records: Array = []
var _debug_steer_dir := Vector3.ZERO
var _sphere_probe_shape: SphereShape3D = null
var _debug_sphere_instance: MeshInstance3D = null
var _debug_sphere_left: MeshInstance3D = null
var _debug_sphere_right: MeshInstance3D = null
var _aux_spheres_active: bool = false
var _last_sphere_probe_pos := Vector3.ZERO
var _last_sphere_probe_color := Color(0.2, 0.95, 0.4, 0.35)
var _last_left_probe_pos := Vector3.ZERO
var _last_left_probe_color := Color(0.2, 0.95, 0.4, 0.35)
var _last_right_probe_pos := Vector3.ZERO
var _last_right_probe_color := Color(1.0, 0.15, 0.15, 0.5)
var _baked_detour_obstacles: Array = []
var gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity", 9.8)

var _active_route_locomotion: int = LOCOMOTION_WALK
var _npc_path_waypoint_index: int = 0
var _npc_path_direction: int = 1
var _npc_path_waypoint_arrived: bool = false
var _idle_look_center_yaw: float = 0.0
var _idle_look_target_yaw: float = 0.0
var _idle_look_timer: float = 0.0
var _bump_touching_player: CharacterBody3D = null
var _bump_cooldown_remaining: float = 0.0
var _flashlight_exposure_active: bool = false
var _flashlight_exposure_fear: float = 0.0
var _player_flashlight: Node3D = null


func _ready() -> void:
	add_to_group("fps_npc")
	current_speed = _get_locomotion_speed()
	current_health = maxf(max_health, 1.0)
	_reset_npc_path_patrol()
	if bump_sensor != null:
		bump_sensor.body_entered.connect(_on_bump_sensor_body_entered)
		bump_sensor.body_exited.connect(_on_bump_sensor_body_exited)
	_update_overhead_status()
	_setup_debug_visuals()

	if nav_agent != null:
		nav_agent.avoidance_enabled = avoidance_enabled
		nav_agent.avoidance_mask = 1 | 2 # Layer 1 (Obstacles), Layer 2 (NPCs)
		nav_agent.max_speed = maxf(run_speed, walk_speed) * 1.5
		nav_agent.avoidance_priority = 0.5 # Yield priority to obstacles so agent steers around them
		nav_agent.time_horizon_agents = 1.5
		nav_agent.time_horizon_obstacles = 1.5
		nav_agent.radius = 0.45
		if not nav_agent.velocity_computed.is_connected(_on_safe_velocity_computed):
			nav_agent.velocity_computed.connect(_on_safe_velocity_computed)



func _process(delta: float) -> void:
	_update_debug_visuals(delta)


func _should_show_debug_visuals() -> bool:
	if debug_visuals:
		return true
	var p := get_player()
	if p:
		var dev = p.get_node_or_null("DevTools")
		if dev:
			if ("is_ai_debug" in dev and dev.is_ai_debug) or ("is_dev_mode" in dev and dev.is_dev_mode) or ("is_godmode" in dev and dev.is_godmode):
				return true
		var hp = p.get_node_or_null("HealthSystem")
		if hp and "is_godmode" in hp and hp.is_godmode:
			return true
	return false


func _setup_debug_visuals() -> void:
	# 1. ImmediateMesh pro vykreslování paprsků a vodicích čar
	_debug_lines_mesh = MeshInstance3D.new()
	_debug_lines_mesh.name = "AIDebugRays"
	_debug_lines_mesh.top_level = true
	var imm := ImmediateMesh.new()
	_debug_lines_mesh.mesh = imm

	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.no_depth_test = true # Paprsky jsou vidět i skrz překážky a zdi
	mat.render_priority = 10
	_debug_lines_mesh.material_override = mat
	add_child(_debug_lines_mesh)

	# 2. 3D Model mrkvičky (Carrot Target)
	_carrot_node = Node3D.new()
	_carrot_node.name = "AICarrotMarker"
	_carrot_node.top_level = true

	# Oranžové tělo mrkve (kužel špičkou dolů)
	var carrot_body := MeshInstance3D.new()
	carrot_body.name = "CarrotCone"
	var body_mesh := CylinderMesh.new()
	body_mesh.top_radius = 0.13
	body_mesh.bottom_radius = 0.01
	body_mesh.height = 0.42
	carrot_body.mesh = body_mesh
	carrot_body.rotation.x = PI

	var body_mat := StandardMaterial3D.new()
	body_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	body_mat.albedo_color = Color(1.0, 0.48, 0.05)
	body_mat.no_depth_test = true
	body_mat.render_priority = 11
	carrot_body.material_override = body_mat
	_carrot_node.add_child(carrot_body)

	# Zelená nať mrkve
	var carrot_greens := MeshInstance3D.new()
	carrot_greens.name = "CarrotGreens"
	var greens_mesh := CylinderMesh.new()
	greens_mesh.top_radius = 0.09
	greens_mesh.bottom_radius = 0.03
	greens_mesh.height = 0.16
	carrot_greens.mesh = greens_mesh
	carrot_greens.position.y = 0.28

	var greens_mat := StandardMaterial3D.new()
	greens_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	greens_mat.albedo_color = Color(0.2, 0.9, 0.2)
	greens_mat.no_depth_test = true
	greens_mat.render_priority = 11
	carrot_greens.material_override = greens_mat
	_carrot_node.add_child(carrot_greens)

	# 3D textový štítek nad mrkví
	var carrot_label := Label3D.new()
	carrot_label.name = "CarrotLabel"
	carrot_label.text = "🥕 Carrot Target"
	carrot_label.position.y = 0.46
	carrot_label.billboard = BaseMaterial3D.BILLBOARD_FIXED_Y
	carrot_label.no_depth_test = true
	carrot_label.font_size = 20
	carrot_label.outline_size = 6
	carrot_label.modulate = Color(1.0, 0.85, 0.2)
	carrot_label.render_priority = 12
	_carrot_node.add_child(carrot_label)

	# Full spherical billboard pro mrkev
	carrot_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	carrot_label.font_size = 24

	_carrot_node.visible = false
	add_child(_carrot_node)

	# 3. 3D stavový štítek přímo nad hlavou NPC
	var state_lbl := Label3D.new()
	state_lbl.name = "AIOverheadStateLabel"
	state_lbl.text = "🥕 PATROLA"
	state_lbl.position = Vector3(0.0, 2.35, 0.0)
	state_lbl.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	state_lbl.no_depth_test = true
	state_lbl.render_priority = 25
	state_lbl.font_size = 28
	state_lbl.outline_size = 8
	state_lbl.outline_modulate = Color(0.0, 0.0, 0.0, 0.95)
	state_lbl.modulate = Color(1.0, 0.85, 0.2)
	state_lbl.double_sided = true
	state_lbl.visible = false
	add_child(state_lbl)

	# 4. 3D Prusvitne koule senzoru: Hlavni stredova (Center), Leva (Left) a Prava (Right)
	_debug_sphere_instance = _create_debug_sphere_helper("AISphereProbeCenter", Color(0.2, 0.95, 0.4, 0.35))
	_debug_sphere_left = _create_debug_sphere_helper("AISphereProbeLeft", Color(0.2, 0.95, 0.4, 0.35))
	_debug_sphere_right = _create_debug_sphere_helper("AISphereProbeRight", Color(1.0, 0.15, 0.15, 0.5))


func _create_debug_sphere_helper(node_name: String, col: Color) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.name = node_name
	mi.top_level = true
	var sph := SphereMesh.new()
	sph.radius = 0.32
	sph.height = 0.64
	mi.mesh = sph
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.no_depth_test = true
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.render_priority = 9
	mat.albedo_color = col
	mi.material_override = mat
	add_child(mi)
	return mi


func _draw_debug_ring(debug_vertices: Array[Vector3], debug_colors: Array[Color], center: Vector3, normal: Vector3, radius: float, ring_color: Color, segments: int = 12) -> void:
	var tangent := Vector3.UP.cross(normal).normalized()
	if tangent.length_squared() < 0.01:
		tangent = Vector3.RIGHT.cross(normal).normalized()
	var bitangent := normal.cross(tangent).normalized()

	for i in range(segments):
		var theta1 := (float(i) / segments) * TAU
		var theta2 := (float(i + 1) / segments) * TAU
		var p1 := center + (tangent * cos(theta1) + bitangent * sin(theta1)) * radius
		var p2 := center + (tangent * cos(theta2) + bitangent * sin(theta2)) * radius
		debug_colors.append(ring_color)
		debug_vertices.append(p1)
		debug_colors.append(ring_color)
		debug_vertices.append(p2)


func _record_debug_probe(from: Vector3, to: Vector3, hit: Dictionary, ptype: String) -> void:
	var hit_pos := Vector3.ZERO
	var is_hit := not hit.is_empty()
	if is_hit:
		hit_pos = hit.get("position", to)
	_debug_probe_records.append({
		"from": from,
		"to": to,
		"hit": is_hit,
		"hit_pos": hit_pos,
		"type": ptype,
		"is_sphere": false
	})


func _record_debug_sphere(res: Dictionary, ptype: String) -> void:
	_debug_probe_records.append({
		"from": res.get("from", Vector3.ZERO),
		"to": res.get("to", Vector3.ZERO),
		"hit": res.get("hit", false),
		"hit_pos": res.get("hit_pos", Vector3.ZERO),
		"safe_dist": res.get("safe_distance", 0.0),
		"radius": res.get("radius", 0.42),
		"type": ptype,
		"is_sphere": true
	})


func _update_debug_visuals(delta: float) -> void:
	if not _debug_lines_mesh or not _carrot_node:
		return

	var should_show := _should_show_debug_visuals()
	var state_lbl := get_node_or_null("AIOverheadStateLabel") as Label3D
	if state_lbl:
		state_lbl.visible = should_show

	if not should_show:
		_debug_lines_mesh.visible = false
		_carrot_node.visible = false
		if _debug_sphere_instance:
			_debug_sphere_instance.visible = false
		return

	_debug_lines_mesh.visible = true
	if _debug_sphere_instance:
		if should_show and show_avoidance_probes and is_moving and _last_sphere_probe_pos.length_squared() > 0.01:
			_debug_sphere_instance.visible = true
			_debug_sphere_instance.global_position = _last_sphere_probe_pos
			var smat := _debug_sphere_instance.material_override as StandardMaterial3D
			if smat:
				smat.albedo_color = _last_sphere_probe_color
			var sp_mesh := _debug_sphere_instance.mesh as SphereMesh
			if sp_mesh:
				var trg_r: float = 0.32 if _aux_spheres_active else 0.18
				sp_mesh.radius = trg_r
				sp_mesh.height = trg_r * 2.0
		else:
			_debug_sphere_instance.visible = false

	if _debug_sphere_left:
		if should_show and show_avoidance_probes and is_moving and _aux_spheres_active and _last_left_probe_pos.length_squared() > 0.01:
			_debug_sphere_left.visible = true
			_debug_sphere_left.global_position = _last_left_probe_pos
			var smat := _debug_sphere_left.material_override as StandardMaterial3D
			if smat:
				smat.albedo_color = _last_left_probe_color
		else:
			_debug_sphere_left.visible = false

	if _debug_sphere_right:
		if should_show and show_avoidance_probes and is_moving and _aux_spheres_active and _last_right_probe_pos.length_squared() > 0.01:
			_debug_sphere_right.visible = true
			_debug_sphere_right.global_position = _last_right_probe_pos
			var smat := _debug_sphere_right.material_override as StandardMaterial3D
			if smat:
				smat.albedo_color = _last_right_probe_color
		else:
			_debug_sphere_right.visible = false

	# 1. Aktualizace pozice a animace 3D mrkve
	var has_carrot := show_carrot and uses_npc_path() and (_navigation_target_is_route or _is_detouring or _stuck_recovery_timer > 0.0 or _is_path_blocked) and _last_carrot_world_pos.length_squared() > 0.01 and is_moving
	if has_carrot:
		_carrot_node.visible = true
		var bob := sin(Time.get_ticks_msec() * 0.005) * 0.08
		_carrot_node.global_position = _last_carrot_world_pos + Vector3(0.0, 0.45 + bob, 0.0)
		_carrot_node.rotate_y(delta * 2.8)
	else:
		_carrot_node.visible = false

		# 2. Vykreslení čar v ImmediateMesh
	var cur_state_text := "🥕 PATROLA"
	var cur_state_color := Color(1.0, 0.85, 0.2)

	if _is_path_blocked:
		cur_state_text = "🛑 CESTA ZABLOKOVÁNA"
		cur_state_color = Color(1.0, 0.2, 0.2)
	elif _coop_role == COOP_ROLE_CALLER and _is_pushing_obstacle:
		cur_state_text = "🤝 SPOLEČNÉ TLAČENÍ"
		cur_state_color = Color(1.0, 0.85, 0.1)
	elif _coop_role == COOP_ROLE_CALLER:
		cur_state_text = "📢 ČEKÁ NA POSILU"
		cur_state_color = Color(1.0, 0.7, 0.2)
	elif _coop_role == COOP_ROLE_HELPER and _is_pushing_obstacle:
		cur_state_text = "🤝 SPOLEČNÉ TLAČENÍ"
		cur_state_color = Color(1.0, 0.85, 0.1)
	elif _coop_role == COOP_ROLE_HELPER:
		cur_state_text = "🏃 BĚŽÍ NA POMOC"
		cur_state_color = Color(0.2, 0.9, 1.0)
	elif _stuck_recovery_timer > 0.0:
		cur_state_text = "↩️ UVOLNĚNÍ (Vycouvání)"
		cur_state_color = Color(1.0, 0.95, 0.2)
	elif _is_pushing_obstacle:
		cur_state_text = "📦 TLAČÍ PŘEKÁŽKU"
		cur_state_color = Color(1.0, 0.65, 0.1)
	elif _is_reversing_patrol:
		cur_state_text = "🔄 REVERZNÍ PATROLA"
		cur_state_color = Color(0.9, 0.45, 1.0)
	elif _is_detouring:
		cur_state_text = "↪️ OBJÍŽĎKA (NavMesh Detour)"
		cur_state_color = Color(0.2, 0.9, 1.0)

	if state_lbl:
		state_lbl.text = cur_state_text
		state_lbl.modulate = cur_state_color

	var carrot_label := _carrot_node.get_node_or_null("CarrotLabel") as Label3D
	if carrot_label:
		carrot_label.text = cur_state_text
		carrot_label.modulate = cur_state_color

	var imm: ImmediateMesh = _debug_lines_mesh.mesh as ImmediateMesh
	if not imm:
		return

	imm.clear_surfaces()

	var debug_vertices: Array[Vector3] = []
	var debug_colors: Array[Color] = []

	var origin := global_position + Vector3(0.0, 0.45, 0.0)

	# A) Vodicí linie ("prut s vlascem") k mrkvi
	if has_carrot:
		var carrot_top := _carrot_node.global_position
		debug_colors.append(Color(1.0, 0.72, 0.1, 0.85))
		debug_vertices.append(origin + Vector3(0.0, 0.4, 0.0))
		debug_colors.append(Color(1.0, 0.48, 0.05, 0.95))
		debug_vertices.append(carrot_top)

	# B) Prostorové koule a paprsky senzorů (Avoidance Sphere Probes)
	if show_avoidance_probes:
		for rec in _debug_probe_records:
			var from_pt: Vector3 = rec["from"]
			var to_pt: Vector3 = rec["to"]
			var is_hit: bool = rec["hit"]
			var hit_pt: Vector3 = rec["hit_pos"]
			var ptype: String = rec["type"]
			var is_sphere: bool = rec.get("is_sphere", false)
			var s_rad: float = rec.get("radius", 0.42)
			var probe_dir: Vector3 = (to_pt - from_pt).normalized()

			if is_hit:
				# Čára od senzoru k překážce: jasně červená
				debug_colors.append(Color(1.0, 0.2, 0.2, 0.95))
				debug_vertices.append(from_pt)
				debug_colors.append(Color(1.0, 0.05, 0.05, 1.0))
				debug_vertices.append(hit_pt)

				# 3D křížek v bodě dopadu
				var cross_size := 0.15
				var ccol := Color(1.0, 0.1, 0.1, 1.0)
				# X
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt - Vector3(cross_size, 0.0, 0.0))
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt + Vector3(cross_size, 0.0, 0.0))
				# Y
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt - Vector3(0.0, cross_size, 0.0))
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt + Vector3(0.0, cross_size, 0.0))
				# Z
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt - Vector3(0.0, 0.0, cross_size))
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt + Vector3(0.0, 0.0, cross_size))

				if is_sphere:
					_draw_debug_ring(debug_vertices, debug_colors, hit_pt, probe_dir, s_rad, Color(1.0, 0.15, 0.15, 0.85), 12)
					_draw_debug_ring(debug_vertices, debug_colors, hit_pt, Vector3.UP, s_rad, Color(1.0, 0.25, 0.25, 0.65), 12)
			else:
				# Volný senzor: zelená / azurová
				var ray_col := Color(0.2, 1.0, 0.35, 0.8)
				if ptype == "whisker" or ptype == "left" or ptype == "right":
					ray_col = Color(0.85, 1.0, 0.2, 0.75)
				elif ptype == "gap_left" or ptype == "gap_right":
					ray_col = Color(0.2, 0.8, 1.0, 0.7)

				debug_colors.append(ray_col)
				debug_vertices.append(from_pt)
				debug_colors.append(ray_col)
				debug_vertices.append(to_pt)

				if is_sphere:
					_draw_debug_ring(debug_vertices, debug_colors, to_pt, probe_dir, s_rad, ray_col, 12)
					_draw_debug_ring(debug_vertices, debug_colors, to_pt, Vector3.UP, s_rad, Color(ray_col.r, ray_col.g, ray_col.b, 0.45), 12)

	# C) Vektor aktuálního směru pohybu / řízení (azurová šipka)
	if show_steer_vector and is_moving and _debug_steer_dir.length_squared() > 0.01:
		var steer_norm := _debug_steer_dir.normalized()
		var steer_end := origin + steer_norm * 1.6
		var scol := Color(0.0, 0.9, 1.0, 1.0)
		debug_colors.append(scol)
		debug_vertices.append(origin)
		debug_colors.append(scol)
		debug_vertices.append(steer_end)

		# Hrot šipky
		var side_arrow := Vector3(-steer_norm.z, 0.0, steer_norm.x).normalized() * 0.16
		var back_arrow := -steer_norm * 0.22
		debug_colors.append(scol)
		debug_vertices.append(steer_end)
		debug_colors.append(scol)
		debug_vertices.append(steer_end + back_arrow + side_arrow)

		debug_colors.append(scol)
		debug_vertices.append(steer_end)
		debug_colors.append(scol)
		debug_vertices.append(steer_end + back_arrow - side_arrow)

		# D) Celá patrol trasa (Path3D Curve3D s waypoints)
	if show_route_curve and npc_path and npc_path.curve:
		var curve: Curve3D = npc_path.curve
		var pt_count := curve.get_point_count()
		var baked_len := curve.get_baked_length()

		# Plynulá linie křivky po zemi
		if baked_len > 0.1:
			var step := 0.35
			var prev_sample: Vector3 = npc_path.to_global(curve.sample_baked(0.0)) + Vector3(0.0, 0.06, 0.0)
			var d: float = step
			var path_col := Color(0.25, 0.7, 1.0, 0.7)
			while d <= baked_len:
				var next_sample: Vector3 = npc_path.to_global(curve.sample_baked(d)) + Vector3(0.0, 0.06, 0.0)
				debug_colors.append(path_col)
				debug_vertices.append(prev_sample)
				debug_colors.append(path_col)
				debug_vertices.append(next_sample)
				prev_sample = next_sample
				d += step
			if curve.is_closed() or d > baked_len:
				var end_sample: Vector3 = npc_path.to_global(curve.sample_baked(baked_len)) + Vector3(0.0, 0.06, 0.0)
				debug_colors.append(path_col)
				debug_vertices.append(prev_sample)
				debug_colors.append(path_col)
				debug_vertices.append(end_sample)

		# Kroužky a piny na jednotlivých waypointech
		for idx in range(pt_count):
			var wp_world: Vector3 = npc_path.to_global(curve.get_point_position(idx)) + Vector3(0.0, 0.07, 0.0)
			var is_current := (idx == _npc_path_waypoint_index)

			var wp_col := Color(0.3, 0.8, 1.0, 0.85)
			var action_dict := get_npc_path_waypoint_action(idx)
			var act := str(action_dict.get("action", "move")).to_lower()
			if act == "run":
				wp_col = Color(1.0, 0.5, 0.1, 0.9)
			elif act == "wait":
				wp_col = Color(0.2, 0.9, 0.35, 0.9)
			elif act == "look_at":
				wp_col = Color(0.85, 0.4, 1.0, 0.9)

			if is_current:
				wp_col = Color(1.0, 0.95, 0.2, 1.0)

			var r: float = 0.35 if is_current else 0.22
			var segments := 8
			for s in range(segments):
				var a1 := (float(s) / segments) * TAU
				var a2 := (float(s + 1) / segments) * TAU
				var p1 := wp_world + Vector3(cos(a1) * r, 0.0, sin(a1) * r)
				var p2 := wp_world + Vector3(cos(a2) * r, 0.0, sin(a2) * r)
				debug_colors.append(wp_col)
				debug_vertices.append(p1)
				debug_colors.append(wp_col)
				debug_vertices.append(p2)

			var pin_h: float = 0.7 if is_current else 0.35
			debug_colors.append(wp_col)
			debug_vertices.append(wp_world)
			debug_colors.append(wp_col)
			debug_vertices.append(wp_world + Vector3(0.0, pin_h, 0.0))

	# E) Živá cesta NavigationAgent3D (reálná trajektorie po NavMeshi kolem překážek)
	if show_nav_path and nav_agent and is_moving:
		var nav_pts: PackedVector3Array = nav_agent.get_current_navigation_path()
		if nav_pts.size() >= 2:
			var nav_col := Color(1.0, 0.85, 0.15, 0.95)
			var prev_pt := global_position + Vector3(0.0, 0.09, 0.0)
			for pt_idx in range(nav_agent.get_current_navigation_path_index(), nav_pts.size()):
				var next_pt := nav_pts[pt_idx] + Vector3(0.0, 0.09, 0.0)
				debug_colors.append(nav_col)
				debug_vertices.append(prev_pt)
				debug_colors.append(nav_col)
				debug_vertices.append(next_pt)

				var d_size := 0.1
				debug_colors.append(nav_col)
				debug_vertices.append(next_pt - Vector3(d_size, 0.0, 0.0))
				debug_colors.append(nav_col)
				debug_vertices.append(next_pt + Vector3(d_size, 0.0, 0.0))

				debug_colors.append(nav_col)
				debug_vertices.append(next_pt - Vector3(0.0, 0.0, d_size))
				debug_colors.append(nav_col)
				debug_vertices.append(next_pt + Vector3(0.0, 0.0, d_size))

				prev_pt = next_pt

	if debug_vertices.is_empty():
		return

	imm.surface_begin(Mesh.PRIMITIVE_LINES)
	for i in debug_vertices.size():
		imm.surface_set_color(debug_colors[i])
		imm.surface_add_vertex(debug_vertices[i])
	imm.surface_end()


func _physics_process(delta: float) -> void:
	if is_dead:
		return
	_update_fear(delta)
	_update_flashlight_exposure(delta)
	_update_bump_reaction(delta)
	_update_overhead_status()

	if not is_on_floor():
		velocity.y -= gravity * delta

	_avoidance_velocity_sent = false
	if is_moving and nav_agent:
		_process_navigation(delta)
	else:
		var horizontal_velocity := Vector3(velocity.x, 0.0, velocity.z)
		horizontal_velocity = horizontal_velocity.move_toward(Vector3.ZERO, maxf(acceleration, 0.1) * delta)
		velocity.x = horizontal_velocity.x
		velocity.z = horizontal_velocity.z

	if not _avoidance_velocity_sent:
		move_and_slide()
		_push_light_debris()


func _rotate_yaw_toward(current_yaw: float, target_yaw: float, max_delta: float) -> float:
	var wrapped := wrapf(current_yaw, -PI, PI)
	var next_yaw := rotate_toward(wrapped, target_yaw, max_delta)
	return wrapf(next_yaw, -PI, PI)


func _process_navigation(delta: float) -> void:
	# 1. Handle BLOCKED state
	if _is_path_blocked:
		var stop_v := Vector3(velocity.x, 0.0, velocity.z).move_toward(Vector3.ZERO, maxf(acceleration, 0.1) * delta)
		velocity.x = stop_v.x
		velocity.z = stop_v.z
		if nav_agent.avoidance_enabled:
			nav_agent.set_velocity(Vector3.ZERO)
			_avoidance_velocity_sent = true
		_blocked_retry_timer += delta
		if _blocked_retry_timer >= 1.0:
			_blocked_retry_timer = 0.0
			if nav_agent.is_target_reachable():
				_is_path_blocked = false
				_is_detouring = true
			elif enable_fallback_patrol and uses_npc_path() and has_valid_npc_path():
				_trigger_fallback_reverse_patrol()
		return

	# 2. Handle STUCK_RECOVERY (backtrack step away from corner/wall friction)
	if _stuck_recovery_timer > 0.0:
		_stuck_recovery_timer -= delta
		var back_v := _stuck_recovery_dir.normalized() * (walk_speed * 0.75)
		velocity.x = move_toward(velocity.x, back_v.x, maxf(acceleration, 0.1) * delta)
		velocity.z = move_toward(velocity.z, back_v.z, maxf(acceleration, 0.1) * delta)
		move_and_slide()
		_push_light_debris()
		if _stuck_recovery_timer <= 0.0:
			_is_detouring = true
		return

	# Detour se ukonci POUZE tehdy, kdyz jsme prekazku skutecne obesli a dostali se dale po trase
	if _is_detouring and uses_npc_path() and has_valid_npc_path():
		var route_curve: Curve3D = npc_path.curve
		var cur_off := route_curve.get_closest_offset(npc_path.to_local(global_position))
		var tgt_off := route_curve.get_closest_offset(npc_path.to_local(_requested_navigation_target))
		var progress_past := (cur_off - tgt_off) * _npc_path_direction
		if progress_past > 1.2 and not _forward_blocked:
			var curve_pt := npc_path.to_global(route_curve.sample_baked(cur_off))
			if global_position.distance_to(curve_pt) < 1.0:
				_is_detouring = false

	var current_flat := Vector2(global_position.x, global_position.z)
	var target_flat := Vector2(nav_agent.target_position.x, nav_agent.target_position.z)
	var dist_to_target := current_flat.distance_to(target_flat)

	# A finished partial path is not arrival at the requested destination.
	# Interior pass-through corner points continue without stopping momentum.
	if is_at_destination():
		if _is_detouring:
			_is_detouring = false
		if not can_smoothly_advance_current_npc_path_waypoint():
			stop()
			return

	var dir := Vector3.ZERO
	if uses_npc_path() and has_valid_npc_path() and _navigation_target_is_route and not _is_detouring:
		var route_curve: Curve3D = npc_path.curve
		var local_pos := npc_path.to_local(global_position)
		var current_offset := route_curve.get_closest_offset(local_pos)
		var target_local := npc_path.to_local(_requested_navigation_target)
		var target_offset := route_curve.get_closest_offset(target_local)

		# Lookahead ("mrkev") na krivce pro dokonale plynuly prujezd oblouky a zatackami
		var lookahead_dist: float = 1.4
		var carrot_offset: float = current_offset + (_npc_path_direction * lookahead_dist)

		if _should_brake_for_navigation_target():
			if _npc_path_direction > 0:
				carrot_offset = minf(carrot_offset, target_offset)
			else:
				carrot_offset = maxf(carrot_offset, target_offset)
		else:
			# Pruchozi bod: mrkev predbiha jen tesne za aktualni bod (max 0.6m), aby neutekla dopredu
			if _npc_path_direction > 0:
				carrot_offset = minf(carrot_offset, target_offset + 0.6)
			else:
				carrot_offset = maxf(carrot_offset, target_offset - 0.6)

		if route_curve.is_closed():
			var total_len := route_curve.get_baked_length()
			if total_len > 0.01:
				carrot_offset = fposmod(carrot_offset, total_len)

		var carrot_local := route_curve.sample_baked(carrot_offset)
		var carrot_world := npc_path.to_global(carrot_local)
		_last_carrot_world_pos = carrot_world
		dir = carrot_world - global_position
		dir.y = 0.0
	else:
		var next_pos := nav_agent.get_next_path_position()
		_last_carrot_world_pos = next_pos
		dir = next_pos - global_position
		dir.y = 0.0

	if dir.length() < 0.05:
		is_turning_to_move = false
		_turning_in_place = false
		var stopping_velocity := Vector3(velocity.x, 0.0, velocity.z)
		stopping_velocity = stopping_velocity.move_toward(Vector3.ZERO, maxf(acceleration, 0.1) * delta)
		velocity.x = stopping_velocity.x
		velocity.z = stopping_velocity.z
		return

	var distance_to_corner := dir.length()
	dir = dir.normalized()
	dir = _probe_obstacle_avoidance(dir, delta)
	var target_yaw := atan2(-dir.x, -dir.z)
	var current_yaw := wrapf(global_rotation.y, -PI, PI)
	var requested_turn := absf(wrapf(target_yaw - current_yaw, -PI, PI))
	var alignment_angle := deg_to_rad(move_alignment_angle_degrees)
	if _turning_in_place:
		if requested_turn <= alignment_angle:
			_turning_in_place = false
	elif requested_turn >= deg_to_rad(turn_in_place_angle_degrees):
		_turning_in_place = true

	if _turning_in_place:
		var pivot_velocity := Vector3(velocity.x, 0.0, velocity.z)
		pivot_velocity = pivot_velocity.move_toward(Vector3.ZERO, maxf(acceleration, 0.1) * delta)
		is_turning_to_move = true
		if pivot_velocity.length() > 0.05:
			velocity.x = pivot_velocity.x
			velocity.z = pivot_velocity.z
		else:
			velocity.x = 0.0
			velocity.z = 0.0
		if nav_agent.avoidance_enabled:
			nav_agent.set_velocity(Vector3.ZERO)
			_avoidance_velocity_sent = true
		global_rotation.y = _rotate_yaw_toward(global_rotation.y, target_yaw, maxf(turn_speed, 0.01) * delta)
		return

	# Blend speed through smaller turns so ordinary route corners remain smooth.
	var target_speed := current_speed * lerpf(1.0, 0.65, clampf(requested_turn / PI, 0.0, 1.0))
	if _should_brake_for_navigation_target():
		var arrival_radius := maxf(nav_agent.target_desired_distance, 0.1)
		var distance_to_stop: float = dist_to_target
		if uses_npc_path() and has_valid_npc_path() and _navigation_target_is_route:
			var route_curve: Curve3D = npc_path.curve
			var cur_off: float = route_curve.get_closest_offset(npc_path.to_local(global_position))
			var tgt_off: float = route_curve.get_closest_offset(npc_path.to_local(_requested_navigation_target))
			distance_to_stop = absf(cur_off - tgt_off)

		var distance_before_arrival := maxf(distance_to_stop - arrival_radius, 0.0)
		# Prirozeny rozbeh brzdneho profilu (decelerace v delsim useku pred cilem)
		var decel_distance := clampf(current_speed * 0.85, 1.6, 4.2)
		var ratio := clampf(distance_before_arrival / decel_distance, 0.0, 1.0)
		var smooth_factor := smoothstep(0.0, 1.0, ratio)
		var min_crawl_speed := clampf(current_speed * 0.25, 0.35, 1.0)
		var braking_speed := lerpf(min_crawl_speed, current_speed, smooth_factor)
		if not is_at_destination():
			braking_speed = maxf(braking_speed, minf(current_speed, 0.25))
		target_speed = minf(target_speed, braking_speed)

	# Only brake for sharp corners (> 65°) or when approaching a final stopping destination
	if not _should_brake_for_navigation_target() and requested_turn > deg_to_rad(65.0):
		var corner_speed: float = maxf(sqrt(2.0 * maxf(acceleration, 0.1) * distance_to_corner), current_speed * 0.5)
		target_speed = minf(target_speed, corner_speed)

	var desired_velocity := dir * target_speed
	var horizontal_velocity := Vector3(velocity.x, 0.0, velocity.z)
	horizontal_velocity = horizontal_velocity.move_toward(desired_velocity, maxf(acceleration, 0.1) * delta)
	if nav_agent.avoidance_enabled:
		nav_agent.set_velocity(horizontal_velocity)
		_avoidance_velocity_sent = true
	else:
		velocity.x = horizontal_velocity.x
		velocity.z = horizontal_velocity.z
	# Hladke otaceni celeho tela i pohledu primo do zamereneho smeru (target_yaw), nikoliv do zdi
	global_rotation.y = _rotate_yaw_toward(global_rotation.y, target_yaw, maxf(turn_speed, 0.01) * delta)
	var remaining_yaw := absf(wrapf(target_yaw - global_rotation.y, -PI, PI))
	is_turning_to_move = remaining_yaw > deg_to_rad(move_alignment_angle_degrees)

	# Fyzikalni detekce uviznuti (skutecny posun v 3D prostoru)
	if not _turning_in_place and _stuck_recovery_timer <= 0.0 and not _is_path_blocked:
		_stuck_sample_timer += delta
		if _stuck_sample_timer >= 0.25:
			_stuck_sample_timer = 0.0
			var moved_dist := global_position.distance_to(_stuck_last_pos)
			_stuck_last_pos = global_position
			if target_speed > 0.4 and moved_dist < 0.08:
				_stuck_timer += 0.25
			else:
				_stuck_timer = maxf(0.0, _stuck_timer - 0.25)

		if _stuck_timer >= 0.5 or (_forward_blocked and _stuck_timer >= 0.25):
			_trigger_unstuck_and_detour()
			return


func _trigger_fallback_reverse_patrol() -> void:
	if not uses_npc_path() or not has_valid_npc_path():
		return
	_is_path_blocked = false
	_is_detouring = false
	_is_pushing_obstacle = false
	_is_reversing_patrol = true
	_blocked_waypoint_index = _npc_path_waypoint_index
	update_debug_state("Zaval: Cesta zablokovana, obracim patrolu!")

	_npc_path_direction = -_npc_path_direction
	var route_curve: Curve3D = npc_path.curve
	var point_count := route_curve.get_point_count()
	if route_curve.is_closed():
		_npc_path_waypoint_index = (_npc_path_waypoint_index + _npc_path_direction + point_count) % point_count
	else:
		_npc_path_waypoint_index = clampi(_npc_path_waypoint_index + _npc_path_direction, 0, point_count - 1)

	var new_target := npc_path.to_global(route_curve.get_point_position(_npc_path_waypoint_index))
	move_to_position(new_target)


func _trigger_unstuck_and_detour() -> void:
	_stuck_timer = 0.0
	if not nav_agent.is_target_reachable():
		if uses_npc_path() and has_valid_npc_path():
			var next_wp := (_npc_path_waypoint_index + _npc_path_direction) % npc_path.curve.point_count
			if next_wp < 0:
				next_wp = npc_path.curve.point_count - 1
			var next_target := npc_path.to_global(npc_path.curve.get_point_position(next_wp))
			nav_agent.target_position = next_target
			if nav_agent.is_target_reachable():
				_npc_path_waypoint_index = next_wp
			else:
				_is_path_blocked = true
				if enable_fallback_patrol:
					_trigger_fallback_reverse_patrol()
				return
		else:
			_is_path_blocked = true
			if enable_fallback_patrol:
				_trigger_fallback_reverse_patrol()
			return

	_avoidance_side = 0.0
	_current_avoidance_angle = 0.0
	_is_detouring = true
	_stuck_recovery_dir = global_transform.basis.z # Krok VZAD od prekazky
	_stuck_recovery_dir.y = 0.0
	if _stuck_recovery_dir.length_squared() < 0.01:
		_stuck_recovery_dir = Vector3(0.0, 0.0, 1.0)
	_stuck_recovery_timer = 0.6


func _on_safe_velocity_computed(safe_velocity: Vector3) -> void:
	if is_dead or not is_moving or _turning_in_place or _is_path_blocked or _stuck_recovery_timer > 0.0:
		velocity.x = 0.0
		velocity.z = 0.0
		return
	velocity.x = safe_velocity.x
	velocity.z = safe_velocity.z
	# Otaceni je spolehlive rizeno target_yaw v _process_navigation
	move_and_slide()
	_push_light_debris()


func _push_light_debris() -> void:
	var delta := get_physics_process_delta_time()
	var pushing_this_frame := false

	if _coop_role == COOP_ROLE_HELPER:
		_coop_push_timer += delta
		if not is_instance_valid(_coop_partner) or _coop_push_timer > (coop_push_duration_max + 10.0):
			finish_coop_help()

	for i in get_slide_collision_count():
		var col := get_slide_collision(i)
		var body := col.get_collider() as RigidBody3D
		if not body or body.freeze:
			continue

		var normal_push := -col.get_normal()
		normal_push.y = 0.0
		if normal_push.length_squared() < 0.01:
			continue
		normal_push = normal_push.normalized()

		if body.mass <= push_mass_limit:
			# Light debris (can, bottle) -> instant impulse
			body.apply_central_impulse(normal_push * push_force)
		elif can_push_obstacles and body.mass <= obstacle_push_mass_limit:
			# Pushable heavy obstacle (crate, barrel) -> steady physical pushing force
			body.sleeping = false
			body.apply_central_force(normal_push * obstacle_push_force)
			if body.linear_velocity.length() < 0.2:
				body.apply_central_impulse(normal_push * (obstacle_push_force * 0.05))
			pushing_this_frame = true
			_current_push_target_body = body
		elif can_push_obstacles and body.mass <= coop_push_mass_limit:
			# Very heavy obstacle -> co-op push
			if _coop_role == COOP_ROLE_NONE:
				if not _request_coop_help(body):
					_trigger_fallback_reverse_patrol()
					return
			elif _coop_role == COOP_ROLE_CALLER or _coop_role == COOP_ROLE_HELPER:
				body.sleeping = false
				body.apply_central_force(normal_push * (obstacle_push_force * 1.9))
				pushing_this_frame = true
				_current_push_target_body = body

	# Helper pushing if positioned near caller's obstacle
	if _coop_role == COOP_ROLE_HELPER and is_instance_valid(_coop_target_obstacle):
		var d_to_obs := global_position.distance_to(_coop_target_obstacle.global_position)
		if d_to_obs < 2.0:
			var push_dir := (_coop_target_obstacle.global_position - global_position)
			push_dir.y = 0.0
			if push_dir.length_squared() > 0.01:
				_coop_target_obstacle.sleeping = false
				_coop_target_obstacle.apply_central_force(push_dir.normalized() * (obstacle_push_force * 1.5))
				pushing_this_frame = true

	if pushing_this_frame:
		_is_pushing_obstacle = true
		_push_attempt_timer += delta
		if _coop_role != COOP_ROLE_NONE:
			_coop_push_timer += delta

		var max_dur := coop_push_duration_max if _coop_role != COOP_ROLE_NONE else obstacle_push_duration_max
		if _push_attempt_timer >= max_dur:
			if _coop_role == COOP_ROLE_CALLER and is_instance_valid(_coop_partner):
				if _coop_partner.has_method("finish_coop_help"):
					_coop_partner.finish_coop_help()
			_is_pushing_obstacle = false
			_push_attempt_timer = 0.0
			_coop_role = COOP_ROLE_NONE
			_coop_partner = null
			_coop_target_obstacle = null
			_trigger_unstuck_and_detour()
	else:
		if _is_pushing_obstacle:
			if _coop_role == COOP_ROLE_CALLER and is_instance_valid(_coop_partner):
				if _coop_partner.has_method("finish_coop_help"):
					_coop_partner.finish_coop_help()
			_is_pushing_obstacle = false
			_push_attempt_timer = 0.0
			_coop_role = COOP_ROLE_NONE
			_coop_partner = null
			_coop_target_obstacle = null
			_current_push_target_body = null


func is_handling_obstacle() -> bool:
	return _is_pushing_obstacle or _is_detouring or _stuck_recovery_timer > 0.0 or _coop_role != COOP_ROLE_NONE


func is_available_for_coop() -> bool:
	return _coop_role == COOP_ROLE_NONE and not _is_path_blocked and not is_dead and not _fear_response_active


func _request_coop_help(target_obstacle: RigidBody3D) -> bool:
	if not can_call_for_coop or _coop_role != COOP_ROLE_NONE:
		return false

	var companions := get_tree().get_nodes_in_group("fps_npc")
	var best_companion: Node3D = null
	var best_dist := coop_call_radius * coop_call_radius

	for c in companions:
		if c == self or not is_instance_valid(c):
			continue
		if c.has_method("is_available_for_coop") and c.is_available_for_coop():
			var d2 := global_position.distance_squared_to(c.global_position)
			if d2 < best_dist:
				best_dist = d2
				best_companion = c

	if best_companion and is_instance_valid(best_companion):
		_coop_role = COOP_ROLE_CALLER
		_coop_partner = best_companion
		_coop_target_obstacle = target_obstacle
		_coop_push_timer = 0.0
		best_companion.respond_to_coop_request(self, target_obstacle)
		update_debug_state("Co-op: Volam posilu pro odtlaceni prekazky!")
		return true

	return false


func respond_to_coop_request(caller: Node3D, target_obstacle: RigidBody3D) -> void:
	_coop_role = COOP_ROLE_HELPER
	_coop_partner = caller
	_coop_target_obstacle = target_obstacle
	_coop_push_timer = 0.0
	_saved_pre_coop_waypoint = _npc_path_waypoint_index
	_saved_pre_coop_target = _requested_navigation_target
	_saved_pre_coop_speed = current_speed

	var assist_pos := caller.global_position + caller.global_transform.basis.x * 1.1
	move_to_position(assist_pos, run_speed)
	update_debug_state("Co-op: Bezim na pomoc spolubojovnikovi!")


func finish_coop_help() -> void:
	_coop_role = COOP_ROLE_NONE
	_coop_partner = null
	_coop_target_obstacle = null
	_coop_push_timer = 0.0
	if uses_npc_path() and has_valid_npc_path() and _saved_pre_coop_waypoint >= 0:
		_npc_path_waypoint_index = _saved_pre_coop_waypoint
		var target_pos := get_current_npc_path_target()
		move_to_position(target_pos, _get_locomotion_speed())
	elif _saved_pre_coop_target.length_squared() > 0.1:
		move_to_position(_saved_pre_coop_target, _get_locomotion_speed())
	else:
		stop()
	update_debug_state("Co-op: Hotovo, vracim se k patrole.")


func _should_brake_for_navigation_target() -> bool:
	if not uses_npc_path():
		return true
	var action := get_npc_path_waypoint_action(_npc_path_waypoint_index)
	var act_name := str(action.get("action", "move")).to_lower()
	if act_name != "move" and act_name != "run":
		return true
	return not can_smoothly_advance_current_npc_path_waypoint()


# =============================================================================
# Veřejné API pro Beehave akce / stavy
# =============================================================================

## Zadá NPC nový navigační cíl
func move_to_position(target_position: Vector3, speed: float = -1.0) -> void:
	if not nav_agent:
		return
	_turning_in_place = false
	_requested_navigation_target = target_position
	_navigation_target_is_route = uses_npc_path() and has_valid_npc_path() and target_position.is_equal_approx(get_current_npc_path_target())
	# Use a point that actually lies on this navigation map. Authored route
	# markers can be slightly offset from the walkable surface, especially in doors.
	var navigation_map := nav_agent.get_navigation_map()
	if NavigationServer3D.map_get_iteration_id(navigation_map) > 0:
		nav_agent.target_position = NavigationServer3D.map_get_closest_point(navigation_map, target_position)
	else:
		nav_agent.target_position = target_position
	current_speed = speed if speed > 0.0 else _get_locomotion_speed()
	is_moving = not _navigation_target_is_route or is_route_target_projection_valid()


func is_route_target_projection_valid() -> bool:
	if nav_agent == null:
		return false
	var offset := nav_agent.target_position - _requested_navigation_target
	# Height snapping accommodates editor markers, but cannot relocate the action
	# to another floor or across a wall when the route has no navigation surface.
	var max_horizontal_dist := maxf(nav_agent.target_desired_distance, 1.5)
	return Vector2(offset.x, offset.z).length() <= max_horizontal_dist and absf(offset.y) <= 1.5


func log_navigation_snapshot(reason: String) -> void:
	if not debug_navigation or nav_agent == null:
		return
	print("[NPC Navigation] %s waypoint=%d npc=%s raw=%s projected=%s projection_valid=%s reachable=%s path=%s" % [reason, _npc_path_waypoint_index + 1, global_position, _requested_navigation_target, nav_agent.target_position, is_route_target_projection_valid(), nav_agent.is_target_reachable(), nav_agent.get_current_navigation_path()])


func _get_locomotion_speed() -> float:
	if uses_npc_path():
		var action := get_npc_path_waypoint_action(_npc_path_waypoint_index)
		var act_name := str(action.get("action", "move")).to_lower()
		if act_name == "run":
			return run_speed
		elif act_name == "move":
			return run_speed if _active_route_locomotion == LOCOMOTION_RUN else walk_speed
		if _active_route_locomotion == LOCOMOTION_RUN:
			return run_speed
	return run_speed if locomotion_style == LOCOMOTION_RUN else walk_speed


func is_running() -> bool:
	return _get_locomotion_speed() >= run_speed - 0.05


## Called by weapons/explosions. The optional arguments match the FPS weapon API.
func take_damage(amount: float, _hit_position: Vector3 = Vector3.ZERO, _hit_direction: Vector3 = Vector3.ZERO) -> void:
	if is_dead or amount <= 0.0:
		return
	current_health = maxf(current_health - amount, 0.0)
	add_fear(fear_from_damage, get_player())
	_update_overhead_status()
	if current_health <= 0.0:
		is_dead = true
		stop()
		velocity = Vector3.ZERO
		collision_layer = 0
		collision_mask = 0
		update_debug_state("NPC: Vyřazen")
		_remove_after_death()


func _remove_after_death() -> void:
	await get_tree().create_timer(0.75).timeout
	if is_inside_tree():
		queue_free()


func add_fear(amount: float, threat: Node3D = null) -> void:
	if not fear_enabled or amount <= 0.0:
		return
	fear_level = clampf(fear_level + amount, 0.0, 100.0)
	if is_instance_valid(threat):
		_fear_threat = threat
	elif not is_instance_valid(_fear_threat):
		_fear_threat = get_player()
	_refresh_fear_state()


func is_frightened() -> bool:
	return fear_enabled and not is_dead and _fear_response_active


func get_fear_threat() -> Node3D:
	if is_instance_valid(_fear_threat) and _fear_threat.is_inside_tree():
		return _fear_threat
	return get_player()


func _update_fear(delta: float) -> void:
	if not fear_enabled:
		fear_level = 0.0
		_flashlight_exposure_fear = 0.0
		_fear_threat = null
		_fear_response_active = false
		return

	var player := get_player()
	if player != null and global_position.distance_to(player.global_position) <= fear_trigger_distance and _can_see_player(player):
		_fear_threat = player
		var player_distance := global_position.distance_to(player.global_position)
		var proximity := 1.0 - clampf(player_distance / maxf(fear_trigger_distance, 0.1), 0.0, 1.0)
		# A nearby player can make the NPC uneasy, but presence alone must never
		# reach the flee threshold. Damage and explicit fear events may exceed this cap.
		var proximity_limit := minf(fear_proximity_limit, fear_threshold - 0.1)
		if _fear_response_active:
			fear_level = move_toward(fear_level, 0.0, fear_decay_per_second * delta)
		elif fear_level > proximity_limit:
			fear_level = move_toward(fear_level, proximity_limit, fear_decay_per_second * delta)
		elif fear_level < proximity_limit:
			var proximity_gain := fear_gain_per_second * maxf(proximity, 0.15) * delta
			fear_level = minf(fear_level + proximity_gain, proximity_limit)
	else:
		fear_level = move_toward(fear_level, 0.0, fear_decay_per_second * delta)
		# In this case the normal fear decay handles the flashlight contribution too.
		_flashlight_exposure_fear = 0.0

	if fear_level <= 0.1:
		_fear_threat = null
	_refresh_fear_state()


func _update_flashlight_exposure(delta: float) -> void:
	_flashlight_exposure_active = false
	if react_to_flashlight:
		var flashlight := _get_player_flashlight()
		if flashlight != null and flashlight.has_method("illuminates_target"):
			_flashlight_exposure_active = bool(flashlight.call("illuminates_target", self))

	if not _flashlight_exposure_active:
		_recover_flashlight_fear(delta)
		return

	# Světlo je NPC nepříjemné, ale samo o sobě ho nemá přivést k útěku.
	var remaining_fear := maxf(flashlight_fear_limit - _flashlight_exposure_fear, 0.0)
	var room_before_fleeing := maxf(fear_threshold - 0.1 - fear_level, 0.0)
	var fear_increase := minf(flashlight_fear_per_second * delta, minf(remaining_fear, room_before_fleeing))
	if fear_increase > 0.0:
		add_fear(fear_increase, get_player())
		_flashlight_exposure_fear += fear_increase


func _recover_flashlight_fear(delta: float) -> void:
	if _flashlight_exposure_fear <= 0.0:
		return

	var recovery := minf(_flashlight_exposure_fear, fear_decay_per_second * delta)
	_flashlight_exposure_fear -= recovery
	fear_level = maxf(fear_level - recovery, 0.0)
	_refresh_fear_state()
	if fear_level <= 0.1:
		_fear_threat = null


func _get_player_flashlight() -> Node3D:
	if is_instance_valid(_player_flashlight) and _player_flashlight.is_inside_tree():
		return _player_flashlight
	_player_flashlight = null
	for candidate in get_tree().get_nodes_in_group("player_flashlight"):
		if candidate is SpotLight3D and candidate.has_method("illuminates_target"):
			_player_flashlight = candidate as Node3D
			return _player_flashlight
	return null


func is_flashlight_on_npc() -> bool:
	return _flashlight_exposure_active


func _on_bump_sensor_body_entered(body: Node3D) -> void:
	if body.is_in_group("player"):
		_bump_touching_player = body as CharacterBody3D


func _on_bump_sensor_body_exited(body: Node3D) -> void:
	if body == _bump_touching_player:
		_bump_touching_player = null


func _update_bump_reaction(delta: float) -> void:
	_bump_cooldown_remaining = maxf(_bump_cooldown_remaining - delta, 0.0)
	if not react_to_bumping or not is_instance_valid(_bump_touching_player):
		return
	if _bump_cooldown_remaining > 0.0:
		return

	var player_velocity := _bump_touching_player.get_real_velocity() - get_real_velocity()
	player_velocity.y = 0.0
	var direction_to_npc := global_position - _bump_touching_player.global_position
	direction_to_npc.y = 0.0
	if direction_to_npc.length_squared() <= 0.01:
		return

	var impact_speed := maxf(player_velocity.dot(direction_to_npc.normalized()), 0.0)
	if impact_speed < bump_min_speed:
		return

	var impact_fear := bump_fear_base + maxf(impact_speed - bump_min_speed, 0.0) * bump_fear_per_speed
	add_fear(clampf(impact_fear, 0.0, 100.0), _bump_touching_player)
	_bump_cooldown_remaining = bump_cooldown


func _can_see_player(player: Node3D) -> bool:
	return can_see_player(player, true)


func can_see_player(player: Node3D, use_vision_cone: bool = false) -> bool:
	if not is_instance_valid(player) or not player.is_inside_tree():
		return false
	var direction_to_player := player.global_position - global_position
	direction_to_player.y = 0.0

	var forward := -global_transform.basis.z
	forward.y = 0.0
	if use_vision_cone and forward.length_squared() > 0.01:
		var minimum_dot := cos(deg_to_rad(fear_vision_angle_degrees * 0.5))
		if forward.normalized().dot(direction_to_player.normalized()) < minimum_dot:
			return false

	var eye_position := global_position + Vector3.UP * 1.5
	var target_position := player.global_position + Vector3.UP * 1.4
	var query := PhysicsRayQueryParameters3D.create(eye_position, target_position)
	query.exclude = [get_rid()]
	query.collide_with_areas = false
	var result := get_world_3d().direct_space_state.intersect_ray(query)
	if result.is_empty():
		return true

	var collider := result.get("collider") as Node
	return collider == player or (collider != null and player.is_ancestor_of(collider))


func _refresh_fear_state() -> void:
	if not fear_enabled:
		_fear_response_active = false
	elif not _fear_response_active and fear_level >= fear_threshold:
		_fear_response_active = true
	elif _fear_response_active and fear_level <= minf(fear_recovery_threshold, fear_threshold):
		_fear_response_active = false


## Chooses a reachable NavMesh point that creates separation from the fear source.
func get_fear_flee_target() -> Vector3:
	var threat := get_fear_threat()
	if threat == null or not is_navigation_map_ready():
		return global_position

	var away := global_position - threat.global_position
	away.y = 0.0
	if away.length_squared() <= 0.01:
		away = -global_transform.basis.z
		away.y = 0.0
	if away.length_squared() <= 0.01:
		return global_position
	away = away.normalized()

	var map := nav_agent.get_navigation_map()
	var best_point := global_position
	var current_threat_distance := Vector2(global_position.x - threat.global_position.x, global_position.z - threat.global_position.z).length()
	var best_score := current_threat_distance
	for angle_degrees in [-100.0, -70.0, -40.0, 0.0, 40.0, 70.0, 100.0, 150.0]:
		var direction := away.rotated(Vector3.UP, deg_to_rad(angle_degrees))
		var sample_position := global_position + direction * fear_flee_distance
		var candidate := NavigationServer3D.map_get_closest_point(map, sample_position)
		if absf(candidate.y - global_position.y) > 0.6 or candidate.distance_to(global_position) < 1.0:
			continue
		var path := NavigationServer3D.map_get_path(map, global_position, candidate, true)
		if path.size() < 2:
			continue
		var away_progress := Vector3(candidate.x - global_position.x, 0.0, candidate.z - global_position.z).dot(away)
		if away_progress <= 0.25:
			continue
		var threat_distance := Vector2(candidate.x - threat.global_position.x, candidate.z - threat.global_position.z).length()
		var score := threat_distance + away_progress * 0.2
		if score > best_score:
			best_score = score
			best_point = candidate
	return best_point


func _update_overhead_status() -> void:
	if health_label != null:
		health_label.visible = show_health_overhead
		if show_health_overhead:
			var health_text := "HP: %d / %d" % [roundi(current_health), roundi(max_health)]
			if health_label.text != health_text:
				health_label.text = health_text
			var health_ratio := current_health / maxf(max_health, 1.0)
			health_label.modulate = Color(0.45, 1.0, 0.5) if health_ratio > 0.6 else (Color(1.0, 0.75, 0.25) if health_ratio > 0.3 else Color(1.0, 0.3, 0.25))
	if fear_label != null:
		fear_label.visible = show_fear_overhead
		if show_fear_overhead:
			var fear_text := "STRACH: %d%%" % roundi(fear_level)
			if fear_label.text != fear_text:
				fear_label.text = fear_text
			fear_label.modulate = Color(0.45, 0.9, 1.0) if fear_level < fear_threshold else (Color(1.0, 0.72, 0.2) if fear_level < 70.0 else Color(1.0, 0.25, 0.2))


## Zastaví pohyb NPC
func stop() -> void:
	is_moving = false
	is_turning_to_move = false
	_turning_in_place = false
	velocity.x = 0.0
	velocity.z = 0.0
	if nav_agent != null and nav_agent.avoidance_enabled:
		nav_agent.set_velocity(Vector3.ZERO)


## Whether the movement branch should consume the assigned Path3D instead of random points.
func uses_npc_path() -> bool:
	return movement_mode == MOVEMENT_MODE_ASSIGNED_PATH


## A patrol requires a live Path3D with at least two curve points.
func has_valid_npc_path() -> bool:
	if not is_instance_valid(npc_path) or not npc_path.is_inside_tree():
		return false
	var route_curve: Curve3D = npc_path.curve
	return route_curve != null and route_curve.get_point_count() >= 2


## Returns the current route waypoint in world space for NavigationAgent3D.
func get_current_npc_path_target() -> Vector3:
	if not has_valid_npc_path():
		return global_position
	var route_curve: Curve3D = npc_path.curve
	var point_count := route_curve.get_point_count()
	_npc_path_waypoint_index = clampi(_npc_path_waypoint_index, 0, point_count - 1)
	return npc_path.to_global(route_curve.get_point_position(_npc_path_waypoint_index))


func get_current_npc_path_waypoint_index() -> int:
	return _npc_path_waypoint_index


func is_current_npc_path_waypoint_arrived() -> bool:
	return _npc_path_waypoint_arrived


## Route actions are serialized on Path3D by the native FPS Studio route editor.
func get_current_npc_path_action() -> Dictionary:
	if not has_valid_npc_path() or not _npc_path_waypoint_arrived:
		return {}
	return get_npc_path_waypoint_action(_npc_path_waypoint_index)


func get_npc_path_waypoint_action(waypoint_index: int) -> Dictionary:
	if not has_valid_npc_path():
		return {}
	var raw_actions: Variant = npc_path.get_meta(
			NPC_PATH_POINT_ACTIONS_META,
			npc_path.get_meta(LEGACY_NPC_PATH_POINT_ACTIONS_META, [])
	)
	if not (raw_actions is Array) or waypoint_index < 0 or waypoint_index >= raw_actions.size():
		return {}
	var raw_action: Variant = raw_actions[waypoint_index]
	return raw_action if raw_action is Dictionary else {}


## Interior Move waypoints can be blended into the following segment without stopping.
func can_smoothly_advance_current_npc_path_waypoint() -> bool:
	if not uses_npc_path() or not has_valid_npc_path() or _npc_path_waypoint_arrived:
		return false
	var route_curve: Curve3D = npc_path.curve
	var point_count := route_curve.get_point_count()
	if point_count < 3:
		return false
	var action := get_npc_path_waypoint_action(_npc_path_waypoint_index)
	var act_name := str(action.get("action", "move")).to_lower()
	if act_name != "move" and act_name != "run":
		return false
	if route_curve.is_closed():
		return true
	# Only the endpoint reverses direction. The preceding point is still a
	# normal pass-through point, even when the next point has a wait action.
	return _npc_path_waypoint_index > 0 and _npc_path_waypoint_index < point_count - 1


func advance_current_npc_path_waypoint_for_smoothing() -> bool:
	if not can_smoothly_advance_current_npc_path_waypoint() or not is_at_destination():
		return false
	_advance_npc_path_waypoint()
	return true


## Called only when the NavigationAgent reaches the current route point.
func mark_current_npc_path_waypoint_arrived() -> void:
	if uses_npc_path():
		_npc_path_waypoint_arrived = true


## Advances after the point's optional wait has completed.
func complete_current_npc_path_waypoint() -> void:
	if not uses_npc_path() or not _npc_path_waypoint_arrived:
		return
	_npc_path_waypoint_arrived = false
	_advance_npc_path_waypoint()


func _reset_npc_path_patrol() -> void:
	_npc_path_waypoint_index = 0
	_npc_path_direction = 1
	_npc_path_waypoint_arrived = false
	_active_route_locomotion = locomotion_style
	if not uses_npc_path() or route_start_mode != 0 or not has_valid_npc_path():
		return
	var curve: Curve3D = npc_path.curve
	var nearest_distance: float = INF
	for index in range(curve.get_point_count()):
		var point := npc_path.to_global(curve.get_point_position(index))
		var distance := global_position.distance_squared_to(point)
		if distance < nearest_distance:
			nearest_distance = distance
			_npc_path_waypoint_index = index
	# An open patrol reverses at its endpoints, including when starting there.
	if not curve.is_closed() and _npc_path_waypoint_index == curve.get_point_count() - 1:
		_npc_path_direction = -1


func _advance_npc_path_waypoint() -> void:
	if not has_valid_npc_path():
		return
	var completed_action := get_npc_path_waypoint_action(_npc_path_waypoint_index)
	var completed_act_name := str(completed_action.get("action", "move")).to_lower()
	if completed_act_name == "run":
		_active_route_locomotion = LOCOMOTION_RUN
	elif completed_act_name == "move":
		_active_route_locomotion = locomotion_style

	var route_curve: Curve3D = npc_path.curve
	var point_count := route_curve.get_point_count()
	var next_idx := _npc_path_waypoint_index + _npc_path_direction
	if route_curve.is_closed():
		next_idx = (next_idx + point_count) % point_count

	# If reversing due to a blockage and we approach the blocked point again, test if it cleared
	if _is_reversing_patrol and next_idx == _blocked_waypoint_index:
		var test_target := npc_path.to_global(route_curve.get_point_position(_blocked_waypoint_index))
		nav_agent.target_position = test_target
		if nav_agent.is_target_reachable():
			_is_reversing_patrol = false
			_blocked_waypoint_index = -1
			update_debug_state("Zaval uvolnen, obnovuji puvodni patrolu")
		else:
			# Still blocked: reverse direction again to keep patrolling accessible area
			_npc_path_direction = -_npc_path_direction
			next_idx = _npc_path_waypoint_index + _npc_path_direction
			if route_curve.is_closed():
				next_idx = (next_idx + point_count) % point_count
			update_debug_state("Zaval stale blokovan, otacim patrolu")

	if route_curve.is_closed():
		_npc_path_waypoint_index = next_idx
		return

	_npc_path_waypoint_index = next_idx
	if _npc_path_waypoint_index >= point_count:
		_npc_path_direction = -1
		_npc_path_waypoint_index = point_count - 2
	elif _npc_path_waypoint_index < 0:
		_npc_path_direction = 1
		_npc_path_waypoint_index = 1


@onready var debug_label: Label3D = get_node_or_null("DebugLabel")

var current_ai_state: String = "Inicializace"


## Aktualizuje text 3D debug popisku nad hlavou NPC
func update_debug_state(new_state: String) -> void:
	if current_ai_state != new_state:
		if not new_state.begins_with("Wander: Chůze") and not new_state.begins_with("Wander: Čeká") and not new_state.begins_with("Route: Chůze") and not new_state.begins_with("Route: Čekám") and not new_state.begins_with("Route: Rozhlížím") and not new_state.begins_with("Route: Sleduji") and not new_state.begins_with("Pozornost: Hráč odešel") and not new_state.begins_with("Pozornost: Sleduji hráče"):
			print("[Beehave AI] %s" % new_state)
	current_ai_state = new_state
	if debug_label:
		debug_label.text = "[NPC AI]\n%s" % new_state


## Vzdálenost k navigačnímu cíli v horizontální rovině
func distance_to_nav_target() -> float:
	if not nav_agent:
		return 0.0
	var current_flat := Vector2(global_position.x, global_position.z)
	var target_flat := Vector2(nav_agent.target_position.x, nav_agent.target_position.z)
	return current_flat.distance_to(target_flat)


## Je cíl navigace skutečně dosažen?
func is_at_destination() -> bool:
	if not nav_agent:
		return true

	var thresh := nav_agent.target_desired_distance
	if can_smoothly_advance_current_npc_path_waypoint():
		thresh = maxf(thresh, 0.35)

	if _navigation_target_is_route:
		if not is_route_target_projection_valid():
			return false
		var authored_offset := global_position - _requested_navigation_target
		var dist_authored := Vector2(authored_offset.x, authored_offset.z).length()
		if dist_authored <= thresh:
			return true

		# Spolehliva kontrola postupu po Curve3D
		if npc_path and npc_path.curve:
			var curve: Curve3D = npc_path.curve
			var cur_off := curve.get_closest_offset(npc_path.to_local(global_position))
			var tgt_off := curve.get_closest_offset(npc_path.to_local(_requested_navigation_target))
			var progress_past := (cur_off - tgt_off) * _npc_path_direction
			if can_smoothly_advance_current_npc_path_waypoint():
				if (dist_authored <= 1.5 and progress_past >= -0.2) or (progress_past >= 0.1 and dist_authored <= 2.5):
					return true
			else:
				if dist_authored <= 0.35 or (dist_authored <= 0.65 and progress_past >= 0.0):
					return true

		# Pro plynulé projetí zatáčkou (radius): uznat přechod, pokud jsme minuli/proťali rovinu bodu ve směru trasy
		if can_smoothly_advance_current_npc_path_waypoint() and dist_authored <= 2.2:
			var prev_idx := _npc_path_waypoint_index - _npc_path_direction
			var curve: Curve3D = npc_path.curve if npc_path else null
			if curve and prev_idx >= 0 and prev_idx < curve.get_point_count():
				var prev_pos := npc_path.to_global(curve.get_point_position(prev_idx))
				var seg := _requested_navigation_target - prev_pos
				var past_vec := global_position - _requested_navigation_target
				if Vector2(seg.x, seg.z).dot(Vector2(past_vec.x, past_vec.z)) > 0.0:
					return true
		return false

	var current_flat := Vector2(global_position.x, global_position.z)
	var target_flat := Vector2(nav_agent.target_position.x, nav_agent.target_position.z)
	var dist := current_flat.distance_to(target_flat)
	return dist <= maxf(thresh, 0.1)


## Plynule natočí NPC k zadané pozici (bez pohybu z místa)
func face_position(target_pos: Vector3, delta: float) -> void:
	var dir := target_pos - global_position
	dir.y = 0.0
	if dir.length() > 0.1:
		var target_yaw := atan2(-dir.x, -dir.z)
		global_rotation.y = _rotate_yaw_toward(global_rotation.y, target_yaw, maxf(turn_speed, 0.01) * delta)


## Starts a subtle, paused scan for use during waypoint waits.
func begin_idle_look() -> void:
	_idle_look_center_yaw = global_rotation.y
	_idle_look_timer = 0.0
	_choose_idle_look_direction()


## Looks at a waypoint target, or slowly scans nearby directions if there is no target.
func process_idle_look(delta: float, target: Node3D = null) -> void:
	if is_instance_valid(target) and target.is_inside_tree():
		face_position(target.global_position, delta)
		return

	_idle_look_timer -= delta
	if _idle_look_timer <= 0.0:
		_choose_idle_look_direction()
	global_rotation.y = _rotate_yaw_toward(global_rotation.y, _idle_look_target_yaw, maxf(turn_speed, 0.01) * delta)


func _choose_idle_look_direction() -> void:
	var look_angle := deg_to_rad(maxf(idle_look_angle_degrees, 0.0))
	_idle_look_target_yaw = _idle_look_center_yaw + randf_range(-look_angle, look_angle)
	var interval_min := maxf(idle_look_interval_min, 0.2)
	var interval_max := maxf(idle_look_interval_max, interval_min)
	_idle_look_timer = randf_range(interval_min, interval_max)


func is_facing_position(target_pos: Vector3, tolerance_degrees: float = 5.0) -> bool:
	var dir := target_pos - global_position
	dir.y = 0.0
	if dir.length_squared() <= 0.01:
		return true
	var target_yaw := atan2(-dir.x, -dir.z)
	return absf(wrapf(target_yaw - global_rotation.y, -PI, PI)) <= deg_to_rad(tolerance_degrees)


## Resolves route look targets relative to the NPC first, then the current scene root.
func resolve_npc_look_target(target_path: String) -> Node3D:
	if target_path.is_empty():
		return null
	var target := get_node_or_null(NodePath(target_path)) as Node3D
	if target != null:
		return target
	var scene_root := get_tree().current_scene
	if scene_root != null:
		return scene_root.get_node_or_null(NodePath(target_path)) as Node3D
	return null


## Nalezne uzel hráče v herním stromu
func get_player() -> Node3D:
	return get_tree().get_first_node_in_group("player") as Node3D


## Spočítá vzdálenost k hráči (nebo vrátí nekonečno)
func distance_to_player() -> float:
	var player := get_player()
	if player == null:
		return INF
	return global_position.distance_to(player.global_position)


## Je navigační mapa inicializována a synchronizována?
func is_navigation_map_ready() -> bool:
	var level := get_tree().current_scene
	if level != null and bool(level.get_meta("npc_navigation_initializing", false)):
		return false
	if nav_agent == null:
		return false
	var map := nav_agent.get_navigation_map()
	return map.is_valid() and NavigationServer3D.map_get_iteration_id(map) > 0


## Vygeneruje náhodný validní bod na NavMeshi v zadaném poloměru (výhradně na podlaze)
func get_random_navmesh_point(radius: float) -> Vector3:
	var map := nav_agent.get_navigation_map()
	if NavigationServer3D.map_get_iteration_id(map) == 0:
		return global_position

	for attempt in range(8):
		var angle := randf() * TAU
		var dist := randf_range(radius * 0.4, radius)
		var offset := Vector3(cos(angle) * dist, 0.0, sin(angle) * dist)
		# Sample around this NPC's floor height so wandering also works on raised floors.
		var sample_pos := Vector3(global_position.x + offset.x, global_position.y, global_position.z + offset.z)
		var pt := NavigationServer3D.map_get_closest_point(map, sample_pos)

		# Ignore an empty-map origin result and surfaces far above or below this floor.
		if pt.is_zero_approx() and global_position.length() > 1.5:
			continue
		if absf(pt.y - global_position.y) > 0.6:
			continue
		if pt.distance_to(global_position) < 2.0:
			continue

		return pt

	# Stay put when no valid nearby NavMesh sample is found instead of aiming off-mesh.
	return global_position


func _cast_sphere_probe(space_state: PhysicsDirectSpaceState3D, from_pos: Vector3, motion: Vector3, mask: int, radius: float = 0.42) -> Dictionary:
	if not _sphere_probe_shape:
		_sphere_probe_shape = SphereShape3D.new()
	_sphere_probe_shape.radius = radius

	var query := PhysicsShapeQueryParameters3D.new()
	query.shape = _sphere_probe_shape
	query.transform = Transform3D(Basis(), from_pos)
	query.motion = motion
	query.collision_mask = mask
	query.exclude = [get_rid()]
	query.collide_with_bodies = true
	query.collide_with_areas = false

	var motion_res := space_state.cast_motion(query)
	var hit: bool = false
	var safe_fraction: float = 1.0
	var safe_dist: float = motion.length()
	var hit_pos: Vector3 = from_pos + motion
	var collider_body: Object = null
	var hit_normal := Vector3.ZERO

	if motion_res.size() > 0:
		hit = true
		safe_fraction = clampf(motion_res[0], 0.0, 1.0)
		safe_dist = safe_fraction * motion.length()
		hit_pos = from_pos + motion.normalized() * safe_dist

		# Spolehlivé zjištění kolidujícího tělesa a normály
		var ray_q := PhysicsRayQueryParameters3D.create(from_pos, from_pos + motion.normalized() * (safe_dist + 0.35), mask, [get_rid()])
		var ray_res := space_state.intersect_ray(ray_q)
		if not ray_res.is_empty():
			collider_body = ray_res.get("collider")
			hit_normal = ray_res.get("normal", Vector3.ZERO)
		else:
			var rest_query := PhysicsShapeQueryParameters3D.new()
			rest_query.shape = _sphere_probe_shape
			rest_query.transform = Transform3D(Basis(), hit_pos + motion.normalized() * 0.05)
			rest_query.collision_mask = mask
			rest_query.exclude = [get_rid()]
			var rest := space_state.get_rest_info(rest_query)
			if not rest.is_empty():
				var col_id = rest.get("collider_id", 0)
				if col_id != 0:
					collider_body = instance_from_id(col_id)
				hit_normal = rest.get("normal", Vector3.ZERO)

		# Šikmé rampy a pochozí podlahy (normal.y > 0.3) nebo terén přímo na NavMeshi jsou pochozí terén
		var is_ramp: bool = hit_normal.y > 0.3
		var is_walkable_navmesh: bool = false
		var nav_map := nav_agent.get_navigation_map() if nav_agent else RID()
		if NavigationServer3D.map_get_iteration_id(nav_map) > 0:
			var nav_pt := NavigationServer3D.map_get_closest_point(nav_map, hit_pos)
			if nav_pt.distance_to(hit_pos) <= 0.35:
				is_walkable_navmesh = true

		if (is_ramp or is_walkable_navmesh) and not _is_valid_detour_obstacle(collider_body):
			hit = false
			safe_fraction = 1.0
			safe_dist = motion.length()
			collider_body = null

	return {
		"hit": hit,
		"safe_fraction": safe_fraction,
		"safe_distance": safe_dist,
		"hit_pos": hit_pos,
		"collider": collider_body,
		"normal": hit_normal,
		"from": from_pos,
		"to": from_pos + motion,
		"radius": radius
	}


## Ověří, zda je kolidující objekt skutečnou herní/fyzikální překážkou (bedna, zával, prop)
## a nikoliv statickou architekturou levelu (zeď, rampa, podlaha).
func _is_valid_detour_obstacle(collider: Object) -> bool:
	if not is_instance_valid(collider):
		return false
	if collider is RigidBody3D:
		return true
	if collider is Node:
		var node := collider as Node
		if node.is_in_group("fps_heavy_obstacle") or node.is_in_group("fps_obstacle"):
			return true
		if node.has_node("NavigationObstacle3D"):
			return true
	if collider is CollisionObject3D:
		var col_obj := collider as CollisionObject3D
		# Layer 2: Heavy Obstacle (bit 1)
		if (col_obj.collision_layer & (1 << 1)) != 0:
			return true
	return false


## Zjistí horizontální poloměr/rozměr modelu překážky z CollisionShape3D, NavigationObstacle3D nebo MeshInstance3D
func _get_obstacle_horizontal_radius(collider: Object) -> float:
	if not is_instance_valid(collider):
		return 0.5
	var rad: float = 0.5
	if collider is Node:
		var node := collider as Node
		for child in node.get_children():
			if child is CollisionShape3D:
				var shape: Shape3D = (child as CollisionShape3D).shape
				if shape is BoxShape3D:
					rad = maxf(rad, maxf(shape.size.x, shape.size.z) * 0.5)
				elif shape is CylinderShape3D:
					rad = maxf(rad, shape.radius)
				elif shape is SphereShape3D:
					rad = maxf(rad, shape.radius)
			elif child is NavigationObstacle3D:
				var obs := child as NavigationObstacle3D
				if obs.radius > 0.1:
					rad = maxf(rad, obs.radius)
		for child in node.get_children():
			if child is MeshInstance3D:
				var aabb: AABB = (child as MeshInstance3D).get_aabb()
				if aabb.size.x > 0.1 and aabb.size.z > 0.1:
					rad = maxf(rad, maxf(aabb.size.x, aabb.size.z) * 0.5)
	return clampf(rad, 0.45, 2.5)


## Tří-koulvý prostorový senzor a dynamické založení objízdné trasy:
## 1. Hlavní dopředná kontrolní koule (scout): v rovných chodbách a volném prostoru zůstává zelená,
##    netestuje zbytečně boční zdi, neničí rychlost a nezpůsobuje cukání.
## 2. Při detekci překážky přímo vpředu (< 3.2m a před cílovým waypointem) se aktivují dvě pomocné koule (vlevo a vpravo).
## 3. Pokud detekuje středová a jedna boční (např. pravá u zdi), NPC se otočí do volného směru (vlevo).
##    Po natočení míří hlavní středová koule do volného prostoru (nic nedetekuje, zezelená) a jde rovně.
## 4. Aby příště nemuselo detekovat znovu, do Curve3D patrol trasy se trvale vloží nová objízdná trasa.
func _probe_obstacle_avoidance(base_dir: Vector3, delta: float) -> Vector3:
	var space_state := get_world_3d().direct_space_state
	if not space_state:
		return base_dir

	_debug_probe_records.clear()

	var from_pos := global_position + Vector3(0.0, 0.55, 0.0) # Výška středu těla
	var mask := (1 << 0) | (1 << 1) # Layer 1: World zdi + Layer 2: Heavy Obstacle
	var forward_dist := 3.6
	var scout_radius := 0.20 # Malá kontrolní koule dopředu

	# Vzdálenost k cílovému waypointu
	var dist_to_wp: float = global_position.distance_to(_requested_navigation_target) if (uses_npc_path() and _navigation_target_is_route) else forward_dist
	var max_scout_dist: float = minf(forward_dist, maxf(dist_to_wp + 0.5, 1.8))

	# 1. KROK: Hlavní dopředná kontrolní hlídka přímo ve směru pohybu
	var scout_res := _cast_sphere_probe(space_state, from_pos, base_dir * max_scout_dist, mask, scout_radius)

	# Překážka je detekována pouze tehdy, pokud leží v dráze PŘED waypointem (ne za rohem)
	var is_blocking_route: bool = scout_res["hit"] and (scout_res["safe_distance"] < maxf(dist_to_wp - 0.25, 0.5) or scout_res["safe_distance"] < 1.3)
	var obstacle_detected: bool = is_blocking_route and scout_res["safe_distance"] < 3.2

	# Pokud před námi NIC NENÍ (volná rovná chodba nebo otevřená místnost):
	# Boční koule zůstávají zcela skryté a neaktivní -> žádné zasekávání o boční zdi!
	if not obstacle_detected:
		_aux_spheres_active = false
		_record_debug_sphere(scout_res, "scout")
		_last_sphere_probe_pos = from_pos + base_dir * minf(max_scout_dist * 0.55, 2.0)
		_last_sphere_probe_color = Color(0.2, 0.95, 0.4, 0.35) # Zelená hlavní kontrolní koule
		_avoidance_clear_time += delta
		if _avoidance_clear_time >= 0.2:
			_avoidance_side = 0.0
		_current_avoidance_angle = move_toward(_current_avoidance_angle, 0.0, delta * deg_to_rad(140.0))
		_forward_blocked = false
		var clear_dir := base_dir
		if not is_zero_approx(_current_avoidance_angle):
			clear_dir = base_dir.rotated(Vector3.UP, _current_avoidance_angle).normalized()
		_debug_steer_dir = clear_dir
		return clear_dir

	# 2. KROK: PŘEKÁŽKA / ZEĎ DETEKOVÁNA PŘED NÁMI!
	# Aktivujeme dvě pomocné koule (jedna vlevo, druhá vpravo)
	_aux_spheres_active = true
	_avoidance_clear_time = 0.0
	_record_debug_sphere(scout_res, "center")
	_last_sphere_probe_pos = scout_res["hit_pos"]
	_last_sphere_probe_color = Color(1.0, 0.2, 0.15, 0.65) # Červená hlavní koule na překážce

	var body_radius := 0.34
	var side_normal := Vector3(-base_dir.z, 0.0, base_dir.x).normalized()
	var left_dir := base_dir.rotated(Vector3.UP, deg_to_rad(38.0))
	var right_dir := base_dir.rotated(Vector3.UP, deg_to_rad(-38.0))

	# Testujeme obě strany těla
	var left_res := _cast_sphere_probe(space_state, from_pos, left_dir * 2.5, mask, body_radius)
	var right_res := _cast_sphere_probe(space_state, from_pos, right_dir * 2.5, mask, body_radius)
	_record_debug_sphere(left_res, "left")
	_record_debug_sphere(right_res, "right")

	var left_blocked: bool = left_res["hit"] and left_res["safe_distance"] < 1.6
	var right_blocked: bool = right_res["hit"] and right_res["safe_distance"] < 1.6

	# Vizuální pozice a barvy levé a pravé koule
	if left_blocked:
		_last_left_probe_pos = left_res["hit_pos"]
		_last_left_probe_color = Color(1.0, 0.15, 0.15, 0.65) # Červená - zablokováno
	else:
		_last_left_probe_pos = from_pos + left_dir * 2.0
		_last_left_probe_color = Color(0.2, 0.95, 0.4, 0.5) # Zelená - volno

	if right_blocked:
		_last_right_probe_pos = right_res["hit_pos"]
		_last_right_probe_color = Color(1.0, 0.15, 0.15, 0.65) # Červená - zablokováno
	else:
		_last_right_probe_pos = from_pos + right_dir * 2.0
		_last_right_probe_color = Color(0.2, 0.95, 0.4, 0.5) # Zelená - volno

	# Vyhodnocení směru vybočení:
	# Když detekuje středová a pravá -> otočí se doleva do volného prostoru
	# Když detekuje středová a levá -> otočí se doprava do volného prostoru
	if not left_blocked and right_blocked:
		_avoidance_side = 1.0 # Uhni doleva do volného prostoru
	elif not right_blocked and left_blocked:
		_avoidance_side = -1.0 # Uhni doprava do volného prostoru
	elif not left_blocked and not right_blocked:
		# Obě strany volné -> vyber stranu s větším odstupem
		if is_zero_approx(_avoidance_side):
			_avoidance_side = 1.0 if left_res["safe_distance"] >= right_res["safe_distance"] else -1.0
	else:
		# Obě strany zablokované (úplný zával / úzká chodba)
		_forward_blocked = true
		_avoidance_side = 0.0
		_current_avoidance_angle = move_toward(_current_avoidance_angle, 0.0, delta * deg_to_rad(140.0))
		_debug_steer_dir = base_dir
		return base_dir

	_forward_blocked = false

	# 4. KROK: Založení trvalé nové trasy (baking), aby příště nemusel detekovat znovu!
	# Založení nové trasy provádíme VÝHRADNĚ pro skutečné překážky (bedny, závaly), nikoliv zdi
	if uses_npc_path() and has_valid_npc_path():
		var col: Object = scout_res.get("collider")
		if _is_valid_detour_obstacle(col):
			var detour_target := _bake_detour_around_obstacle(col, _avoidance_side, scout_res["hit_pos"], base_dir)
			if detour_target.length_squared() > 0.01:
				var to_detour := (detour_target - global_position)
				to_detour.y = 0.0
				if to_detour.length_squared() > 0.01:
					var detour_steer := to_detour.normalized()
					_debug_steer_dir = detour_steer
					return detour_steer

	var target_avoid_deg: float = 48.0
	var target_angle := _avoidance_side * deg_to_rad(target_avoid_deg)
	_current_avoidance_angle = move_toward(_current_avoidance_angle, target_angle, delta * deg_to_rad(140.0))

	var steer_dir := base_dir.rotated(Vector3.UP, _current_avoidance_angle).normalized()
	_debug_steer_dir = steer_dir
	return steer_dir


## Trvale vytvoří a vloží novou plynulou Bézier objízdnou trasu do patrol křivky Curve3D okolo překážky
func _bake_detour_around_obstacle(collider: Object, avoidance_side: float, hit_pos: Vector3, base_dir: Vector3) -> Vector3:
	if not uses_npc_path() or not has_valid_npc_path():
		return Vector3.ZERO
	if not _is_valid_detour_obstacle(collider):
		return Vector3.ZERO

	# Identifikace překážky proti opakovanému vkládání bodů
	var obs_id: int = collider.get_instance_id() if is_instance_valid(collider) else 0
	var grid_key: String = "detour_%.1f_%.1f" % [roundf(hit_pos.x * 0.5), roundf(hit_pos.z * 0.5)]
	if (obs_id != 0 and obs_id in _baked_detour_obstacles) or (grid_key in _baked_detour_obstacles):
		return Vector3.ZERO
	if obs_id != 0:
		_baked_detour_obstacles.append(obs_id)
	_baked_detour_obstacles.append(grid_key)

	var obs_pos: Vector3 = hit_pos
	if is_instance_valid(collider) and collider is Node3D:
		obs_pos = (collider as Node3D).global_position
	obs_pos.y = global_position.y

	# Zjistíme rozměr modelu a nastavíme offset přesně ~1.0 m od okraje modelu
	var obs_radius := _get_obstacle_horizontal_radius(collider)
	var desired_clearance: float = obs_radius + 1.0 # Přesně 1 metr od modelu překážky

	var side_normal := Vector3(-base_dir.z, 0.0, base_dir.x).normalized()
	# avoidance_side > 0.0 = VLEVO, < 0.0 = VPRAVO
	var clear_dir := (-side_normal if avoidance_side > 0.0 else side_normal).normalized()

	var space_state := get_world_3d().direct_space_state
	var mask := (1 << 0) | (1 << 1)
	if space_state:
		# Zkontrolujeme zda máme na zvolené straně dost místa
		var side_probe := _cast_sphere_probe(space_state, obs_pos, clear_dir * (desired_clearance + 0.3), mask, 0.3)
		if side_probe["hit"] and side_probe["safe_distance"] < (obs_radius + 0.75):
			# Na této straně je stěna příliš blízko! Vyzkoušíme opačnou stranu do volného prostoru
			var alt_dir := -clear_dir
			var alt_probe := _cast_sphere_probe(space_state, obs_pos, alt_dir * (desired_clearance + 0.3), mask, 0.3)
			if not alt_probe["hit"] or alt_probe["safe_distance"] >= side_probe["safe_distance"]:
				clear_dir = alt_dir
				avoidance_side = -avoidance_side
				if alt_probe["hit"]:
					desired_clearance = clampf(alt_probe["safe_distance"] - 0.2, obs_radius + 0.6, desired_clearance)
		elif side_probe["hit"]:
			desired_clearance = clampf(side_probe["safe_distance"] - 0.2, obs_radius + 0.6, desired_clearance)

	var nav_map := nav_agent.get_navigation_map()
	var lead_dist := obs_radius + 0.8
	var pt_a := obs_pos - base_dir * lead_dist + clear_dir * desired_clearance
	var pt_b := obs_pos + base_dir * lead_dist + clear_dir * desired_clearance
	if NavigationServer3D.map_get_iteration_id(nav_map) > 0:
		pt_a = NavigationServer3D.map_get_closest_point(nav_map, pt_a)
		pt_b = NavigationServer3D.map_get_closest_point(nav_map, pt_b)

	var insert_idx := _npc_path_waypoint_index
	if _npc_path_direction < 0:
		insert_idx = _npc_path_waypoint_index + 1

	var local_a := npc_path.to_local(pt_a)
	var local_b := npc_path.to_local(pt_b)

	# Vypočet hladkých kubických Bézier tečen pro zaoblení do plynulého oblouku (žádné kostrbaté hrany!)
	var handle_len := lead_dist * 0.45
	var tangent_a_dir := (base_dir + clear_dir * 0.4).normalized()
	var tangent_b_dir := (base_dir - clear_dir * 0.4).normalized()
	var tangent_a_local := npc_path.to_local(pt_a + tangent_a_dir * handle_len) - local_a
	var tangent_b_local := npc_path.to_local(pt_b + tangent_b_dir * handle_len) - local_b

	npc_path.curve.add_point(local_a, -tangent_a_local, tangent_a_local, insert_idx)
	npc_path.curve.add_point(local_b, -tangent_b_local, tangent_b_local, insert_idx + 1)

	var meta_key := NPC_PATH_POINT_ACTIONS_META if npc_path.has_meta(NPC_PATH_POINT_ACTIONS_META) else LEGACY_NPC_PATH_POINT_ACTIONS_META
	var raw_actions = npc_path.get_meta(meta_key, [])
	var actions_arr: Array = raw_actions.duplicate() if raw_actions is Array else []
	if insert_idx <= actions_arr.size():
		actions_arr.insert(insert_idx, {"action": "move", "duration": 0.0, "target": ""})
		actions_arr.insert(insert_idx + 1, {"action": "move", "duration": 0.0, "target": ""})
		npc_path.set_meta(meta_key, actions_arr)

	# Posun indexů čekajících stavů pokud byly za bodem vložení
	if _blocked_waypoint_index >= insert_idx:
		_blocked_waypoint_index += 2
	if _saved_pre_coop_waypoint >= insert_idx:
		_saved_pre_coop_waypoint += 2

	# Aktualizujeme cíl navigace na první bod nově vzniklé objížďky
	_npc_path_waypoint_index = insert_idx
	_requested_navigation_target = pt_a
	nav_agent.target_position = pt_a
	_navigation_target_is_route = true
	is_moving = true
	print("[NPC Path] Do patrol trasy byly trvale vlozeny hladke Bezier body objizdky: wp=%d a %d (model_rad=%.2fm, odstup=%.2fm)" % [insert_idx + 1, insert_idx + 2, obs_radius, desired_clearance])
	update_debug_state("Nova hladka trasa objizdky zalozena")
	return pt_a
