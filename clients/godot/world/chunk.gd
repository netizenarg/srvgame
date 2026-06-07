extends Node3D

class_name Chunk

var chunk_x: int = 0
var chunk_z: int = 0
var chunk_size: int = 32
var chunk_height: int = 64
var spacing: float = 1.0
var heightmap: Array = []
var biomes: Array = []

var mesh_instance: MeshInstance3D
var collision_shape: CollisionShape3D

var lod_meshes: Array = []
var current_lod: int = 0

var entities_container: Node3D

func initialize(x: int, z: int, size: int, height: int):
	chunk_x = x
	chunk_z = z
	chunk_size = size
	chunk_height = height
	heightmap.resize(size * size)
	biomes.resize(size * size)
	mesh_instance = MeshInstance3D.new()
	add_child(mesh_instance)
	entities_container = Node3D.new()
	entities_container.name = "Entities"
	add_child(entities_container)

func _pyf3d_height(wx: float, wz: float) -> float:
	return (sin(wx * 0.1) * cos(wz * 0.1) +
			0.3 * sin(wx * 0.3 + 1.2) +
			0.3 * cos(wz * 0.3 + 2.4) +
			0.2 * sin((wx * 0.6 + wz * 0.4) * 0.8)) * 2.0 + 0.5

func get_height_at(x: int, z: int) -> float:
	if x >= 0 and x < chunk_size and z >= 0 and z < chunk_size:
		return heightmap[z * chunk_size + x]
	return 0.0

func generate(height_data: Array = [], _biome_data: Array = []):
	if not height_data.is_empty():
		heightmap = height_data.duplicate()
	else:
		generate_heightmap()
	generate_mesh()

func generate_heightmap():
	for x in range(chunk_size):
		for z in range(chunk_size):
			var wx = chunk_x * chunk_size + x * spacing
			var wz = chunk_z * chunk_size + z * spacing
			heightmap[z * chunk_size + x] = _pyf3d_height(wx, wz)

func generate_mesh():
	var surface_tool = SurfaceTool.new()
	surface_tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	for z in range(chunk_size + 1):
		for x in range(chunk_size + 1):
			var wx = chunk_x * chunk_size + x * spacing
			var wz = chunk_z * chunk_size + z * spacing
			var wy = _pyf3d_height(wx, wz)
			var pos = Vector3(wx, wy, wz)
			surface_tool.set_normal(Vector3.UP)
			surface_tool.add_vertex(pos)
	for z in range(chunk_size):
		for x in range(chunk_size):
			var i = z * (chunk_size + 1) + x
			surface_tool.add_index(i)
			surface_tool.add_index(i + 1)
			surface_tool.add_index(i + chunk_size + 1)
			surface_tool.add_index(i + 1)
			surface_tool.add_index(i + chunk_size + 2)
			surface_tool.add_index(i + chunk_size + 1)
	surface_tool.generate_normals()
	var mesh = surface_tool.commit()
	mesh_instance.mesh = mesh
	var static_body = StaticBody3D.new()
	static_body.position = Vector3.ZERO
	add_child(static_body)
	var trimesh_shape = mesh.create_trimesh_shape()
	collision_shape = CollisionShape3D.new()
	collision_shape.shape = trimesh_shape
	static_body.add_child(collision_shape)

func _compute_normal(x: int, z: int) -> Vector3:
	var hx1 = get_height_at(x + 1, z) if x + 1 < chunk_size else get_height_at(x, z)
	var hx2 = get_height_at(x - 1, z) if x - 1 >= 0 else get_height_at(x, z)
	var hz1 = get_height_at(x, z + 1) if z + 1 < chunk_size else get_height_at(x, z)
	var hz2 = get_height_at(x, z - 1) if z - 1 >= 0 else get_height_at(x, z)
	var dx = hx1 - hx2
	var dz = hz1 - hz2
	return Vector3(-dx, 2.0 * spacing, -dz).normalized()

func spawn_entities():
	var rng = RandomNumberGenerator.new()
	rng.seed = chunk_x * 1000003 + chunk_z * 1000033
	_spawn_trees(rng)
	_spawn_stones(rng)
	_spawn_houses(rng)
	_spawn_water(rng)
	_spawn_mobs(rng)
	_spawn_health_boxes(rng)

func _spawn_trees(rng: RandomNumberGenerator):
	var count = rng.randi_range(5, 10)
	var phys_size = (chunk_size - 1) * spacing
	for _i in range(count):
		var lx = rng.randf_range(1.5, phys_size - 1.5)
		var lz = rng.randf_range(1.5, phys_size - 1.5)
		var wx = chunk_x * chunk_size + lx
		var wz = chunk_z * chunk_size + lz
		var y = _pyf3d_height(wx, wz)
		if y < 0.1:
			continue
		var trunk_height = rng.randf_range(1.8, 2.2)
		var foliage_radius = rng.randf_range(1.0, 1.4)
		var rot_y = rng.randf_range(0, TAU)
		var tree = _create_tree(trunk_height, foliage_radius)
		tree.position = Vector3(wx, y, wz)
		tree.rotation.y = rot_y
		entities_container.add_child(tree)

func _create_tree(trunk_h: float, foliage_r: float) -> Node3D:
	var node = Node3D.new()
	var trunk_mesh = CylinderMesh.new()
	trunk_mesh.top_radius = 0.3
	trunk_mesh.bottom_radius = 0.3
	trunk_mesh.height = trunk_h
	var trunk_mat = StandardMaterial3D.new()
	trunk_mat.albedo_color = Color(0.545, 0.271, 0.075)
	trunk_mat.roughness = 0.9
	trunk_mesh.material = trunk_mat
	var trunk_instance = MeshInstance3D.new()
	trunk_instance.mesh = trunk_mesh
	trunk_instance.position.y = trunk_h * 0.5
	node.add_child(trunk_instance)
	var foliage_mesh = SphereMesh.new()
	foliage_mesh.radius = foliage_r
	foliage_mesh.height = foliage_r * 2.0
	var foliage_mat = StandardMaterial3D.new()
	foliage_mat.albedo_color = Color(0.133, 0.545, 0.133)
	foliage_mat.roughness = 0.8
	foliage_mesh.material = foliage_mat
	var foliage_instance = MeshInstance3D.new()
	foliage_instance.mesh = foliage_mesh
	foliage_instance.position.y = trunk_h + foliage_r * 0.5
	node.add_child(foliage_instance)
	return node

func _spawn_stones(rng: RandomNumberGenerator):
	var count = rng.randi_range(5, 10)
	var phys_size = (chunk_size - 1) * spacing
	for _i in range(count):
		var lx = rng.randf_range(1.5, phys_size - 1.5)
		var lz = rng.randf_range(1.5, phys_size - 1.5)
		var wx = chunk_x * chunk_size + lx
		var wz = chunk_z * chunk_size + lz
		var y = _pyf3d_height(wx, wz)
		var shade = rng.randf_range(0.4, 0.7)
		var sx = rng.randf_range(0.7, 1.3)
		var sy = rng.randf_range(0.5, 1.2)
		var sz = rng.randf_range(0.7, 1.3)
		var rot = rng.randf_range(0, TAU)
		var stone = _create_stone(shade, Vector3(sx, sy, sz))
		stone.position = Vector3(wx, y, wz)
		stone.rotation.y = rot
		entities_container.add_child(stone)

func _create_stone(shade: float, scl: Vector3) -> MeshInstance3D:
	var mesh = BoxMesh.new()
	mesh.size = Vector3(0.8, 0.8, 0.8)
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(shade, shade, shade)
	mat.roughness = 0.9
	mesh.material = mat
	var instance = MeshInstance3D.new()
	instance.mesh = mesh
	instance.scale = scl
	return instance

func _spawn_houses(rng: RandomNumberGenerator):
	if rng.randf() > 0.2:
		return
	var phys_size = (chunk_size - 1) * spacing
	var lx = rng.randf_range(3.0, phys_size - 3.0)
	var lz = rng.randf_range(3.0, phys_size - 3.0)
	var wx = chunk_x * chunk_size + lx
	var wz = chunk_z * chunk_size + lz
	var y = _pyf3d_height(wx, wz)
	var s = rng.randf_range(0.8, 1.2)
	var house = _create_house(s)
	house.position = Vector3(wx, y, wz)
	entities_container.add_child(house)

func _create_house(s: float) -> Node3D:
	var node = Node3D.new()
	var wall_mat = StandardMaterial3D.new()
	wall_mat.albedo_color = Color(0.8, 0.6, 0.4)
	wall_mat.roughness = 0.8
	var roof_mat = StandardMaterial3D.new()
	roof_mat.albedo_color = Color(0.6, 0.2, 0.1)
	roof_mat.roughness = 0.7
	var door_mat = StandardMaterial3D.new()
	door_mat.albedo_color = Color(0.5, 0.25, 0.0)
	door_mat.roughness = 0.8
	var w = 1.5
	var d = 1.5
	var wh = 1.8
	var rh = 0.8
	var base = BoxMesh.new()
	base.size = Vector3(w, wh, d)
	base.material = wall_mat
	var base_inst = MeshInstance3D.new()
	base_inst.mesh = base
	base_inst.position.y = wh * 0.5
	node.add_child(base_inst)
	var roof = PrismMesh.new()
	roof.size = Vector3(w + 0.2, rh, d + 0.2)
	roof.material = roof_mat
	var roof_inst = MeshInstance3D.new()
	roof_inst.mesh = roof
	roof_inst.position.y = wh + rh * 0.5
	node.add_child(roof_inst)
	var door = BoxMesh.new()
	door.size = Vector3(0.5, 1.5, 0.05)
	door.material = door_mat
	var door_inst = MeshInstance3D.new()
	door_inst.mesh = door
	door_inst.position = Vector3(0, 0.75, d * 0.5 + 0.03)
	node.add_child(door_inst)
	node.scale = Vector3(s, s, s)
	return node

func _spawn_water(_rng: RandomNumberGenerator):
	var water_level = 0.3
	var has_water = false
	for x in range(0, chunk_size, 4):
		for z in range(0, chunk_size, 4):
			if get_height_at(x, z) < water_level:
				has_water = true
				break
		if has_water:
			break
	if not has_water:
		return
	var plane_mesh = PlaneMesh.new()
	plane_mesh.size = Vector2(chunk_size * spacing, chunk_size * spacing)
	var mat = StandardMaterial3D.new()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.albedo_color = Color(0.2, 0.4, 0.8, 0.6)
	mat.metallic = 0.3
	mat.roughness = 0.1
	plane_mesh.material = mat
	var water_inst = MeshInstance3D.new()
	water_inst.mesh = plane_mesh
	water_inst.position = Vector3(
		chunk_x * chunk_size + chunk_size * spacing * 0.5,
		water_level,
		chunk_z * chunk_size + chunk_size * spacing * 0.5
	)
	add_child(water_inst)

func _spawn_mobs(rng: RandomNumberGenerator):
	var count = rng.randi_range(0, 1)
	var phys_size = (chunk_size - 1) * spacing
	for _i in range(count):
		var lx = rng.randf_range(2.0, phys_size - 2.0)
		var lz = rng.randf_range(2.0, phys_size - 2.0)
		var wx = chunk_x * chunk_size + lx
		var wz = chunk_z * chunk_size + lz
		var y = _pyf3d_height(wx, wz)
		if y < 0.5:
			continue
		var mob = _create_mob(rng)
		mob.position = Vector3(wx, y, wz)
		mob.set_meta("health", rng.randi_range(30, 80))
		mob.set_meta("max_health", mob.get_meta("health"))
		mob.set_meta("speed", 1.5)
		mob.set_meta("damage", rng.randi_range(8, 20))
		mob.set_meta("collision_radius", 0.6)
		entities_container.add_child(mob)

func _create_mob(_rng: RandomNumberGenerator) -> Node3D:
	var node = Node3D.new()
	var mob_mat = StandardMaterial3D.new()
	mob_mat.albedo_color = Color(0.8, 0.2, 0.2)
	mob_mat.roughness = 0.7
	var body = SphereMesh.new()
	body.radius = 0.4
	body.height = 0.6
	body.material = mob_mat
	var body_inst = MeshInstance3D.new()
	body_inst.mesh = body
	body_inst.position.y = 0.5
	node.add_child(body_inst)
	var head = SphereMesh.new()
	head.radius = 0.25
	head.height = 0.5
	head.material = mob_mat
	var head_inst = MeshInstance3D.new()
	head_inst.mesh = head
	head_inst.position = Vector3(0, 0.9, 0.25)
	node.add_child(head_inst)
	for i in range(4):
		var leg = SphereMesh.new()
		leg.radius = 0.15
		leg.height = 0.3
		leg.material = mob_mat
		var leg_inst = MeshInstance3D.new()
		leg_inst.mesh = leg
		var lx = 0.2 if i % 2 == 0 else -0.2
		var lz = 0.25 if i < 2 else -0.25
		leg_inst.position = Vector3(lx, 0.15, lz)
		node.add_child(leg_inst)
	return node

func _spawn_health_boxes(rng: RandomNumberGenerator):
	var lx = rng.randf_range(2.0, (chunk_size - 2.0) * spacing)
	var lz = rng.randf_range(2.0, (chunk_size - 2.0) * spacing)
	var wx = chunk_x * chunk_size + lx
	var wz = chunk_z * chunk_size + lz
	var y = _pyf3d_height(wx, wz)
	if y < 0.1:
		return
	var health_box = _create_health_box()
	health_box.position = Vector3(wx, y + 0.8, wz)
	health_box.set_meta("health_restore", 10)
	entities_container.add_child(health_box)

func _create_health_box() -> Node3D:
	var node = Node3D.new()
	var box_mesh = BoxMesh.new()
	box_mesh.size = Vector3(0.8, 0.8, 0.8)
	var mat = StandardMaterial3D.new()
	mat.albedo_color = Color(1.0, 1.0, 1.0)
	mat.emission_enabled = true
	mat.emission = Color(1.0, 0.2, 0.2)
	mat.emission_energy_multiplier = 0.5
	box_mesh.material = mat
	var box_inst = MeshInstance3D.new()
	box_inst.mesh = box_mesh
	node.add_child(box_inst)
	var cross_h = BoxMesh.new()
	cross_h.size = Vector3(0.5, 0.15, 0.05)
	var cross_mat = StandardMaterial3D.new()
	cross_mat.albedo_color = Color(1.0, 0.0, 0.0)
	cross_h.material = cross_mat
	var cross_inst_h = MeshInstance3D.new()
	cross_inst_h.mesh = cross_h
	cross_inst_h.position.z = -0.41
	node.add_child(cross_inst_h)
	var cross_v = BoxMesh.new()
	cross_v.size = Vector3(0.15, 0.5, 0.05)
	cross_v.material = cross_mat
	var cross_inst_v = MeshInstance3D.new()
	cross_inst_v.mesh = cross_v
	cross_inst_v.position.z = -0.41
	node.add_child(cross_inst_v)
	return node

func setup_lod(distances: Array):
	lod_meshes = distances

func update_lod(distance: float):
	if lod_meshes.is_empty():
		return
	var new_lod = 0
	for i in range(lod_meshes.size()):
		if distance > lod_meshes[i]:
			new_lod = i + 1
	if new_lod != current_lod:
		current_lod = new_lod
		if mesh_instance:
			mesh_instance.visible = current_lod == 0

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
	if collision_shape:
		collision_shape.queue_free()
