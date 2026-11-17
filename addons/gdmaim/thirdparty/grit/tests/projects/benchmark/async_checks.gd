extends RefCounted

signal step(value: int)

var log: Array = []
var member_total := 0
static var static_lambdas := [func(): return "static"]; var instance_lambdas := [func(): return "instance"]


func range_loop(count: int) -> int:
	var total := 0
	for i in range(count):
		var value: int = await step
		total += value * (i + 1)
		log.append("range %d %d" % [i, value])
	return total


func run_range_loop(count: int) -> void:
	log.append(["range result", await range_loop(count)])


func run_each(items: Array[Vector2]) -> void:
	var sum := Vector2()
	for item in items:
		if item.x > 1.0:
			var scale: float = await step
			sum += item * scale
		else:
			sum += item
	log.append(["each", sum])


func run_text(text: String) -> void:
	var result := ""
	for character in text:
		if character == "-":
			result += str(await step)
		else:
			result += character
	log.append(["text", result])


func run_while(limit: int) -> void:
	var text := ""
	var index := 0
	while index < limit:
		index += 1
		if index % 2 == 0:
			continue
		text += str(await step)
	log.append(["while", text])


func run_chained() -> void:
	var first := await range_loop(2)
	var second: int = await step
	log.append(["chained", first * 100 + second])


func run_immediate(value: int) -> void:
	@warning_ignore("redundant_await")
	var result = await value
	log.append(["immediate", result * 2])


static func run_static(source: Signal, output: Array) -> void:
	var value = await source
	output.append(["static", value])


func run_lambdas() -> void:
	var offset := 3
	var add := func(x: int) -> int: return x + offset
	var twice := func(x): return x * 2
	log.append(["lambda calls", add.call(4), twice.call(5)])
	var numbers := [1, 2, 3, 4]
	log.append(["same line", numbers.map(func(x): return x * offset).filter(func(x): return x > 5)])
	member_total = 0
	numbers.map(func(x): member_total += x)
	log.append(["member lambda", member_total])
	var counter := [0]
	var increment := func(): counter[0] += 1
	increment.call()
	increment.call()
	log.append(["shared capture", counter])
	var nested := func(a): return func(b): return a * 10 + b
	log.append(["nested", nested.call(4).call(2)])
	log.append(["roots", instance_lambdas[0].call(), static_lambdas[0].call()])
	var async_lambda := func():
		var value = await step
		log.append(["async lambda", value])
	async_lambda.call()


func assert_lambdas() -> void:
	var numbers := [1, 2, 3]
	assert(numbers.all(func(x): return x > 0), "positive"); log.append(["after assert", numbers.map(func(x): return -x)])
