extends Control

class_name MainMenu

signal play_pressed()
signal local_play_pressed()
signal settings_pressed()
signal quit_pressed()

@onready var title_label = $VBoxContainer/TitleLabel
@onready var play_button = $VBoxContainer/PlayButton
@onready var local_play_button = $VBoxContainer/LocalPlayButton
@onready var settings_button = $VBoxContainer/SettingsButton
@onready var quit_button = $VBoxContainer/QuitButton
@onready var version_label = $VersionLabel
@onready var status_label = $StatusLabel

func _ready():
	play_button.grab_focus()
	play_button.pressed.connect(_on_play_pressed)
	local_play_button.pressed.connect(_on_local_play_pressed)
	settings_button.pressed.connect(_on_settings_pressed)
	quit_button.pressed.connect(_on_quit_pressed)
	version_label.text = "v0.1.0"

func set_status(text: String, color: Color = Color.WHITE):
	status_label.text = text
	status_label.modulate = color

func _on_play_pressed():
	emit_signal("play_pressed")

func _on_local_play_pressed():
	emit_signal("local_play_pressed")

func _on_settings_pressed():
	emit_signal("settings_pressed")

func _on_quit_pressed():
	emit_signal("quit_pressed")
	get_tree().quit()
