extends Node3D

class_name GameClient

# Scene references
@onready var world: GameWorld = $World
@onready var player: PlayerController = $Player
@onready var ui: Control = $UI
@onready var network_manager: NetworkManager = $NetworkManager
@onready var model_generator: ModelGenerator = $ModelGenerator
@onready var entity_manager: EntityManager = $EntityManager
@onready var config_loader: ConfigLoader = $ConfigLoader

# UI references
@onready var main_menu: MainMenu = $UI/MainMenu
@onready var login_menu: LoginMenu = $UI/LoginMenu
@onready var settings_menu: SettingsMenu = $UI/SettingsMenu
@onready var hud: GameHUD = $UI/HUD
@onready var pause_menu: PauseMenu = $PauseMenu

# Game state
var current_chunks: Dictionary = {}
var entities: Dictionary = {}
var player_entity_id: int = 0
var local_player_position: Vector3 = Vector3.ZERO
var game_state: String = "menu"  # menu, playing, paused

# Settings
var server_address: String = "127.0.0.1"
var server_port: int = 8080
var username: String = ""
var password: String = ""
var render_distance: int = 3
var chunk_size: int = 32
var chunk_height: int = 64

# Performance
var lod_distances: Array = [20.0, 40.0, 80.0]
var max_entities: int = 100

func _ready():
	load_settings()
	setup_menu_signals()
	setup_network_signals()
	initialize_world()
	show_main_menu()

func _process(delta):
	if game_state == "playing":
		update_gameplay(delta)

func _input(event):
	if game_state == "playing":
		if event is InputEventKey:
			if event.pressed and event.keycode == KEY_F1:
				hud.toggle_debug()
			elif event.pressed and event.keycode == KEY_F2:
				world.toggle_wireframe()
			elif event.pressed and event.keycode == KEY_F3:
				network_manager.prediction_enabled = not network_manager.prediction_enabled

func initialize_world():
	world.initialize(chunk_size, chunk_height, render_distance, lod_distances)
	if entity_manager:
		entity_manager.max_entities = max_entities
		entity_manager.despawn_distance = 150.0

func setup_menu_signals():
	main_menu.play_pressed.connect(_on_play_pressed)
	main_menu.local_play_pressed.connect(_on_local_play_pressed)
	main_menu.settings_pressed.connect(_on_main_settings_pressed)
	main_menu.quit_pressed.connect(_on_quit_pressed)
	
	login_menu.login_submitted.connect(_on_login_submitted)
	login_menu.back_pressed.connect(_on_login_back_pressed)
	
	settings_menu.back_pressed.connect(_on_settings_back_pressed)
	settings_menu.settings_saved.connect(_on_settings_saved)
	
	hud.chat_submitted.connect(_on_chat_submitted)
	
	pause_menu.resume_pressed.connect(_on_resume_pressed)
	pause_menu.settings_pressed.connect(_on_pause_settings_pressed)
	pause_menu.disconnect_pressed.connect(_on_disconnect_pressed)
	pause_menu.quit_pressed.connect(_on_quit_pressed)

func setup_network_signals():
	network_manager.connection_state_changed.connect(_on_connection_state_changed)
	network_manager.authentication_result.connect(_on_authentication_result)
	network_manager.message_received.connect(_on_message_received)
	network_manager.error_occurred.connect(_on_error_occurred)
	network_manager.player_position_corrected.connect(_on_position_corrected)

func show_main_menu():
	game_state = "menu"
	main_menu.visible = true
	login_menu.visible = false
	settings_menu.visible = false
	hud.visible = false
	pause_menu.visible = false
	get_tree().paused = false

func show_login_menu():
	main_menu.visible = false
	login_menu.visible = true
	settings_menu.visible = false
	login_menu.set_credentials(server_address, server_port, username, password)

func show_settings_menu(from: String = "main"):
	settings_menu.visible = true
	main_menu.visible = false
	login_menu.visible = false
	hud.visible = false
	settings_menu.set_meta("from", from)

func show_hud():
	main_menu.visible = false
	login_menu.visible = false
	settings_menu.visible = false
	hud.show_hud()
	pause_menu.visible = false

func start_game():
	game_state = "playing"
	show_hud()
	player.visible = true
	player.set_process(true)
	player.set_physics_process(true)
	hud.update_health(100, 100)
	hud.update_mana(100, 100)
	hud.update_xp(0, 100)
	generate_local_chunks(player.global_transform.origin)

func pause_game():
	if game_state == "playing":
		game_state = "paused"
		pause_menu.visible = true
		get_tree().paused = true

func resume_game():
	game_state = "playing"
	pause_menu.visible = false
	get_tree().paused = false

func update_gameplay(_delta):
	var current_position = player.global_transform.origin
	if current_position.distance_to(local_player_position) > 0.1:
		local_player_position = current_position
		if network_manager.connection_state == NetworkManager.ConnectionState.CONNECTED:
			network_manager.update_player_position(
				current_position,
				player.rotation,
				Vector3.ZERO
			)
	request_nearby_chunks(current_position)
	world.update_lod(current_position)

func request_nearby_chunks(pos: Vector3):
	var chunk_x = int(floor(pos.x / chunk_size))
	var chunk_z = int(floor(pos.z / chunk_size))
	for x in range(-render_distance, render_distance + 1):
		for z in range(-render_distance, render_distance + 1):
			var target_x = chunk_x + x
			var target_z = chunk_z + z
			var chunk_key = Vector2(target_x, target_z)
			if not current_chunks.has(chunk_key):
				if network_manager.connection_state == NetworkManager.ConnectionState.CONNECTED:
					network_manager.request_chunk(target_x, target_z)
				else:
					world.create_chunk(target_x, target_z)
					current_chunks[chunk_key] = true

func generate_local_chunks(pos: Vector3):
	current_chunks.clear()
	request_nearby_chunks(pos)

func clear_world():
	for entity_id in entities.keys():
		despawn_entity(entity_id)
	entities.clear()
	current_chunks.clear()
	world.clear_chunks()

func connect_to_server():
	network_manager.server_address = login_menu.get_server_address()
	network_manager.server_port = login_menu.get_server_port()
	network_manager.connect_to_server(
		network_manager.server_address,
		network_manager.server_port,
		NetworkManager.ProtocolType.BINARY
	)

func disconnect_from_server():
	network_manager.disconnect_from_server()
	clear_world()

@warning_ignore("shadowed_variable")
func authenticate(user: String, password: String):
	username = user
	network_manager.authenticate(user, password)

func spawn_entity(entity_id: int, entity_type: String, pos: Vector3, data: Dictionary):
	if entities.size() >= max_entities:
		remove_farthest_entity(pos)
	var entity_node = create_entity_node(entity_type, data)
	entity_node.global_transform.origin = pos
	entity_node.name = "Entity_%s" % entity_id
	add_child(entity_node)
	entities[entity_id] = {
		"node": entity_node,
		"type": entity_type,
		"data": data,
		"position": pos
	}

func update_entity(entity_id: int, data: Dictionary):
	if entities.has(entity_id):
		var entity = entities[entity_id]
		if entity.node and data.has("position"):
			var new_pos = Vector3(data.position.x, data.position.y, data.position.z)
			entity.node.global_transform.origin = entity.node.global_transform.origin.lerp(new_pos, 0.5)
			entity.position = new_pos

func despawn_entity(entity_id: int):
	if entities.has(entity_id):
		var entity = entities[entity_id]
		if entity.node:
			entity.node.queue_free()
		entities.erase(entity_id)

func remove_farthest_entity(from_position: Vector3):
	var farthest_id = -1
	var farthest_distance = 0.0
	for entity_id in entities.keys():
		var entity = entities[entity_id]
		var distance = entity.position.distance_to(from_position)
		if distance > farthest_distance:
			farthest_distance = distance
			farthest_id = entity_id
	if farthest_id != -1:
		despawn_entity(farthest_id)

func create_entity_node(entity_type: String, data: Dictionary) -> Node3D:
	var entity_node = Node3D.new()
	var model = model_generator.generate_model(entity_type, data)
	if model:
		entity_node.add_child(model)
	var collision = CollisionShape3D.new()
	collision.shape = SphereShape3D.new()
	collision.shape.radius = 0.5
	entity_node.add_child(collision)
	return entity_node

func handle_chunk_data(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var chunk_data = json.data
			var chunk_x = chunk_data.chunk_x
			var chunk_z = chunk_data.chunk_z
			world.create_chunk(chunk_x, chunk_z, chunk_data)
			current_chunks[Vector2(chunk_x, chunk_z)] = true

func handle_entity_spawn(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var entity_data = json.data
			var entity_id = entity_data.entity_id
			var entity_type = entity_data.entity_type
			var pos = Vector3(entity_data.position.x, entity_data.position.y, entity_data.position.z)
			if entity_manager:
				entity_manager.handle_spawn_message(entity_data)
			else:
				spawn_entity(entity_id, entity_type, pos, entity_data)

func handle_entity_update(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var update_data = json.data
			var entity_id = update_data.entity_id
			if entity_manager:
				entity_manager.handle_update_message(update_data)
			elif entities.has(entity_id):
				update_entity(entity_id, update_data)

func handle_entity_despawn(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var despawn_data = json.data
			var entity_id = despawn_data.entity_id
			if entity_manager:
				entity_manager.handle_despawn_message(despawn_data)
			elif entities.has(entity_id):
				despawn_entity(entity_id)

func handle_chat_message(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var chat_data = json.data
			hud.add_chat_message(chat_data.sender, chat_data.message, chat_data.channel)

func handle_inventory_update(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var inventory_data = json.data
			hud.update_inventory(inventory_data)

func handle_health_update(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var health_data = json.data
			hud.update_health(health_data.health, health_data.max_health)

# Menu callbacks
func _on_play_pressed():
	show_login_menu()

func _on_local_play_pressed():
	player.global_transform.origin = Vector3(0, 2, 0)
	start_game()

func _on_main_settings_pressed():
	show_settings_menu("main")

@warning_ignore("shadowed_variable")
func _on_login_submitted(user: String, password: String):
	login_menu.set_loading(true)
	connect_to_server()
	authenticate(user, password)

func _on_login_back_pressed():
	show_main_menu()

func _on_settings_back_pressed():
	var from = settings_menu.get_meta("from", "main")
	settings_menu.visible = false
	match from:
		"main":
			show_main_menu()
		"pause":
			show_hud()
			pause_game()

func _on_settings_saved(settings: Dictionary):
	apply_settings(settings)
	save_settings()

func _on_chat_submitted(message: String):
	if network_manager.connection_state == NetworkManager.ConnectionState.CONNECTED:
		network_manager.send_chat_message(message)

func _on_resume_pressed():
	resume_game()

func _on_pause_settings_pressed():
	show_settings_menu("pause")

func _on_disconnect_pressed():
	disconnect_from_server()
	show_main_menu()

func _on_quit_pressed():
	save_settings()
	get_tree().quit()

# Network callbacks
func _on_connection_state_changed(state: int):
	hud.update_connection_status(state)
	match state:
		NetworkManager.ConnectionState.CONNECTED:
			hud.add_chat_message("System", "Connected to server", "system")
		NetworkManager.ConnectionState.DISCONNECTED:
			hud.add_chat_message("System", "Disconnected from server", "system")
			if game_state == "playing":
				clear_world()
		NetworkManager.ConnectionState.ERROR:
			hud.add_chat_message("System", "Connection error", "system")
			login_menu.set_loading(false)

func _on_authentication_result(success: bool, message: String):
	login_menu.set_loading(false)
	if success:
		login_menu.set_status("Login successful!", Color.GREEN)
		start_game()
	else:
		login_menu.set_status("Login failed: " + message, Color.RED)

func _on_message_received(message_type: int, data):
	match message_type:
		NetworkManager.MessageType.MESSAGE_TYPE_CHUNK_DATA:
			handle_chunk_data(data)
		NetworkManager.MessageType.MESSAGE_TYPE_ENTITY_SPAWN:
			handle_entity_spawn(data)
		NetworkManager.MessageType.MESSAGE_TYPE_ENTITY_UPDATE:
			handle_entity_update(data)
		NetworkManager.MessageType.MESSAGE_TYPE_ENTITY_DESPAWN:
			handle_entity_despawn(data)
		NetworkManager.MessageType.MESSAGE_TYPE_CHAT_MESSAGE:
			handle_chat_message(data)
		NetworkManager.MessageType.MESSAGE_TYPE_INVENTORY_UPDATE:
			handle_inventory_update(data)
		NetworkManager.MessageType.MESSAGE_TYPE_HEALTH_UPDATE:
			handle_health_update(data)

func _on_error_occurred(error_code: int, message: String):
	hud.add_chat_message("System", "Error %d: %s" % [error_code, message], "system")
	login_menu.set_loading(false)

func _on_position_corrected(pos: Vector3):
	player.global_transform.origin = player.global_transform.origin.lerp(pos, 0.3)

func apply_settings(settings: Dictionary):
	if settings.has("host"):
		server_address = settings.host
		network_manager.server_address = server_address
	if settings.has("port"):
		server_port = settings.port
		network_manager.server_port = server_port
	if settings.has("login"):
		username = settings.login
	if settings.has("password"):
		password = settings.password
	if settings.has("render_distance"):
		render_distance = settings.render_distance
	if settings.has("mouse_sensitivity"):
		player.mouse_sensitivity = settings.mouse_sensitivity

func load_settings():
	if config_loader:
		config_loader.load_config()
		var network_config = config_loader.get_network_config()
		server_address = network_config.get("server_address", "127.0.0.1")
		server_port = network_config.get("server_port", 8080)
		render_distance = config_loader.get_value("graphics", "render_distance", 3)
		network_manager.server_address = server_address
		network_manager.server_port = server_port
		config_loader.apply_graphics_settings()

func save_settings():
	if config_loader:
		config_loader.save_config()
