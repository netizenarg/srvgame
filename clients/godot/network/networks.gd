extends Node

# Protocol types
enum ProtocolType {
	BINARY = 0,
	WEBSOCKET = 1,
}

# Message types matching server BinaryProtocol.hpp exactly
enum MessageType {
	MESSAGE_TYPE_INVALID = 0,
	MESSAGE_TYPE_HEARTBEAT = 100,
	MESSAGE_TYPE_PROTOCOL_NEGOTIATION = 102,
	MESSAGE_TYPE_AUTHENTICATION = 103,
	MESSAGE_TYPE_ERROR = 104,
	MESSAGE_TYPE_SUCCESS = 105,
	MESSAGE_TYPE_COLLISION_CHECK = 150,
	MESSAGE_TYPE_CHUNK_PARAMS = 200,
	MESSAGE_TYPE_CHUNK_DATA = 201,
	MESSAGE_TYPE_BIOME_DATA = 202,
	MESSAGE_TYPE_PLAYER_CONNECT = 300,
	MESSAGE_TYPE_PLAYER_DISCONNECT = 301,
	MESSAGE_TYPE_PLAYER_STATE = 302,
	MESSAGE_TYPE_PLAYER_SPAWN = 303,
	MESSAGE_TYPE_PLAYER_DESPAWN = 304,
	MESSAGE_TYPE_PLAYER_UPDATE = 305,
	MESSAGE_TYPE_PLAYER_VELOCITY = 306,
	MESSAGE_TYPE_PLAYER_ROTATION = 307,
	MESSAGE_TYPE_PLAYER_POSITION = 308,
	MESSAGE_TYPE_PLAYER_POSITION_CORRECTION = 309,
	MESSAGE_TYPE_PLAYERS_UPDATE = 350,
	MESSAGE_TYPE_ENTITY_SPAWN = 400,
	MESSAGE_TYPE_ENTITY_UPDATE = 401,
	MESSAGE_TYPE_ENTITY_DESPAWN = 402,
	MESSAGE_TYPE_ENTITY_BATCH_UPDATE = 403,
	MESSAGE_TYPE_NPC_SPAWN = 500,
	MESSAGE_TYPE_NPC_UPDATE = 501,
	MESSAGE_TYPE_NPC_DESPAWN = 502,
	MESSAGE_TYPE_NPC_INTERACTION = 503,
	MESSAGE_TYPE_COMBAT_EVENT = 600,
	MESSAGE_TYPE_DAMAGE_EVENT = 601,
	MESSAGE_TYPE_HEALTH_UPDATE = 602,
	MESSAGE_TYPE_LOOT_SPAWN = 700,
	MESSAGE_TYPE_LOOT_PICKUP = 701,
	MESSAGE_TYPE_INVENTORY_UPDATE = 702,
	MESSAGE_TYPE_INVENTORY_MOVE = 703,
	MESSAGE_TYPE_CHAT_MESSAGE = 800,
	MESSAGE_TYPE_SYSTEM_MESSAGE = 801,
	MESSAGE_TYPE_FAMILIAR_COMMAND = 900,
	MESSAGE_TYPE_CUSTOM_EVENT = 1000,
}

# Protocol flags matching server
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
var ws_path: String = "/game"

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
var protocol_negotiated: bool = false
var recv_buffer: PackedByteArray = PackedByteArray()

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
	protocol_negotiated = false
	recv_buffer = PackedByteArray()

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
	if protocol_type == ProtocolType.WEBSOCKET:
		send_ws_json(json_data)
	else:
		var json_string = JSON.stringify(json_data)
		var data = json_string.to_utf8_buffer()
		send_message(message_type, data)

func send_binary_message(message_type: int, binary_data: PackedByteArray):
	send_message(message_type, binary_data)

func authenticate(username: String, password: String):
	if protocol_type == ProtocolType.WEBSOCKET:
		send_ws_json({
			"msg": "authentication",
			"login": username,
			"password": password,
		})
	else:
		var body = PackedByteArray()
		body.append_array(_write_uint64(0))
		body.append_array(_write_string(username))
		body.append_array(_write_string(password))
		send_message(MessageType.MESSAGE_TYPE_AUTHENTICATION, body)
	set_connection_state(ConnectionState.AUTHENTICATING)

func request_chunk(chunk_x: int, chunk_z: int):
	if protocol_type == ProtocolType.WEBSOCKET:
		send_ws_json({
			"msg": "get_chunk",
			"x": chunk_x,
			"z": chunk_z,
		})
	else:
		var body = PackedByteArray()
		body.append_array(_write_int32(chunk_x))
		body.append_array(_write_int32(chunk_z))
		send_message(MessageType.MESSAGE_TYPE_CHUNK_DATA, body)

func update_player_position(position: Vector3, rotation: Vector3, velocity: Vector3 = Vector3.ZERO):
	if protocol_type == ProtocolType.WEBSOCKET:
		send_ws_json({
			"msg": "player_position",
			"player_id": player_id,
			"x": position.x,
			"y": position.y,
			"z": position.z,
			"vx": velocity.x,
			"vy": velocity.y,
			"vz": velocity.z,
			"timestamp": Time.get_ticks_msec(),
		})
	else:
		var body = PackedByteArray()
		body.append_array(_write_uint64(player_id))
		body.append_array(_write_vector3(position))
		body.append_array(_write_vector3(velocity))
		body.append_array(_write_uint64(Time.get_ticks_msec()))
		send_message(MessageType.MESSAGE_TYPE_PLAYER_POSITION, body)

	if prediction_enabled:
		store_input(sequence_number, position, rotation, velocity)
		sequence_number += 1

func send_chat_message(message: String, sender: String = "Player"):
	if protocol_type == ProtocolType.WEBSOCKET:
		send_ws_json({
			"msg": "chat_message",
			"sender": sender,
			"message": message,
			"timestamp": Time.get_ticks_msec(),
		})
	else:
		var body = PackedByteArray()
		body.append_array(_write_string(sender))
		body.append_array(_write_string(message))
		body.append_array(_write_uint64(Time.get_ticks_msec()))
		send_message(MessageType.MESSAGE_TYPE_CHAT_MESSAGE, body)

func interact_with_entity(entity_id: int, interaction_type: String = "use"):
	if protocol_type == ProtocolType.WEBSOCKET:
		send_ws_json({
			"msg": "npc_interaction",
			"npc_id": entity_id,
			"npc_type": interaction_type,
		})
	else:
		var body = PackedByteArray()
		body.append_array(_write_uint64(entity_id))
		body.append_array(_write_string(interaction_type))
		send_message(MessageType.MESSAGE_TYPE_NPC_INTERACTION, body)

# Message handlers
func setup_message_handlers():
	message_handlers[MessageType.MESSAGE_TYPE_HEARTBEAT] = _handle_heartbeat
	message_handlers[MessageType.MESSAGE_TYPE_SUCCESS] = _handle_success
	message_handlers[MessageType.MESSAGE_TYPE_ERROR] = _handle_error
	message_handlers[MessageType.MESSAGE_TYPE_CHUNK_DATA] = _handle_chunk_data
	message_handlers[MessageType.MESSAGE_TYPE_BIOME_DATA] = _handle_biome_data
	message_handlers[MessageType.MESSAGE_TYPE_ENTITY_SPAWN] = _handle_entity_spawn
	message_handlers[MessageType.MESSAGE_TYPE_ENTITY_UPDATE] = _handle_entity_update
	message_handlers[MessageType.MESSAGE_TYPE_ENTITY_DESPAWN] = _handle_entity_despawn
	message_handlers[MessageType.MESSAGE_TYPE_PLAYER_POSITION_CORRECTION] = _handle_position_correction
	message_handlers[MessageType.MESSAGE_TYPE_PLAYER_STATE] = _handle_player_state
	message_handlers[MessageType.MESSAGE_TYPE_HEALTH_UPDATE] = _handle_health_update
	message_handlers[MessageType.MESSAGE_TYPE_INVENTORY_UPDATE] = _handle_inventory_update
	message_handlers[MessageType.MESSAGE_TYPE_LOOT_SPAWN] = _handle_loot_spawn
	message_handlers[MessageType.MESSAGE_TYPE_CHAT_MESSAGE] = _handle_chat_message
	message_handlers[MessageType.MESSAGE_TYPE_SYSTEM_MESSAGE] = _handle_system_message

# --- Binary protocol helpers (big-endian body fields, matching server BinaryReader) ---

func _write_uint8(value: int) -> PackedByteArray:
	return PackedByteArray([value & 0xFF])

func _write_uint16(value: int) -> PackedByteArray:
	return PackedByteArray([(value >> 8) & 0xFF, value & 0xFF])

func _write_uint32(value: int) -> PackedByteArray:
	return PackedByteArray([
		(value >> 24) & 0xFF,
		(value >> 16) & 0xFF,
		(value >> 8) & 0xFF,
		value & 0xFF
	])

func _write_uint64(value: int) -> PackedByteArray:
	return PackedByteArray([
		(value >> 56) & 0xFF,
		(value >> 48) & 0xFF,
		(value >> 40) & 0xFF,
		(value >> 32) & 0xFF,
		(value >> 24) & 0xFF,
		(value >> 16) & 0xFF,
		(value >> 8) & 0xFF,
		value & 0xFF
	])

func _write_int32(value: int) -> PackedByteArray:
	return _write_uint32(value)

func _write_float(value: float) -> PackedByteArray:
	var int_val = int(value)
	return _write_uint32(int_val)

func _write_vector3(v: Vector3) -> PackedByteArray:
	var buf = PackedByteArray()
	buf.append_array(_write_float(v.x))
	buf.append_array(_write_float(v.y))
	buf.append_array(_write_float(v.z))
	return buf

func _write_string(s: String) -> PackedByteArray:
	var encoded = s.to_utf8_buffer()
	var buf = PackedByteArray()
	buf.append_array(_write_uint16(encoded.size()))
	buf.append_array(encoded)
	return buf

func _read_uint8(data: PackedByteArray, offset: int) -> int:
	return data[offset]

func _read_uint16_be(data: PackedByteArray, offset: int) -> int:
	return (data[offset] << 8) | data[offset + 1]

func _read_uint32_be(data: PackedByteArray, offset: int) -> int:
	return (data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) | data[offset + 3]

func _read_uint64_be(data: PackedByteArray, offset: int) -> int:
	var val: int = 0
	for i in range(8):
		val = (val << 8) | data[offset + i]
	return val

func _read_int32_be(data: PackedByteArray, offset: int) -> int:
	return _read_uint32_be(data, offset)

func _read_float_be(data: PackedByteArray, offset: int) -> float:
	return float(_read_uint32_be(data, offset))

func _read_vector3_be(data: PackedByteArray, offset: int) -> Vector3:
	return Vector3(
		_read_float_be(data, offset),
		_read_float_be(data, offset + 4),
		_read_float_be(data, offset + 8)
	)

func _read_string_be(data: PackedByteArray, offset: int) -> String:
	var length = _read_uint16_be(data, offset)
	offset += 2
	if offset + length > data.size():
		return ""
	return data.slice(offset, offset + length).get_string_from_utf8()

# --- CRC32 (matching server polynomial 0xEDB88320) ---

func _crc32(data: PackedByteArray) -> int:
	var crc: int = 0xFFFFFFFF
	for i in range(data.size()):
		crc ^= data[i]
		for _j in range(8):
			if crc & 1:
				crc = (crc >> 1) ^ 0xEDB88320
			else:
				crc = crc >> 1
	return ~crc & 0xFFFFFFFF

# --- Connection ---

func connect_binary():
	tcp_client = StreamPeerTCP.new()
	var error = tcp_client.connect_to_host(server_address, server_port)

	if error == OK:
		if use_ssl:
			ssl_context = StreamPeerTLS.new()
			error = ssl_context.connect_to_stream(tcp_client, server_address)
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
	var url = "ws://%s:%d%s" % [server_address, server_port, ws_path]
	if use_ssl:
		url = "wss://%s:%d%s" % [server_address, server_port, ws_path]

	var error = websocket.connect_to_url(url)

	if error == OK:
		set_connection_state(ConnectionState.HANDSHAKE)
	else:
		set_connection_state(ConnectionState.ERROR)
		emit_signal("error_occurred", error, "WebSocket connection failed")

# --- Network processing ---

func process_network():
	match protocol_type:
		ProtocolType.BINARY:
			process_binary()
		ProtocolType.WEBSOCKET:
			process_websocket()

func process_binary():
	if not tcp_client or tcp_client.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		return

	var available = tcp_client.get_available_bytes()
	if available > 0:
		var result = tcp_client.get_data(available)
		if result[0] == OK:
			var new_data = result[1]
			recv_buffer.append_array(new_data)
			process_recv_buffer()

func process_recv_buffer():
	while recv_buffer.size() >= 20:
		var msg_type = _read_uint16_be(recv_buffer, 2)
		var length = _read_uint32_be(recv_buffer, 12)

		if length > 10 * 1024 * 1024:
			recv_buffer = PackedByteArray()
			return

		var total = 20 + length
		if recv_buffer.size() < total:
			return

		var message = {
			"version": recv_buffer[0],
			"flags": recv_buffer[1],
			"message_type": msg_type,
			"sequence": _read_uint32_be(recv_buffer, 4),
			"timestamp": _read_uint32_be(recv_buffer, 8),
			"length": length,
		}

		if length > 0:
			message.data = recv_buffer.slice(20, total)
		else:
			message.data = PackedByteArray()

		recv_buffer = recv_buffer.slice(total, recv_buffer.size())

		if message.flags & ProtocolFlags.FLAG_RELIABLE:
			handle_ack(message.sequence)

		incoming_queue.append(message)

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
	while incoming_queue.size() > 0:
		var message = incoming_queue.pop_front()
		handle_incoming_message(message)

	while outgoing_queue.size() > 0 and connection_state == ConnectionState.CONNECTED:
		var message = outgoing_queue.pop_front()
		send_network_message(message)

# --- Serialization ---

func send_network_message(message: Dictionary):
	match protocol_type:
		ProtocolType.BINARY:
			var packet = serialize_binary_message(message)
			if use_ssl and ssl_context:
				ssl_context.put_data(packet)
			elif tcp_client:
				tcp_client.put_data(packet)

		ProtocolType.WEBSOCKET:
			pass

func send_ws_json(json_data: Dictionary):
	if websocket:
		websocket.send(JSON.stringify(json_data).to_utf8_buffer())

func serialize_binary_message(message: Dictionary) -> PackedByteArray:
	var buffer = PackedByteArray()

	# Header: all little-endian matching server memcpy on x86
	buffer.append(message.version & 0xFF)
	buffer.append(message.flags & 0xFF)

	# message_type (2 bytes, little-endian)
	buffer.append(message.message_type & 0xFF)
	buffer.append((message.message_type >> 8) & 0xFF)

	# sequence (4 bytes, little-endian)
	for i in range(4):
		buffer.append((message.sequence >> (i * 8)) & 0xFF)

	# timestamp (4 bytes, little-endian)
	var timestamp = message.timestamp
	for i in range(4):
		buffer.append((timestamp >> (i * 8)) & 0xFF)

	# length (4 bytes, little-endian)
	var length = message.data.size()
	for i in range(4):
		buffer.append((length >> (i * 8)) & 0xFF)

	# checksum (4 bytes, placeholder, little-endian)
	for i in range(4):
		buffer.append(0)

	# Data
	buffer.append_array(message.data)

	# Calculate CRC32 checksum over body only (matching server)
	var body_bytes = message.data
	if body_bytes.size() > 0:
		var checksum = _crc32(body_bytes)
		for i in range(4):
			buffer[20 + i] = (checksum >> (i * 8)) & 0xFF

	return buffer

# --- Incoming parsing ---

func parse_websocket_data(data: PackedByteArray):
	var json_string = data.get_string_from_utf8()
	if json_string == "":
		return

	var json = JSON.new()
	var error = json.parse(json_string)
	if error != OK:
		return

	var message_data = json.data
	if not message_data is Dictionary:
		return

	var message = {
		"message_type": 0,
		"data": message_data,
		"flags": 0,
		"sequence": 0,
		"timestamp": 0
	}
	incoming_queue.append(message)

# --- Checksum ---

func verify_checksum(data: PackedByteArray) -> bool:
	if data.size() < 20:
		return false
	var length = _read_uint32_be(data, 12)
	if data.size() < 20 + length:
		return false
	var stored = 0
	for i in range(4):
		stored |= (data[20 + i] << (i * 8))
	var body = data.slice(20, 20 + length)
	return _crc32(body) == stored

# --- Message dispatch ---

func handle_incoming_message(message: Dictionary):
	var message_type = message.message_type

	if message_handlers.has(message_type):
		message_handlers[message_type].call(message.data)
	else:
		if default_message_handler:
			default_message_handler.call(message_type, message.data)

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
	recv_buffer = PackedByteArray()
	incoming_queue.clear()
	outgoing_queue.clear()
	pending_acks.clear()
	sequence_number = 0
	player_id = 0
	auth_token = ""
	protocol_negotiated = false

func update_network_quality(success: bool):
	pass

# --- Heartbeat ---

func check_heartbeat(delta: float):
	last_heartbeat += delta
	if last_heartbeat >= heartbeat_interval:
		send_heartbeat()
		last_heartbeat = 0.0

func send_heartbeat():
	send_message(MessageType.MESSAGE_TYPE_HEARTBEAT, PackedByteArray())

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

# --- Prediction system ---

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

	if input_buffer.size() > 100:
		input_buffer.pop_front()

func update_prediction(delta: float):
	if not prediction_enabled or input_buffer.size() == 0:
		return

func reconcile_with_server(server_state: Dictionary):
	if not prediction_enabled:
		return

	var last_confirmed_input_id = server_state.get("last_input_id", 0)

	var i = 0
	while i < input_buffer.size():
		if input_buffer[i].input_id <= last_confirmed_input_id:
			input_buffer.remove_at(i)
		else:
			i += 1

	var correction = server_state.get("correction", null)
	if correction:
		emit_signal("player_position_corrected", Vector3(
			correction.x,
			correction.y,
			correction.z
		))

# --- Message handlers ---

func _handle_heartbeat(_data: PackedByteArray):
	last_heartbeat = 0.0
	update_network_quality(true)

func _handle_success(data: PackedByteArray):
	if connection_state == ConnectionState.HANDSHAKE or connection_state == ConnectionState.CONNECTING:
		protocol_negotiated = true
		set_connection_state(ConnectionState.CONNECTED)
		return

	var json_string = data.get_string_from_utf8()
	if json_string:
		var json = JSON.new()
		if json.parse(json_string) == OK:
			var success_data = json.data
			if connection_state == ConnectionState.AUTHENTICATING:
				emit_signal("authentication_result", true, success_data.get("message", "Authenticated"))
				set_connection_state(ConnectionState.CONNECTED)
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

func _handle_biome_data(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_BIOME_DATA, data)

func _handle_entity_spawn(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_ENTITY_SPAWN, data)

func _handle_entity_update(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_ENTITY_UPDATE, data)

func _handle_entity_despawn(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_ENTITY_DESPAWN, data)

func _handle_position_correction(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_PLAYER_POSITION_CORRECTION, data)

func _handle_player_state(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_PLAYER_STATE, data)

func _handle_health_update(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_HEALTH_UPDATE, data)

func _handle_inventory_update(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_INVENTORY_UPDATE, data)

func _handle_loot_spawn(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_LOOT_SPAWN, data)

func _handle_chat_message(data: PackedByteArray):
	var result = {"sender": "", "message": "", "timestamp": 0}
	if data.size() > 0:
		var offset = 0
		result.sender = _read_string_be(data, offset)
		offset += 2 + result.sender.to_utf8_buffer().size()
		if offset < data.size():
			result.message = _read_string_be(data, offset)
			offset += 2 + result.message.to_utf8_buffer().size()
		if offset + 8 <= data.size():
			result.timestamp = _read_uint64_be(data, offset)
	emit_signal("message_received", MessageType.MESSAGE_TYPE_CHAT_MESSAGE, result)

func _handle_system_message(data: PackedByteArray):
	emit_signal("message_received", MessageType.MESSAGE_TYPE_SYSTEM_MESSAGE, data)
