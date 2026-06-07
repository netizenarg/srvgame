extends Node

class_name TerrainGenerator

# Noise generators
var height_noise: FastNoiseLite
var temperature_noise: FastNoiseLite
var moisture_noise: FastNoiseLite
var biome_noise: FastNoiseLite

# Terrain parameters
var terrain_scale: float = 0.05
var height_multiplier: float = 20.0
var base_height: float = 10.0

# Biome definitions
enum BiomeType {
	OCEAN,
	BEACH,
	DESERT,
	GRASSLAND,
	FOREST,
	JUNGLE,
	SAVANNA,
	SWAMP,
	TAIGA,
	TUNDRA,
	MOUNTAIN,
	SNOW
}

# Biome colors
var biome_colors: Dictionary = {
	BiomeType.OCEAN: Color(0.0, 0.2, 0.6),
	BiomeType.BEACH: Color(0.9, 0.8, 0.5),
	BiomeType.DESERT: Color(0.9, 0.8, 0.4),
	BiomeType.GRASSLAND: Color(0.4, 0.7, 0.2),
	BiomeType.FOREST: Color(0.2, 0.5, 0.2),
	BiomeType.JUNGLE: Color(0.1, 0.4, 0.1),
	BiomeType.SAVANNA: Color(0.8, 0.7, 0.3),
	BiomeType.SWAMP: Color(0.3, 0.4, 0.2),
	BiomeType.TAIGA: Color(0.4, 0.5, 0.3),
	BiomeType.TUNDRA: Color(0.7, 0.8, 0.8),
	BiomeType.MOUNTAIN: Color(0.5, 0.5, 0.5),
	BiomeType.SNOW: Color(0.9, 0.9, 0.9)
}

func _ready():
	initialize_noise()

func initialize_noise():
	# Height noise (combination of multiple frequencies)
	height_noise = FastNoiseLite.new()
	height_noise.noise_type = FastNoiseLite.TYPE_PERLIN
	height_noise.frequency = terrain_scale
	height_noise.fractal_octaves = 4
	height_noise.fractal_gain = 0.5
	height_noise.fractal_lacunarity = 2.0
	
	# Temperature noise (large scale)
	temperature_noise = FastNoiseLite.new()
	temperature_noise.noise_type = FastNoiseLite.TYPE_PERLIN
	temperature_noise.frequency = 0.01
	temperature_noise.fractal_octaves = 2
	
	# Moisture noise (medium scale)
	moisture_noise = FastNoiseLite.new()
	moisture_noise.noise_type = FastNoiseLite.TYPE_PERLIN
	moisture_noise.frequency = 0.02
	moisture_noise.fractal_octaves = 2
	
	# Biome noise (for biome blending)
	biome_noise = FastNoiseLite.new()
	biome_noise.noise_type = FastNoiseLite.TYPE_CELLULAR
	biome_noise.frequency = 0.005

func generate_terrain(chunk: Chunk):
	var chunk_x = chunk.chunk_x
	var chunk_z = chunk.chunk_z
	var chunk_size = chunk.chunk_size
	
	# Generate heightmap
	var heightmap = []
	heightmap.resize(chunk_size * chunk_size)
	
	# Generate biomes
	var biomes = []
	biomes.resize(chunk_size * chunk_size)
	
	for x in range(chunk_size):
		for z in range(chunk_size):
			var world_x = chunk_x * chunk_size + x
			var world_z = chunk_z * chunk_size + z
			
			# Calculate height
			var height = calculate_height(world_x, world_z)
			heightmap[z * chunk_size + x] = height
			
			# Determine biome
			var biome = determine_biome(world_x, world_z, height)
			biomes[z * chunk_size + x] = biome
	
	# Apply to chunk
	chunk.heightmap = heightmap
	chunk.biomes = biomes
	chunk.generate_mesh()
	
	# Apply biome-based coloring
	apply_biome_colors(chunk)

func calculate_height(x: float, z: float) -> float:
	# Base height from noise
	var height = height_noise.get_noise_2d(x, z) * height_multiplier + base_height
	
	# Add mountain ridges
	var mountain_noise = FastNoiseLite.new()
	mountain_noise.noise_type = FastNoiseLite.TYPE_PERLIN
	mountain_noise.frequency = 0.02
	height += max(0.0, mountain_noise.get_noise_2d(x, z)) * 30.0
	
	# Add some terrain features
	height = add_terrain_features(x, z, height)
	
	# Ensure minimum height
	return max(0.0, height)

func add_terrain_features(x: float, z: float, h: float) -> float:
	var height = h
	
	# Add hills
	var hill_noise = FastNoiseLite.new()
	hill_noise.noise_type = FastNoiseLite.TYPE_SIMPLEX
	hill_noise.frequency = 0.03
	
	height += hill_noise.get_noise_2d(x * 0.5, z * 0.5) * 10.0
	
	# Add some randomness
	var random_noise = FastNoiseLite.new()
	random_noise.noise_type = FastNoiseLite.TYPE_VALUE
	random_noise.frequency = 0.1
	
	height += random_noise.get_noise_2d(x, z) * 2.0
	
	return height

func determine_biome(x: float, z: float, height: float) -> int:
	# Get temperature and moisture
	var temperature = temperature_noise.get_noise_2d(x, z) * 0.5 + 0.5  # 0-1 range
	var moisture = moisture_noise.get_noise_2d(x, z) * 0.5 + 0.5  # 0-1 range
	
	# Height-based biomes
	if height < 5.0:
		return BiomeType.OCEAN
	elif height < 10.0:
		return BiomeType.BEACH
	elif height > 60.0:
		if temperature < 0.3:
			return BiomeType.SNOW
		else:
			return BiomeType.MOUNTAIN
	
	# Temperature and moisture based biomes
	if temperature > 0.8:
		if moisture < 0.3:
			return BiomeType.DESERT
		elif moisture < 0.6:
			return BiomeType.SAVANNA
		else:
			return BiomeType.JUNGLE
	elif temperature > 0.6:
		if moisture < 0.4:
			return BiomeType.GRASSLAND
		elif moisture < 0.7:
			return BiomeType.FOREST
		else:
			return BiomeType.SWAMP
	elif temperature > 0.4:
		if moisture < 0.5:
			return BiomeType.GRASSLAND
		else:
			return BiomeType.FOREST
	elif temperature > 0.2:
		return BiomeType.TAIGA
	else:
		return BiomeType.TUNDRA

func apply_biome_colors(chunk: Chunk):
	# Create a material with vertex colors based on biomes
	var material = StandardMaterial3D.new()
	material.vertex_color_use_as_albedo = true
	
	# Create a copy of the mesh with vertex colors
	var original_mesh = chunk.mesh_instance.mesh
	var new_mesh = ArrayMesh.new()
	
	var surface_count = original_mesh.get_surface_count()
	for surface_idx in range(surface_count):
		var surface_arrays = original_mesh.surface_get_arrays(surface_idx)
		
		# Add vertex colors
		var vertex_count = surface_arrays[Mesh.ARRAY_VERTEX].size()
		var vertex_colors = PackedColorArray()
		vertex_colors.resize(vertex_count)
		
		# Assign colors based on biome at vertex position
		for i in range(vertex_count):
			var vertex = surface_arrays[Mesh.ARRAY_VERTEX][i]
			var local_x = int(vertex.x)
			var local_z = int(vertex.z)
			
			if local_x >= 0 and local_x < chunk.chunk_size and local_z >= 0 and local_z < chunk.chunk_size:
				var biome_idx = chunk.biomes[local_z * chunk.chunk_size + local_x]
				var biome_color = biome_colors.get(biome_idx, Color.WHITE)
				
				# Adjust color based on height for variation
				var height_factor = vertex.y / chunk.chunk_height
				vertex_colors[i] = biome_color.darkened(height_factor * 0.2)
			else:
				vertex_colors[i] = Color.WHITE
		
		# Add colors to surface arrays
		surface_arrays[Mesh.ARRAY_COLOR] = vertex_colors
		
		# Create new surface
		new_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, surface_arrays)
	
	# Apply the new mesh
	chunk.mesh_instance.mesh = new_mesh
	chunk.mesh_instance.material_override = material

func generate_vegetation(chunk: Chunk, density: float = 0.01):
	# Generate trees, rocks, and other vegetation based on biome
	var vegetation_nodes = Node3D.new()
	vegetation_nodes.name = "Vegetation"
	
	for x in range(chunk.chunk_size):
		for z in range(chunk.chunk_size):
			var biome = chunk.biomes[z * chunk.chunk_size + x]
			var height = chunk.heightmap[z * chunk.chunk_size + x]
			
			# Check if we should place vegetation here
			if randf() < density * get_biome_density(biome):
				var vegetation = create_vegetation_for_biome(biome)
				if vegetation:
					var world_x = chunk.chunk_x * chunk.chunk_size + x
					var world_z = chunk.chunk_z * chunk.chunk_size + z
					var world_y = height
					
					vegetation.global_transform.origin = Vector3(world_x, world_y, world_z)
					vegetation_nodes.add_child(vegetation)
	
	chunk.add_child(vegetation_nodes)

func get_biome_density(biome: int) -> float:
	match biome:
		BiomeType.FOREST, BiomeType.JUNGLE:
			return 0.8
		BiomeType.TAIGA:
			return 0.6
		BiomeType.SAVANNA, BiomeType.GRASSLAND:
			return 0.3
		BiomeType.SWAMP:
			return 0.4
		_:
			return 0.1

func create_vegetation_for_biome(biome: int) -> MeshInstance3D:
	var model_generator = get_node("/root/GameClient/ModelGenerator")
	
	match biome:
		BiomeType.FOREST, BiomeType.TAIGA:
			return model_generator.generate_low_poly_tree(randi() % 3)
		BiomeType.JUNGLE:
			var tree = model_generator.generate_low_poly_tree(randi() % 2)
			# Scale for jungle trees
			tree.scale *= Vector3(1.2, 1.5, 1.2)
			return tree
		BiomeType.MOUNTAIN, BiomeType.DESERT:
			return model_generator.generate_low_poly_rock(randi() % 3)
		_:
			# Small chance for rocks in other biomes
			if randf() < 0.1:
				return model_generator.generate_low_poly_rock(randi() % 2)
	
	return null

func generate_rivers(chunk: Chunk):
	# Simple river generation using noise
	var river_noise = FastNoiseLite.new()
	river_noise.noise_type = FastNoiseLite.TYPE_SIMPLEX
	river_noise.frequency = 0.01
	
	var river_nodes = Node3D.new()
	river_nodes.name = "Rivers"
	
	for x in range(0, chunk.chunk_size, 4):
		for z in range(0, chunk.chunk_size, 4):
			var world_x = chunk.chunk_x * chunk.chunk_size + x
			var world_z = chunk.chunk_z * chunk.chunk_size + z
			
			var river_value = river_noise.get_noise_2d(world_x, world_z)
			
			if abs(river_value) < 0.05:  # River path
				var height = chunk.heightmap[z * chunk.chunk_size + x]
				if height < 15.0:  # Only in low areas
					var river_segment = create_river_segment()
					river_segment.global_transform.origin = Vector3(
						world_x,
						height - 0.5,  # Slightly below terrain
						world_z
					)
					river_nodes.add_child(river_segment)
	
	chunk.add_child(river_nodes)

func create_river_segment() -> MeshInstance3D:
	var model_generator = get_node("/root/GameClient/ModelGenerator")
	return model_generator.generate_procedural_model("river", {"width": 3.0})

func get_height_at(x: float, z: float) -> float:
	return calculate_height(x, z)

func get_biome_at(x: float, z: float) -> int:
	var height = get_height_at(x, z)
	return determine_biome(x, z, height)

func generate_world_chunk(chunk_x: int, chunk_z: int, chunk_size: int) -> Dictionary:
	# Generate chunk data for network transmission
	var chunk_data = {
		"chunk_x": chunk_x,
		"chunk_z": chunk_z,
		"size": chunk_size,
		"heightmap": [],
		"biomes": []
	}
	
	chunk_data.heightmap.resize(chunk_size * chunk_size)
	chunk_data.biomes.resize(chunk_size * chunk_size)
	
	for x in range(chunk_size):
		for z in range(chunk_size):
			var world_x = chunk_x * chunk_size + x
			var world_z = chunk_z * chunk_size + z
			
			var height = calculate_height(world_x, world_z)
			var biome = determine_biome(world_x, world_z, height)
			
			chunk_data.heightmap[z * chunk_size + x] = height
			chunk_data.biomes[z * chunk_size + x] = biome
	
	return chunk_data