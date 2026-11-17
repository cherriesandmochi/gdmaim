extends RefCounted


class Shape:
	extends RefCounted

	func describe() -> String:
		return "%s with %d corners" % [kind(), corners()]

	func kind() -> String:
		return "shape"

	func corners() -> int:
		return 0


class Square:
	extends Shape

	func kind() -> String:
		return "square"

	func corners() -> int:
		return 4


class Holder:
	extends RefCounted

	var item: Item


class Item:
	extends RefCounted

	var value := 5
	var holder: Holder

	func drop() -> int:
		holder.item = null
		return value + 1


class Freer:
	extends Node

	func try_free() -> void:
		var node = self
		node.free()


static func scaled(value: float, factor := 2.0, offset := 0.5) -> float:
	return value * factor + offset


static func depth(remaining: int) -> int:
	if remaining == 0:
		return 0
	return 1 + depth(remaining - 1)


func accumulate(values: Array[int], start: int) -> int:
	var total := start
	for value in values:
		total = add(total, value)
	return total


func add(left: int, right: int) -> int:
	return left + right


func sum_positive(values: Array[int]) -> int:
	var total := 0
	for value in values:
		if value < 0:
			continue
		total = add(total, value)
	return total


func run() -> Array:
	var results := []
	results.append([Shape.new().describe(), Square.new().describe()])
	results.append([scaled(3.0), scaled(3.0, 4.0), scaled(3.0, 4.0, 1.0)])
	results.append(depth(3000))
	results.append(accumulate([1, 2, 3, 4], 10))
	results.append(sum_positive([3, -1, 4, -1, 5]))
	var holder := Holder.new()
	var item := Item.new()
	holder.item = item
	item.holder = holder
	var drop := Callable(item, "drop")
	item = null
	results.append(drop.call())
	results.append(holder.item)
	var freer := Freer.new()
	freer.try_free()
	var survived := is_instance_valid(freer)
	if survived:
		freer.free()
	results.append(survived)
	return results
