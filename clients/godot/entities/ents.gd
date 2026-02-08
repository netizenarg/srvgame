extends Node

class_name EntityManager

# Entity types
enum EntityType {
	PLAYER,
	NPC,
	MOB,
	FAMILIAR,
	LOOT,
	PROJECTILE,
	VEHICLE
}

# Entity registry
var entities: Dictionary = {}  # entity_id -> entity_node
var entity_counters: Dictionary = {}

# Spawn parameters
var max_entities: int = 1000
var spawn_radius: float = 100.0
var despawn_distance: float = 150.0

# References
@onready var world = get_parent().get_node("World")
@onready var model_generator = get_parent().get_node("ModelGenerator")

func _ready():
	initialize_counters()

func initialize_counters():
	for type in EntityType.values():
		entity_counters[type] = 0

func spawn_entity(entity_type: int, position: Vector3, data: Dictionary = {}) -> int:
	# Check entity limit
	if get_total_entities() >= max_entities:
		despawn_farthest_entity(position)
	
	# Generate entity ID
	var entity_id = generate_entity_id(entity_type)
	
	# Create entity
	var entity = create_entity_node(entity_type, data)
	if not entity:
		return -1
	
	# Position entity
	entity.global_transform.origin = position
	
	# Add to world
	world.add_child(entity)
	
	# Register entity
	entities[entity_id] = {
		"node": entity,
		"type": entity_type,
		"data": data,
		"position": position,
		"spawn_time": Time.get_ticks_msec()
	}
	
	# Update counter
	entity_counters[entity_type] += 1
	
	print("Spawned entity %d of type %s at %s" % [entity_id, EntityType.keys()[entity_type], position])
	
	return entity_id

func despawn_entity(entity_id: int):
	if entities.has(entity_id):
		var entity = entities[entity_id]
		
		# Remove from world
		if entity.node:
			entity.node.queue_free()
		
		# Update counter
		entity_counters[entity.type] -= 1
		
		# Remove from registry
		entities.erase(entity_id)
		
		print("Despawned entity %d" % entity_id)

func update_entity(entity_id: int, new_data: Dictionary):
	if entities.has(entity_id):
		var entity = entities[entity_id]
		
		# Update position if provided
		if new_data.has("position"):
			var new_position = Vector3(
				new_data.position.x,
				new_data.position.y,
				new_data.position.z
			)
			
			# Interpolate movement
			if entity.node:
				var current_pos = entity.node.global_transform.origin
				entity.node.global_transform.origin = current_pos.lerp(new_position, 0.3)
			
			entity.position = new_position
		
		# Update rotation if provided
		if new_data.has("rotation") and entity.node:
			var new_rotation = Vector3(
				new_data.rotation.x,
				new_data.rotation.y,
				new_data.rotation.z
			)
			entity.node.rotation = new_rotation
		
		# Update entity data
		entity.data.merge(new_data, true)
		
		# Update visual state
		update_entity_visual(entity_id)

func get_entity(entity_id: int):
	return entities.get(entity_id)

func get_entity_node(entity_id: int) -> Node3D:
	var entity = entities.get(entity_id)
	if entity:
		return entity.node
	return null

func get_total_entities() -> int:
	return entities.size()

func get_entities_by_type(entity_type: int) -> Array:
	var result = []
	for entity_id in entities.keys():
		if entities[entity_id].type == entity_type:
			result.append(entity_id)
	return result

func get_entities_in_radius(position: Vector3, radius: float) -> Array:
	var result = []
	
	for entity_id in entities.keys():
		var entity = entities[entity_id]
		if entity.position.distance_to(position) <= radius:
			result.append({
				"id": entity_id,
				"type": entity.type,
				"position": entity.position,
				"distance": entity.position.distance_to(position)
			})
	
	# Sort by distance
	result.sort_custom(func(a, b): return a.distance < b.distance)
	
	return result

func despawn_farthest_entity(from_position: Vector3):
	var farthest_id = -1
	var farthest_distance = 0.0
	
	for entity_id in entities.keys():
		var entity = entities[entity_id]
		var distance = entity.position.distance_to(from_position)
		
		# Don't despawn players or important NPCs
		if entity.type == EntityType.PLAYER:
			continue
		
		if distance > farthest_distance:
			farthest_distance = distance
			farthest_id = entity_id
	
	if farthest_id != -1 and farthest_distance > despawn_distance:
		despawn_entity(farthest_id)

func generate_entity_id(entity_type: int) -> int:
	var prefix = (entity_type + 1) * 1000000
	var counter = entity_counters[entity_type] + 1
	return prefix + counter

func create_entity_node(entity_type: int, data: Dictionary) -> Node3D:
	var entity_node = Node3D.new()
	
	# Add model based on type
	var model = create_entity_model(entity_type, data)
	if model:
		entity_node.add_child(model)
	
	# Add collision
	add_collision_to_entity(entity_node, entity_type, data)
	
	# Add interaction area for NPCs and loot
	if entity_type == EntityType.NPC or entity_type == EntityType.LOOT:
		add_interaction_area(entity_node, entity_type, data)
	
	# Add health bar for mobs
	if entity_type == EntityType.MOB:
		add_health_display(entity_node, data)
	
	# Set entity name
	entity_node.name = "Entity_%d" % generate_entity_id(entity_type)
	
	return entity_node

func create_entity_model(entity_type: int, data: Dictionary) -> MeshInstance3D:
	var type_name = EntityType.keys()[entity_type].to_lower()
	
	# Generate model using model generator
	var model = model_generator.generate_model(type_name, data)
	
	# Apply scale from data
	if data.has("scale"):
		model.scale = Vector3(data.scale.x, data.scale.y, data.scale.z)
	
	# Apply color from data
	if data.has("color") and model.material_override is StandardMaterial3D:
		var material = model.material_override.duplicate()
		material.albedo_color = Color(data.color)
		model.material_override = material
	
	return model

func add_collision_to_entity(entity_node: Node3D, entity_type: int, data: Dictionary):
	var collision_shape = CollisionShape3D.new()
	
	match entity_type:
		EntityType.PLAYER, EntityType.NPC:
			var capsule = CapsuleShape3D.new()
			capsule.height = 1.8
			capsule.radius = 0.3
			collision_shape.shape = capsule
			collision_shape.position = Vector3(0, 0.9, 0)
		
		EntityType.MOB:
			var sphere = SphereShape3D.new()
			sphere.radius = 0.5
			collision_shape.shape = sphere
			collision_shape.position = Vector3(0, 0.5, 0)
		
		EntityType.LOOT:
			var box = BoxShape3D.new()
			box.size = Vector3(0.5, 0.5, 0.5)
			collision_shape.shape = box
		
		_:
			# Default collision
			var box = BoxShape3D.new()
			box.size = Vector3(1, 1, 1)
			collision_shape.shape = box
	
	entity_node.add_child(collision_shape)

func add_interaction_area(entity_node: Node3D, entity_type: int, data: Dictionary):
	var area = Area3D.new()
	area.collision_layer = 2  # Interaction layer
	area.collision_mask = 1   # Player layer
	
	# Connect interaction signal
	area.input_event.connect(_on_entity_interacted.bind(entity_node))
	
	# Add collision shape
	var collision_shape = CollisionShape3D.new()
	var sphere = SphereShape3D.new()
	sphere.radius = 2.0  # Interaction radius
	collision_shape.shape = sphere
	
	area.add_child(collision_shape)
	entity_node.add_child(area)

func add_health_display(entity_node: Node3D, data: Dictionary):
	# Create health bar UI
	var health_bar = Sprite3D.new()
	health_bar.texture = preload("res://textures/health_bar.png")
	health_bar.pixel_size = 0.01
	health_bar.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	health_bar.position = Vector3(0, 1.5, 0)
	
	# Store health data
	entity_node.set_meta("health", data.get("health", 100))
	entity_node.set_meta("max_health", data.get("max_health", 100))
	
	entity_node.add_child(health_bar)

func update_entity_visual(entity_id: int):
	var entity = entities.get(entity_id)
	if not entity or not entity.node:
		return
	
	# Update health display for mobs
	if entity.type == EntityType.MOB:
		update_health_display(entity.node, entity.data)
	
	# Update animation based on state
	if entity.data.has("state"):
		update_entity_animation(entity.node, entity.data.state)

func update_health_display(entity_node: Node3D, data: Dictionary):
	var health = data.get("health", 100)
	var max_health = data.get("max_health", 100)
	
	# Update health bar scale
	var health_bar = entity_node.get_node("Sprite3D")
	if health_bar:
		var health_ratio = float(health) / max_health
		health_bar.scale.x = health_ratio

func update_entity_animation(entity_node: Node3D, state: String):
	# This would typically control animation player
	pass

func _on_entity_interacted(camera, event, position, normal, shape_idx, entity_node):
	if event is InputEventMouseButton and event.pressed:
		# Find entity ID from node
		var entity_name = entity_node.name
		if entity_name.begins_with("Entity_"):
			var entity_id = int(entity_name.replace("Entity_", ""))
			
			# Emit interaction signal
			emit_signal("entity_interacted", entity_id)
			
			# Send to server if it's an NPC or loot
			var entity = get_entity(entity_id)
			if entity and (entity.type == EntityType.NPC or entity.type == EntityType.LOOT):
				# This would be connected to network manager
				pass

func cleanup_old_entities():
	var current_time = Time.get_ticks_msec()
	var to_remove = []
	
	for entity_id in entities.keys():
		var entity = entities[entity_id]
		var age = current_time - entity.spawn_time
		
		# Remove temporary entities older than 5 minutes
		if entity.type == EntityType.LOOT and age > 300000:  # 5 minutes
			to_remove.append(entity_id)
	
	for entity_id in to_remove:
		despawn_entity(entity_id)

func _process(delta):
	# Periodically clean up old entities
	if Time.get_ticks_msec() % 10000 < delta * 1000:  # Every 10 seconds
		cleanup_old_entities()

# Network message handlers
func handle_spawn_message(data: Dictionary):
	var entity_id = data.get("entity_id")
	var entity_type = data.get("entity_type")
	var position = Vector3(
		data.position.x,
		data.position.y,
		data.position.z
	)
	
	spawn_entity(entity_type, position, data)

func handle_update_message(data: Dictionary):
	var entity_id = data.get("entity_id")
	update_entity(entity_id, data)

func handle_despawn_message(data: Dictionary):
	var entity_id = data.get("entity_id")
	despawn_entity(entity_id)

signal entity_interacted(entity_id: int)
signal entity_spawned(entity_id: int, entity_type: int)
signal entity_despawned(entity_id: int)