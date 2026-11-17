extends RefCounted

const LIMIT := 3


class Counter:
	extends RefCounted

	static var created := 0

	var value := 0
	var history: Array[int] = []

	func _init(start: int = 0) -> void:
		value = start
		created += 1

	func add(amount: int) -> Counter:
		value += amount
		history.append(value)
		return self

	func describe() -> String:
		return "%s:%d" % [get_kind(), value]

	func get_kind() -> String:
		return "counter"


class DoubleCounter:
	extends Counter

	func add(amount: int) -> Counter:
		return super.add(amount * 2)

	func get_kind() -> String:
		return "double"


class Outer:
	class Deep:
		var label := "deep"

		static func make(text: String) -> Deep:
			var deep := Deep.new()
			deep.label = text
			return deep

	var children: Array[Deep] = []

	func build(count: int) -> Array:
		for i in range(count):
			children.append(Deep.make("child %d" % i))
		return children.map(func(child: Deep): return child.label)


class Waiter:
	signal arrived(value: int)

	var received := []

	func wait_twice() -> void:
		received.append(await arrived)
		received.append(await arrived)


func run() -> Array:
	var results := []
	var counter := Counter.new(5).add(1).add(2)
	results.append([counter.describe(), counter.history])
	var doubled := DoubleCounter.new()
	doubled.add(3).add(4)
	results.append([doubled.describe(), doubled.history, doubled is Counter, counter is DoubleCounter])
	results.append(["created", Counter.created])
	var outer := Outer.new()
	results.append(outer.build(LIMIT))
	var typed: Array[Counter] = [counter, doubled]
	results.append(typed.map(func(item: Counter): return item.get_kind()))
	var waiter := Waiter.new()
	waiter.wait_twice()
	waiter.arrived.emit(1)
	waiter.arrived.emit(2)
	results.append(waiter.received)
	return results
