# meta-description: FPS NPC controller with RVO dynamic obstacle avoidance, path patrol, action execution, and debris physics filtering

extends _BASE_

@export_group("Debug Visuals")
@export var debug_visuals: bool = false
@export var show_carrot: bool = true
@export var show_avoidance_probes: bool = true
@export var show_steer_vector: bool = true
@export var show_route_curve: bool = true
@export var show_nav_path: bool = true

@export_group("Movement")
## Movement speeds
@export var walk_speed: float = 3.2
@export var run_speed: float = 6.0
@export var acceleration: float = 12.0

## Patrol route (Path3D with Curve3D created via Level Viewport -> FPS Gameplay -> NPC Path)
@export var patrol_path: Path3D
@export var loop_patrol: bool = true

## Physics filtering: push light debris aside instead of halting
@export var push_force: float = 3.5
@export var push_mass_limit: float = 5.0

## Solo obstacle pushing (heavy crates, barrels up to specified mass)
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

## Target destination (if not patrolling a path)
@export var target_node: Node3D

@onready var nav_agent: NavigationAgent3D = get_node_or_null("NavigationAgent3D")

const COOP_ROLE_NONE := 0
const COOP_ROLE_CALLER := 1
const COOP_ROLE_HELPER := 2

var current_waypoint_index: int = 0
var is_waiting: bool = false
var wait_timer: float = 0.0
var current_speed: float = 3.2
var active_locomotion: String = "move"
var gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity", 9.8)

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

var _coop_role: int = COOP_ROLE_NONE
var _coop_partner: Node3D = null
var _coop_target_obstacle: RigidBody3D = null
var _coop_push_timer: float = 0.0
var _saved_pre_coop_waypoint: int = -1
var _saved_pre_coop_target := Vector3.ZERO

var _patrol_direction: int = 1
var _is_reversing_patrol: bool = false
var _blocked_waypoint_index: int = -1

var _debug_lines_mesh: MeshInstance3D = null
var _carrot_node: Node3D = null
var _last_carrot_world_pos := Vector3.ZERO
var _debug_probe_records: Array = []
var _debug_steer_dir := Vector3.ZERO


func _ready() -> void:
	add_to_group("fps_npc")
	# Enforce FPS collision configuration:
	# Layer 5: NPC (bit 4, value 16)
	# Mask: Layer 1 (World), Layer 2 (Heavy Obstacle), Layer 4 (Player), Layer 5 (NPC) -> 27
	# Crucially, Layer 3 (Debris, bit 2, value 4) is EXCLUDED so small trash never blocks movement.
	collision_layer = (1 << 4)
	collision_mask = (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4)

	if nav_agent:
		nav_agent.avoidance_enabled = true
		nav_agent.avoidance_mask = 1 | 2 # Avoid heavy dynamic obstacles and other NPCs
		nav_agent.max_speed = maxf(run_speed, walk_speed) * 1.5
		nav_agent.avoidance_priority = 0.5 # Yield priority to obstacles so agent steers around them
		nav_agent.time_horizon_agents = 1.5
		nav_agent.time_horizon_obstacles = 1.5
		nav_agent.velocity_computed.connect(_on_safe_velocity_computed)

	current_speed = walk_speed
	_advance_patrol_destination()
	_setup_debug_visuals()


func _process(delta: float) -> void:
	_update_debug_visuals(delta)


func _should_show_debug_visuals() -> bool:
	if debug_visuals:
		return true
	var p := get_tree().get_first_node_in_group("player") as Node
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
	_debug_lines_mesh = MeshInstance3D.new()
	_debug_lines_mesh.name = "AIDebugRays"
	_debug_lines_mesh.top_level = true
	var imm := ImmediateMesh.new()
	_debug_lines_mesh.mesh = imm

	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.no_depth_test = true
	mat.render_priority = 10
	_debug_lines_mesh.material_override = mat
	add_child(_debug_lines_mesh)

	_carrot_node = Node3D.new()
	_carrot_node.name = "AICarrotMarker"
	_carrot_node.top_level = true

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

	carrot_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	carrot_label.font_size = 24

	_carrot_node.visible = false
	add_child(_carrot_node)

	# 3D stavovy stitek primo nad hlavou NPC
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
		"type": ptype
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
		return

	_debug_lines_mesh.visible = true

	var has_carrot := show_carrot and patrol_path and _last_carrot_world_pos.length_squared() > 0.01 and not is_waiting
	if has_carrot:
		_carrot_node.visible = true
		var bob := sin(Time.get_ticks_msec() * 0.005) * 0.08
		_carrot_node.global_position = _last_carrot_world_pos + Vector3(0.0, 0.45 + bob, 0.0)
		_carrot_node.rotate_y(delta * 2.8)

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
	else:
		_carrot_node.visible = false

	var imm: ImmediateMesh = _debug_lines_mesh.mesh as ImmediateMesh
	if not imm:
		return

	imm.clear_surfaces()

	var debug_vertices: Array[Vector3] = []
	var debug_colors: Array[Color] = []
	var origin := global_position + Vector3(0.0, 0.45, 0.0)

	if has_carrot:
		var carrot_top := _carrot_node.global_position
		debug_colors.append(Color(1.0, 0.72, 0.1, 0.85))
		debug_vertices.append(origin + Vector3(0.0, 0.4, 0.0))
		debug_colors.append(Color(1.0, 0.48, 0.05, 0.95))
		debug_vertices.append(carrot_top)

	if show_avoidance_probes:
		for rec in _debug_probe_records:
			var from_pt: Vector3 = rec["from"]
			var to_pt: Vector3 = rec["to"]
			var is_hit: bool = rec["hit"]
			var hit_pt: Vector3 = rec["hit_pos"]
			var ptype: String = rec["type"]

			if is_hit:
				debug_colors.append(Color(1.0, 0.2, 0.2, 0.95))
				debug_vertices.append(from_pt)
				debug_colors.append(Color(1.0, 0.05, 0.05, 1.0))
				debug_vertices.append(hit_pt)

				var cross_size := 0.15
				var ccol := Color(1.0, 0.1, 0.1, 1.0)
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt - Vector3(cross_size, 0.0, 0.0))
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt + Vector3(cross_size, 0.0, 0.0))
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt - Vector3(0.0, cross_size, 0.0))
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt + Vector3(0.0, cross_size, 0.0))
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt - Vector3(0.0, 0.0, cross_size))
				debug_colors.append(ccol)
				debug_vertices.append(hit_pt + Vector3(0.0, 0.0, cross_size))
			else:
				var ray_col := Color(0.2, 1.0, 0.35, 0.8)
				if ptype == "whisker":
					ray_col = Color(0.85, 1.0, 0.2, 0.75)
				elif ptype == "wide":
					ray_col = Color(0.2, 0.8, 1.0, 0.7)

				debug_colors.append(ray_col)
				debug_vertices.append(from_pt)
				debug_colors.append(ray_col)
				debug_vertices.append(to_pt)

	if show_steer_vector and not is_waiting and _debug_steer_dir.length_squared() > 0.01:
		var steer_norm := _debug_steer_dir.normalized()
		var steer_end := origin + steer_norm * 1.6
		var scol := Color(0.0, 0.9, 1.0, 1.0)
		debug_colors.append(scol)
		debug_vertices.append(origin)
		debug_colors.append(scol)
		debug_vertices.append(steer_end)

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
	if show_route_curve and patrol_path and patrol_path.curve:
		var curve: Curve3D = patrol_path.curve
		var pt_count := curve.get_point_count()
		var baked_len := curve.get_baked_length()

		if baked_len > 0.1:
			var step := 0.35
			var prev_sample: Vector3 = patrol_path.to_global(curve.sample_baked(0.0)) + Vector3(0.0, 0.06, 0.0)
			var d: float = step
			var path_col := Color(0.25, 0.7, 1.0, 0.7)
			while d <= baked_len:
				var next_sample: Vector3 = patrol_path.to_global(curve.sample_baked(d)) + Vector3(0.0, 0.06, 0.0)
				debug_colors.append(path_col)
				debug_vertices.append(prev_sample)
				debug_colors.append(path_col)
				debug_vertices.append(next_sample)
				prev_sample = next_sample
				d += step
			if curve.is_closed() or d > baked_len:
				var end_sample: Vector3 = patrol_path.to_global(curve.sample_baked(baked_len)) + Vector3(0.0, 0.06, 0.0)
				debug_colors.append(path_col)
				debug_vertices.append(prev_sample)
				debug_colors.append(path_col)
				debug_vertices.append(end_sample)

		var actions: Array = patrol_path.get_meta("_fps_npc_point_actions", [])
		for idx in range(pt_count):
			var wp_world: Vector3 = patrol_path.to_global(curve.get_point_position(idx)) + Vector3(0.0, 0.07, 0.0)
			var is_current := (idx == current_waypoint_index)

			var wp_col := Color(0.3, 0.8, 1.0, 0.85)
			if idx < actions.size():
				var act := str(actions[idx].get("action", "move")).to_lower()
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
	if show_nav_path and nav_agent and not is_waiting:
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
	# Apply gravity
	if not is_on_floor():
		velocity.y -= gravity * delta

	# Handle waiting at action points
	if is_waiting:
		wait_timer -= delta
		if wait_timer <= 0.0:
			is_waiting = false
			_advance_waypoint()
		else:
			# Halt immediately on the waypoint during wait
			velocity.x = 0.0
			velocity.z = 0.0
			move_and_slide()
			return

	if not nav_agent:
		move_and_slide()
		return

	# 1. Handle BLOCKED state
	if _is_path_blocked:
		velocity.x = move_toward(velocity.x, 0.0, acceleration * delta)
		velocity.z = move_toward(velocity.z, 0.0, acceleration * delta)
		move_and_slide()
		_blocked_retry_timer += delta
		if _blocked_retry_timer >= 1.0:
			_blocked_retry_timer = 0.0
			if patrol_path and patrol_path.curve:
				var target_wp_pos: Vector3 = patrol_path.to_global(patrol_path.curve.get_point_position(current_waypoint_index))
				nav_agent.target_position = target_wp_pos
				if nav_agent.is_target_reachable():
					_is_path_blocked = false
					_is_detouring = true
				elif enable_fallback_patrol:
					_trigger_fallback_reverse_patrol()
		return

	# 2. Handle STUCK_RECOVERY state (backtrack step to break out of corner/wall friction)
	if _stuck_recovery_timer > 0.0:
		_stuck_recovery_timer -= delta
		var back_v := _stuck_recovery_dir.normalized() * (walk_speed * 0.75)
		velocity.x = move_toward(velocity.x, back_v.x, acceleration * delta)
		velocity.z = move_toward(velocity.z, back_v.z, acceleration * delta)
		move_and_slide()
		if _stuck_recovery_timer <= 0.0:
			_is_detouring = true
		return

	# If reached current waypoint/destination
	if nav_agent.is_navigation_finished():
		if _is_detouring:
			_is_detouring = false
		if patrol_path:
			_process_waypoint_action()
		return

	# Re-join curve if detour brought us back near the path and corridor is clear
	if _is_detouring and patrol_path and patrol_path.curve:
		var local_pos: Vector3 = patrol_path.to_local(global_position)
		var closest_offset: float = patrol_path.curve.get_closest_offset(local_pos)
		var curve_pt: Vector3 = patrol_path.to_global(patrol_path.curve.sample_baked(closest_offset))
		var dist_to_curve := global_position.distance_to(curve_pt)
		if dist_to_curve < 0.85 and not _forward_blocked:
			_is_detouring = false

	var to_target: Vector3 = Vector3.ZERO
	if patrol_path and patrol_path.curve and not _is_detouring:
		var curve: Curve3D = patrol_path.curve
		var local_pos: Vector3 = patrol_path.to_local(global_position)
		var current_offset: float = curve.get_closest_offset(local_pos)
		var carrot_offset: float = current_offset + 1.4
		var target_offset: float = curve.get_closest_offset(curve.get_point_position(current_waypoint_index))
		if current_waypoint_index == curve.point_count - 1 and not loop_patrol:
			carrot_offset = minf(carrot_offset, target_offset)
		else:
			carrot_offset = minf(carrot_offset, target_offset + 0.6)
		if loop_patrol and curve.get_baked_length() > 0.01:
			carrot_offset = fposmod(carrot_offset, curve.get_baked_length())
		var carrot_world: Vector3 = patrol_path.to_global(curve.sample_baked(carrot_offset))
		_last_carrot_world_pos = carrot_world
		to_target = carrot_world - global_position
		to_target.y = 0.0
	else:
		if patrol_path and patrol_path.curve and _is_detouring:
			var target_wp_pos: Vector3 = patrol_path.to_global(patrol_path.curve.get_point_position(current_waypoint_index))
			nav_agent.target_position = target_wp_pos
			if not nav_agent.is_target_reachable():
				var next_wp := (current_waypoint_index + 1) % patrol_path.curve.point_count
				var next_target := patrol_path.to_global(patrol_path.curve.get_point_position(next_wp))
				nav_agent.target_position = next_target
				if nav_agent.is_target_reachable():
					current_waypoint_index = next_wp
				else:
					_is_path_blocked = true
					return
		var next_path_pos: Vector3 = nav_agent.get_next_path_position()
		_last_carrot_world_pos = next_path_pos
		to_target = next_path_pos - global_position
		to_target.y = 0.0

	var desired_velocity: Vector3 = Vector3.ZERO
	if to_target.length_squared() > 0.04:
		var move_dir: Vector3 = to_target.normalized()
		# Probe forward for dynamic and heavy obstacles (Layers 1 & 2)
		move_dir = _probe_obstacle_avoidance(move_dir, delta)
		var target_speed: float = current_speed

		# Smooth deceleration profile when approaching stopping points
		var is_stopping_point: bool = false
		if patrol_path and patrol_path.curve:
			if not loop_patrol and current_waypoint_index == patrol_path.curve.point_count - 1:
				is_stopping_point = true
			else:
				var actions: Array = patrol_path.get_meta("_fps_npc_point_actions", [])
				if current_waypoint_index < actions.size():
					var act: String = str(actions[current_waypoint_index].get("action", "move")).to_lower()
					is_stopping_point = (act == "wait" or act == "look_at" or act == "interact")
		else:
			is_stopping_point = true

		if is_stopping_point and nav_agent:
			var dist_to_target: float = global_position.distance_to(nav_agent.target_position)
			var decel_distance: float = clampf(current_speed * 0.85, 1.6, 4.2)
			var ratio: float = clampf((dist_to_target - nav_agent.target_desired_distance) / decel_distance, 0.0, 1.0)
			var min_crawl: float = clampf(current_speed * 0.25, 0.35, 1.0)
			target_speed = lerpf(min_crawl, current_speed, smoothstep(0.0, 1.0, ratio))

		desired_velocity = move_dir * target_speed

		# Fyzikalni detekce uviznuti (skutecny posun v 3D prostoru)
		if _stuck_recovery_timer <= 0.0 and not _is_path_blocked:
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

		if not nav_agent.avoidance_enabled:
			# Smoothly rotate towards movement direction when avoidance is inactive
			var look_rot: float = atan2(move_dir.x, move_dir.z)
			rotation.y = lerp_angle(rotation.y, look_rot, 8.0 * delta)

	if nav_agent.avoidance_enabled:
		nav_agent.set_velocity(desired_velocity)
	else:
		_apply_velocity_and_move(desired_velocity, delta)


func _trigger_fallback_reverse_patrol() -> void:
	if not patrol_path or not patrol_path.curve:
		return
	_is_path_blocked = false
	_is_detouring = false
	_is_pushing_obstacle = false
	_is_reversing_patrol = true
	_blocked_waypoint_index = current_waypoint_index

	_patrol_direction = -_patrol_direction
	var curve: Curve3D = patrol_path.curve
	var pt_count := curve.point_count
	if loop_patrol:
		current_waypoint_index = (current_waypoint_index + _patrol_direction + pt_count) % pt_count
	else:
		current_waypoint_index = clampi(current_waypoint_index + _patrol_direction, 0, pt_count - 1)
	_advance_patrol_destination()


func _trigger_unstuck_and_detour() -> void:
	_stuck_timer = 0.0
	if patrol_path and patrol_path.curve:
		var target_wp_pos: Vector3 = patrol_path.to_global(patrol_path.curve.get_point_position(current_waypoint_index))
		nav_agent.target_position = target_wp_pos
		if not nav_agent.is_target_reachable():
			var next_wp := (current_waypoint_index + 1) % patrol_path.curve.point_count
			var next_target := patrol_path.to_global(patrol_path.curve.get_point_position(next_wp))
			nav_agent.target_position = next_target
			if nav_agent.is_target_reachable():
				current_waypoint_index = next_wp
			else:
				if enable_fallback_patrol:
					_trigger_fallback_reverse_patrol()
					return
				else:
					_is_path_blocked = true
					return
	else:
		if not nav_agent.is_target_reachable():
			_is_path_blocked = true
			return

	_avoidance_side = 0.0
	_current_avoidance_angle = 0.0
	_stuck_recovery_dir = global_transform.basis.z # Krok VZAD od prekazky
	_stuck_recovery_dir.y = 0.0
	if _stuck_recovery_dir.length_squared() < 0.01:
		_stuck_recovery_dir = Vector3(0.0, 0.0, 1.0)
	_stuck_recovery_timer = 0.6


func _on_safe_velocity_computed(safe_velocity: Vector3) -> void:
	if is_waiting or _is_path_blocked or _stuck_recovery_timer > 0.0:
		velocity.x = 0.0
		velocity.z = 0.0
		return
	# Called by NavigationServer with RVO-avoided velocity around heavy obstacles
	var delta: float = get_physics_process_delta_time()
	if Vector2(safe_velocity.x, safe_velocity.z).length_squared() > 0.04:
		var travel_rot: float = atan2(safe_velocity.x, safe_velocity.z)
		rotation.y = lerp_angle(rotation.y, travel_rot, 8.0 * delta)
	_apply_velocity_and_move(safe_velocity, delta)


## Casts forward spatial probes to proactively steer around Layer 2 Heavy Obstacles
func _probe_obstacle_avoidance(base_dir: Vector3, delta: float) -> Vector3:
	if _is_detouring:
		_avoidance_side = 0.0
		_current_avoidance_angle = 0.0
		_debug_steer_dir = base_dir
		return base_dir

	var space_state := get_world_3d().direct_space_state
	if not space_state:
		return base_dir

	_debug_probe_records.clear()

	var from_pos := global_position + Vector3(0.0, 0.45, 0.0)
	var mask := (1 << 0) | (1 << 1) # Layer 1: World + Layer 2: Heavy Obstacle
	var probe_dist := 2.6
	var body_half_width := 0.38
	var side_normal := Vector3(-base_dir.z, 0.0, base_dir.x).normalized()

	# 1. Center probe
	var center_q := PhysicsRayQueryParameters3D.create(from_pos, from_pos + base_dir * probe_dist, mask, [get_rid()])
	var center_hit := space_state.intersect_ray(center_q)
	_record_debug_probe(from_pos, from_pos + base_dir * probe_dist, center_hit, "center")

	# 2. Parallel body-width probes (left and right corridor)
	var left_from := from_pos - side_normal * body_half_width
	var right_from := from_pos + side_normal * body_half_width
	var left_q := PhysicsRayQueryParameters3D.create(left_from, left_from + base_dir * (probe_dist * 0.9), mask, [get_rid()])
	var right_q := PhysicsRayQueryParameters3D.create(right_from, right_from + base_dir * (probe_dist * 0.9), mask, [get_rid()])
	var left_hit := space_state.intersect_ray(left_q)
	var right_hit := space_state.intersect_ray(right_q)
	_record_debug_probe(left_from, left_from + base_dir * (probe_dist * 0.9), left_hit, "corridor")
	_record_debug_probe(right_from, right_from + base_dir * (probe_dist * 0.9), right_hit, "corridor")

	# 3. Angled whiskers (30° out)
	var whisker_l_dir := base_dir.rotated(Vector3.UP, deg_to_rad(30.0))
	var whisker_r_dir := base_dir.rotated(Vector3.UP, deg_to_rad(-30.0))
	var whisker_l_q := PhysicsRayQueryParameters3D.create(from_pos, from_pos + whisker_l_dir * 1.8, mask, [get_rid()])
	var whisker_r_q := PhysicsRayQueryParameters3D.create(from_pos, from_pos + whisker_r_dir * 1.8, mask, [get_rid()])
	var whisker_l_hit := space_state.intersect_ray(whisker_l_q)
	var whisker_r_hit := space_state.intersect_ray(whisker_r_q)
	_record_debug_probe(from_pos, from_pos + whisker_l_dir * 1.8, whisker_l_hit, "whisker")
	_record_debug_probe(from_pos, from_pos + whisker_r_dir * 1.8, whisker_r_hit, "whisker")

	var left_blocked := not left_hit.is_empty() or not whisker_l_hit.is_empty()
	var right_blocked := not right_hit.is_empty() or not whisker_r_hit.is_empty()
	var center_blocked := not center_hit.is_empty()

	_forward_blocked = center_blocked or (left_blocked and right_blocked)
	var has_hit := center_blocked or left_blocked or right_blocked

	if not has_hit:
		_avoidance_clear_time += delta
		if _avoidance_clear_time >= 0.2:
			_avoidance_side = 0.0
		_current_avoidance_angle = move_toward(_current_avoidance_angle, 0.0, delta * deg_to_rad(140.0))
		var clear_dir := base_dir
		if not is_zero_approx(_current_avoidance_angle):
			clear_dir = base_dir.rotated(Vector3.UP, _current_avoidance_angle).normalized()
		_debug_steer_dir = clear_dir
		return clear_dir

	_avoidance_clear_time = 0.0

	# Pokud jsou OBE strany zablokovane, NESMI se zatacet do zdi ani do prekazky!
	if left_blocked and right_blocked:
		_avoidance_side = 0.0
		_current_avoidance_angle = move_toward(_current_avoidance_angle, 0.0, delta * deg_to_rad(140.0))
		_debug_steer_dir = base_dir
		return base_dir

	# Vyber strany: vyhybej se POUZE do volneho koridoru
	if left_blocked and not right_blocked:
		_avoidance_side = -1.0 # Uhni vpravo (volno)
	elif right_blocked and not left_blocked:
		_avoidance_side = 1.0 # Uhni vlevo (volno)
	elif not left_blocked and not right_blocked and center_blocked:
		var wide_l_q := PhysicsRayQueryParameters3D.create(from_pos - side_normal * 0.7, (from_pos - side_normal * 0.7) + base_dir * probe_dist, mask, [get_rid()])
		var wide_r_q := PhysicsRayQueryParameters3D.create(from_pos + side_normal * 0.7, (from_pos + side_normal * 0.7) + base_dir * probe_dist, mask, [get_rid()])
		var wide_l_hit := space_state.intersect_ray(wide_l_q)
		var wide_r_hit := space_state.intersect_ray(wide_r_q)
		if not wide_r_hit.is_empty() and wide_l_hit.is_empty():
			_avoidance_side = 1.0
		elif not wide_l_hit.is_empty() and wide_r_hit.is_empty():
			_avoidance_side = -1.0
		else:
			var to_hit: Vector3 = Vector3(center_hit.get("position", from_pos)) - from_pos
			_avoidance_side = 1.0 if side_normal.dot(to_hit) >= 0.0 else -1.0

	# Kontrola bezpecnosti: Pokud strana, kam chceme uhnout, neni volna, okamzite zrusit vyboceni
	if _avoidance_side > 0.0 and left_blocked:
		_avoidance_side = 0.0
	elif _avoidance_side < 0.0 and right_blocked:
		_avoidance_side = 0.0

	# Vypocet minimalni vzdalenosti
	var min_hit_dist := probe_dist
	if not center_hit.is_empty():
		min_hit_dist = minf(min_hit_dist, from_pos.distance_to(center_hit.position))
	if not left_hit.is_empty():
		min_hit_dist = minf(min_hit_dist, left_from.distance_to(left_hit.position))
	if not right_hit.is_empty():
		min_hit_dist = minf(min_hit_dist, right_from.distance_to(right_hit.position))
	if not whisker_l_hit.is_empty():
		min_hit_dist = minf(min_hit_dist, from_pos.distance_to(whisker_l_hit.position))
	if not whisker_r_hit.is_empty():
		min_hit_dist = minf(min_hit_dist, from_pos.distance_to(whisker_r_hit.position))

	var max_steer_deg := 35.0
	var min_steer_deg := 18.0
	var urgency := clampf((probe_dist - min_hit_dist) / (probe_dist - 0.6), 0.0, 1.0)
	var target_deg := lerpf(min_steer_deg, max_steer_deg, urgency) * _avoidance_side

	_current_avoidance_angle = move_toward(_current_avoidance_angle, deg_to_rad(target_deg), delta * deg_to_rad(140.0))
	var res := base_dir
	if not is_zero_approx(_current_avoidance_angle):
		res = base_dir.rotated(Vector3.UP, _current_avoidance_angle).normalized()
	_debug_steer_dir = res
	return res


func _apply_velocity_and_move(target_horizontal_vel: Vector3, delta: float) -> void:
	velocity.x = move_toward(velocity.x, target_horizontal_vel.x, acceleration * delta)
	velocity.z = move_toward(velocity.z, target_horizontal_vel.z, acceleration * delta)

	move_and_slide()

	_handle_physics_push(delta)


func _handle_physics_push(delta: float) -> void:
	var pushing_this_frame := false

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
			# Light debris (cups, cans)
			body.apply_central_impulse(normal_push * push_force)
		elif can_push_obstacles and body.mass <= obstacle_push_mass_limit:
			# Solo pushable obstacle
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


func is_available_for_coop() -> bool:
	return _coop_role == COOP_ROLE_NONE and not _is_path_blocked


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
		return true

	return false


func respond_to_coop_request(caller: Node3D, target_obstacle: RigidBody3D) -> void:
	_coop_role = COOP_ROLE_HELPER
	_coop_partner = caller
	_coop_target_obstacle = target_obstacle
	_coop_push_timer = 0.0
	_saved_pre_coop_waypoint = current_waypoint_index
	_saved_pre_coop_target = nav_agent.target_position

	var assist_pos := caller.global_position + caller.global_transform.basis.x * 1.1
	if nav_agent:
		nav_agent.target_position = assist_pos
	current_speed = run_speed


func finish_coop_help() -> void:
	_coop_role = COOP_ROLE_NONE
	_coop_partner = null
	_coop_target_obstacle = null
	_coop_push_timer = 0.0
	if patrol_path and patrol_path.curve and _saved_pre_coop_waypoint >= 0:
		current_waypoint_index = _saved_pre_coop_waypoint
		_advance_patrol_destination()
	elif _saved_pre_coop_target.length_squared() > 0.1 and nav_agent:
		nav_agent.target_position = _saved_pre_coop_target
	current_speed = walk_speed


func _advance_patrol_destination() -> void:
	if not patrol_path or not nav_agent:
		if target_node and nav_agent:
			nav_agent.target_position = target_node.global_position
		return

	var curve: Curve3D = patrol_path.curve
	if not curve or curve.point_count == 0:
		return

	if current_waypoint_index >= curve.point_count:
		if loop_patrol:
			current_waypoint_index = 0
		else:
			return

	var local_pos: Vector3 = curve.get_point_position(current_waypoint_index)
	var world_pos: Vector3 = patrol_path.global_transform * local_pos
	nav_agent.target_position = world_pos

	var actions: Array = patrol_path.get_meta("_fps_npc_point_actions", [])
	if current_waypoint_index < actions.size():
		var pt_action: Dictionary = actions[current_waypoint_index]
		var act: String = str(pt_action.get("action", "move")).to_lower()
		if act == "run":
			current_speed = run_speed
		elif act == "move":
			current_speed = run_speed if active_locomotion == "run" else walk_speed
		else:
			current_speed = run_speed if active_locomotion == "run" else walk_speed
	else:
		current_speed = run_speed if active_locomotion == "run" else walk_speed


func _process_waypoint_action() -> void:
	if not patrol_path:
		return

	var actions: Array = patrol_path.get_meta("_fps_npc_point_actions", [])
	if current_waypoint_index < actions.size():
		var pt_action: Dictionary = actions[current_waypoint_index]
		var act_name: String = str(pt_action.get("action", "move")).to_lower()
		var duration: float = float(pt_action.get("duration", 0.0))

		match act_name:
			"run":
				active_locomotion = "run"
				current_speed = run_speed
				_advance_waypoint()
			"wait":
				is_waiting = true
				wait_timer = maxf(duration, 0.5)
			"look_at":
				var target_name: String = str(pt_action.get("target", ""))
				if target_name != "":
					var target_obj := get_tree().current_scene.find_child(target_name, true, false) as Node3D
					if target_obj:
						look_at(Vector3(target_obj.global_position.x, global_position.y, target_obj.global_position.z))
				if duration > 0.0:
					is_waiting = true
					wait_timer = duration
				else:
					_advance_waypoint()
			_:
				current_speed = walk_speed
				_advance_waypoint()
	else:
		current_speed = walk_speed
		_advance_waypoint()


func _advance_waypoint() -> void:
	if not patrol_path:
		return
	var curve: Curve3D = patrol_path.curve
	if not curve:
		return

	var pt_count := curve.point_count
	var next_idx := current_waypoint_index + _patrol_direction
	if loop_patrol:
		next_idx = (next_idx + pt_count) % pt_count

	# If reversing due to blockage and we approach the blocked waypoint again, test if it cleared
	if _is_reversing_patrol and next_idx == _blocked_waypoint_index:
		var test_target: Vector3 = patrol_path.to_global(curve.get_point_position(_blocked_waypoint_index))
		nav_agent.target_position = test_target
		if nav_agent.is_target_reachable():
			_is_reversing_patrol = false
			_blocked_waypoint_index = -1
		else:
			# Still blocked: reverse direction again to keep patrolling open area
			_patrol_direction = -_patrol_direction
			next_idx = current_waypoint_index + _patrol_direction
			if loop_patrol:
				next_idx = (next_idx + pt_count) % pt_count

	if loop_patrol:
		current_waypoint_index = next_idx
	else:
		if next_idx >= pt_count:
			_patrol_direction = -1
			current_waypoint_index = pt_count - 2
		elif next_idx < 0:
			_patrol_direction = 1
			current_waypoint_index = 1
		else:
			current_waypoint_index = next_idx
	_advance_patrol_destination()
