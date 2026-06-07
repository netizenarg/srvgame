extends Node3D

class_name GameWorld

@export var chunk_size: int = 32
@export var chunk_height: int = 64
@export var render_distance: int = 3
@export var lod_distances: Array = [20.0, 40.0, 80.0]

var terrain_material: ShaderMaterial
var water_material: StandardMaterial3D
var chunks: Dictionary = {}
var active_chunks: Array = []
var terrain_generator: TerrainGenerator
var max_chunks_per_frame: int = 1
var wireframe_mode: bool = false

func _ready():
	terrain_generator = TerrainGenerator.new()
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
	var chunk = Chunk.new()
	chunk.initialize(chunk_x, chunk_z, chunk_size, chunk_height)
	chunk.name = "Chunk_%d_%d" % [chunk_x, chunk_z]
	chunk.position = Vector3.ZERO
	add_child(chunk)
	if chunk_data.is_empty():
		chunk.generate()
	else:
		chunk.load_from_data(chunk_data)
	chunk.apply_material(terrain_material)
	chunk.spawn_entities()
	chunks[chunk_key] = chunk
	active_chunks.append(chunk_key)
	return chunk

func update_lod(viewer_position: Vector3):
	for chunk_key in active_chunks:
		var chunk = chunks[chunk_key]
		if chunk:
			var chunk_center = Vector3(
				chunk.chunk_x * chunk_size + chunk_size / 2.0,
				0,
				chunk.chunk_z * chunk_size + chunk_size / 2.0
			)
			var distance = viewer_position.distance_to(chunk_center)
			chunk.update_lod(distance)

func clear_chunks():
	for chunk_key in chunks.keys():
		var chunk = chunks[chunk_key]
		if chunk:
			chunk.queue_free()
	chunks.clear()
	active_chunks.clear()

func get_height_at(x: float, z: float) -> float:
	var chunk_x = int(floor(x / chunk_size))
	var chunk_z = int(floor(z / chunk_size))
	var chunk_key = Vector2(chunk_x, chunk_z)
	if chunks.has(chunk_key):
		var chunk = chunks[chunk_key]
		var local_x = int(x - chunk_x * chunk_size)
		var local_z = int(z - chunk_z * chunk_size)
		return chunk.get_height_at(local_x, local_z)
	return _pyf3d_height(x, z)

func _pyf3d_height(wx: float, wz: float) -> float:
	return (sin(wx * 0.1) * cos(wz * 0.1) +
			0.3 * sin(wx * 0.3 + 1.2) +
			0.3 * cos(wz * 0.3 + 2.4) +
			0.2 * sin((wx * 0.6 + wz * 0.4) * 0.8)) * 2.0 + 0.5

func setup_materials():
	var shader = load("res://shaders/terrain.gdshader")
	if shader:
		terrain_material = ShaderMaterial.new()
		terrain_material.shader = shader
	else:
		terrain_material = null
	water_material = StandardMaterial3D.new()
	water_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	water_material.albedo_color = Color(0.2, 0.4, 0.8, 0.7)
	water_material.metallic = 0.3
	water_material.roughness = 0.1

func get_nearby_chunks(pos: Vector3, radius: float) -> Array:
	var nearby = []
	var chunk_x = int(floor(pos.x / chunk_size))
	var chunk_z = int(floor(pos.z / chunk_size))
	var radius_chunks = int(radius / chunk_size) + 1
	for x in range(chunk_x - radius_chunks, chunk_x + radius_chunks + 1):
		for z in range(chunk_z - radius_chunks, chunk_z + radius_chunks + 1):
			var chunk_key = Vector2(x, z)
			if chunks.has(chunk_key):
				nearby.append(chunks[chunk_key])
	return nearby

func toggle_wireframe():
	wireframe_mode = !wireframe_mode
	for chunk in chunks.values():
		chunk.set_wireframe(wireframe_mode)

func _exit_tree():
	clear_chunks()
