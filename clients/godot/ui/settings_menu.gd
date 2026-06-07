extends Control

class_name SettingsMenu

signal back_pressed()
signal settings_saved(settings: Dictionary)

# Network
@onready var host_input = $VBox/TabContainer/Network/VBox/HostInput
@onready var port_input = $VBox/TabContainer/Network/VBox/PortInput
@onready var protocol_option = $VBox/TabContainer/Network/VBox/ProtocolOption

# Account
@onready var login_input = $VBox/TabContainer/Account/VBox/LoginInput
@onready var password_input = $VBox/TabContainer/Account/VBox/PasswordInput

# Graphics
@onready var render_distance_slider = $VBox/TabContainer/Graphics/VBox/RenderDistance/Slider
@onready var render_distance_label = $VBox/TabContainer/Graphics/VBox/RenderDistance/Value
@onready var vsync_button = $VBox/TabContainer/Graphics/VBox/VSync/CheckBox
@onready var msaa_button = $VBox/TabContainer/Graphics/VBox/MSAA/CheckBox
@onready var fullscreen_button = $VBox/TabContainer/Graphics/VBox/Fullscreen/CheckBox
@onready var mouse_sensitivity_slider = $VBox/TabContainer/Graphics/VBox/MouseSensitivity/Slider
@onready var mouse_sensitivity_label = $VBox/TabContainer/Graphics/VBox/MouseSensitivity/Value
@onready var volume_slider = $VBox/TabContainer/Graphics/VBox/Volume/Slider
@onready var volume_label = $VBox/TabContainer/Graphics/VBox/Volume/Value

@onready var save_button = $VBox/Buttons/SaveButton
@onready var back_button = $VBox/Buttons/BackButton

func _ready():
	save_button.pressed.connect(_on_save_pressed)
	back_button.pressed.connect(_on_back_pressed)
	render_distance_slider.value_changed.connect(_on_render_distance_changed)
	mouse_sensitivity_slider.value_changed.connect(_on_mouse_sensitivity_changed)
	volume_slider.value_changed.connect(_on_volume_changed)
	vsync_button.toggled.connect(_on_vsync_toggled)
	fullscreen_button.toggled.connect(_on_fullscreen_toggled)
	msaa_button.toggled.connect(_on_msaa_toggled)
	load_defaults()

func load_defaults():
	host_input.text = "127.0.0.1"
	port_input.text = "8080"
	protocol_option.clear()
	protocol_option.add_item("Binary", 0)
	protocol_option.add_item("WebSocket", 1)
	protocol_option.selected = 0
	login_input.text = ""
	password_input.text = ""
	render_distance_slider.value = 3
	render_distance_label.text = "3"
	mouse_sensitivity_slider.value = 0.002
	mouse_sensitivity_label.text = "0.002"
	volume_slider.value = 80
	volume_label.text = "80%"
	vsync_button.button_pressed = true
	fullscreen_button.button_pressed = false
	msaa_button.button_pressed = true

func load_settings(settings: Dictionary):
	if settings.has("host"):
		host_input.text = settings.host
	if settings.has("port"):
		port_input.text = str(settings.port)
	if settings.has("protocol"):
		protocol_option.selected = settings.protocol
	if settings.has("login"):
		login_input.text = settings.login
	if settings.has("password"):
		password_input.text = settings.password
	if settings.has("render_distance"):
		render_distance_slider.value = settings.render_distance
		render_distance_label.text = str(settings.render_distance)
	if settings.has("mouse_sensitivity"):
		mouse_sensitivity_slider.value = settings.mouse_sensitivity
		mouse_sensitivity_label.text = str(settings.mouse_sensitivity)
	if settings.has("volume"):
		volume_slider.value = settings.volume
		volume_label.text = str(int(settings.volume)) + "%"
	if settings.has("vsync"):
		vsync_button.button_pressed = settings.vsync
	if settings.has("fullscreen"):
		fullscreen_button.button_pressed = settings.fullscreen
	if settings.has("msaa"):
		msaa_button.button_pressed = settings.msaa

func _on_render_distance_changed(value: float):
	render_distance_label.text = str(int(value))

func _on_mouse_sensitivity_changed(value: float):
	mouse_sensitivity_label.text = str(value)

func _on_volume_changed(value: float):
	volume_label.text = str(int(value)) + "%"
	AudioServer.set_bus_volume_db(0, linear_to_db(value / 100.0))

func _on_vsync_toggled(pressed: bool):
	DisplayServer.window_set_vsync_mode(
		DisplayServer.VSYNC_ENABLED if pressed else DisplayServer.VSYNC_DISABLED
	)

func _on_fullscreen_toggled(pressed: bool):
	DisplayServer.window_set_mode(
		DisplayServer.WINDOW_MODE_FULLSCREEN if pressed else DisplayServer.WINDOW_MODE_WINDOWED
	)

func _on_msaa_toggled(pressed: bool):
	get_viewport().msaa_3d = Viewport.MSAA_4X if pressed else Viewport.MSAA_DISABLED

func _on_save_pressed():
	emit_signal("settings_saved", get_settings())

func _on_back_pressed():
	emit_signal("back_pressed")

func get_settings() -> Dictionary:
	return {
		"host": host_input.text.strip_edges(),
		"port": port_input.text.strip_edges().to_int(),
		"protocol": protocol_option.selected,
		"login": login_input.text.strip_edges(),
		"password": password_input.text,
		"render_distance": int(render_distance_slider.value),
		"mouse_sensitivity": mouse_sensitivity_slider.value,
		"volume": volume_slider.value,
		"vsync": vsync_button.button_pressed,
		"fullscreen": fullscreen_button.button_pressed,
		"msaa": msaa_button.button_pressed,
	}
