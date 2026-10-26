class_name NPCController
extends CharacterBody3D

signal state_changed(state: StringName)
signal threat_detected(target: Node3D)
signal interaction_requested(target: Node)
signal attack_requested(target: Node, damage: float)
signal cover_entered(cover_point: Node3D)
signal animation_requested(animation_name: StringName)

const ARCHETYPE_CIVILIAN: int = 0
const ARCHETYPE_HOSTILE: int = 1
const ARCHETYPE_PASSIVE: int = 2
const MOVEMENT_MODE_RANDOM_WANDER: int = 0
const MOVEMENT_MODE_ASSIGNED_PATH: int = 1
const LOCOMOTION_WALK: int = 0
const LOCOMOTION_RUN: int = 1
const ROUTE_ACTIONS_META: StringName = &"_fps_npc_point_actions"

@export_group("Role")
@export_enum("Civilian", "Hostile", "Passive") var archetype: int = ARCHETYPE_CIVILIAN

@export_group("Movement")
@export_enum("Random Wander", "Assigned NPC Path") var movement_mode: int = MOVEMENT_MODE_RANDOM_WANDER
@export var npc_path: Path3D
@export_enum("Nearest Waypoint", "First Waypoint") var route_start_mode: int = 1
@export_enum("Walk", "Run") var locomotion_style: int = LOCOMOTION_WALK
@export var walk_speed: float = 2.8
@export var run_speed: float = 4.8
@export var turn_speed: float = 8.0
@export_range(0.1, 40.0, 0.1) var acceleration: float = 12.0
@export_range(1.0, 45.0, 1.0) var move_alignment_angle_degrees: float = 12.0
@export_range(1.0, 30.0, 0.5) var wander_radius: float = 6.0
@export_range(0.0, 10.0, 0.1) var wander_pause_min_seconds: float = 0.5
@export_range(0.0, 10.0, 0.1) var wander_pause_max_seconds: float = 1.5

@export_group("Awareness")
@export var fear_enabled: bool = true
@export var look_at_player_when_seen: bool = true
@export_range(1.0, 100.0, 1.0) var fear_threshold: float = 70.0
@export_range(1.0, 100.0, 1.0) var alert_radius: float = 10.0
@export_range(0.1, 2.0, 0.1) var sensor_interval: float = 0.2
@export_range(0.0, 60.0, 0.5) var target_memory_seconds: float = 3.0
@export_range(0.0, 100.0, 1.0) var fear_gain_per_second: float = 10.0
@export_range(0.0, 100.0, 1.0) var fear_decay_per_second: float = 8.0
@export var player_group: StringName = &"player"
@export_flags_3d_physics var alert_collision_mask: int = 0xFFFFFFFF

@export_group("Threat Response")
@export_range(2.0, 30.0, 0.5) var flee_distance: float = 10.0
@export_range(0.5, 5.0, 0.1) var attack_range: float = 1.6
@export_range(1.0, 100.0, 1.0) var attack_damage: float = 10.0
@export_range(0.1, 5.0, 0.1) var attack_cooldown: float = 1.2
@export_range(0.5, 5.0, 0.1) var target_repath_distance: float = 1.2

@export_group("Cover")
@export var use_cover_when_threatened: bool = true
@export var cover_point_group: StringName = &"fps_npc_cover_point"
@export_range(1.0, 40.0, 0.5) var cover_search_radius: float = 18.0
@export_flags_3d_physics var cover_obstacle_collision_mask: int = (1 << 0) | (1 << 1)
@export var cover_animation_name: StringName = &"take_cover"

@export_group("Animation")
@export var animation_player_path: NodePath = NodePath("AnimationPlayer")
@export var sit_animation_name: StringName = &"sit"
@export_range(0.1, 5.0, 0.1) var look_at_default_duration: float = 1.2

@export_group("Small Props")
@export_flags_3d_physics var reactive_prop_collision_mask: int = 1 << 7
@export_range(0.0, 30.0, 0.1) var reactive_prop_push_force: float = 4.0
@export_range(0.1, 20.0, 0.1) var reactive_prop_max_mass: float = 3.0

@export_group("Health")
@export_range(1.0, 1000.0, 1.0) var max_health: float = 100.0
@export var debug_navigation: bool = false

var current_speed: float = 2.8
var is_moving: bool = false
var health: float = 100.0
var fear: float = 0.0
var is_sitting: bool = false

var _target_destination: Vector3 = Vector3.ZERO
var _target_set_physics_frame: int = -1
var _gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity", 9.8)
var _path_waypoint_index: int = 0
var _route_initialized: bool = false
var _route_target_active: bool = false
var _route_action_started: bool = false
var _route_action_started_at_msec: int = 0
var _route_action_duration_seconds: float = 0.0
var _route_action_name: StringName = &"move"
var _route_action_target: Node
var _route_next_attack_msec: int = 0
var _wander_goal_active: bool = false
var _wander_pause_until_msec: int = 0
var _flee_goal_active: bool = false
var _flee_retarget_after_msec: int = 0
var _active_cover_point: Node3D
var _is_in_cover: bool = false
var _perceived_target: Node3D
var _threat_memory_until_msec: int = 0
var _forced_alert_until_msec: int = 0
var _next_sensor_check_msec: int = 0
var _last_sensor_check_msec: int = 0
var _physics_delta: float = 1.0 / 60.0
var _fallback_brain_enabled: bool = false
var _candidate_navigation_point: Vector3 = Vector3.ZERO
var _current_state: StringName = &"Idle"
var _rng: RandomNumberGenerator = RandomNumberGenerator.new()

@onready var nav_agent: NavigationAgent3D = get_node_or_null("NavigationAgent3D") as NavigationAgent3D
@onready var debug_label: Label3D = get_node_or_null("DebugLabel") as Label3D
@onready var health_label: Label3D = get_node_or_null("HealthLabel") as Label3D
@onready var fear_label: Label3D = get_node_or_null("FearLabel") as Label3D
@onready var animation_player: AnimationPlayer = get_node_or_null(animation_player_path) as AnimationPlayer


func _ready() -> void:
	_rng.randomize()
	var sensor_start_time: int = Time.get_ticks_msec()
	_last_sensor_check_msec = sensor_start_time
	_next_sensor_check_msec = sensor_start_time
	add_to_group("fps_npc")
	health = max_health
	current_speed = walk_speed
	_fallback_brain_enabled = get_node_or_null("BeehaveTree") == null
	if nav_agent != null and nav_agent.avoidance_enabled:
		nav_agent.velocity_computed.connect(_on_velocity_computed)
	if movement_mode == MOVEMENT_MODE_ASSIGNED_PATH:
		_initialize_route_start_index()
	_update_labels()
	_set_ai_state("Idle")


func _physics_process(delta: float) -> void:
	_physics_delta = delta
	if archetype != ARCHETYPE_PASSIVE:
		_refresh_threat_sensor()
	if not is_on_floor():
		velocity.y -= _gravity * delta
	else:
		velocity.y = 0.0

	if is_moving and not is_at_destination():
		_navigate_towards_target(delta)
	else:
		_decelerate_to_stop(delta)

	if not _uses_rvo():
		move_and_slide()
		_apply_reactive_prop_impulses()

	if _fallback_brain_enabled:
		_tick_fallback_brain()


func _navigate_towards_target(delta: float) -> void:
	if nav_agent == null:
		return
	var next_position: Vector3 = nav_agent.get_next_path_position()
	if nav_agent.is_navigation_finished() and not nav_agent.is_target_reachable():
		_handle_unreachable_navigation_target()
		return
	var direction: Vector3 = next_position - global_position
	direction.y = 0.0
	if direction.length_squared() <= 0.01:
		_decelerate_to_stop(delta)
		return

	direction = direction.normalized()
	_face_direction(direction, delta)
	var target_yaw: float = atan2(-direction.x, -direction.z)
	var yaw_error_degrees: float = rad_to_deg(absf(wrapf(target_yaw - global_rotation.y, -PI, PI)))
	if yaw_error_degrees > move_alignment_angle_degrees:
		_decelerate_to_stop(delta)
		return
	var desired_horizontal: Vector3 = direction * current_speed
	var current_horizontal: Vector3 = Vector3(velocity.x, 0.0, velocity.z)
	current_horizontal = current_horizontal.move_toward(desired_horizontal, maxf(acceleration, 0.1) * delta)
	if _uses_rvo():
		nav_agent.set_velocity(current_horizontal)
	else:
		velocity.x = current_horizontal.x
		velocity.z = current_horizontal.z
	_set_ai_state("Running" if locomotion_style == LOCOMOTION_RUN else "Walking")


func _decelerate_to_stop(delta: float) -> void:
	var current_horizontal: Vector3 = Vector3(velocity.x, 0.0, velocity.z)
	current_horizontal = current_horizontal.move_toward(Vector3.ZERO, maxf(acceleration, 0.1) * delta)
	if _uses_rvo():
		nav_agent.set_velocity(current_horizontal)
	else:
		velocity.x = current_horizontal.x
		velocity.z = current_horizontal.z


func _on_velocity_computed(safe_velocity: Vector3) -> void:
	velocity.x = safe_velocity.x
	velocity.z = safe_velocity.z
	move_and_slide()
	_apply_reactive_prop_impulses()


func _uses_rvo() -> bool:
	return nav_agent != null and nav_agent.avoidance_enabled and nav_agent.get_navigation_map().is_valid()


func _navigation_map_is_synchronized(map: RID) -> bool:
	return map.is_valid() and NavigationServer3D.map_get_iteration_id(map) > 0


func _is_navigation_position_reachable(map: RID, destination: Vector3) -> bool:
	if nav_agent == null or not _navigation_map_is_synchronized(map):
		return false
	var path: PackedVector3Array = NavigationServer3D.map_get_path(
		map, global_position, destination, true, nav_agent.navigation_layers
	)
	if path.is_empty():
		return false
	var final_path_position: Vector3 = path[path.size() - 1]
	var endpoint_tolerance: float = maxf(nav_agent.target_desired_distance, 0.5)
	return final_path_position.distance_to(destination) <= endpoint_tolerance


func _handle_unreachable_navigation_target() -> void:
	stop()
	var now: int = Time.get_ticks_msec()
	if _route_target_active and uses_npc_path():
		advance_npc_path_waypoint()
		_route_target_active = false
		_route_action_started = false
		_route_action_target = null
	_wander_goal_active = false
	_wander_pause_until_msec = now + 250
	_flee_goal_active = false
	_flee_retarget_after_msec = now + 250
	_clear_cover()
	_set_ai_state("Replanning unreachable target")


func move_to_position(target: Vector3, speed: float = -1.0) -> void:
	_target_destination = target
	_target_set_physics_frame = Engine.get_physics_frames()
	if speed > 0.0:
		current_speed = speed
		locomotion_style = LOCOMOTION_RUN if speed >= run_speed else LOCOMOTION_WALK
	else:
		current_speed = run_speed if locomotion_style == LOCOMOTION_RUN else walk_speed
	if nav_agent != null:
		nav_agent.max_speed = maxf(current_speed, 0.1)
		nav_agent.target_position = target
	is_moving = true


func stop() -> void:
	is_moving = false


func is_at_destination() -> bool:
	if nav_agent == null:
		return true
	if Engine.get_physics_frames() <= _target_set_physics_frame:
		return false
	var tolerance: float = maxf(nav_agent.target_desired_distance, 0.2)
	if global_position.distance_to(_target_destination) <= tolerance:
		return true
	return nav_agent.is_navigation_finished() and nav_agent.is_target_reachable()


func distance_to_nav_target() -> float:
	return global_position.distance_to(_target_destination)


func uses_npc_path() -> bool:
	return movement_mode == MOVEMENT_MODE_ASSIGNED_PATH and npc_path != null and npc_path.curve != null and npc_path.curve.get_point_count() > 0


func get_current_npc_path_target() -> Vector3:
	if not uses_npc_path():
		return global_position
	return npc_path.to_global(npc_path.curve.get_point_position(_path_waypoint_index))


func advance_npc_path_waypoint() -> void:
	if not uses_npc_path():
		return
	_path_waypoint_index = (_path_waypoint_index + 1) % npc_path.curve.get_point_count()
	_route_target_active = false
	_route_action_started = false


func _initialize_route_start_index() -> void:
	if not uses_npc_path():
		return
	_path_waypoint_index = 0
	if route_start_mode == 0:
		var best_distance: float = INF
		for index: int in range(npc_path.curve.get_point_count()):
			var point_position: Vector3 = npc_path.to_global(npc_path.curve.get_point_position(index))
			var distance: float = global_position.distance_squared_to(point_position)
			if distance < best_distance:
				best_distance = distance
				_path_waypoint_index = index
	_route_initialized = true


func tick_assigned_route() -> bool:
	if not uses_npc_path():
		stop()
		_set_ai_state("Route not assigned")
		return false
	if not _route_initialized:
		_initialize_route_start_index()

	if not _route_target_active and not _route_action_started:
		move_to_position(get_current_npc_path_target())
		_route_target_active = true
		_set_ai_state("Patrol")
		return true

	if _route_target_active:
		if not is_at_destination():
			return true
		stop()
		_route_target_active = false
		_route_action_started = false

	return _tick_current_route_action()


func _get_current_route_action() -> Dictionary:
	var default_action: Dictionary = {"action": "move", "duration": 0.0, "target": ""}
	if not uses_npc_path():
		return default_action
	var actions: Array = npc_path.get_meta(ROUTE_ACTIONS_META, [])
	if _path_waypoint_index < 0 or _path_waypoint_index >= actions.size():
		return default_action
	var action_variant: Variant = actions[_path_waypoint_index]
	if action_variant is Dictionary:
		var action_dictionary: Dictionary = action_variant
		return action_dictionary
	return default_action


func _tick_current_route_action() -> bool:
	var action_data: Dictionary = _get_current_route_action()
	if not _route_action_started:
		_begin_route_action(action_data)
	var elapsed: float = float(Time.get_ticks_msec() - _route_action_started_at_msec) / 1000.0

	match _route_action_name:
		&"move":
			locomotion_style = LOCOMOTION_WALK
			current_speed = walk_speed
			_finish_route_action()
		&"run":
			locomotion_style = LOCOMOTION_RUN
			current_speed = run_speed
			_finish_route_action()
		&"wait":
			_set_ai_state("Waiting")
			if elapsed >= _route_action_duration_seconds:
				_finish_route_action()
		&"sit":
			_set_ai_state("Sitting")
			if _route_action_duration_seconds > 0.0 and elapsed >= _route_action_duration_seconds:
				_finish_route_action()
		&"look_at":
			_set_ai_state("Looking")
			if _route_action_target is Node3D:
				_face_toward_point((_route_action_target as Node3D).global_position, _physics_delta)
			if elapsed >= _route_action_duration_seconds:
				_finish_route_action()
		&"play_animation":
			_set_ai_state("Playing animation")
			if elapsed >= _route_action_duration_seconds:
				_finish_route_action()
		&"interact":
			_set_ai_state("Interacting")
			if elapsed >= _route_action_duration_seconds:
				_finish_route_action()
		&"attack":
			_set_ai_state("Attacking")
			if _route_action_target != null and Time.get_ticks_msec() >= _route_next_attack_msec:
				_perform_attack(_route_action_target)
				_route_next_attack_msec = Time.get_ticks_msec() + int(attack_cooldown * 1000.0)
			if elapsed >= _route_action_duration_seconds:
				_finish_route_action()
		&"take_cover":
			_set_ai_state("Taking cover")
			if _route_action_target is Node3D:
				_face_toward_point((_route_action_target as Node3D).global_position, _physics_delta)
			if _route_action_duration_seconds > 0.0 and elapsed >= _route_action_duration_seconds:
				_finish_route_action()
		_:
			_finish_route_action()
	return true


func _begin_route_action(action_data: Dictionary) -> void:
	_route_action_name = StringName(str(action_data.get("action", "move")))
	_route_action_started = true
	_route_action_started_at_msec = Time.get_ticks_msec()
	_route_action_duration_seconds = maxf(float(action_data.get("duration", 0.0)), 0.0)
	_route_action_target = null
	_route_next_attack_msec = 0
	is_sitting = _route_action_name == &"sit"
	var target_text: String = str(action_data.get("target", ""))

	match _route_action_name:
		&"sit":
			var animation_name: StringName = StringName(target_text) if not target_text.is_empty() else sit_animation_name
			_play_npc_animation(animation_name)
			if _route_action_duration_seconds <= 0.0:
				_route_action_duration_seconds = 0.0
		&"play_animation":
			var animation_name: StringName = StringName(target_text)
			var animation_length: float = _play_npc_animation(animation_name)
			if _route_action_duration_seconds <= 0.0:
				_route_action_duration_seconds = animation_length
		&"look_at":
			_route_action_target = _resolve_route_target(target_text)
			if _route_action_duration_seconds <= 0.0:
				_route_action_duration_seconds = look_at_default_duration
		&"interact":
			_route_action_target = _resolve_route_target(target_text)
			_dispatch_interaction(_route_action_target)
		&"attack":
			_route_action_target = _resolve_route_target(target_text)
			if _route_action_target == null:
				_route_action_target = get_active_threat_target()
		&"take_cover":
			_route_action_target = _resolve_route_target(target_text)
		_:
			pass


func _finish_route_action() -> void:
	is_sitting = false
	advance_npc_path_waypoint()
	_route_target_active = false
	_route_action_started = false
	_route_action_target = null
	_route_action_duration_seconds = 0.0


func _resolve_route_target(target_text: String) -> Node:
	if target_text.is_empty():
		return null
	var target_path: NodePath = NodePath(target_text)
	var target_node: Node = get_node_or_null(target_path)
	if target_node != null:
		return target_node
	var current_scene: Node = get_tree().current_scene
	if current_scene != null:
		return current_scene.get_node_or_null(target_path)
	return null


func _dispatch_interaction(target: Node) -> void:
	interaction_requested.emit(target)
	if target == null:
		return
	if target.has_method("interact_with_npc"):
		target.call("interact_with_npc", self)
	elif target.has_method("interact"):
		target.call("interact", self)
	elif target.has_method("activate"):
		target.call("activate")


func _play_npc_animation(animation_name: StringName) -> float:
	animation_requested.emit(animation_name)
	if animation_player == null or animation_name.is_empty() or not animation_player.has_animation(animation_name):
		return 0.0
	animation_player.play(animation_name)
	var animation: Animation = animation_player.get_animation(animation_name)
	return animation.length if animation != null else 0.0


func _perform_attack(target: Node) -> void:
	attack_requested.emit(target, attack_damage)
	if target.has_method("take_damage"):
		target.call("take_damage", attack_damage)


func interrupt_route_following() -> void:
	stop()
	_route_target_active = false
	_route_action_started = false
	_route_action_target = null
	if is_sitting and animation_player != null:
		animation_player.stop()
	is_sitting = false


func tick_free_wander() -> bool:
	if movement_mode != MOVEMENT_MODE_RANDOM_WANDER:
		return false
	var now: int = Time.get_ticks_msec()
	if is_moving and not is_at_destination():
		_set_ai_state("Wandering")
		return true
	if is_moving and is_at_destination():
		stop()
		_wander_goal_active = false
		var pause_seconds: float = _rng.randf_range(wander_pause_min_seconds, maxf(wander_pause_min_seconds, wander_pause_max_seconds))
		_wander_pause_until_msec = now + int(pause_seconds * 1000.0)
	if now < _wander_pause_until_msec:
		_set_ai_state("Pausing")
		return true
	if not _wander_goal_active:
		if not _try_choose_random_navigation_point(wander_radius):
			_wander_pause_until_msec = now + 100
			_set_ai_state("Waiting for NavigationMesh")
			return true
		move_to_position(_candidate_navigation_point)
		_wander_goal_active = true
		_set_ai_state("Wandering")
	return true


func interrupt_free_wander() -> void:
	stop()
	_wander_goal_active = false


func _try_choose_random_navigation_point(radius: float) -> bool:
	if nav_agent == null:
		return false
	var map: RID = nav_agent.get_navigation_map()
	if not _navigation_map_is_synchronized(map):
		return false
	for attempt: int in range(12):
		var angle: float = _rng.randf_range(0.0, TAU)
		var distance: float = sqrt(_rng.randf()) * radius
		var desired: Vector3 = global_position + Vector3(cos(angle), 0.0, sin(angle)) * distance
		if not NavigationServer3D.map_get_closest_point_owner(map, desired).is_valid():
			continue
		var point: Vector3 = NavigationServer3D.map_get_closest_point(map, desired)
		if not _is_navigation_position_reachable(map, point):
			continue
		if global_position.distance_to(point) < maxf(nav_agent.target_desired_distance * 2.0, 1.0):
			continue
		_candidate_navigation_point = point
		return true
	return false


func _try_find_flee_destination(target: Node3D) -> bool:
	if nav_agent == null:
		return false
	var map: RID = nav_agent.get_navigation_map()
	if not _navigation_map_is_synchronized(map):
		return false
	var away: Vector3 = global_position - target.global_position
	away.y = 0.0
	if away.length_squared() <= 0.01:
		away = Vector3.FORWARD
	away = away.normalized()
	var found: bool = false
	var best_score: float = -INF
	for index: int in range(9):
		var angle: float = float(index - 4) * 0.38
		var desired: Vector3 = global_position + away.rotated(Vector3.UP, angle) * flee_distance
		if not NavigationServer3D.map_get_closest_point_owner(map, desired).is_valid():
			continue
		var point: Vector3 = NavigationServer3D.map_get_closest_point(map, desired)
		if not _is_navigation_position_reachable(map, point):
			continue
		var score: float = point.distance_squared_to(target.global_position)
		if score > best_score:
			best_score = score
			_candidate_navigation_point = point
			found = true
	return found


func _tick_fallback_brain() -> void:
	if has_active_threat():
		tick_threat_response()
	elif uses_npc_path():
		tick_assigned_route()
	elif movement_mode == MOVEMENT_MODE_RANDOM_WANDER:
		tick_free_wander()
	else:
		stop()


func tick_threat_response() -> void:
	var target: Node3D = get_active_threat_target()
	if target == null:
		return
	if archetype == ARCHETYPE_CIVILIAN:
		_tick_civilian_flee(target)
	elif archetype == ARCHETYPE_HOSTILE:
		_tick_hostile_response(target)


func _tick_civilian_flee(target: Node3D) -> void:
	var now: int = Time.get_ticks_msec()
	if not _flee_goal_active or (is_at_destination() and now >= _flee_retarget_after_msec):
		if _try_find_flee_destination(target):
			move_to_position(_candidate_navigation_point, run_speed)
			_flee_goal_active = true
			_flee_retarget_after_msec = now + 800
			_set_ai_state("Fleeing")
		else:
			_flee_goal_active = false
			stop()
			_set_ai_state("Afraid; no escape route")
	elif not is_moving:
		move_to_position(_target_destination, run_speed)


func _tick_hostile_response(target: Node3D) -> void:
	var distance: float = global_position.distance_to(target.global_position)
	if distance <= attack_range:
		_clear_cover()
		stop()
		_face_toward_point(target.global_position, _physics_delta)
		_set_ai_state("Attacking")
		if Time.get_ticks_msec() >= _route_next_attack_msec:
			_perform_attack(target)
			_route_next_attack_msec = Time.get_ticks_msec() + int(attack_cooldown * 1000.0)
		return

	if use_cover_when_threatened and nav_agent != null and _navigation_map_is_synchronized(nav_agent.get_navigation_map()):
		if not is_instance_valid(_active_cover_point):
			_active_cover_point = _find_cover_point(target)
		if is_instance_valid(_active_cover_point):
			var cover_position: Vector3 = NavigationServer3D.map_get_closest_point(nav_agent.get_navigation_map(), _active_cover_point.global_position)
			var cover_tolerance: float = maxf(nav_agent.target_desired_distance, 0.35)
			if global_position.distance_to(cover_position) > cover_tolerance:
				if not is_moving or _target_destination.distance_to(cover_position) > target_repath_distance:
					move_to_position(cover_position, run_speed)
				_set_ai_state("Moving to cover")
				return
			stop()
			if not _is_in_cover:
				_is_in_cover = true
				if not cover_animation_name.is_empty():
					_play_npc_animation(cover_animation_name)
				cover_entered.emit(_active_cover_point)
			var cover_facing: Vector3 = -_active_cover_point.global_transform.basis.z
			_face_direction(cover_facing, _physics_delta)
			_set_ai_state("Holding cover")
			return

	if not is_moving or _target_destination.distance_to(target.global_position) > target_repath_distance:
		move_to_position(target.global_position, run_speed)
	_set_ai_state("Pursuing target")


func _find_cover_point(target: Node3D) -> Node3D:
	if nav_agent == null:
		return null
	var map: RID = nav_agent.get_navigation_map()
	if not _navigation_map_is_synchronized(map):
		return null
	var best_point: Node3D
	var best_distance_squared: float = INF
	for candidate: Node in get_tree().get_nodes_in_group(cover_point_group):
		var cover_point: Node3D = candidate as Node3D
		if cover_point == null or cover_point == self:
			continue
		if cover_point.has_meta("fps_cover_enabled") and not bool(cover_point.get_meta("fps_cover_enabled")):
			continue
		var distance_squared: float = global_position.distance_squared_to(cover_point.global_position)
		if distance_squared > cover_search_radius * cover_search_radius or distance_squared >= best_distance_squared:
			continue
		if not NavigationServer3D.map_get_closest_point_owner(map, cover_point.global_position).is_valid():
			continue
		if not _is_navigation_position_reachable(map, cover_point.global_position):
			continue
		if not _cover_point_blocks_line_of_sight(cover_point, target):
			continue
		best_point = cover_point
		best_distance_squared = distance_squared
	return best_point


func _cover_point_blocks_line_of_sight(cover_point: Node3D, target: Node3D) -> bool:
	var start: Vector3 = cover_point.global_position + Vector3.UP * 1.0
	var finish: Vector3 = target.global_position + Vector3.UP * 1.0
	var query: PhysicsRayQueryParameters3D = PhysicsRayQueryParameters3D.create(start, finish)
	query.collision_mask = cover_obstacle_collision_mask
	var excluded_rids: Array[RID] = [get_rid()]
	var target_body: CollisionObject3D = target as CollisionObject3D
	if target_body != null:
		excluded_rids.append(target_body.get_rid())
	query.exclude = excluded_rids
	var result: Dictionary = get_world_3d().direct_space_state.intersect_ray(query)
	return not result.is_empty()


func _clear_cover() -> void:
	if _is_in_cover and animation_player != null and animation_player.current_animation == cover_animation_name:
		animation_player.stop()
	_active_cover_point = null
	_is_in_cover = false


func interrupt_threat_response() -> void:
	stop()
	_flee_goal_active = false
	_clear_cover()
	_set_ai_state("Returning to routine")


func has_active_threat() -> bool:
	if archetype == ARCHETYPE_PASSIVE:
		return false
	var target: Node3D = get_active_threat_target()
	if target == null:
		return false
	if archetype == ARCHETYPE_CIVILIAN and fear_enabled and fear < fear_threshold:
		return false
	return true


func has_player_to_observe() -> bool:
	return get_player_observation_target() != null


func get_player_observation_target() -> Node3D:
	if archetype != ARCHETYPE_CIVILIAN or not look_at_player_when_seen:
		return null
	_refresh_threat_sensor()
	var now: int = Time.get_ticks_msec()
	if now <= _threat_memory_until_msec or now <= _forced_alert_until_msec:
		if is_instance_valid(_perceived_target):
			return _perceived_target
	return null


func tick_player_observation() -> bool:
	var target: Node3D = get_player_observation_target()
	if target == null:
		return false
	stop()
	_face_toward_point(target.global_position, _physics_delta)
	_set_ai_state("Observing player")
	return true


func get_active_threat_target() -> Node3D:
	if archetype == ARCHETYPE_PASSIVE:
		return null
	_refresh_threat_sensor()
	var now: int = Time.get_ticks_msec()
	if now <= _threat_memory_until_msec or now <= _forced_alert_until_msec:
		if is_instance_valid(_perceived_target):
			return _perceived_target
	return null


func raise_alert(source: Node3D, intensity: float = 1.0, duration: float = 4.0) -> void:
	if source == null:
		return
	var was_active: bool = get_active_threat_target() != null
	_perceived_target = source
	var now: int = Time.get_ticks_msec()
	_forced_alert_until_msec = now + int(maxf(duration, 0.0) * 1000.0)
	_threat_memory_until_msec = maxi(_threat_memory_until_msec, _forced_alert_until_msec)
	fear = clampf(fear + clampf(intensity, 0.0, 1.0) * 100.0, 0.0, 100.0)
	_update_labels()
	if not was_active:
		threat_detected.emit(source)


func _refresh_threat_sensor() -> void:
	var now: int = Time.get_ticks_msec()
	if now < _next_sensor_check_msec:
		return
	var elapsed: float = maxf(float(now - _last_sensor_check_msec) / 1000.0, sensor_interval)
	_last_sensor_check_msec = now
	_next_sensor_check_msec = now + int(sensor_interval * 1000.0)
	var previous_target: Node3D = _perceived_target
	var visible_target: Node3D = _find_visible_player()
	if visible_target != null:
		_perceived_target = visible_target
		_threat_memory_until_msec = now + int(target_memory_seconds * 1000.0)
		if archetype == ARCHETYPE_HOSTILE:
			fear = clampf(fear + fear_gain_per_second * elapsed, 0.0, 100.0)
		if previous_target != visible_target:
			threat_detected.emit(visible_target)
	else:
		fear = maxf(fear - fear_decay_per_second * elapsed, 0.0)
		if now > _threat_memory_until_msec and now > _forced_alert_until_msec:
			_perceived_target = null
	_update_labels()


func _find_visible_player() -> Node3D:
	for candidate: Node in get_tree().get_nodes_in_group(player_group):
		var player: Node3D = candidate as Node3D
		if player == null or player == self:
			continue
		if global_position.distance_to(player.global_position) > alert_radius:
			continue
		if _has_line_of_sight_to(player):
			return player
	return null


func _has_line_of_sight_to(target: Node3D) -> bool:
	var space_state: PhysicsDirectSpaceState3D = get_world_3d().direct_space_state
	var from: Vector3 = global_position + Vector3.UP * 1.4
	var to: Vector3 = target.global_position + Vector3.UP * 1.0
	var query: PhysicsRayQueryParameters3D = PhysicsRayQueryParameters3D.create(from, to)
	query.collision_mask = alert_collision_mask
	query.exclude = [get_rid()]
	var result: Dictionary = space_state.intersect_ray(query)
	return result.is_empty() or result.get("collider") == target


func _apply_reactive_prop_impulses() -> void:
	if reactive_prop_push_force <= 0.0 or reactive_prop_collision_mask == 0:
		return
	var horizontal_velocity: Vector3 = Vector3(velocity.x, 0.0, velocity.z)
	if horizontal_velocity.length_squared() <= 0.01:
		return
	for collision_index: int in range(get_slide_collision_count()):
		var collision: KinematicCollision3D = get_slide_collision(collision_index)
		var body: RigidBody3D = collision.get_collider() as RigidBody3D
		if body == null or body.mass > reactive_prop_max_mass:
			continue
		if (body.collision_layer & reactive_prop_collision_mask) == 0:
			continue
		var push_direction: Vector3 = body.global_position - global_position
		push_direction.y = 0.0
		if push_direction.length_squared() <= 0.01:
			push_direction = horizontal_velocity
		body.apply_central_impulse(push_direction.normalized() * reactive_prop_push_force * _physics_delta)


func take_damage(
	amount: float,
	_hit_position: Vector3 = Vector3.ZERO,
	_hit_direction: Vector3 = Vector3.ZERO
) -> void:
	health = clampf(health - amount, 0.0, max_health)
	if amount > 0.0 and health > 0.0 and archetype == ARCHETYPE_CIVILIAN:
		var attacker: Node3D = _find_visible_player()
		if attacker == null:
			attacker = get_tree().get_first_node_in_group(player_group) as Node3D
		if attacker != null:
			raise_alert(attacker, 1.0, maxf(target_memory_seconds, 4.0))
	_update_labels()
	if health <= 0.0:
		die()


func die() -> void:
	stop()
	_set_ai_state("Dead")
	queue_free()


func _set_ai_state(state: StringName) -> void:
	if _current_state == state:
		return
	_current_state = state
	state_changed.emit(state)
	update_debug_state(String(state))


func update_debug_state(text: String) -> void:
	if debug_label != null:
		debug_label.text = "[NPC AI]\n%s" % text


func set_navigation_path_debug_enabled(enabled: bool) -> void:
	debug_navigation = enabled


func get_vision_camera() -> Camera3D:
	var vision_camera: Camera3D = get_node_or_null("VisionCamera") as Camera3D
	if vision_camera != null:
		return vision_camera
	return find_child("Camera3D", true, false) as Camera3D


func _update_labels() -> void:
	if health_label != null:
		health_label.text = "HP: %d / %d" % [int(health), int(max_health)]
	if fear_label != null:
		fear_label.text = "STRACH: %d%%" % int(fear)


func _face_direction(direction: Vector3, delta: float) -> void:
	if direction.length_squared() <= 0.0001:
		return
	var target_yaw: float = atan2(-direction.x, -direction.z)
	global_rotation.y = _rotate_yaw_toward(global_rotation.y, target_yaw, maxf(turn_speed, 0.01) * delta)


func _face_toward_point(point: Vector3, delta: float) -> void:
	var direction: Vector3 = point - global_position
	direction.y = 0.0
	_face_direction(direction, delta)


func _rotate_yaw_toward(current_yaw: float, target_yaw: float, max_delta: float) -> float:
	var difference: float = wrapf(target_yaw - current_yaw, -PI, PI)
	return current_yaw + clampf(difference, -max_delta, max_delta)
