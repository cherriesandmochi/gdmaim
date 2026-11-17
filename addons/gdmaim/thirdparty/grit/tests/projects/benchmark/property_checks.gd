extends RefCounted


class Walker:
	extends Node2D

	func walk(steps: int) -> Array:
		var trail := []
		for i in range(steps):
			position += Vector2(1.5, -0.5)
			rotation = rotation + 0.25
			scale *= 1.1
			trail.append([position, snappedf(rotation, 0.001), scale.snapped(Vector2(0.001, 0.001))])
		return trail


class Intercepted:
	extends Node2D

	var rotations := []

	func _get(property: StringName) -> Variant:
		if property == &"position":
			return Vector2(42, 42)
		return null

	func _set(property: StringName, value: Variant) -> bool:
		if property == &"rotation":
			rotations.append(value)
			return true
		return false

	func own_position() -> Vector2:
		return position


class Shadow:
	extends Node

	var position := Vector2(7, 7)


class Stats:
	extends RefCounted

	signal changed

	const LIMIT := 10

	var count: int = 1
	var label = "stats"
	var health: int = 5:
		set(value):
			health = clampi(value, 0, LIMIT)
	var doubled: int:
		get:
			return count * 2

	func describe() -> String:
		return "%s:%d" % [label, count]


class Boosted:
	extends Stats

	var boost := 3


func script_members() -> Array:
	var stats := Stats.new()
	stats.count += 4
	stats.label = "hero"
	stats.health = 50
	var loose = 7.9
	var converted := Stats.new()
	converted.count = loose
	var boosted: Stats = Boosted.new()
	boosted.count = 9
	var untyped = Boosted.new()
	untyped.boost += 1
	untyped.label = [1, 2]
	var method: Callable = stats.describe
	var signal_value: Signal = stats.changed
	return [stats.count, stats.label, stats.health, stats.doubled, converted.count, boosted.count, boosted.doubled, untyped.boost, untyped.label, untyped.count, method.call(), signal_value.get_name()]


func read_positions(nodes: Array[Node2D]) -> Array:
	var result := []
	for node in nodes:
		result.append(node.position)
	return result


func move_all(nodes: Array[Node2D], offset: Vector2) -> void:
	for node in nodes:
		node.position = node.position + offset


func move_unlabeled(nodes: Array[Node2D], offset: Vector2) -> void:
	for node in nodes:
		if node is Sprite2D:
			continue
		node.position = node.position + offset


func run() -> Array:
	var results := []

	var walker := Walker.new()
	results.append(walker.walk(4))
	walker.free()

	var sprite := Sprite2D.new()
	sprite.position = Vector2(3, 4)
	sprite.offset = Vector2(-1, 2)
	sprite.flip_h = true
	sprite.z_index = 5
	sprite.modulate = Color(0.5, 0.25, 1.0)
	sprite.name = &"Sprite"
	sprite.texture = null
	results.append([sprite.position, sprite.offset, sprite.flip_h, sprite.z_index, sprite.modulate, sprite.name, sprite.texture])
	sprite.free()

	var panel := Control.new()
	panel.offset_left = 12.5
	panel.offset_right = 40.0
	results.append([panel.offset_left, panel.offset_right, panel.size])
	panel.free()

	var slider := HSlider.new()
	slider.max_value = 10.0
	slider.value = 50.0
	results.append([slider.value, slider.ratio])
	slider.free()

	var intercepted := Intercepted.new()
	var intercepted_node: Node2D = intercepted
	intercepted_node.position = Vector2(1, 1)
	intercepted_node.rotation = 2.0
	results.append([intercepted_node.position, intercepted.own_position(), intercepted_node.rotation, intercepted.rotations])
	intercepted.free()

	var shadowed := Node2D.new()
	shadowed.set_script(Shadow)
	var shadowed_node: Node2D = shadowed
	results.append(shadowed_node.position)
	shadowed.free()

	var nodes: Array[Node2D] = [Node2D.new(), Sprite2D.new(), Camera2D.new(), Marker2D.new(), Line2D.new(), Polygon2D.new(), Walker.new()]
	for i in range(nodes.size()):
		nodes[i].position = Vector2(i, i * 2)
	move_all(nodes, Vector2(0.5, -1))
	move_all(nodes, Vector2(0.5, -1))
	move_unlabeled(nodes, Vector2(10, 10))
	results.append(read_positions(nodes))
	for node in nodes:
		node.free()

	var resource := Resource.new()
	resource.resource_name = "named"
	results.append(resource.resource_name)
	results.append(script_members())
	results.append(object_properties())
	return results


func read_texture(sprite: Sprite2D) -> Texture2D:
	return sprite.texture


func assign_texture(sprite: Sprite2D, value: Texture2D) -> void:
	sprite.texture = value


func assign_untyped(sprite: Sprite2D, value: Variant) -> void:
	sprite.texture = value


func object_properties() -> Array:
	var out := []
	var empty := Sprite2D.new()
	out.append(read_texture(empty) == null)
	var texture := PlaceholderTexture2D.new()
	texture.size = Vector2(8, 4)
	assign_texture(empty, texture)
	var read: Texture2D = read_texture(empty)
	out.append([read == texture, read.get_size(), texture.get_reference_count()])
	assign_texture(empty, null)
	out.append(read_texture(empty) == null)
	assign_untyped(empty, texture)
	out.append(read_texture(empty) == texture)
	empty.free()
	var node := Node2D.new()
	out.append(node.owner)
	node.free()
	return out
