# meta-description: FPS NPC controller with RVO dynamic obstacle avoidance, path patrol, action execution, and debris physics filtering

extends _BASE_

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

## Target destination (if not patrolling a path)
@export var target_node: Node3D

@onready var nav_agent: NavigationAgent3D = get_node_or_null("NavigationAgent3D")

var current_waypoint_index: int = 0
var is_waiting: bool = false
var wait_timer: float = 0.0
var current_speed: float = 3.2
var active_locomotion: String = "move"
var gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity", 9.8)


func _ready() -> void:
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
		nav_agent.velocity_computed.connect(_on_safe_velocity_computed)

	current_speed = walk_speed
	_advance_patrol_destination()


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

	# If reached current waypoint/destination
	if nav_agent.is_navigation_finished():
		if patrol_path:
			_process_waypoint_action()
		return

	var to_target: Vector3 = Vector3.ZERO
	if patrol_path and patrol_path.curve:
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
		to_target = carrot_world - global_position
		to_target.y = 0.0
	else:
		var next_path_pos: Vector3 = nav_agent.get_next_path_position()
		to_target = next_path_pos - global_position
		to_target.y = 0.0

	var desired_velocity: Vector3 = Vector3.ZERO
	if to_target.length_squared() > 0.04:
		var move_dir: Vector3 = to_target.normalized()
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
		# Smoothly rotate towards movement direction
		var look_rot: float = atan2(move_dir.x, move_dir.z)
		rotation.y = lerp_angle(rotation.y, look_rot, 8.0 * delta)

	if nav_agent.avoidance_enabled:
		nav_agent.set_velocity(desired_velocity)
	else:
		_apply_velocity_and_move(desired_velocity, delta)


func _on_safe_velocity_computed(safe_velocity: Vector3) -> void:
	if is_waiting:
		velocity.x = 0.0
		velocity.z = 0.0
		return
	# Called by NavigationServer with RVO-avoided velocity around heavy obstacles
	var delta: float = get_physics_process_delta_time()
	_apply_velocity_and_move(safe_velocity, delta)


func _apply_velocity_and_move(target_horizontal_vel: Vector3, delta: float) -> void:
	velocity.x = move_toward(velocity.x, target_horizontal_vel.x, acceleration * delta)
	velocity.z = move_toward(velocity.z, target_horizontal_vel.z, acceleration * delta)

	move_and_slide()

	# Physics filtering: push light debris out of the way smoothly
	for i in get_slide_collision_count():
		var col := get_slide_collision(i)
		var body := col.get_collider() as RigidBody3D
		if body and body.mass <= push_mass_limit:
			var push_dir := -col.get_normal()
			push_dir.y = 0.0
			if push_dir.length_squared() > 0.01:
				body.apply_central_impulse(push_dir.normalized() * push_force)


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
	current_waypoint_index += 1
	if current_waypoint_index >= curve.point_count:
		if loop_patrol:
			current_waypoint_index = 0
		else:
			return
	_advance_patrol_destination()
