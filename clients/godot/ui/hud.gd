extends Control

class_name GameHUD

signal chat_submitted(message: String)
signal inventory_toggled()

@onready var health_bar = $MarginContainer/VBoxLeft/HealthBar
@onready var health_label = $MarginContainer/VBoxLeft/HealthBar/HealthLabel
@onready var mana_bar = $MarginContainer/VBoxLeft/ManaBar
@onready var mana_label = $MarginContainer/VBoxLeft/ManaBar/ManaLabel
@onready var xp_bar = $MarginContainer/VBoxLeft/XPBar
@onready var xp_label = $MarginContainer/VBoxLeft/XPBar/XPLabel
@onready var chat_panel = $MarginContainer/VBoxRight/ChatPanel
@onready var chat_display = $MarginContainer/VBoxRight/ChatPanel/ScrollContainer/ChatDisplay
@onready var chat_input = $MarginContainer/VBoxRight/ChatPanel/ChatInput
@onready var minimap = $Minimap
@onready var connection_status = $ConnectionStatus
@onready var debug_info = $DebugInfo
@onready var fps_label = $FPSLabel

var chat_messages: Array = []
var max_chat_messages: int = 50
var is_chat_focused: bool = false

func _ready():
	visible = false
	chat_input.text_submitted.connect(_on_chat_submitted)
	chat_input.focus_entered.connect(_on_chat_focus_entered)
	chat_input.focus_exited.connect(_on_chat_focus_exited)

func show_hud():
	visible = true
	hide_all_popups()

func hide_hud():
	visible = false

func hide_all_popups():
	chat_panel.visible = false
	minimap.visible = false
	debug_info.visible = false

func _process(_delta):
	if fps_label:
		fps_label.text = "FPS: %d" % Engine.get_frames_per_second()

func _input(event):
	if not visible:
		return
	
	if event is InputEventKey:
		if event.pressed:
			match event.keycode:
				KEY_ENTER:
					if not is_chat_focused:
						show_chat()
					elif chat_input.text.strip_edges() != "":
						_on_chat_submitted(chat_input.text)
						hide_chat()
				KEY_ESCAPE:
					if is_chat_focused:
						hide_chat()
					else:
						get_parent().get_parent().get_node("PauseMenu").toggle()
				KEY_TAB:
					toggle_minimap()
				KEY_F1:
					toggle_debug()
				KEY_I:
					emit_signal("inventory_toggled")

func update_health(current: float, maximum: float):
	if health_bar:
		health_bar.value = current
		health_bar.max_value = maximum
		health_label.text = "%d / %d" % [int(current), int(maximum)]

func update_mana(current: float, maximum: float):
	if mana_bar:
		mana_bar.value = current
		mana_bar.max_value = maximum
		mana_label.text = "%d / %d" % [int(current), int(maximum)]

func update_xp(current: float, maximum: float):
	if xp_bar:
		xp_bar.value = current
		xp_bar.max_value = maximum
		xp_label.text = "%d / %d" % [int(current), int(maximum)]

func add_chat_message(sender: String, message: String, channel: String = "global"):
	var timestamp = Time.get_time_string_from_system()
	var formatted = "[%s] [%s] %s: %s" % [timestamp, channel, sender, message]
	chat_messages.append(formatted)
	
	if chat_messages.size() > max_chat_messages:
		chat_messages.pop_front()
	
	_update_chat_display()

func _update_chat_display():
	if chat_display:
		chat_display.text = "\n".join(chat_messages)

func show_chat():
	is_chat_focused = true
	chat_panel.visible = true
	chat_input.text = ""
	chat_input.grab_focus()

func hide_chat():
	is_chat_focused = false
	chat_input.text = ""
	chat_input.release_focus()
	chat_panel.visible = false

func _on_chat_submitted(text: String):
	var message = text.strip_edges()
	if message != "":
		emit_signal("chat_submitted", message)
		add_chat_message("You", message)
	chat_input.text = ""

func _on_chat_focus_entered():
	is_chat_focused = true

func _on_chat_focus_exited():
	is_chat_focused = false

func toggle_minimap():
	minimap.visible = not minimap.visible

func toggle_debug():
	debug_info.visible = not debug_info.visible

func update_connection_status(state: int):
	match state:
		0:
			connection_status.text = "Disconnected"
			connection_status.modulate = Color.RED
		1:
			connection_status.text = "Connecting..."
			connection_status.modulate = Color.YELLOW
		2:
			connection_status.text = "Handshake..."
			connection_status.modulate = Color.YELLOW
		3:
			connection_status.text = "Authenticating..."
			connection_status.modulate = Color.YELLOW
		4:
			connection_status.text = "Connected"
			connection_status.modulate = Color.GREEN
		5:
			connection_status.text = "Error"
			connection_status.modulate = Color.RED

func update_debug_info(info: Dictionary):
	if not debug_info.visible:
		return
	var text = "=== DEBUG ===\n"
	text += "FPS: %d\n" % info.get("fps", Engine.get_frames_per_second())
	text += "Ping: %dms\n" % info.get("latency", 0)
	text += "Players: %d\n" % info.get("players", 0)
	debug_info.text = text
