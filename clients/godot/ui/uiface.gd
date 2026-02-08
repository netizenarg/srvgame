extends Control

class_name UIManager

# UI nodes
@onready var connection_status = $ConnectionStatus
@onready var login_panel = $LoginPanel
@onready var chat_panel = $ChatPanel
@onready var inventory_panel = $InventoryPanel
@onready var health_bar = $HealthBar
@onready var minimap = $Minimap
@onready var debug_info = $DebugInfo

# Game references
var network_manager: NetworkManager
var player_node: Node3D

# UI state
var chat_messages: Array = []
var inventory_items: Array = []
var is_chat_focused: bool = false

func _ready():
	hide_all_panels()
	connection_status.visible = true

func _input(event):
	if event is InputEventKey:
		if event.pressed:
			if event.keycode == KEY_ENTER:
				if not is_chat_focused:
					focus_chat()
				else:
					send_chat_message()
			elif event.keycode == KEY_ESCAPE:
				if is_chat_focused:
					unfocus_chat()
				else:
					toggle_inventory()
			elif event.keycode == KEY_TAB:
				toggle_minimap()

func setup_references(network: NetworkManager, player: Node3D):
	network_manager = network
	player_node = player
	
	# Connect signals
	if network_manager:
		network_manager.connection_state_changed.connect(_on_connection_state_changed)
		network_manager.message_received.connect(_on_message_received)

func show_login_panel():
	login_panel.visible = true
	login_panel.get_node("Username").grab_focus()

func hide_login_panel():
	login_panel.visible = false

func show_message(message: String, duration: float = 3.0):
	var message_label = Label.new()
	message_label.text = message
	message_label.modulate = Color(1, 1, 1, 0)
	message_label.position = Vector2(get_viewport().size.x / 2 - 100, 50)
	add_child(message_label)
	
	var tween = create_tween()
	tween.tween_property(message_label, "modulate", Color.WHITE, 0.5)
	tween.tween_property(message_label, "modulate", Color(1, 1, 1, 0), 0.5).set_delay(duration)
	tween.tween_callback(message_label.queue_free)

func update_connection_status(state: int):
	match state:
		NetworkManager.ConnectionState.DISCONNECTED:
			connection_status.text = "Disconnected"
			connection_status.modulate = Color.RED
		NetworkManager.ConnectionState.CONNECTING:
			connection_status.text = "Connecting..."
			connection_status.modulate = Color.YELLOW
		NetworkManager.ConnectionState.HANDSHAKE:
			connection_status.text = "Handshake..."
			connection_status.modulate = Color.YELLOW
		NetworkManager.ConnectionState.AUTHENTICATING:
			connection_status.text = "Authenticating..."
			connection_status.modulate = Color.YELLOW
		NetworkManager.ConnectionState.CONNECTED:
			connection_status.text = "Connected"
			connection_status.modulate = Color.GREEN
		NetworkManager.ConnectionState.ERROR:
			connection_status.text = "Error"
			connection_status.modulate = Color.RED

func add_chat_message(sender: String, message: String, channel: String = "global"):
	var timestamp = Time.get_time_string_from_system()
	var formatted_message = "[%s] [%s] %s: %s" % [timestamp, channel, sender, message]
	
	chat_messages.append(formatted_message)
	
	# Keep only last 50 messages
	if chat_messages.size() > 50:
		chat_messages.pop_front()
	
	# Update chat display
	update_chat_display()
	
	# Show notification for important messages
	if channel == "system" or sender == "SERVER":
		show_message("[%s] %s" % [channel, message])

func update_chat_display():
	var chat_display = chat_panel.get_node("ChatDisplay")
	if chat_display:
		chat_display.text = "\n".join(chat_messages)
		# Scroll to bottom
		var scroll = chat_display.get_parent()
		if scroll is ScrollContainer:
			scroll.scroll_vertical = scroll.get_v_scroll_bar().max_value

func focus_chat():
	is_chat_focused = true
	chat_panel.visible = true
	chat_panel.get_node("ChatInput").grab_focus()
	chat_panel.get_node("ChatInput").text = ""

func unfocus_chat():
	is_chat_focused = false
	chat_panel.visible = false
	chat_panel.get_node("ChatInput").release_focus()

func send_chat_message():
	var chat_input = chat_panel.get_node("ChatInput")
	var message = chat_input.text.strip_edges()
	
	if message != "":
		if network_manager and network_manager.connection_state == NetworkManager.ConnectionState.CONNECTED:
			network_manager.send_chat_message(message)
		
		# Add to local chat
		add_chat_message("You", message)
	
	chat_input.text = ""
	unfocus_chat()

func update_inventory(inventory_data: Dictionary):
	inventory_items.clear()
	
	if inventory_data.has("items"):
		for item in inventory_data.items:
			inventory_items.append(item)
	
	update_inventory_display()

func update_inventory_display():
	var inventory_grid = inventory_panel.get_node("InventoryGrid")
	if inventory_grid:
		# Clear current display
		for child in inventory_grid.get_children():
			child.queue_free()
		
		# Create inventory slots
		for i in range(min(inventory_items.size(), 40)):  # Max 40 slots
			var item = inventory_items[i]
			var slot = create_inventory_slot(item)
			slot.position = Vector2((i % 8) * 60, (i / 8) * 60)
			inventory_grid.add_child(slot)

func create_inventory_slot(item_data: Dictionary) -> Control:
	var slot = Panel.new()
	slot.custom_minimum_size = Vector2(50, 50)
	slot.mouse_filter = Control.MOUSE_FILTER_PASS
	
	# Item icon
	var icon = TextureRect.new()
	if item_data.has("icon_path"):
		var texture = load(item_data.icon_path)
		if texture:
			icon.texture = texture
	slot.add_child(icon)
	
	# Item count
	if item_data.has("quantity") and item_data.quantity > 1:
		var count_label = Label.new()
		count_label.text = str(item_data.quantity)
		count_label.position = Vector2(30, 30)
		slot.add_child(count_label)
	
	# Tooltip
	slot.tooltip_text = "%s\n%s" % [item_data.get("name", "Unknown"), 
								   item_data.get("description", "")]
	
	return slot

func update_health(current: float, max_health: float):
	if health_bar:
		health_bar.value = (current / max_health) * 100
		health_bar.get_node("HealthText").text = "%d/%d" % [current, max_health]

func toggle_inventory():
	inventory_panel.visible = !inventory_panel.visible

func toggle_minimap():
	minimap.visible = !minimap.visible

func toggle_debug_info():
	debug_info.visible = !debug_info.visible

func update_minimap(player_position: Vector3, entities: Array):
	if not minimap.visible:
		return
	
	# Update minimap display
	# This would typically render a small map with player and nearby entities
	pass

func update_debug_info(network_stats: Dictionary, player_stats: Dictionary):
	if not debug_info.visible:
		return
	
	var info_text = "=== DEBUG INFO ===\n"
	info_text += "FPS: %d\n" % Engine.get_frames_per_second()
	info_text += "Position: %s\n" % str(player_node.global_transform.origin)
	info_text += "Rotation: %s\n" % str(player_node.rotation)
	
	if network_manager:
		info_text += "Ping: %dms\n" % network_stats.get("latency", 0)
		info_text += "Packet Loss: %.1f%%\n" % network_stats.get("packet_loss", 0.0)
		info_text += "Queue: %d/%d\n" % [network_stats.get("queue_size", 0), 
									   network_stats.get("max_queue", 100)]
	
	debug_info.text = info_text

func hide_all_panels():
	login_panel.visible = false
	chat_panel.visible = false
	inventory_panel.visible = false
	minimap.visible = false
	debug_info.visible = false

func _on_login_submitted(username: String, password: String):
	# This would typically validate locally first
	if username.strip_edges() == "":
		show_message("Username cannot be empty")
		return
	
	# Signal to main game to authenticate
	emit_signal("login_requested", username, password)

func _on_connection_state_changed(state: int):
	update_connection_status(state)

func _on_message_received(message_type: int, data):
	match message_type:
		NetworkManager.MessageType.MESSAGE_TYPE_CHAT_MESSAGE:
			if data is Dictionary:
				add_chat_message(data.get("sender", "Unknown"), 
							   data.get("message", ""),
							   data.get("channel", "global"))
		
		NetworkManager.MessageType.MESSAGE_TYPE_SYSTEM_MESSAGE:
			if data is Dictionary:
				show_message(data.get("message", ""), 5.0)

func _on_inventory_item_clicked(item_index: int, button_index: int):
	if button_index == MOUSE_BUTTON_LEFT:
		# Use item
		emit_signal("item_use_requested", item_index)
	elif button_index == MOUSE_BUTTON_RIGHT:
		# Drop item
		emit_signal("item_drop_requested", item_index)