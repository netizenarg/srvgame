extends Node3D

class_name ChunkLoader

func load_chunk(chunk_x: int, chunk_z: int, chunk_size: int) -> Dictionary:
	# This would load chunk data from disk or network
	# For now, return empty data
	return {
		"chunk_x": chunk_x,
		"chunk_z": chunk_z,
		"heightmap": [],
		"biomes": []
	}

class_name GameWorld

# World parameters
@export var chunk_size: int = 16
@export var chunk_height: int = 64
@export var render_distance: int = 3
@export var lod_distances: Array = [20.0, 40.0, 80.0]

# Materials
@onready var terrain_material = preload("res://materials/terrain.tres")
@onready var water_material = preload("res://materials/water.tres")

# World state
var chunks: Dictionary = {}
var active_chunks: Array = []
var chunk_loader: ChunkLoader
var terrain_generator: TerrainGenerator

# Performance
var max_chunks_per_frame: int = 1
var wireframe_mode: bool = false
var show_debug: bool = false

func _ready():
	chunk_loader = ChunkLoader.new()
	terrain_generator = TerrainGenerator.new()
	
	# Setup materials
	setup_materials()

func initialize(size: int, height: int, distance: int, lod_array: Array):
	chunk_size = size
	chunk_height = height
	render_distance = distance
	lod_distances = lod_array
	
	print("World initialized: %dx%d chunks, render distance: %d" % [size, size, distance])

func create_chunk(chunk_x: int, chunk_z: int, chunk_data: Dictionary = {}):
	var chunk_key = Vector2(chunk_x, chunk_z)
	
	if chunks.has(chunk_key):
		return chunks[chunk_key]
	
	# Create new chunk
	var chunk = Chunk.new()
	chunk.initialize(chunk_x, chunk_z, chunk_size, chunk_height)
	chunk.name = "Chunk_%d_%d" % [chunk_x, chunk_z]
	chunk.position = Vector3(chunk_x * chunk_size, 0, chunk_z * chunk_size)
	add_child(chunk)
	
	# Generate or load terrain
	if chunk_data.is_empty():
		terrain_generator.generate_terrain(chunk)
	else:
		chunk.load_from_data(chunk_data)
	
	# Setup LOD
	chunk.setup_lod(lod_distances)
	
	# Apply materials
	chunk.apply_material(terrain_material)
	
	chunks[chunk_key] = chunk
	active_chunks.append(chunk_key)
	
	return chunk

func update_lod(viewer_position: Vector3):
	for chunk_key in active_chunks:
		var chunk = chunks[chunk_key]
		if chunk:
			var distance = viewer_position.distance_to(chunk.global_transform.origin + Vector3(chunk_size/2, 0, chunk_size/2))
			chunk.update_lod(distance)

func clear_chunks():
	for chunk_key in chunks.keys():
		var chunk = chunks[chunk_key]
		if chunk:
			chunk.queue_free()
	
	chunks.clear()
	active_chunks.clear()

func get_height_at(x: float, z: float) -> float:
	var chunk_x = int(x / chunk_size)
	var chunk_z = int(z / chunk_size)
	var chunk_key = Vector2(chunk_x, chunk_z)
	
	if chunks.has(chunk_key):
		var chunk = chunks[chunk_key]
		var local_x = int(x - chunk_x * chunk_size)
		var local_z = int(z - chunk_z * chunk_size)
		return chunk.get_height(local_x, local_z)
	
	return 0.0

func add_water_chunk(chunk_x: int, chunk_z: int, water_level: float = 10.0):
	var chunk_key = Vector2(chunk_x, chunk_z)
	
	var water_mesh = MeshInstance3D.new()
	var plane_mesh = PlaneMesh.new()
	plane_mesh.size = Vector2(chunk_size, chunk_size)
	
	water_mesh.mesh = plane_mesh
	water_mesh.material_override = water_material
	water_mesh.position = Vector3(chunk_x * chunk_size + chunk_size/2, water_level, chunk_z * chunk_size + chunk_size/2)
	
	add_child(water_mesh)
	return water_mesh

func toggle_wireframe():
	wireframe_mode = !wireframe_mode
	for chunk in chunks.values():
		chunk.set_wireframe(wireframe_mode)

func setup_materials():
	# Setup terrain material with noise
	if terrain_material:
		var noise = FastNoiseLite.new()
		noise.noise_type = FastNoiseLite.TYPE_SIMPLEX
		noise.frequency = 0.05
		terrain_material.set_shader_parameter("noise_texture", noise)

func get_nearby_chunks(position: Vector3, radius: float) -> Array:
	var nearby = []
	var chunk_x = int(position.x / chunk_size)
	var chunk_z = int(position.z / chunk_size)
	
	var radius_chunks = int(radius / chunk_size) + 1
	
	for x in range(chunk_x - radius_chunks, chunk_x + radius_chunks + 1):
		for z in range(chunk_z - radius_chunks, chunk_z + radius_chunks + 1):
			var chunk_key = Vector2(x, z)
			if chunks.has(chunk_key):
				nearby.append(chunks[chunk_key])
	
	return nearby

func _exit_tree():
	clear_chunks()
