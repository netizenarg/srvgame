extends Node

class_name NetworkManager

# Protocol types
enum ProtocolType {
	BINARY = 0,
	WEBSOCKET = 1,
}

# Message types matching server
enum MessageType {
	MESSAGE_TYPE_INVALID = 0,
	MESSAGE_TYPE_HEARTBEAT = 1,
	MESSAGE_TYPE_PROTOCOL_NEGOTIATION = 2,
	MESSAGE_TYPE_AUTHENTICATION = 3,
	MESSAGE_TYPE_ERROR = 4,
	MESSAGE_TYPE_SUCCESS = 5,
	MESSAGE_TYPE_CHUNK_DATA = 100,
	MESSAGE_TYPE_CHUNK_REQUEST = 101,
	MESSAGE_TYPE_TERRAIN_HEIGHT = 102,
	MESSAGE_TYPE_BIOME_DATA = 103,
	MESSAGE_TYPE_ENTITY_SPAWN = 200,
	MESSAGE_TYPE_ENTITY_UPDATE = 201,
	MESSAGE_TYPE_ENTITY_DESPAWN = 202,
	MESSAGE_TYPE_ENTITY_BATCH_UPDATE = 203,
	MESSAGE_TYPE_PLAYER_POSITION = 300,
	MESSAGE_TYPE_PLAYER_VELOCITY = 301,
	MESSAGE_TYPE_PLAYER_ROTATION = 302,
	MESSAGE_TYPE_PLAYER_STATE = 303,
	MESSAGE_TYPE_PLAYER_POSITION_CORRECTION = 304,
	MESSAGE_TYPE_NPC_SPAWN = 400,
	MESSAGE_TYPE_NPC_UPDATE = 401,
	MESSAGE_TYPE_NPC_DESPAWN = 402,
	MESSAGE_TYPE_NPC_INTERACTION = 403,
	MESSAGE_TYPE_COMBAT_EVENT = 500,
	MESSAGE_TYPE_DAMAGE_EVENT = 501,
	MESSAGE_TYPE_HEALTH_UPDATE = 502,
	MESSAGE_TYPE_INVENTORY_UPDATE = 600,
	MESSAGE_TYPE_LOOT_SPAWN = 601,
	MESSAGE_TYPE_LOOT_PICKUP = 602,
	MESSAGE_TYPE_CHAT_MESSAGE = 700,
	MESSAGE_TYPE_SYSTEM_MESSAGE = 701,
	MESSAGE_TYPE_CUSTOM_EVENT = 1000
}

# Protocol flags
enum ProtocolFlags {
	FLAG_COMPRESSED = 0x01,
	FLAG_ENCRYPTED = 0x02,
	FLAG_RELIABLE = 0x04,
	FLAG_ORDERED = 0x08,
	FLAG_PRIORITY_HIGH = 0x10,
	FLAG_PRIORITY_LOW = 0x20
}

# Network state
enum ConnectionState {
	DISCONNECTED = 0,
	CONNECTING = 1,
	HANDSHAKE = 2,
	AUTHENTICATING = 3,
	CONNECTED = 4,
	ERROR = 5
}

# Signals
signal connection_state_changed(state)
signal authentication_result(success, message)
signal message_received(message_type, data)
signal error_occurred(error_code, message)
signal player_position_corrected(position)

# Configuration
var server_address: String = "127.0.0.1"
var server_port: int = 8080
var protocol_type: int = ProtocolType.BINARY
var use_ssl: bool = false
var reconnect_attempts: int = 3
var reconnect_delay: float = 2.0

# Network objects
var tcp_client: StreamPeerTCP
var websocket: WebSocketPeer
var ssl_context: StreamPeerTLS

# State
var connection_state: int = ConnectionState.DISCONNECTED
var session_id: int = 0
var player_id: int = 0
var auth_token: String = ""
var sequence_number: int = 0
var pending_acks: Dictionary = {}
var last_heartbeat: float = 0.0
var heartbeat_interval: float = 5.0

# Quality monitoring
var network_quality: Dictionary = {
	"latency": 0.0,
	"packet_loss": 0.0,
	"jitter": 0.0,
	"quality_score": 1.0
}

# Message handlers
var message_handlers: Dictionary = {}
var default_message_handler: Callable

# Queues
var incoming_queue: Array = []
var outgoing_queue: Array = []

# Prediction system
var prediction_enabled: bool = true
var input_buffer: Array = []
var last_confirmed_state: Dictionary = {}
var predicted_states: Dictionary = {}

func _ready():
	setup_message_handlers()
	set_process(false)

func _process(delta):
	process_network()
	process_queues()
	check_heartbeat(delta)
	update_prediction(delta)

# Public API
func connect_to_server(address: String, port: int, protocol: int = ProtocolType.BINARY):
	if connection_state != ConnectionState.DISCONNECTED:
		disconnect_from_server()
	
	server_address = address
	server_port = port
	protocol_type = protocol
	
	set_connection_state(ConnectionState.CONNECTING)
	
	match protocol_type:
		ProtocolType.BINARY:
			connect_binary()
		ProtocolType.WEBSOCKET:
			connect_websocket()

func disconnect_from_server():
	match protocol_type:
		ProtocolType.BINARY:
			if tcp_client:
				tcp_client.disconnect_from_host()
		ProtocolType.WEBSOCKET:
			if websocket:
				websocket.close()
	
	set_connection_state(ConnectionState.DISCONNECTED)
	cleanup()

func send_message(message_type: int, data: PackedByteArray, reliable: bool = true, priority: int = 0):
	var message = create_message(message_type, data, reliable, priority)
	outgoing_queue.append(message)

func send_json_message(message_type: int, json_data: Dictionary):
	var json_string = JSON.stringify(json_data)
	var data = json_string.to_utf8_buffer()
	send_message(message_type, data)

func send_binary_message(message_type: int, binary_data: PackedByteArray):
	send_message(message_type, binary_data)

func authenticate(username: String, password: String):
	var auth_data = {
		"username": username,
		"password": password,
		"client_version": "1.0.0"
	}
	send_json_message(MessageType.MESSAGE_TYPE_AUTHENTICATION, auth_data)
	set_connection_state(ConnectionState.AUTHENTICATING)

func request_chunk(chunk_x: int, chunk_z: int):
	var request_data = {
		"chunk_x": chunk_x,
		"chunk_z": chunk_z
	}
	send_json_message(MessageType.MESSAGE_TYPE_CHUNK_REQUEST, request_data)

func update_player_position(position: Vector3, rotation: Vector3, velocity: Vector3 = Vector3.ZERO):
	var pos_data = {
		"position": {
			"x": position.x,
			"y": position.y,
			"z": position.z
		},
		"rotation": {
			"x": rotation.x,
			"y": rotation.y,
			"z": rotation.z
		},
		"velocity": {
			"x": velocity.x,
			"y": velocity.y,
			"z": velocity.z
		},
		"timestamp": Time.get_ticks_msec(),
		"input_id": sequence_number
	}
	send_json_message(MessageType.MESSAGE_TYPE_PLAYER_POSITION, pos_data)
	
	if prediction_enabled:
		store_input(sequence_number, position, rotation, velocity)
		sequence_number += 1

func send_chat_message(message: String, channel: String = "global"):
	var chat_data = {
		"message": message,
		"channel": channel,
		"timestamp": Time.get_ticks_msec()
	}
	send_json_message(MessageType.MESSAGE_TYPE_CHAT_MESSAGE, chat_data)

func interact_with_entity(entity_id: int, interaction_type: String = "use"):
	var interaction_data = {
		"entity_id": entity_id,
		"interaction_type": interaction_type,
		"timestamp": Time.get_ticks_msec()
	}
	send_json_message(MessageType.MESSAGE_TYPE_NPC_INTERACTION, interaction_data)

# Message handlers
func setup_message_handlers():
	# System messages
	message_handlers[MessageType.MESSAGE_TYPE_HEARTBEAT] = _handle_heartbeat
	message_handlers[MessageType.MESSAGE_TYPE_SUCCESS] = _handle_success
	message_handlers[MessageType.MESSAGE_TYPE_ERROR] = _handle_error
	
	# World messages
	message_handlers[MessageType.MESSAGE_TYPE_CHUNK_DATA] = _handle_chunk_data
	message_handlers[MessageType.MESSAGE_TYPE_TERRAIN_HEIGHT] = _handle_terrain_height
	message_handlers[MessageType.MESSAGE_TYPE_BIOME_DATA] = _handle_biome_data
	
	# Entity messages
	message_handlers[MessageType.MESSAGE_TYPE_ENTITY_SPAWN] = _handle_entity_spawn
	message_handlers[MessageType.MESSAGE_TYPE_ENTITY_UPDATE] = _handle_entity_update
	message_handlers[MessageType.MESSAGE_TYPE_ENTITY_DESPAWN] = _handle_entity_despawn
	
	# Player messages
	message_handlers[MessageType.MESSAGE_TYPE_PLAYER_POSITION_CORRECTION] = _handle_position_correction
	message_handlers[MessageType.MESSAGE_TYPE_HEALTH_UPDATE] = _handle_health_update
	
	# Inventory messages
	message_handlers[MessageType.MESSAGE_TYPE_INVENTORY_UPDATE] = _handle_inventory_update
	message_handlers[MessageType.MESSAGE_TYPE_LOOT_SPAWN] = _handle_loot_spawn
	
	# Chat messages
	message_handlers[MessageType.MESSAGE_TYPE_CHAT_MESSAGE] = _handle_chat_message
	message_handlers[MessageType.MESSAGE_TYPE_SYSTEM_MESSAGE] = _handle_system_message

# Private methods
func connect_binary():
	tcp_client = StreamPeerTCP.new()
	var error = tcp_client.connect_to_host(server_address, server_port)
	
	if error == OK:
		if use_ssl:
			ssl_context = StreamPeerTLS.new()
			error = ssl_context.connect_to_stream(tcp_client)
			if error == OK:
				set_connection_state(ConnectionState.HANDSHAKE)
			else:
				set_connection_state(ConnectionState.ERROR)
				emit_signal("error_occurred", error, "SSL connection failed")
		else:
			set_connection_state(ConnectionState.HANDSHAKE)
	else:
		set_connection_state(ConnectionState.ERROR)
		emit_signal("error_occurred", error, "TCP connection failed")

func connect_websocket():
	websocket = WebSocketPeer.new()
	var url = "ws://%s:%d" % [server_address, server_port]
	if use_ssl:
		url = "wss://%s:%d" % [server_address, server_port]
	
	var error = websocket.connect_to_url(url)
	
	if error == OK:
		set_connection_state(ConnectionState.HANDSHAKE)
	else:
		set_connection_state(ConnectionState.ERROR)
		emit_signal("error_occurred", error, "WebSocket connection failed")

func process_network():
	match protocol_type:
		ProtocolType.BINARY:
			process_binary()
		ProtocolType.WEBSOCKET:
			process_websocket()

func process_binary():
	if not tcp_client or tcp_client.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		return
	
	# Check for incoming data
	var available_bytes = tcp_client.get_available_bytes()
	if available_bytes > 0:
		var data = tcp_client.get_data(available_bytes)
		if data[0] == OK:
			parse_binary_data(data[1])

func process_websocket():
	if not websocket:
		return
	
	websocket.poll()
	
	var state = websocket.get_ready_state()
	match state:
		WebSocketPeer.STATE_OPEN:
			if connection_state != ConnectionState.CONNECTED:
				set_connection_state(ConnectionState.CONNECTED)
			
			while websocket.get_available_packet_count():
				var packet = websocket.get_packet()
				parse_websocket_data(packet)
		
		WebSocketPeer.STATE_CLOSED:
			var code = websocket.get_close_code()
			var reason = websocket.get_close_reason()
			set_connection_state(ConnectionState.DISCONNECTED)
			emit_signal("error_occurred", code, "WebSocket closed: %s" % reason)

func process_queues():
	# Process incoming messages
	while incoming_queue.size() > 0:
		var message = incoming_queue.pop_front()
		handle_incoming_message(message)
	
	# Process outgoing messages
	while outgoing_queue.size() > 0 and connection_state == ConnectionState.CONNECTED:
		var message = outgoing_queue.pop_front()
		send_network_message(message)

func check_heartbeat(delta: float):
	last_heartbeat += delta
	if last_heartbeat >= heartbeat_interval:
		send_heartbeat()
		last_heartbeat = 0.0

func send_heartbeat():
	var heartbeat_data = PackedByteArray()
	send_message(MessageType.MESSAGE_TYPE_HEARTBEAT, heartbeat_data)

func create_message(message_type: int, data: PackedByteArray, reliable: bool, priority: int) -> Dictionary:
	var flags: int = 0
	if reliable:
		flags |= ProtocolFlags.FLAG_RELIABLE
	
	match priority:
		1:
			flags |= ProtocolFlags.FLAG_PRIORITY_HIGH
		-1:
			flags |= ProtocolFlags.FLAG_PRIORITY_LOW
	
	var message = {
		"version": 1,
		"flags": flags,
		"message_type": message_type,
		"sequence": get_next_sequence(),
		"timestamp": Time.get_ticks_msec(),
		"data": data
	}
	
	if reliable:
		pending_acks[message.sequence] = {
			"message": message,
			"timestamp": Time.get_ticks_msec(),
			"retries": 0
		}
	
	return message

func get_next_sequence() -> int:
	var seq = sequence_number
	sequence_number = (sequence_number + 1) % 65536
	return seq

func send_network_message(message: Dictionary):
	var packet = serialize_message(message)
	
	match protocol_type:
		ProtocolType.BINARY:
			if use_ssl and ssl_context:
				ssl_context.put_data(packet)
			elif tcp_client:
				tcp_client.put_data(packet)
		
		ProtocolType.WEBSOCKET:
			if websocket:
				websocket.send(packet)

func serialize_message(message: Dictionary) -> PackedByteArray:
	var buffer = PackedByteArray()
	
	# Header (28 bytes)
	buffer.append(message.version)  # version
	buffer.append(message.flags)    # flags
	
	# message_type (2 bytes)
	buffer.append((message.message_type >> 8) & 0xFF)
	buffer.append(message.message_type & 0xFF)
	
	# sequence (4 bytes)
	for i in range(4):
		buffer.append((message.sequence >> (i * 8)) & 0xFF)
	
	# timestamp (4 bytes)
	var timestamp = message.timestamp
	for i in range(4):
		buffer.append((timestamp >> (i * 8)) & 0xFF)
	
	# length (4 bytes)
	var length = message.data.size()
	for i in range(4):
		buffer.append((length >> (i * 8)) & 0xFF)
	
	# checksum (4 bytes) - placeholder
	for i in range(4):
		buffer.append(0)
	
	# Data
	buffer.append_array(message.data)
	
	# Calculate and update checksum
	update_checksum(buffer)
	
	return buffer

func update_checksum(buffer: PackedByteArray):
	# Simple XOR checksum for now
	var checksum: int = 0
	for i in range(24, buffer.size()):  # Skip header
		checksum ^= buffer[i]
	
	# Update checksum in buffer (positions 24-27)
	for i in range(4):
		buffer[24 + i] = (checksum >> (i * 8)) & 0xFF

func parse_binary_data(data: PackedByteArray):
	if data.size() < 28:
		return
	
	var message = {
		"version": data[0],
		"flags": data[1],
		"message_type": (data[2] << 8) | data[3],
		"sequence": (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7],
		"timestamp": (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11],
		"length": (data[12] << 24) | (data[13] << 16) | (data[14] << 8) | data[15]
	}
	
	# Verify checksum
	if not verify_checksum(data):
		print("Checksum verification failed for message type: ", message.message_type)
		return
	
	# Extract data
	if message.length > 0 and data.size() >= 28 + message.length:
		message.data = data.slice(28, 28 + message.length)
	
	incoming_queue.append(message)
	
	# Handle ACKs
	if message.flags & ProtocolFlags.FLAG_RELIABLE:
		handle_ack(message.sequence)

func parse_websocket_data(data: PackedByteArray):
	# For WebSocket, assume JSON or custom binary format
	# Try to parse as JSON first
	var json_string = data.get_string_from_utf8()
	if json_string != "":
		var json = JSON.new()
		var error = json.parse(json_string)
		if error == OK:
			var message_data = json.data
			if message_data.has("type") and message_data.has("data"):
				var message = {
					"message_type": message_data.type,
					"data": message_data.data,
					"flags": 0,
					"sequence": message_data.get("sequence", 0),
					"timestamp": message_data.get("timestamp", 0)
				}
				incoming_queue.append(message)
	else:
		# Try to parse as binary protocol
		parse_binary_data(data)

func verify_checksum(data: PackedByteArray) -> bool:
	# Simple XOR checksum verification
	var checksum: int = 0
	for i in range(24, data.size()):
		checksum ^= data[i]
	
	var stored_checksum: int = 0
	for i in range(4):
		stored_checksum |= (data[24 + i] << (i * 8))
	
	return checksum == stored_checksum

func handle_incoming_message(message: Dictionary):
	var message_type = message.message_type
	
	if message_handlers.has(message_type):
		message_handlers[message_type].call(message.data)
	else:
		if default_message_handler:
			default_message_handler.call(message_type, message.data)
		else:
			print("Unhandled message type: ", message_type)

func handle_ack(sequence: int):
	if pending_acks.has(sequence):
		pending_acks.erase(sequence)
		update_network_quality(true)

func set_connection_state(new_state: int):
	if connection_state != new_state:
		connection_state = new_state
		emit_signal("connection_state_changed", new_state)
		
		if new_state == ConnectionState.CONNECTED:
			set_process(true)
		elif new_state == ConnectionState.DISCONNECTED:
			set_process(false)

func cleanup():
	tcp_client = null
	websocket = null
	ssl_context = null
	incoming_queue.clear()
	outgoing_queue.clear()
	pending_acks.clear()
	sequence_number = 0
	player_id = 0
	auth_token = ""

func update_network_quality(success: bool):
	# Update network quality metrics
	pass

# Prediction system
func store_input(input_id: int, position: Vector3, rotation: Vector3, velocity: Vector3):
	if not prediction_enabled:
		return
	
	var input = {
		"input_id": input_id,
		"position": position,
		"rotation": rotation,
		"velocity": velocity,
		"timestamp": Time.get_ticks_msec()
	}
	
	input_buffer.append(input)
	
	# Keep buffer size reasonable
	if input_buffer.size() > 100:
		input_buffer.pop_front()

func update_prediction(delta: float):
	if not prediction_enabled or input_buffer.size() == 0:
		return
	
	# Apply prediction to local entities
	pass

func reconcile_with_server(server_state: Dictionary):
	if not prediction_enabled:
		return
	
	# Find the last confirmed input
	var last_confirmed_input_id = server_state.get("last_input_id", 0)
	
	# Remove processed inputs from buffer
	var i = 0
	while i < input_buffer.size():
		if input_buffer[i].input_id <= last_confirmed_input_id:
			input_buffer.remove_at(i)
		else:
			i += 1
	
	# Apply correction if needed
	var correction = server_state.get("correction", null)
	if correction:
		emit_signal("player_position_corrected", Vector3(
			correction.x,
			correction.y,
			correction.z
		))

# Message handlers implementation
func _handle_heartbeat(data: PackedByteArray):
	# Update last heartbeat time
	last_heartbeat = 0.0
	update_network_quality(true)

func _handle_success(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var success_data = json.data
			
			if connection_state == ConnectionState.AUTHENTICATING:
				if success_data.get("type") == "authentication":
					auth_token = success_data.get("auth_token", "")
					player_id = success_data.get("player_id", 0)
					session_id = success_data.get("session_id", 0)
					set_connection_state(ConnectionState.CONNECTED)
					emit_signal("authentication_result", true, success_data.get("message", "Authenticated"))
			
			emit_signal("message_received", MessageType.MESSAGE_TYPE_SUCCESS, success_data)

func _handle_error(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var error_data = json.data
			
			if connection_state == ConnectionState.AUTHENTICATING:
				emit_signal("authentication_result", false, error_data.get("message", "Authentication failed"))
			
			emit_signal("error_occurred", error_data.get("code", -1), error_data.get("message", "Unknown error"))
			emit_signal("message_received", MessageType.MESSAGE_TYPE_ERROR, error_data)

func _handle_chunk_data(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_CHUNK_DATA, data)

func _handle_terrain_height(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_TERRAIN_HEIGHT, data)

func _handle_biome_data(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_BIOME_DATA, data)

func _handle_entity_spawn(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_ENTITY_SPAWN, data)

func _handle_entity_update(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_ENTITY_UPDATE, data)

func _handle_entity_despawn(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_ENTITY_DESPAWN, data)

func _handle_position_correction(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var correction_data = json.data
			
			# Apply prediction reconciliation
			if correction_data.has("server_state"):
				reconcile_with_server(correction_data.server_state)
			
			# Emit correction signal
			if correction_data.has("position"):
				var pos = correction_data.position
				emit_signal("player_position_corrected", Vector3(pos.x, pos.y, pos.z))
			
			emit_signal("message_received", MessageType.MESSAGE_TYPE_PLAYER_POSITION_CORRECTION, correction_data)

func _handle_health_update(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_HEALTH_UPDATE, data)

func _handle_inventory_update(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_INVENTORY_UPDATE, data)

func _handle_loot_spawn(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_LOOT_SPAWN, data)

func _handle_chat_message(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_CHAT_MESSAGE, data)

func _handle_system_message(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_SYSTEM_MESSAGE, data)