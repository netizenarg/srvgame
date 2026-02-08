extends Node3D

class_name GameClient

# Scene references
@onready var world = $World
@onready var player = $Player
@onready var ui = $UI
@onready var network_manager = $NetworkManager
@onready var model_generator = $ModelGenerator
@onready var entity_manager = $EntityManager  # ADDED
@onready var config_loader = $ConfigLoader    # ADDED

# Game state
var current_chunks: Dictionary = {}
var entities: Dictionary = {}
var player_entity_id: int = 0
var local_player_position: Vector3 = Vector3.ZERO

# Settings
var server_address: String = "127.0.0.1"
var server_port: int = 8080
var username: String = ""
var render_distance: int = 3
var chunk_size: int = 16
var chunk_height: int = 64

# Performance
var lod_distances: Array = [20.0, 40.0, 80.0]
var max_entities: int = 100

func _ready():
	load_settings()
	setup_network_signals()
	setup_input()
	
	# Initialize world
	world.initialize(chunk_size, chunk_height, render_distance, lod_distances)
	
	# Initialize entity manager
	if entity_manager:
		entity_manager.max_entities = max_entities
		entity_manager.despawn_distance = 150.0

func _process(delta):
	if network_manager.connection_state == NetworkManager.ConnectionState.CONNECTED:
		# Update player position to server
		var current_position = player.global_transform.origin
		if current_position.distance_to(local_player_position) > 0.1:
			local_player_position = current_position
			network_manager.update_player_position(
				current_position,
				player.rotation,
				Vector3.ZERO  # Simplified velocity
			)
		
		# Request chunks around player
		request_nearby_chunks(current_position)
		
		# Update world LOD
		world.update_lod(current_position)

func _input(event):
	if event is InputEventKey:
		if event.pressed and event.keycode == KEY_F1:
			ui.toggle_debug_info()
		elif event.pressed and event.keycode == KEY_F2:
			world.toggle_wireframe()
		elif event.pressed and event.keycode == KEY_F3:
			network_manager.prediction_enabled = !network_manager.prediction_enabled

# Network setup
func setup_network_signals():
	network_manager.connection_state_changed.connect(_on_connection_state_changed)
	network_manager.authentication_result.connect(_on_authentication_result)
	network_manager.message_received.connect(_on_message_received)
	network_manager.error_occurred.connect(_on_error_occurred)
	network_manager.player_position_corrected.connect(_on_position_corrected)

func connect_to_server():
	print("Connecting to server...")
	network_manager.connect_to_server(server_address, server_port, NetworkManager.ProtocolType.BINARY)

func disconnect_from_server():
	print("Disconnecting from server...")
	network_manager.disconnect_from_server()
	clear_world()

func authenticate():
	if username == "":
		ui.show_login_panel()
	else:
		# In a real game, you'd use proper authentication
		var password = "demo_password"  # This should come from UI
		network_manager.authenticate(username, password)

# Message handlers
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
		NetworkManager.MessageType.MESSAGE_TYPE_PLAYER_POSITION_CORRECTION:
			handle_position_correction(data)
		NetworkManager.MessageType.MESSAGE_TYPE_CHAT_MESSAGE:
			handle_chat_message(data)
		NetworkManager.MessageType.MESSAGE_TYPE_INVENTORY_UPDATE:
			handle_inventory_update(data)
		NetworkManager.MessageType.MESSAGE_TYPE_HEALTH_UPDATE:
			handle_health_update(data)

func handle_chunk_data(data: PackedByteArray):
	# Parse chunk data (simplified - would need proper deserialization)
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var chunk_data = json.data
			var chunk_x = chunk_data.chunk_x
			var chunk_z = chunk_data.chunk_z
			
			# Create chunk in world
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
			var position = Vector3(
				entity_data.position.x,
				entity_data.position.y,
				entity_data.position.z
			)
			
			# Use entity manager instead of direct spawn
			if entity_manager:
				entity_manager.handle_spawn_message(entity_data)
			else:
				spawn_entity(entity_id, entity_type, position, entity_data)

func handle_entity_update(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var update_data = json.data
			var entity_id = update_data.entity_id
			
			# Use entity manager instead of direct update
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
			
			# Use entity manager instead of direct despawn
			if entity_manager:
				entity_manager.handle_despawn_message(despawn_data)
			elif entities.has(entity_id):
				despawn_entity(entity_id)

func handle_position_correction(data):
	# Position correction handled by network manager
	# We can optionally smooth the correction here
	pass

func handle_chat_message(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var chat_data = json.data
			ui.add_chat_message(chat_data.sender, chat_data.message, chat_data.channel)

func handle_inventory_update(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var inventory_data = json.data
			ui.update_inventory(inventory_data)

func handle_health_update(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var health_data = json.data
			ui.update_health(health_data.health, health_data.max_health)

# Entity management - Kept for backward compatibility
func spawn_entity(entity_id: int, entity_type: String, position: Vector3, data: Dictionary):
	if entities.size() >= max_entities:
		# Remove farthest entity
		remove_farthest_entity(position)
	
	var entity_node = create_entity_node(entity_type, data)
	entity_node.global_transform.origin = position
	entity_node.name = "Entity_%s" % entity_id
	add_child(entity_node)
	
	entities[entity_id] = {
		"node": entity_node,
		"type": entity_type,
		"data": data,
		"position": position
	}
	
	print("Spawned entity ", entity_id, " of type ", entity_type)

func update_entity(entity_id: int, data: Dictionary):
	var entity = entities[entity_id]
	if entity and entity.node:
		# Update position if provided
		if data.has("position"):
			var new_pos = Vector3(
				data.position.x,
				data.position.y,
				data.position.z
			)
			
			# Interpolate position for smooth movement
			var current_pos = entity.node.global_transform.origin
			entity.node.global_transform.origin = current_pos.lerp(new_pos, 0.5)
			entity.position = new_pos
		
		# Update other properties
		if data.has("rotation"):
			entity.node.rotation = Vector3(
				data.rotation.x,
				data.rotation.y,
				data.rotation.z
			)
		
		# Update entity data
		entity.data.merge(data, true)

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
	# Create appropriate entity based on type
	var entity_node = Node3D.new()
	
	# Add model
	var model = model_generator.generate_model(entity_type, data)
	if model:
		entity_node.add_child(model)
	
	# Add collision shape
	var collision = CollisionShape3D.new()
	collision.shape = SphereShape3D.new()
	collision.shape.radius = 0.5
	entity_node.add_child(collision)
	
	# Add interaction area
	var area = Area3D.new()
	area.collision_layer = 2
	area.collision_mask = 1
	area.input_event.connect(_on_entity_interacted.bind(entity_node))
	entity_node.add_child(area)
	
	return entity_node

func _on_entity_interacted(camera, event, position, normal, shape_idx, entity_node):
	if event is InputEventMouseButton and event.pressed:
		# Find entity ID from node name
		var entity_name = entity_node.name
		if entity_name.begins_with("Entity_"):
			var entity_id = int(entity_name.replace("Entity_", ""))
			network_manager.interact_with_entity(entity_id)

# World management
func request_nearby_chunks(position: Vector3):
	var chunk_x = int(position.x / chunk_size)
	var chunk_z = int(position.z / chunk_size)
	
	for x in range(-render_distance, render_distance + 1):
		for z in range(-render_distance, render_distance + 1):
			var target_x = chunk_x + x
			var target_z = chunk_z + z
			var chunk_key = Vector2(target_x, target_z)
			
			if not current_chunks.has(chunk_key):
				network_manager.request_chunk(target_x, target_z)

func clear_world():
	# Clear entities
	for entity_id in entities.keys():
		despawn_entity(entity_id)
	entities.clear()
	
	# Clear chunks
	current_chunks.clear()
	world.clear_chunks()

# UI callbacks
func _on_connection_state_changed(state: int):
	match state:
		NetworkManager.ConnectionState.CONNECTED:
			ui.show_message("Connected to server")
		NetworkManager.ConnectionState.DISCONNECTED:
			ui.show_message("Disconnected from server")
			clear_world()
		NetworkManager.ConnectionState.ERROR:
			ui.show_message("Connection error")
	
	ui.update_connection_status(state)

func _on_authentication_result(success: bool, message: String):
	if success:
		ui.show_message("Authentication successful")
		ui.hide_login_panel()
	else:
		ui.show_message("Authentication failed: " + message)

func _on_error_occurred(error_code: int, message: String):
	ui.show_message("Error %d: %s" % [error_code, message])

func _on_position_corrected(position: Vector3):
	# Smoothly correct player position
	player.global_transform.origin = player.global_transform.origin.lerp(position, 0.3)

func _on_login_submitted(user: String, passw: String):  # Changed from 'pass' to 'passw'
	username = user
	authenticate()

# Settings
func load_settings():
	# Load from config file
	if config_loader:
		config_loader.load_config()
		
		# Apply loaded settings
		var network_config = config_loader.get_network_config()
		server_address = network_config.get("server_address", "127.0.0.1")
		server_port = network_config.get("server_port", 8080)
		render_distance = config_loader.get_value("graphics", "render_distance", 3)
		
		# Apply to network manager
		network_manager.server_address = server_address
		network_manager.server_port = server_port
		
		# Apply graphics settings
		config_loader.apply_graphics_settings()
	else:
		# Fallback to old method
		var config = ConfigFile.new()
		var err = config.load("user://settings.cfg")
		
		if err == OK:
			server_address = config.get_value("network", "server_address", "127.0.0.1")
			server_port = config.get_value("network", "server_port", 8080)
			username = config.get_value("player", "username", "")
			render_distance = config.get_value("graphics", "render_distance", 3)
			
			# Apply settings to network manager
			network_manager.server_address = server_address
			network_manager.server_port = server_port

func save_settings():
	if config_loader:
		config_loader.save_config()
	else:
		var config = ConfigFile.new()
		
		config.set_value("network", "server_address", server_address)
		config.set_value("network", "server_port", server_port)
		config.set_value("player", "username", username)
		config.set_value("graphics", "render_distance", render_distance)
		
		config.save("user://settings.cfg")

func _exit_tree():
	save_settings()
	disconnect_from_server()