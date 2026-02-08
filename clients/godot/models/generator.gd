extends Node

class_name ModelGenerator

# Model types
enum ModelType {
	PLAYER,
	NPC,
	MOB,
	FAMILIAR,
	TREE,
	STONE,
	WATER,
	RIVER,
	HOUSE
}

# Materials
@onready var player_material = preload("res://materials/player.tres")
@onready var npc_material = preload("res://materials/npc.tres")
@onready var mob_material = preload("res://materials/mob.tres")
@onready var tree_material = preload("res://materials/tree.tres")
@onready var stone_material = preload("res://materials/stone.tres")
@onready var water_material = preload("res://materials/water.tres")

# Model presets
var model_presets: Dictionary = {}

func _ready():
	initialize_presets()

func initialize_presets():
	# Player model preset
	model_presets[ModelType.PLAYER] = {
		"vertices": [
			Vector3(0, 1, 0),    # Head top
			Vector3(-0.3, 0.7, 0),  # Shoulder left
			Vector3(0.3, 0.7, 0),   # Shoulder right
			Vector3(-0.2, 0, 0),    # Foot left
			Vector3(0.2, 0, 0),     # Foot right
			Vector3(0, 0.8, -0.3)   # Chest front
		],
		"indices": [0,1,2, 1,3,2, 2,3,4, 0,5,1, 0,5,2],
		"material": player_material,
		"scale": Vector3(0.5, 1.0, 0.3)
	}
	
	# Tree model preset
	model_presets[ModelType.TREE] = {
		"vertices": [
			Vector3(0, 3, 0),      # Top
			Vector3(-1, 2, -1),    # Branch 1
			Vector3(1, 2, -1),     # Branch 2
			Vector3(-1, 2, 1),     # Branch 3
			Vector3(1, 2, 1),      # Branch 4
			Vector3(0, 0, 0),      # Base
			Vector3(-0.3, 0, 0),   # Trunk base 1
			Vector3(0.3, 0, 0)     # Trunk base 2
		],
		"indices": [0,1,2, 0,2,3, 0,3,4, 0,4,1, 5,6,7],
		"material": tree_material,
		"scale": Vector3(1.0, 1.0, 1.0)
	}
	
	# Stone model preset
	model_presets[ModelType.STONE] = {
		"vertices": [
			Vector3(-0.5, 0, -0.5),
			Vector3(0.5, 0, -0.5),
			Vector3(-0.5, 0.5, -0.5),
			Vector3(0.5, 0.5, -0.5),
			Vector3(-0.5, 0, 0.5),
			Vector3(0.5, 0, 0.5),
			Vector3(-0.5, 0.5, 0.5),
			Vector3(0.5, 0.5, 0.5)
		],
		"indices": [0,1,2, 1,3,2, 4,5,6, 5,7,6, 0,4,2, 4,6,2, 1,5,3, 5,7,3, 2,3,6, 3,7,6, 0,1,4, 1,5,4],
		"material": stone_material,
		"scale": Vector3(0.8, 0.4, 0.8)
	}
	
	# Mob model preset (simple creature)
	model_presets[ModelType.MOB] = {
		"vertices": [
			Vector3(0, 0.6, 0),     # Body top
			Vector3(-0.4, 0.3, 0),  # Left side
			Vector3(0.4, 0.3, 0),   # Right side
			Vector3(0, 0, 0),       # Base
			Vector3(-0.2, 0.3, 0.3), # Front left
			Vector3(0.2, 0.3, 0.3),  # Front right
			Vector3(0, 0.7, 0.1)    # Head
		],
		"indices": [0,1,2, 1,3,2, 0,4,5, 4,3,5, 6,4,5, 6,1,2],
		"material": mob_material,
		"scale": Vector3(0.6, 0.6, 0.8)
	}
	
	# NPC model preset
	model_presets[ModelType.NPC] = {
		"vertices": [
			Vector3(0, 1, 0),       # Head
			Vector3(-0.25, 0.6, 0), # Shoulder L
			Vector3(0.25, 0.6, 0),  # Shoulder R
			Vector3(-0.15, 0, 0),   # Foot L
			Vector3(0.15, 0, 0),    # Foot R
			Vector3(0, 0.7, -0.2),  # Chest
			Vector3(0, 1.1, -0.1)   # Hat/Headgear
		],
		"indices": [0,1,2, 1,3,2, 2,3,4, 0,5,1, 0,5,2, 0,6,5],
		"material": npc_material,
		"scale": Vector3(0.4, 0.9, 0.25)
	}

func generate_model(model_type_str: String, data: Dictionary = {}) -> MeshInstance3D:
	var model_type = get_model_type_from_string(model_type_str)
	
	if model_presets.has(model_type):
		return generate_from_preset(model_type, data)
	else:
		return generate_procedural_model(model_type_str, data)

func generate_from_preset(model_type: int, data: Dictionary) -> MeshInstance3D:
	var preset = model_presets[model_type]
	
	var mesh_instance = MeshInstance3D.new()
	var array_mesh = ArrayMesh.new()
	
	var vertices = PackedVector3Array()
	var normals = PackedVector3Array()
	var indices = PackedInt32Array()
	
	# Scale vertices
	for vertex in preset.vertices:
		vertices.append(vertex * preset.scale)
	
	# Generate normals (simplified)
	for i in range(vertices.size()):
		normals.append(Vector3.UP)
	
	indices = preset.indices
	
	# Create surface
	var arrays = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_INDEX] = indices
	
	array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh_instance.mesh = array_mesh
	
	# Apply material
	if preset.material:
		mesh_instance.material_override = preset.material
	
	# Apply color from data if available
	if data.has("color"):
		var material = mesh_instance.material_override.duplicate()
		material.albedo_color = Color(data.color)
		mesh_instance.material_override = material
	
	return mesh_instance

func generate_procedural_model(model_type: String, data: Dictionary) -> MeshInstance3D:
	# Procedural model generation based on type
	match model_type:
		"familiar":
			return generate_familiar_model(data)
		"river":
			return generate_river_model(data)
		"house":
			return generate_house_model(data)
		"water":
			return generate_water_model(data)
		_:
			return generate_basic_model(model_type, data)

func generate_familiar_model(data: Dictionary) -> MeshInstance3D:
	var mesh_instance = MeshInstance3D.new()
	
	# Create a simple familiar (floating crystal/creature)
	var csg = CSGMesh3D.new()
	var sphere_mesh = SphereMesh.new()
	sphere_mesh.radius = 0.3
	sphere_mesh.height = 0.6
	
	csg.mesh = sphere_mesh
	
	var material = StandardMaterial3D.new()
	material.albedo_color = Color(0.8, 0.3, 0.8, 0.7)
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.emission_enabled = true
	material.emission = Color(0.5, 0.1, 0.5, 0.3)
	
	csg.material = material
	mesh_instance.add_child(csg)
	
	# Add floating animation
	var tween = create_tween()
	tween.set_loops()
	tween.tween_property(mesh_instance, "position:y", 0.2, 1.0)
	tween.tween_property(mesh_instance, "position:y", 0.0, 1.0)
	
	return mesh_instance

func generate_river_model(data: Dictionary) -> MeshInstance3D:
	var mesh_instance = MeshInstance3D.new()
	
	var plane_mesh = PlaneMesh.new()
	if data.has("width"):
		plane_mesh.size = Vector2(data.width, 10.0)
	else:
		plane_mesh.size = Vector2(3.0, 10.0)
	
	plane_mesh.subdivide_width = 10
	plane_mesh.subdivide_depth = 10
	
	mesh_instance.mesh = plane_mesh
	
	var material = ShaderMaterial.new()
	var shader = preload("res://shaders/water.shader")
	material.shader = shader
	
	if data.has("color"):
		material.set_shader_parameter("water_color", Color(data.color))
	else:
		material.set_shader_parameter("water_color", Color(0.2, 0.4, 0.8, 0.6))
	
	mesh_instance.material_override = material
	
	return mesh_instance

func generate_house_model(data: Dictionary) -> MeshInstance3D:
	var mesh_instance = MeshInstance3D.new()
	
	# Create simple house from CSG nodes
	var house_base = CSGBox3D.new()
	house_base.width = 2.0
	house_base.height = 1.5
	house_base.depth = 2.0
	
	var house_roof = CSGPolygon3D.new()
	house_roof.polygon = PackedVector2Array([
		Vector2(-1.2, 0),
		Vector2(1.2, 0),
		Vector2(0, 1.5)
	])
	house_roof.mode = CSGPolygon3D.MODE_DEPTH
	house_roof.depth = 2.2
	house_roof.position = Vector3(0, 1.5, 0)
	house_roof.rotation_degrees = Vector3(0, 90, 0)
	
	var material = StandardMaterial3D.new()
	material.albedo_color = Color(0.8, 0.6, 0.4)
	house_base.material = material
	
	var roof_material = StandardMaterial3D.new()
	roof_material.albedo_color = Color(0.5, 0.2, 0.1)
	house_roof.material = roof_material
	
	mesh_instance.add_child(house_base)
	mesh_instance.add_child(house_roof)
	
	return mesh_instance

func generate_water_model(data: Dictionary) -> MeshInstance3D:
	var mesh_instance = MeshInstance3D.new()
	
	var plane_mesh = PlaneMesh.new()
	if data.has("size"):
		plane_mesh.size = Vector2(data.size.x, data.size.y)
	else:
		plane_mesh.size = Vector2(10.0, 10.0)
	
	mesh_instance.mesh = plane_mesh
	mesh_instance.material_override = water_material
	
	return mesh_instance

func generate_basic_model(model_type: String, data: Dictionary) -> MeshInstance3D:
	# Generate a basic placeholder model
	var mesh_instance = MeshInstance3D.new()
	
	var cube_mesh = BoxMesh.new()
	cube_mesh.size = Vector3(1.0, 1.0, 1.0)
	
	mesh_instance.mesh = cube_mesh
	
	var material = StandardMaterial3D.new()
	material.albedo_color = Color(
		randf_range(0.2, 0.8),
		randf_range(0.2, 0.8),
		randf_range(0.2, 0.8)
	)
	
	mesh_instance.material_override = material
	mesh_instance.name = model_type
	
	return mesh_instance

func get_model_type_from_string(type_str: String) -> int:
	match type_str.to_lower():
		"player":
			return ModelType.PLAYER
		"npc", "villager", "merchant":
			return ModelType.NPC
		"mob", "enemy", "creature":
			return ModelType.MOB
		"familiar", "pet", "companion":
			return ModelType.FAMILIAR
		"tree", "pine", "oak":
			return ModelType.TREE
		"stone", "rock", "boulder":
			return ModelType.STONE
		"water", "lake", "ocean":
			return ModelType.WATER
		"river", "stream":
			return ModelType.RIVER
		"house", "building":
			return ModelType.HOUSE
		_:
			return ModelType.PLAYER

func generate_low_poly_tree(variation: int = 0) -> MeshInstance3D:
	return generate_from_preset(ModelType.TREE, {"variation": variation})

func generate_low_poly_rock(variation: int = 0) -> MeshInstance3D:
	return generate_from_preset(ModelType.STONE, {"variation": variation})

func generate_player_model(skin_color: Color = Color(0.8, 0.6, 0.4), 
                          hair_color: Color = Color(0.3, 0.2, 0.1)) -> MeshInstance3D:
	var mesh_instance = generate_from_preset(ModelType.PLAYER, {})
	
	# Customize player appearance
	var material = mesh_instance.material_override.duplicate()
	material.albedo_color = skin_color
	mesh_instance.material_override = material
	
	return mesh_instance

func generate_npc_model(npc_type: String, color: Color = Color.WHITE) -> MeshInstance3D:
	var mesh_instance = generate_from_preset(ModelType.NPC, {"color": color})
	
	# Add NPC type specific features
	match npc_type:
		"merchant":
			var hat = generate_simple_hat()
			mesh_instance.add_child(hat)
		"guard":
			var weapon = generate_simple_weapon()
			mesh_instance.add_child(weapon)
	
	return mesh_instance

func generate_simple_hat() -> MeshInstance3D:
	var hat = MeshInstance3D.new()
	var cylinder = CylinderMesh.new()
	cylinder.top_radius = 0.4
	cylinder.bottom_radius = 0.5
	cylinder.height = 0.2
	
	hat.mesh = cylinder
	hat.position = Vector3(0, 0.6, 0)
	
	var material = StandardMaterial3D.new()
	material.albedo_color = Color(0.2, 0.2, 0.2)
	hat.material_override = material
	
	return hat

func generate_simple_weapon() -> MeshInstance3D:
	var weapon = MeshInstance3D.new()
	
	var box = BoxMesh.new()
	box.size = Vector3(0.1, 1.0, 0.1)
	
	weapon.mesh = box
	weapon.position = Vector3(0.3, 0.3, 0)
	
	var material = StandardMaterial3D.new()
	material.albedo_color = Color(0.5, 0.5, 0.5)
	weapon.material_override = material
	
	return weapon