extends Control

class_name PauseMenu

signal resume_pressed()
signal settings_pressed()
signal disconnect_pressed()
signal quit_pressed()

@onready var resume_button = $VBoxContainer/ResumeButton
@onready var settings_button = $VBoxContainer/SettingsButton
@onready var disconnect_button = $VBoxContainer/DisconnectButton
@onready var quit_button = $VBoxContainer/QuitButton

func _ready():
	visible = false
	resume_button.pressed.connect(_on_resume_pressed)
	settings_button.pressed.connect(_on_settings_pressed)
	disconnect_button.pressed.connect(_on_disconnect_pressed)
	quit_button.pressed.connect(_on_quit_pressed)

func toggle():
	visible = !visible
	if visible:
		resume_button.grab_focus()
		get_tree().paused = true
	else:
		get_tree().paused = false

func _on_resume_pressed():
	visible = false
	get_tree().paused = false
	emit_signal("resume_pressed")

func _on_settings_pressed():
	emit_signal("settings_pressed")

func _on_disconnect_pressed():
	visible = false
	get_tree().paused = false
	emit_signal("disconnect_pressed")

func _on_quit_pressed():
	emit_signal("quit_pressed")
	get_tree().quit()
