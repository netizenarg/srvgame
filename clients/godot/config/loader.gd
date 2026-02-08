extends Node

class_name ConfigLoader

# Default configuration
var default_config: Dictionary = {
	"network": {
		"server_address": "127.0.0.1",
		"server_port": 8080,
		"protocol_type": 0,
		"use_ssl": false,
		"reconnect_attempts": 3,
		"reconnect_delay": 2.0,
		"heartbeat_interval": 5.0,
		"prediction_enabled": true,
		"compression_enabled": false,
		"encryption_enabled": false,
		"max_message_size": 10485760
	},
	"player": {
		"username": "Player",
		"character_name": "",
		"character_class": "warrior",
		"character_level": 1,
		"character_race": "human",
		"player_scale_x": 1.0,
		"player_scale_y": 1.0,
		"player_scale_z": 1.0,
		"player_color_primary": "#4A6F9B",
		"player_color_secondary": "#2A4D6B",
		"player_color_accent": "#FFD700",
		"auto_login": false,
		"remember_password": false
	},
	"graphics": {
		"render_distance": 3,
		"chunk_size": 16,
		"chunk_height": 64,
		"max_chunks": 100,
		"lod_levels": 3,
		"lod_distances": [20, 40, 80],
		"fov": 70.0,
		"view_distance": 1000.0,
		"shadow_quality": 1,
		"texture_quality": 1,
		"water_quality": 1,
		"particle_quality": 1,
		"antialiasing": 1,
		"vsync": true,
		"fullscreen": false,
		"resolution_width": 1920,
		"resolution_height": 1080,
		"frame_rate_limit": 0,
		"brightness": 1.0,
		"contrast": 1.0,
		"saturation": 1.0,
		"gamma": 1.0
	},
	"audio": {
		"master_volume": 1.0,
		"music_volume": 0.8,
		"effects_volume": 1.0,
		"ui_volume": 0.6,
		"environment_volume": 1.0,
		"voice_volume": 0.9,
		"mute_when_minimized": true,
		"output_device": "Default",
		"enable_3d_audio": true,
		"reverb_quality": 1
	},
	"controls": {
		"mouse_sensitivity": 0.002,
		"mouse_smoothing": 0.1,
		"mouse_invert_x": false,
		"mouse_invert_y": false,
		"mouse_acceleration": false,
		"keyboard_layout": "QWERTY",
		"move_forward": 87,
		"move_back": 83,
		"move_left": 65,
		"move_right": 68,
		"jump": 32,
		"crouch": 67,
		"sprint": 16777237,
		"interact": 69,
		"attack": 1,
		"alt_attack": 2,
		"inventory": 9,
		"map": 77,
		"journal": 74,
		"character_sheet": 80,
		"skills": 75,
		"quests": 81,
		"chat": 13,
		"console": 192,
		"screenshot": 44,
		"debug_info": 16777247,
		"wireframe": 16777248,
		"toggle_prediction": 16777249,
		"minimap": 16777250,
		"quick_slot_1": 49,
		"quick_slot_2": 50,
		"quick_slot_3": 51,
		"quick_slot_4": 52,
		"quick_slot_5": 53,
		"quick_slot_6": 54,
		"quick_slot_7": 55,
		"quick_slot_8": 56,
		"quick_slot_9": 57,
		"quick_slot_0": 48
	},
	"game": {
		"language": "en",
		"region": "US",
		"time_format": 24,
		"date_format": 0,
		"unit_system": 0,
		"show_tutorials": true,
		"show_hints": true,
		"auto_loot": true,
		"auto_equip": true,
		"auto_sort_inventory": true,
		"loot_notifications": true,
		"xp_notifications": true,
		"quest_notifications": true,
		"trade_notifications": true,
		"guild_notifications": true,
		"combat_text": true,
		"damage_numbers": true,
		"healing_numbers": true,
		"crit_indicator": true,
		"floating_text": true,
		"target_health_bars": true,
		"party_health_bars": true,
		"enemy_health_bars": true,
		"friendly_health_bars": true,
		"nameplates": true,
		"nameplate_distance": 50.0,
		"pet_health_bars": true,
		"familiar_health_bars": true,
		"show_own_nameplate": false,
		"show_fps": true,
		"show_ping": true,
		"show_location": true,
		"show_clock": true,
		"show_day_night_cycle": true
	},
	"ui": {
		"ui_scale": 1.0,
		"font_size": 14,
		"chat_font_size": 12,
		"inventory_size": 40,
		"tooltip_delay": 0.5,
		"tooltip_duration": 5.0,
		"chat_opacity": 0.8,
		"chat_background_opacity": 0.3,
		"chat_lines": 100,
		"chat_timestamps": true,
		"chat_channel_colors": true,
		"minimap_enabled": true,
		"minimap_size": 200,
		"minimap_opacity": 0.8,
		"minimap_rotation": true,
		"minimap_zoom": 1.0,
		"health_bar_style": 0,
		"mana_bar_style": 0,
		"stamina_bar_style": 0,
		"experience_bar_style": 0,
		"cast_bar_style": 0,
		"target_frame_style": 0,
		"player_frame_style": 0,
		"party_frame_style": 0,
		"action_bar_style": 0,
		"action_bar_rows": 2,
		"action_bar_columns": 10,
		"action_bar_scale": 1.0,
		"action_bar_opacity": 0.9,
		"action_bar_keybind_visibility": true,
		"action_bar_numbers": true,
		"buff_display_style": 0,
		"debuff_display_style": 0,
		"show_ui_in_combat": true,
		"show_ui_in_cutscene": false,
		"ui_animations": true,
		"ui_sound_effects": true,
		"highlight_hover": true,
		"highlight_selected": true
	},
	"world": {
		"terrain_quality": 1,
		"water_quality": 1,
		"vegetation_density": 0.7,
		"tree_density": 0.5,
		"rock_density": 0.3,
		"grass_density": 0.8,
		"flower_density": 0.4,
		"shadow_distance": 50.0,
		"shadow_resolution": 2048,
		"shadow_filter_quality": 1,
		"ambient_occlusion": true,
		"ambient_occlusion_radius": 1.0,
		"ambient_occlusion_intensity": 1.0,
		"screen_space_reflections": true,
		"screen_space_reflections_quality": 1,
		"depth_of_field": false,
		"depth_of_field_intensity": 1.0,
		"bloom": true,
		"bloom_intensity": 0.8,
		"bloom_threshold": 0.8,
		"color_grading": true,
		"color_grading_lut": "Default",
		"weather_effects": true,
		"weather_intensity": 1.0,
		"particle_effects": true,
		"particle_limit": 1000,
		"dynamic_sky": true,
		"day_night_cycle_speed": 1.0,
		"fog_enabled": true,
		"fog_density": 0.01,
		"fog_color": "#AAAAAA",
		"fog_start_distance": 10.0,
		"fog_end_distance": 100.0
	},
	"performance": {
		"max_fps": 0,
		"max_chunks_per_frame": 2,
		"max_entities": 1000,
		"entity_despawn_distance": 150.0,
		"entity_update_rate": 0.1,
		"chunk_update_rate": 0.5,
		"lod_update_rate": 0.2,
		"physics_update_rate": 60,
		"collision_check_distance": 50.0,
		"enable_culling": true,
		"enable_occlusion_culling": true,
		"enable_frustum_culling": true,
		"enable_distance_culling": true,
		"enable_lod_culling": true,
		"enable_mipmaps": true,
		"enable_texture_streaming": true,
		"enable_model_streaming": true,
		"enable_sound_streaming": true,
		"texture_filtering": 2,
		"anisotropic_filtering": 4,
		"multisampling": 0,
		"thread_count": 0
	},
	"debug": {
		"show_debug_info": false,
		"show_collision_shapes": false,
		"show_navigation_mesh": false,
		"show_bounding_boxes": false,
		"show_wireframe": false,
		"show_skeleton": false,
		"show_physics_fps": true,
		"show_memory_usage": true,
		"show_network_stats": true,
		"show_chunk_borders": false,
		"show_entity_ids": false,
		"log_level": 2,
		"log_to_file": true,
		"log_file_path": "user://game.log",
		"profiling_enabled": false,
		"profiling_interval": 1.0,
		"debug_console_enabled": false
	},
	"multiplayer": {
		"auto_join_world": true,
		"default_world_id": 1,
		"crossplay_enabled": true,
		"allow_cross_region": true,
		"voice_chat_enabled": true,
		"voice_chat_volume": 1.0,
		"voice_chat_push_to_talk": true,
		"voice_chat_push_to_talk_key": 86,
		"party_auto_invite": false,
		"guild_auto_join": false,
		"trade_requests_enabled": true,
		"duel_requests_enabled": true,
		"friend_requests_enabled": true,
		"group_auto_loot": 1,
		"group_auto_invite": false,
		"group_experience_sharing": true,
		"group_item_sharing": true,
		"group_gold_sharing": true,
		"pvp_enabled": true,
		"pvp_duels_enabled": true,
		"pvp_arenas_enabled": true,
		"pvp_battlegrounds_enabled": true
	},
	"social": {
		"chat_enabled": true,
		"chat_filter_level": 1,
		"chat_allow_links": true,
		"chat_allow_images": false,
		"chat_allow_emojis": true,
		"chat_allow_custom_emojis": false,
		"friend_list_enabled": true,
		"friend_notifications": true,
		"ignore_list_enabled": true,
		"guild_chat_enabled": true,
		"party_chat_enabled": true,
		"whisper_enabled": true,
		"global_chat_enabled": true,
		"trade_chat_enabled": true,
		"looking_for_group_enabled": true,
		"looking_for_guild_enabled": true,
		"auto_decline_duels": false,
		"auto_decline_trades": false,
		"auto_decline_party_invites": false,
		"auto_decline_guild_invites": false,
		"show_online_friends": true,
		"show_offline_friends": false,
		"show_friend_location": false
	},
	"hotkeys": {
		"hotkey_slot_1": 0,
		"hotkey_slot_2": 0,
		"hotkey_slot_3": 0,
		"hotkey_slot_4": 0,
		"hotkey_slot_5": 0,
		"hotkey_slot_6": 0,
		"hotkey_slot_7": 0,
		"hotkey_slot_8": 0,
		"hotkey_slot_9": 0,
		"hotkey_slot_10": 0,
		"macro_slot_1": "",
		"macro_slot_2": "",
		"macro_slot_3": "",
		"macro_slot_4": "",
		"macro_slot_5": ""
	},
	"accessibility": {
		"subtitles_enabled": true,
		"subtitle_size": 16,
		"subtitle_background": true,
		"subtitle_background_opacity": 0.5,
		"color_blind_mode": 0,
		"color_blind_strength": 1.0,
		"high_contrast_ui": false,
		"ui_outline_thickness": 1,
		"text_to_speech_enabled": false,
		"screen_reader_enabled": false,
		"haptic_feedback_enabled": true,
		"reduce_motion": false,
		"reduce_flashing": false,
		"camera_shake_intensity": 1.0,
		"head_bob_intensity": 1.0,
		"disable_screen_effects": false
	},
	"save": {
		"auto_save": true,
		"auto_save_interval": 300,
		"max_auto_saves": 10,
		"save_slot": 1,
		"backup_saves": true,
		"max_backups": 5,
		"compress_saves": true,
		"save_screenshot": true,
		"cloud_saves_enabled": false,
		"local_saves_enabled": true
	},
	"advanced": {
		"developer_mode": false,
		"allow_unsafe_scripts": false,
		"allow_mods": false,
		"mod_folder": "user://mods/",
		"script_debugging": false,
		"physics_debugging": false,
		"network_debugging": false,
		"memory_debugging": false,
		"force_opengl": false,
		"force_directx": false,
		"force_vulkan": false,
		"graphics_api": 0,
		"render_threads": 0,
		"texture_threads": 0,
		"audio_threads": 0,
		"physics_threads": 0,
		"network_threads": 0,
		"enable_jit_compilation": true,
		"enable_gpu_skinning": true,
		"enable_tessellation": false,
		"enable_compute_shaders": true,
		"enable_async_compute": true,
		"enable_multi_draw_indirect": true
	},
	"meta": {
		"last_used_version": "1.0.0",
		"last_login_time": 0,
		"total_play_time": 0,
		"first_launch": true,
		"settings_version": 1,
		"config_hash": ""
	}
}

# Current configuration
var config: Dictionary = {}

# Config file path
var config_path: String = "user://settings.cfg"
var config_file: ConfigFile

# Singleton instance
static var instance: ConfigLoader

func _ready():
	instance = self
	load_config()

static func get_instance() -> ConfigLoader:
	return instance

func load_config() -> bool:
	config_file = ConfigFile.new()
	var err = config_file.load(config_path)
	
	if err == OK:
		# Load all sections and keys
		for section in default_config.keys():
			for key in default_config[section].keys():
				var value = config_file.get_value(section, key, default_config[section][key])
				
				if not config.has(section):
					config[section] = {}
				
				config[section][key] = value
		
		print("Configuration loaded from: ", config_path)
		return true
	else:
		# Create default config
		config = default_config.duplicate(true)
		save_config()
		print("Created default configuration")
		return false

func save_config() -> bool:
	if not config_file:
		config_file = ConfigFile.new()
	
	# Save all values
	for section in config.keys():
		for key in config[section].keys():
			config_file.set_value(section, key, config[section][key])
	
	# Update meta info
	var current_time = Time.get_unix_time_from_system()
	config_file.set_value("meta", "last_used_version", "1.0.0")
	config_file.set_value("meta", "settings_version", 1)
	
	var err = config_file.save(config_path)
	if err == OK:
		print("Configuration saved to: ", config_path)
		return true
	else:
		print("Error saving configuration: ", err)
		return false

func get_value(section: String, key: String, default_value = null):
	if config.has(section) and config[section].has(key):
		return config[section][key]
	elif default_value != null:
		return default_value
	else:
		if default_config.has(section) and default_config[section].has(key):
			return default_config[section][key]
		else:
			return null

func set_value(section: String, key: String, value):
	if not config.has(section):
		config[section] = {}
	
	config[section][key] = value
	
	# Auto-save on critical changes
	if section in ["network", "player", "graphics", "audio", "controls"]:
		save_config()

func get_section(section: String) -> Dictionary:
	return config.get(section, {}).duplicate()

func set_section(section: String, values: Dictionary):
	config[section] = values.duplicate()
	save_config()

func reset_to_defaults():
	config = default_config.duplicate(true)
	save_config()
	print("Configuration reset to defaults")

func has_section(section: String) -> bool:
	return config.has(section)

func has_key(section: String, key: String) -> bool:
	return config.has(section) and config[section].has(key)

func remove_section(section: String) -> bool:
	if config.has(section):
		config.erase(section)
		save_config()
		return true
	return false

func remove_key(section: String, key: String) -> bool:
	if config.has(section) and config[section].has(key):
		config[section].erase(key)
		save_config()
		return true
	return false

# Helper methods for common operations
func get_network_config() -> Dictionary:
	return get_section("network")

func get_graphics_config() -> Dictionary:
	return get_section("graphics")

func get_audio_config() -> Dictionary:
	return get_section("audio")

func get_controls_config() -> Dictionary:
	return get_section("controls")

func get_game_config() -> Dictionary:
	return get_section("game")

# Apply graphics settings to engine
func apply_graphics_settings():
	var graphics = get_graphics_config()
	
	# Apply resolution
	if graphics.fullscreen:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
	else:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
		DisplayServer.window_set_size(Vector2i(graphics.resolution_width, graphics.resolution_height))
	
	# Apply VSync
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED if graphics.vsync else DisplayServer.VSYNC_DISABLED)
	
	# Apply FPS limit
	Engine.max_fps = graphics.frame_rate_limit if graphics.frame_rate_limit > 0 else 0
	
	# Apply brightness/contrast/gamma (would need custom shader or post-processing)
	apply_post_processing(graphics)
	
	print("Graphics settings applied")

func apply_post_processing(graphics: Dictionary):
	# This would apply to the WorldEnvironment node
	var world_env = get_node_or_null("/root/GameClient/World/WorldEnvironment")
	if world_env:
		var environment = world_env.environment
		
		# Apply ambient occlusion
		if environment.ssao_enabled != graphics.ambient_occlusion:
			environment.ssao_enabled = graphics.ambient_occlusion
		
		# Apply screen space reflections
		if environment.ssr_enabled != graphics.screen_space_reflections:
			environment.ssr_enabled = graphics.screen_space_reflections
		
		# Apply bloom
		if environment.glow_enabled != graphics.bloom:
			environment.glow_enabled = graphics.bloom
		
		# Apply fog
		if environment.fog_enabled != graphics.fog_enabled:
			environment.fog_enabled = graphics.fog_enabled

func apply_audio_settings():
	var audio = get_audio_config()
	
	# Set bus volumes
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Master"), linear_to_db(audio.master_volume))
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Music"), linear_to_db(audio.music_volume))
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Effects"), linear_to_db(audio.effects_volume))
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("UI"), linear_to_db(audio.ui_volume))
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Environment"), linear_to_db(audio.environment_volume))
	AudioServer.set_bus_volume_db(AudioServer.get_bus_index("Voice"), linear_to_db(audio.voice_volume))
	
	print("Audio settings applied")

func apply_control_settings():
	var controls = get_controls_config()
	
	# Input map would be updated here
	# For example:
	# InputMap.action_set_deadzone("move_forward", controls.mouse_sensitivity)
	
	print("Control settings applied")

# Utility functions
func linear_to_db(linear: float) -> float:
	if linear <= 0:
		return -80.0  # Minimum dB
	return 20.0 * log(linear) / log(10.0)

func db_to_linear(db: float) -> float:
	return pow(10.0, db / 20.0)

func get_key_name(keycode: int) -> String:
	var key_names = {
		32: "Space",
		65: "A", 66: "B", 67: "C", 68: "D", 69: "E", 70: "F", 71: "G", 72: "H",
		73: "I", 74: "J", 75: "K", 76: "L", 77: "M", 78: "N", 79: "O", 80: "P",
		81: "Q", 82: "R", 83: "S", 84: "T", 85: "U", 86: "V", 87: "W", 88: "X",
		89: "Y", 90: "Z",
		49: "1", 50: "2", 51: "3", 52: "4", 53: "5", 54: "6", 55: "7", 56: "8", 57: "9", 48: "0",
		16777237: "Shift",
		16777248: "F2",
		16777249: "F3",
		16777250: "F4",
		16777247: "F1",
		1: "Mouse Left",
		2: "Mouse Right",
		9: "Tab",
		13: "Enter",
		44: "Print Screen",
		192: "`",
		86: "V"
	}
	
	return key_names.get(keycode, "Key " + str(keycode))

func get_color(color_string: String) -> Color:
	return Color(color_string)

func set_color(section: String, key: String, color: Color):
	set_value(section, key, color.to_html())

func increment_play_time(delta: float):
	var current_time = get_value("meta", "total_play_time", 0)
	set_value("meta", "total_play_time", current_time + delta)
	
	# Auto-save every 5 minutes
	if int(current_time + delta) % 300 < int(delta):
		save_config()

func update_last_login():
	var current_time = Time.get_unix_time_from_system()
	set_value("meta", "last_login_time", current_time)
	save_config()

# Check if first launch
func is_first_launch() -> bool:
	return get_value("meta", "first_launch", true)

func complete_first_launch():
	set_value("meta", "first_launch", false)
	save_config()

# Get server connection info
func get_server_info() -> Dictionary:
	return {
		"address": get_value("network", "server_address", "127.0.0.1"),
		"port": get_value("network", "server_port", 8080),
		"protocol": get_value("network", "protocol_type", 0),
		"use_ssl": get_value("network", "use_ssl", false)
	}

# Get player info
func get_player_info() -> Dictionary:
	return {
		"username": get_value("player", "username", "Player"),
		"character_name": get_value("player", "character_name", ""),
		"character_class": get_value("player", "character_class", "warrior"),
		"character_level": get_value("player", "character_level", 1),
		"character_race": get_value("player", "character_race", "human")
	}

# Get world generation settings
func get_world_settings() -> Dictionary:
	return {
		"render_distance": get_value("graphics", "render_distance", 3),
		"chunk_size": get_value("graphics", "chunk_size", 16),
		"chunk_height": get_value("graphics", "chunk_height", 64),
		"lod_distances": get_value("graphics", "lod_distances", [20, 40, 80])
	}

func _exit_tree():
	# Save config on exit
	save_config()