extends CharacterBody3D

class_name PlayerController

# Movement parameters
@export var move_speed: float = 5.0
@export var sprint_speed: float = 8.0
@export var jump_force: float = 4.5
@export var mouse_sensitivity: float = 0.002
@export var gravity: float = 9.8

# Camera
@onready var camera_pivot = $CameraPivot
@onready var camera = $CameraPivot/Camera3D

# Components
@onready var animation_player = $AnimationPlayer if has_node("AnimationPlayer") else null
@onready var model = $PlayerModel if has_node("PlayerModel") else self

# State
var current_speed: float = 0.0
var is_sprinting: bool = false
var is_jumping: bool = false
var is_crouching: bool = false
var is_grounded: bool = true

# Input
var move_input: Vector2 = Vector2.ZERO
var look_input: Vector2 = Vector2.ZERO

# Network
var network_manager
var last_sent_position: Vector3 = Vector3.ZERO
var position_update_rate: float = 0.1  # 10 times per second
var last_update_time: float = 0.0

func _ready():
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
	setup_camera()

func _physics_process(delta):
	process_movement(delta)
	process_camera(delta)
	process_animation(delta)
	
	# Network updates
	if network_manager and Time.get_ticks_msec() - last_update_time > position_update_rate * 1000:
		send_position_update()
		last_update_time = Time.get_ticks_msec()

func _input(event):
	# Mouse look
	if event is InputEventMouseMotion and Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED:
		look_input = event.relative * mouse_sensitivity
	
	# Mouse click
	if event is InputEventMouseButton:
		if event.pressed:
			if event.button_index == MOUSE_BUTTON_LEFT:
				perform_attack()
			elif event.button_index == MOUSE_BUTTON_RIGHT:
				perform_interaction()
	
	# Keyboard
	if event is InputEventKey:
		if event.pressed:
			if event.keycode == KEY_ESCAPE:
				toggle_mouse_capture()
			elif event.keycode == KEY_SHIFT:
				is_sprinting = true
			elif event.keycode == KEY_CTRL:
				toggle_crouch()

func process_movement(delta):
	# Get input
	move_input = Input.get_vector("move_left", "move_right", "move_backward", "move_forward")
	
	# Apply gravity
	if not is_on_floor():
		velocity.y -= gravity * delta
		is_grounded = false
	else:
		is_grounded = true
		if is_jumping:
			velocity.y = jump_force
			is_jumping = false
	
	# Calculate movement
	var speed = sprint_speed if is_sprinting else move_speed
	var direction = Vector3.ZERO
	
	if move_input.length() > 0:
		# Get camera forward and right vectors
		var camera_forward = -camera_pivot.global_transform.basis.z
		var camera_right = camera_pivot.global_transform.basis.x
		
		# Flatten vectors
		camera_forward.y = 0
		camera_right.y = 0
		camera_forward = camera_forward.normalized()
		camera_right = camera_right.normalized()
		
		# Calculate direction
		direction = camera_forward * move_input.y + camera_right * move_input.x
		direction = direction.normalized()
		
		# Apply speed
		velocity.x = direction.x * speed
		velocity.z = direction.z * speed
		
		current_speed = speed
	else:
		# Apply friction
		velocity.x = lerp(velocity.x, 0.0, 0.2)
		velocity.z = lerp(velocity.z, 0.0, 0.2)
		current_speed = 0.0
	
	# Apply crouch
	if is_crouching:
		velocity.x *= 0.5
		velocity.z *= 0.5
	
	# Move the character
	move_and_slide()

func process_camera(_delta):
	# Horizontal rotation (Y axis)
	camera_pivot.rotate_y(-look_input.x)
	
	# Vertical rotation (X axis)
	camera.rotate_x(-look_input.y)
	camera.rotation.x = clamp(camera.rotation.x, deg_to_rad(-70), deg_to_rad(70))
	
	# Reset look input
	look_input = Vector2.ZERO

func process_animation(delta):
	if animation_player:
		if is_grounded:
			if current_speed > 0:
				if is_sprinting:
					play_anim("run")
				else:
					play_anim("walk")
			else:
				if is_crouching:
					play_anim("crouch_idle")
				else:
					play_anim("idle")
		else:
			if velocity.y > 0:
				play_anim("jump_up")
			else:
				play_anim("jump_down")
		
		if move_input.length() > 0 and is_grounded:
			var direction = Vector3(velocity.x, 0, velocity.z).normalized()
			if direction.length() > 0.1:
				var target_rotation = atan2(direction.x, direction.z)
				model.rotation.y = lerp_angle(model.rotation.y, target_rotation, 10.0 * delta)

func play_anim(anim_name: String):
	if animation_player.has_animation(anim_name):
		animation_player.play(anim_name)

func setup_camera():
	# Set initial camera position
	camera_pivot.position.y = 1.5  # Eye level

func toggle_mouse_capture():
	if Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED:
		Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
	else:
		Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)

func toggle_crouch():
	is_crouching = !is_crouching
	if is_crouching:
		camera_pivot.position.y = 0.8
	else:
		camera_pivot.position.y = 1.5

func jump():
	if is_grounded and not is_jumping:
		is_jumping = true

func perform_attack():
	play_anim("attack")
	
	# Send combat event to server
	if network_manager:
		network_manager.send_json_message(
			NetworkManager.MessageType.MESSAGE_TYPE_COMBAT_EVENT,
			{"action": "attack", "timestamp": Time.get_ticks_msec()}
		)

func perform_interaction():
	# Raycast for interaction
	var ray_length = 5.0
	var from = camera.global_transform.origin
	var to = from - camera.global_transform.basis.z * ray_length
	
	var space_state = get_world_3d().direct_space_state
	var query = PhysicsRayQueryParameters3D.create(from, to)
	query.exclude = [self]
	
	var result = space_state.intersect_ray(query)
	if result:
		var collider = result.collider
		if collider is Area3D:
			# Check if it's an interactable entity
			pass

func send_position_update():
	if network_manager and network_manager.connection_state == NetworkManager.ConnectionState.CONNECTED:
		var current_position = global_transform.origin
		if current_position.distance_to(last_sent_position) > 0.1:
			network_manager.update_player_position(
				current_position,
				rotation,
				velocity
			)
			last_sent_position = current_position

func set_network_manager(manager):
	network_manager = manager

func apply_position_correction(corrected_position: Vector3):
	# Smoothly interpolate to corrected position
	global_transform.origin = global_transform.origin.lerp(corrected_position, 0.3)

func _on_grounded_changed(is_now_grounded: bool):
	is_grounded = is_now_grounded

func get_camera() -> Camera3D:
	return camera

func set_model_visibility(vis: bool):
	if model:
		model.visible = vis