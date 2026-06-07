extends Node3D

class_name WeaponManager

enum WeaponType { GUN, RIFLE, SHOTGUN, SNIPER, LAUNCHER }

var current_weapon: int = WeaponType.GUN
var weapons: Dictionary = {}
var ammo_scene: PackedScene

func _ready():
	initialize_weapons()

func initialize_weapons():
	weapons[WeaponType.GUN] = {
		"name": "Pistol",
		"damage": 25,
		"ammo_speed": 30.0,
		"ammo_range": 50.0,
		"cooldown": 0.3,
		"magazine_size": 12,
		"reload_time": 1.5,
		"spread": 0.02
	}
	weapons[WeaponType.RIFLE] = {
		"name": "Rifle",
		"damage": 35,
		"ammo_speed": 40.0,
		"ammo_range": 60.0,
		"cooldown": 0.15,
		"magazine_size": 30,
		"reload_time": 2.0,
		"spread": 0.01
	}
	weapons[WeaponType.SHOTGUN] = {
		"name": "Shotgun",
		"damage": 15,
		"ammo_speed": 25.0,
		"ammo_range": 30.0,
		"cooldown": 0.8,
		"magazine_size": 8,
		"reload_time": 2.5,
		"spread": 0.1,
		"pellets": 6
	}
	weapons[WeaponType.SNIPER] = {
		"name": "Sniper",
		"damage": 100,
		"ammo_speed": 60.0,
		"ammo_range": 100.0,
		"cooldown": 1.5,
		"magazine_size": 5,
		"reload_time": 3.0,
		"spread": 0.005
	}
	weapons[WeaponType.LAUNCHER] = {
		"name": "Launcher",
		"damage": 150,
		"ammo_speed": 20.0,
		"ammo_range": 40.0,
		"cooldown": 2.0,
		"magazine_size": 3,
		"reload_time": 3.5,
		"spread": 0.0,
		"explosion_radius": 5.0
	}

func get_weapon_data(weapon_type: int) -> Dictionary:
	return weapons.get(weapon_type, {})

func switch_weapon(weapon_type: int):
	if weapons.has(weapon_type):
		current_weapon = weapon_type

func get_current_weapon_data() -> Dictionary:
	return get_weapon_data(current_weapon)

func create_ammo(origin: Vector3, direction: Vector3) -> Node3D:
	var data = get_current_weapon_data()
	var ammo = Node3D.new()
	ammo.set_meta("damage", data.get("damage", 25))
	ammo.set_meta("speed", data.get("ammo_speed", 30.0))
	ammo.set_meta("range", data.get("ammo_range", 50.0))
	ammo.set_meta("traveled", 0.0)
	ammo.set_meta("active", true)
	ammo.set_meta("direction", direction.normalized())
	ammo.global_transform.origin = origin
	var mesh = SphereMesh.new()
	mesh.radius = 0.08
	mesh.height = 0.16
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(1.0, 0.8, 0.0)
	mat.emission_enabled = true
	mat.emission = Color(1.0, 0.6, 0.0)
	mesh.material = mat
	var mesh_inst = MeshInstance3D.new()
	mesh_inst.mesh = mesh
	ammo.add_child(mesh_inst)
	return ammo

func update_ammo(ammo: Node3D, delta: float):
	if not ammo.get_meta("active", false):
		return
	var speed = ammo.get_meta("speed", 30.0)
	var dir = ammo.get_meta("direction", Vector3.FORWARD)
	var distance = speed * delta
	ammo.global_transform.origin += dir * distance
	var traveled = ammo.get_meta("traveled", 0.0) + distance
	ammo.set_meta("traveled", traveled)
	if traveled >= ammo.get_meta("range", 50.0):
		ammo.set_meta("active", false)
		ammo.queue_free()

func get_weapon_model() -> Node3D:
	var node = Node3D.new()
	var data = get_current_weapon_data()
	match current_weapon:
		WeaponType.GUN:
			return _create_gun_model()
		WeaponType.RIFLE:
			return _create_rifle_model()
		WeaponType.SHOTGUN:
			return _create_shotgun_model()
		WeaponType.SNIPER:
			return _create_sniper_model()
		WeaponType.LAUNCHER:
			return _create_launcher_model()
		_:
			return _create_gun_model()
	return node

func _create_gun_model() -> Node3D:
	var node = Node3D.new()
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(0.2, 0.2, 0.2)
	var barrel = BoxMesh.new()
	barrel.size = Vector3(0.08, 0.08, 0.4)
	barrel.material = mat
	var barrel_inst = MeshInstance3D.new()
	barrel_inst.mesh = barrel
	barrel_inst.position = Vector3(0, 0, -0.2)
	node.add_child(barrel_inst)
	var grip = BoxMesh.new()
	grip.size = Vector3(0.1, 0.2, 0.1)
	grip.material = mat
	var grip_inst = MeshInstance3D.new()
	grip_inst.mesh = grip
	grip_inst.position = Vector3(0, -0.15, -0.05)
	grip_inst.rotation.x = -0.3
	node.add_child(grip_inst)
	return node

func _create_rifle_model() -> Node3D:
	var node = Node3D.new()
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(0.3, 0.25, 0.2)
	var barrel = BoxMesh.new()
	barrel.size = Vector3(0.06, 0.06, 0.6)
	barrel.material = mat
	var barrel_inst = MeshInstance3D.new()
	barrel_inst.mesh = barrel
	barrel_inst.position = Vector3(0, 0, -0.3)
	node.add_child(barrel_inst)
	var stock = BoxMesh.new()
	stock.size = Vector3(0.1, 0.15, 0.2)
	stock.material = mat
	var stock_inst = MeshInstance3D.new()
	stock_inst.mesh = stock
	stock_inst.position = Vector3(0, -0.05, 0.2)
	node.add_child(stock_inst)
	return node

func _create_shotgun_model() -> Node3D:
	var node = Node3D.new()
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(0.4, 0.3, 0.15)
	var barrel = BoxMesh.new()
	barrel.size = Vector3(0.1, 0.1, 0.5)
	barrel.material = mat
	var barrel_inst = MeshInstance3D.new()
	barrel_inst.mesh = barrel
	barrel_inst.position = Vector3(0, 0, -0.25)
	node.add_child(barrel_inst)
	return node

func _create_sniper_model() -> Node3D:
	var node = Node3D.new()
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(0.15, 0.15, 0.15)
	var barrel = BoxMesh.new()
	barrel.size = Vector3(0.05, 0.05, 0.8)
	barrel.material = mat
	var barrel_inst = MeshInstance3D.new()
	barrel_inst.mesh = barrel
	barrel_inst.position = Vector3(0, 0, -0.4)
	node.add_child(barrel_inst)
	var scope = CylinderMesh.new()
	scope.top_radius = 0.04
	scope.bottom_radius = 0.04
	scope.height = 0.2
	scope.material = mat
	var scope_inst = MeshInstance3D.new()
	scope_inst.mesh = scope
	scope_inst.position = Vector3(0, 0.1, -0.1)
	scope_inst.rotation.x = PI / 2
	node.add_child(scope_inst)
	return node

func _create_launcher_model() -> Node3D:
	var node = Node3D.new()
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(0.4, 0.4, 0.1)
	var barrel = CylinderMesh.new()
	barrel.top_radius = 0.08
	barrel.bottom_radius = 0.08
	barrel.height = 0.5
	barrel.material = mat
	var barrel_inst = MeshInstance3D.new()
	barrel_inst.mesh = barrel
	barrel_inst.position = Vector3(0, 0, -0.25)
	barrel_inst.rotation.x = PI / 2
	node.add_child(barrel_inst)
	return node
