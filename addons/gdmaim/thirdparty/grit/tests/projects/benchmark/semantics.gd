extends RefCounted

const PRIMES := [2, 3, 5, 7]


static func wrapping_add(value: int) -> int:
	return value + 1


static func wrapping_multiply(value: int) -> int:
	return value * 3


static func divide(a: int, b: int) -> int:
	return a / b


static func modulo(a: int, b: int) -> int:
	return a % b


static func mixed_arithmetic(a: int, b: float) -> float:
	return a * b + a / b - b


static func comparisons(a: int, b: float) -> int:
	var flags := 0
	if a < b:
		flags += 1
	if a == b:
		flags += 2
	if a >= b:
		flags += 4
	if not (a > b) or a != 0:
		flags += 8
	return flags


static func choose(a: int) -> int:
	return a * 2 if a > 0 else -a


static func narrow(value: float) -> int:
	var result: int = value
	return result


static func widen(value: int) -> float:
	var result: float = value
	return result


static func truthiness(a: int, b: float) -> bool:
	return (a and b) or (not a and not b)


static func loop_control(limit: int) -> int:
	var total := 0
	for i in range(limit, 0, -3):
		if i % 2 == 0:
			continue
		if i < 5:
			break
		total += i
	return total


static func zero_step(step: int) -> int:
	var count := 0
	for i in range(0, 10, step):
		count += 1
	return count


static func nested_loops(size: int) -> int:
	var total := 0
	var i := 0
	while i < size:
		var j := 0
		while true:
			j += 1
			if j > i:
				break
			if j % 3 == 0:
				continue
			total += i * j
		i += 1
	return total


static func bit_operations(a: int, b: int) -> int:
	return ((a & b) | (a ^ b)) + (a << 3) + (b >> 1) + ~a + (-b >> 2)


static func untyped_passthrough(value):
	return value


static func packed_access(count: int) -> Array:
	var bytes := PackedByteArray()
	bytes.resize(count)
	var floats := PackedFloat32Array()
	floats.resize(count)
	var names := PackedStringArray()
	names.resize(count)
	for i in range(count):
		bytes[i] = i * 97 - 300
		floats[i] = i / 3.0
		names[-1 - i] = str(i)
	var total := 0
	var fraction := 0.0
	for i in range(count):
		total += bytes[-1 - i]
		fraction += floats[i]
	return [total, fraction, names[0], names[-1], ",".join(names)]


static func array_access(count: int) -> Array:
	var values: Array[Vector2] = []
	values.resize(count)
	var untyped := []
	untyped.resize(count)
	for i in range(count):
		values[i] = Vector2(i, -i)
		untyped[-1 - i] = i * 2
	var sum := Vector2()
	for i in range(count):
		sum += values[-1 - i] * 0.5
	var first: int = untyped[0]
	return [sum, first, untyped[-1], values[count - 1].y]


static func constant_access(index: int) -> int:
	var total := 0
	for i in range(PRIMES.size()):
		total += PRIMES[i] * PRIMES[-1 - i]
	return total + PRIMES[index]


static func rest_parameters(first: int, second := 10, ...rest: Array) -> Array:
	return [first, second, rest, rest.size()]


static func empty_values() -> Array:
	var rid := RID()
	var callable := Callable()
	var signal_value := Signal()
	return [rid.is_valid(), rid == RID(), callable.is_null(), signal_value.is_null(), callable == Callable()]


static func stack_probe() -> Array:
	var outer_marker := 1
	return stack_inner(outer_marker)


static func stack_inner(marker: int) -> Array:
	var frames := get_stack()
	return frames.slice(0, 3).map(func(frame): return [frame.function, frame.line, marker])


static func dynamic_numbers(a, b) -> Array:
	return [a + b, a - b, a * b, a / b, a < b, a <= b, a == b, a != b, a > b, a >= b]


static func dynamic_strings(a, b) -> Array:
	return [a + b, a < b, a == b, a != b]


static func builtin_calls() -> Array:
	var list := [3, 1, 4, 1, 5]
	var dict := {"a": 1, "b": 2}
	var text := "grit compiler"
	var typed: Dictionary[String, int] = {"x": 10}
	typed["y"] = 20
	dict["c"] = dict.get("a", 0) + dict.get("missing", 5)
	list.append(9)
	list.append_array([2, 6])
	return [list.find(1), list.find(1, 2), list.has(4), list.count(1), dict.get("c"), dict.get("zzz"), dict.has("b"), text.substr(5), text.substr(0, 4), text.find("c"), list.slice(1), typed.get("y"), typed["x"] + typed["y"], dict.keys(), dict["b"], list]


static func iteration() -> Array:
	var log := []
	var mixed := [1, "two", 3.5, null, Vector2(1, 2)]
	for value in mixed:
		log.append(value)
	var shrinking := [1, 2, 3, 4, 5, 6]
	for value in shrinking:
		log.append(value)
		shrinking.pop_back()
	var typed: Array[int] = [5, 6, 7, 8, 9]
	var total := 0
	for value in typed:
		if value == 6:
			continue
		if value == 9:
			break
		total += value
	log.append(total)
	for i in 4:
		log.append(i * 10)
	var none := 0
	for i in none:
		log.append("never")
	for i in -3:
		log.append("never")
	for value in PackedByteArray([250, 1, 2]):
		log.append(value)
	for value in PackedFloat32Array([0.1, 2.5]):
		log.append(value)
	for value in PackedStringArray(["a", "bc"]):
		log.append(value + "!")
	var sum := Vector2()
	for point in PackedVector2Array([Vector2(1, 2), Vector2(3, 4)]):
		sum += point
	log.append(sum)
	for x in 3:
		for y in [10, 20]:
			if y == 20 and x == 1:
				continue
			log.append(x + y)
	return log


static func matching(value) -> String:
	match value:
		1:
			return "one"
		1.5:
			return "one and a half"
		"text":
			return "text"
		[1, 2]:
			return "pair"
		null:
			return "null"
		_:
			return "other %d" % typeof(value)


static func matching_typed(state: int) -> String:
	match state:
		0:
			return "zero"
		2:
			return "two"
	return "none %d" % typeof(state)
