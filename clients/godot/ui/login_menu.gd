extends Control

class_name LoginMenu

signal login_submitted(username: String, password: String)
signal back_pressed()

@onready var username_input = $VBoxContainer/UsernameInput
@onready var password_input = $VBoxContainer/PasswordInput
@onready var login_button = $VBoxContainer/LoginButton
@onready var back_button = $VBoxContainer/BackButton
@onready var status_label = $VBoxContainer/StatusLabel
@onready var server_address_input = $VBoxContainer/ServerAddressInput
@onready var server_port_input = $VBoxContainer/ServerPortInput

func _ready():
	username_input.grab_focus()
	login_button.pressed.connect(_on_login_pressed)
	back_button.pressed.connect(_on_back_pressed)
	password_input.text_submitted.connect(_on_login_pressed)
	
	server_address_input.text = "127.0.0.1"
	server_port_input.text = "8080"

func set_status(text: String, color: Color = Color.WHITE):
	status_label.text = text
	status_label.modulate = color

func _on_login_pressed(_text: String = ""):
	var username = username_input.text.strip_edges()
	var password = password_input.text
	
	if username == "":
		set_status("Username cannot be empty", Color.RED)
		return
	
	emit_signal("login_submitted", username, password)

func _on_back_pressed():
	emit_signal("back_pressed")

func get_server_address() -> String:
	return server_address_input.text.strip_edges()

func get_server_port() -> int:
	return server_port_input.text.to_int()

func set_server(host: String, port: int):
	server_address_input.text = host
	server_port_input.text = str(port)

func set_credentials(host: String, port: int, user: String, passw: String):
	server_address_input.text = host
	server_port_input.text = str(port)
	username_input.text = user
	password_input.text = passw

func set_loading(loading: bool):
	login_button.disabled = loading
	if loading:
		set_status("Connecting...", Color.YELLOW)
