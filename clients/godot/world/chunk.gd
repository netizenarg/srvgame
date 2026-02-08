extends Node3D

class_name Chunk

# Chunk properties
var chunk_x: int = 0
var chunk_z: int = 0
var chunk_size: int = 16
var chunk_height: int = 64
var heightmap: Array = []
var biomes: Array = []
var lod_level: int = 0

# Mesh components
var mesh_instance: MeshInstance3D
var collision_shape: CollisionShape3D
var nav_region: NavigationRegion3D

# LOD meshes
var lod_meshes: Array = []
var current_lod: int = 0

func initialize(x: int, z: int, size: int, height: int):
	chunk_x = x
	chunk_z = z
	chunk_size = size
	chunk_height = height
	
	# Initialize arrays
	heightmap.resize(size * size)
	biomes.resize(size * size)
	
	# Create mesh instance
	mesh_instance = MeshInstance3D.new()
	add_child(mesh_instance)

func generate(height_data: Array = [], biome_data: Array = []):
	if not height_data.is_empty():
		heightmap = height_data.duplicate()
	else:
		generate_heightmap()
	
	if not biome_data.is_empty():
		biomes = biome_data.duplicate()
	else:
		generate_biomes()
	
	generate_mesh()

func generate_heightmap():
	var noise = FastNoiseLite.new()
	noise.noise_type = FastNoiseLite.TYPE_PERLIN
	noise.frequency = 0.05
	noise.fractal_octaves = 4
	
	for x in range(chunk_size):
		for z in range(chunk_size):
			var world_x = chunk_x * chunk_size + x
			var world_z = chunk_z * chunk_size + z
			
			var height = noise.get_noise_2d(world_x, world_z) * 20 + 10
			
			# Add some hills/mountains
			var hill_noise = FastNoiseLite.new()
			hill_noise.noise_type = FastNoiseLite.TYPE_CELLULAR
			hill_noise.frequency = 0.02
			height += hill_noise.get_noise_2d(world_x, world_z) * 30
			
			heightmap[z * chunk_size + x] = max(0, height)

func generate_biomes():
	var temperature_noise = FastNoiseLite.new()
	temperature_noise.noise_type = FastNoiseLite.TYPE_PERLIN
	temperature_noise.frequency = 0.01
	
	var moisture_noise = FastNoiseLite.new()
	moisture_noise.noise_type = FastNoiseLite.TYPE_PERLIN
	moisture_noise.frequency = 0.01
	
	for x in range(chunk_size):
		for z in range(chunk_size):
			var world_x = chunk_x * chunk_size + x
			var world_z = chunk_z * chunk_size + z
			
			var temperature = temperature_noise.get_noise_2d(world_x, world_z)
			var moisture = moisture_noise.get_noise_2d(world_x, world_z)
			
			var biome = determine_biome(temperature, moisture, heightmap[z * chunk_size + x])
			biomes[z * chunk_size + x] = biome

func determine_biome(temperature: float, moisture: float, height: float) -> int:
	# Simple biome determination
	if height < 5:
		return 0  # Water
	elif height < 10:
		if moisture > 0.5:
			return 1  # Beach/Swamp
		else:
			return 2  # Desert
	elif height < 30:
		if temperature > 0.3:
			if moisture > 0.6:
				return 3  # Forest
			else:
				return 4  # Plains
		else:
			return 5  # Tundra
	else:
		return 6  # Mountain

func generate_mesh():
	var surface_tool = SurfaceTool.new()
	surface_tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	for x in range(chunk_size - 1):
		for z in range(chunk_size - 1):
			# Create quad from two triangles
			var v1 = Vector3(x, get_height(x, z), z)
			var v2 = Vector3(x + 1, get_height(x + 1, z), z)
			var v3 = Vector3(x, get_height(x, z + 1), z + 1)
			var v4 = Vector3(x + 1, get_height(x + 1, z + 1), z + 1)
			
			# Calculate normals
			var normal1 = (v2 - v1).cross(v3 - v1).normalized()
			var normal2 = (v3 - v2).cross(v4 - v2).normalized()
			
			# First triangle
			surface_tool.set_normal(normal1)
			surface_tool.add_vertex(v1)
			surface_tool.add_vertex(v2)
			surface_tool.add_vertex(v3)
			
			# Second triangle
			surface_tool.set_normal(normal2)
			surface_tool.add_vertex(v2)
			surface_tool.add_vertex(v4)
			surface_tool.add_vertex(v3)
	
	surface_tool.generate_normals()
	var mesh = surface_tool.commit()
	mesh_instance.mesh = mesh

func get_height(x: int, z: int) -> float:
	if x >= 0 and x < chunk_size and z >= 0 and z < chunk_size:
		return heightmap[z * chunk_size + x]
	return 0.0

func setup_lod(distances: Array):
	lod_meshes.resize(distances.size())
	lod_meshes[0] = mesh_instance.mesh
	
	# Generate lower LOD meshes
	for i in range(1, distances.size()):
		lod_meshes[i] = generate_lod_mesh(i + 1)

func generate_lod_mesh(step: int) -> Mesh:
	var surface_tool = SurfaceTool.new()
	surface_tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	
	for x in range(0, chunk_size - step, step):
		for z in range(0, chunk_size - step, step):
			# Create quad with lower resolution
			var v1 = Vector3(x, get_height(x, z), z)
			var v2 = Vector3(x + step, get_height(x + step, z), z)
			var v3 = Vector3(x, get_height(x, z + step), z + step)
			var v4 = Vector3(x + step, get_height(x + step, z + step), z + step)
			
			var normal1 = (v2 - v1).cross(v3 - v1).normalized()
			var normal2 = (v3 - v2).cross(v4 - v2).normalized()
			
			surface_tool.set_normal(normal1)
			surface_tool.add_vertex(v1)
			surface_tool.add_vertex(v2)
			surface_tool.add_vertex(v3)
			
			surface_tool.set_normal(normal2)
			surface_tool.add_vertex(v2)
			surface_tool.add_vertex(v4)
			surface_tool.add_vertex(v3)
	
	surface_tool.generate_normals()
	return surface_tool.commit()

func update_lod(distance: float):
	if lod_meshes.is_empty():
		return
	
	var new_lod = 0
	for i in range(lod_meshes.size() - 1, -1, -1):
		if distance > 20 * (i + 1):  # Simplified distance calculation
			new_lod = i
			break
	
	if new_lod != current_lod and new_lod < lod_meshes.size():
		current_lod = new_lod
		mesh_instance.mesh = lod_meshes[new_lod]

func load_from_data(chunk_data: Dictionary):
	if chunk_data.has("heightmap"):
		heightmap = chunk_data.heightmap
	if chunk_data.has("biomes"):
		biomes = chunk_data.biomes
	
	generate_mesh()

func apply_material(material: Material):
	if mesh_instance:
		mesh_instance.material_override = material

func set_wireframe(enabled: bool):
	if mesh_instance:
		if enabled:
			var wireframe_material = StandardMaterial3D.new()
			wireframe_material.albedo_color = Color(0.2, 0.8, 0.2, 0.3)
			wireframe_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			mesh_instance.material_override = wireframe_material
		else:
			mesh_instance.material_override = null

func _exit_tree():
	if mesh_instance:
		mesh_instance.queue_free()