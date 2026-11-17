extends Node


class Mover:
	extends Node2D

	func advance(steps: int) -> float:
		for i in range(steps):
			position += Vector2(0.5, 0.25)
			rotation += 0.001
		return position.x + position.y + rotation


var positions := PackedVector2Array()
var velocities: Array[Vector2] = []
var gravity := Vector2(0.0, 98.0)
var floor_height := 500.0


func simulate_particles(count: int, steps: int) -> float:
	positions.resize(count)
	velocities.clear()
	for i in range(count):
		positions[i] = Vector2(i, 0.0)
		velocities.append(Vector2(1.0, -float(i % 7)))

	var delta := 1.0 / 60.0
	for step in range(steps):
		for i in range(count):
			var velocity := velocities[i] + gravity * delta
			var position := positions[i] + velocity * delta
			if position.y > floor_height:
				position.y = floor_height
				velocity.y = -velocity.y * 0.5
			velocities[i] = velocity
			positions[i] = position

	var total := 0.0
	for position in positions:
		total += position.length()
	return snappedf(total, 0.001)


func build_text(count: int) -> String:
	var parts := PackedStringArray()
	for i in range(count):
		parts.append(str(i * 3))
	var text := ",".join(parts)
	return "%d:%s" % [text.length(), text.md5_text()]


func count_keys(count: int) -> int:
	var counts := {}
	for i in range(count):
		var key := i % 97
		counts[key] = counts.get(key, 0) + 1
	var checksum := 0
	for key in counts:
		checksum += key * counts[key]
	return checksum


func spawn_nodes(count: int) -> int:
	for i in range(count):
		var node := Node.new()
		node.name = "Spawned%d" % i
		add_child(node)
	var total := 0
	for child in get_children():
		total += String(child.name).length()
		child.free()
	return total


func move_nodes(count: int, steps: int) -> float:
	var nodes: Array[Node2D] = []
	for i in range(count):
		var node := Node2D.new()
		node.position = Vector2(i, 0.0)
		nodes.append(node)
	var velocity := Vector2(1.5, -0.5)
	for step in range(steps):
		for node in nodes:
			node.position += velocity
			node.rotation += 0.01
	var total := 0.0
	for node in nodes:
		total += node.position.x + node.rotation
		node.free()
	return snappedf(total, 0.001)


func move_self(steps: int) -> float:
	var mover := Mover.new()
	var value := mover.advance(steps)
	mover.free()
	return snappedf(value, 0.001)
