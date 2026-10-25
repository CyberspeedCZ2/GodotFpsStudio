class_name NPCController
extends CharacterBody3D

const MOVEMENT_MODE_RANDOM_WANDER: int = 0
const MOVEMENT_MODE_ASSIGNED_PATH: int = 1

const LOCOMOTION_WALK: int = 0
const LOCOMOTION_RUN: int = 1

@export_group("Movement")
@export_enum("Random Wander", "Assigned NPC Path") var movement_mode: int = MOVEMENT_MODE_RANDOM_WANDER
@export var npc_path: Path3D
@export_enum("Nearest Waypoint", "First Waypoint") var route_start_mode: int = 1
@export_enum("Walk", "Run") var locomotion_style: int = LOCOMOTION_WALK
@export var walk_speed: float = 2.8
@export var run_speed: float = 4.8
@export var turn_speed: float = 8.0
@export_range(0.1, 40.0, 0.1) var acceleration: float = 12.0
@export var debug_navigation: bool = false
@export_range(1.0, 45.0, 1.0) var move_alignment_angle_degrees: float = 12.0

@export_group("Health")
@export_range(1.0, 1000.0, 1.0) var max_health: float = 100.0

@export_group("Fear")
@export var fear_enabled: bool = true
@export_range(1.0, 100.0, 1.0) var fear_threshold: float = 30.0
@export_range(0.0, 99.0, 1.0) var fear_proximity_limit: float = 20.0
@export_range(0.0, 100.0, 1.0) var fear_decay_per_second: float = 8.0

var current_speed: float = 2.8
var is_moving: bool = false
var is_turning_to_move: bool = false
var health: float = 100.0
var fear: float = 0.0

var _target_destination: Vector3 = Vector3.ZERO
var _gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity", 9.8)
var _path_waypoint_index: int = 0

@onready var nav_agent: NavigationAgent3D = $NavigationAgent3D
@onready var debug_label: Label3D = get_node_or_null("DebugLabel")
@onready var health_label: Label3D = get_node_or_null("HealthLabel")
@onready var fear_label: Label3D = get_node_or_null("FearLabel")


func _ready() -> void:
	add_to_group("fps_npc")
	health = max_health
	current_speed = walk_speed
	if nav_agent != null and nav_agent.avoidance_enabled:
		nav_agent.velocity_computed.connect(_on_velocity_computed)
	_update_labels()


func _physics_process(delta: float) -> void:
	# 1. Gravitace
	if not is_on_floor():
		velocity.y -= _gravity * delta
	else:
		velocity.y = 0.0

	# 2. Navigace a pohyb
	if is_moving and nav_agent != null:
		if is_at_destination():
			stop()
		else:
			_navigate_towards_target(delta)
	else:
		_decelerate_to_stop(delta)

	# 3. Pohyb tělesa, pokud není aktivní RVO avoidance callback
	if nav_agent == null or not nav_agent.avoidance_enabled:
		move_and_slide()


func _navigate_towards_target(delta: float) -> void:
	var next_position: Vector3 = nav_agent.get_next_path_position()

	var direction := next_position - global_position
	direction.y = 0.0

	if direction.length_squared() > 0.01:
		direction = direction.normalized()
		# Plynulé otáčení těla ve směru pohybu
		var target_yaw := atan2(-direction.x, -direction.z)
		global_rotation.y = _rotate_yaw_toward(global_rotation.y, target_yaw, maxf(turn_speed, 0.01) * delta)

		var desired_horizontal := direction * current_speed
		var current_horizontal := Vector3(velocity.x, 0.0, velocity.z)
		current_horizontal = current_horizontal.move_toward(desired_horizontal, maxf(acceleration, 0.1) * delta)

		if nav_agent.avoidance_enabled:
			nav_agent.set_velocity(current_horizontal)
		else:
			velocity.x = current_horizontal.x
			velocity.z = current_horizontal.z
	else:
		_decelerate_to_stop(delta)


func _decelerate_to_stop(delta: float) -> void:
	var current_horizontal := Vector3(velocity.x, 0.0, velocity.z)
	current_horizontal = current_horizontal.move_toward(Vector3.ZERO, maxf(acceleration, 0.1) * delta)
	if nav_agent != null and nav_agent.avoidance_enabled:
		nav_agent.set_velocity(current_horizontal)
	else:
		velocity.x = current_horizontal.x
		velocity.z = current_horizontal.z


func _on_velocity_computed(safe_velocity: Vector3) -> void:
	velocity.x = safe_velocity.x
	velocity.z = safe_velocity.z
	move_and_slide()


func move_to_position(target: Vector3, speed: float = -1.0) -> void:
	_target_destination = target
	if speed > 0.0:
		current_speed = speed
	else:
		current_speed = run_speed if locomotion_style == LOCOMOTION_RUN else walk_speed

	if nav_agent != null:
		nav_agent.target_position = target

	is_moving = true


func stop() -> void:
	is_moving = false


func is_at_destination() -> bool:
	if nav_agent == null:
		return true
	return nav_agent.is_navigation_finished()


func distance_to_nav_target() -> float:
	return global_position.distance_to(_target_destination)


func uses_npc_path() -> bool:
	return movement_mode == MOVEMENT_MODE_ASSIGNED_PATH and npc_path != null and npc_path.curve != null


func get_current_npc_path_target() -> Vector3:
	if not uses_npc_path() or npc_path.curve.get_point_count() == 0:
		return global_position
	var point_pos: Vector3 = npc_path.curve.get_point_position(_path_waypoint_index)
	return npc_path.to_global(point_pos)


func advance_npc_path_waypoint() -> void:
	if not uses_npc_path():
		return
	var count := npc_path.curve.get_point_count()
	if count > 0:
		_path_waypoint_index = (_path_waypoint_index + 1) % count


func take_damage(amount: float) -> void:
	health = clampf(health - amount, 0.0, max_health)
	_update_labels()
	if health <= 0.0:
		die()


func die() -> void:
	stop()
	update_debug_state("Zemřel")
	queue_free()


func update_debug_state(text: String) -> void:
	if debug_label != null:
		debug_label.text = "[NPC AI]\n%s" % text


func set_navigation_path_debug_enabled(_enabled: bool) -> void:
	pass


func get_vision_camera() -> Camera3D:
	return null


func _update_labels() -> void:
	if health_label != null:
		health_label.text = "HP: %d / %d" % [int(health), int(max_health)]
	if fear_label != null:
		fear_label.text = "STRACH: %d%%" % int(fear)


func _rotate_yaw_toward(current_yaw: float, target_yaw: float, max_delta: float) -> float:
	var diff := wrapf(target_yaw - current_yaw, -PI, PI)
	return current_yaw + clampf(diff, -max_delta, max_delta)
