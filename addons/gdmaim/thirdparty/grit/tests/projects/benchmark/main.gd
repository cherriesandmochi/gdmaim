extends Node

const AsyncChecks = preload("res://async_checks.gd")
const BuiltinChecks = preload("res://builtin_checks.tscn")
const CallChecks = preload("res://call_checks.gd")
const Compute = preload("res://compute.gd")
const InnerChecks = preload("res://inner_checks.gd")
const PropertyChecks = preload("res://property_checks.gd")
const Semantics = preload("res://semantics.gd")
const Simulation = preload("res://simulation.gd")

var only := ""
var repeat := 1


func _ready() -> void:
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--only="):
			only = argument.trim_prefix("--only=")
		elif argument.begins_with("--repeat="):
			repeat = maxi(1, argument.trim_prefix("--repeat=").to_int())
	if only.is_empty():
		check_semantics()
		check_async()
	run_benchmarks()
	get_tree().quit()


func check_semantics() -> void:
	var loose_float = 7.9
	var loose_int = 3
	report("wrapping_add", Semantics.wrapping_add(9223372036854775807))
	report("wrapping_multiply", Semantics.wrapping_multiply(4611686018427387904))
	report("divide", [Semantics.divide(7, 2), Semantics.divide(-7, 2), Semantics.divide(7, -2)])
	report("divide_converted_argument", Semantics.divide(loose_float, 2))
	report("modulo", [Semantics.modulo(7, 3), Semantics.modulo(-7, 3), Semantics.modulo(7, -3)])
	report("mixed_arithmetic", Semantics.mixed_arithmetic(3, 0.5))
	report("comparisons", [Semantics.comparisons(1, 1.5), Semantics.comparisons(2, 2.0), Semantics.comparisons(0, -1.0)])
	report("choose", [Semantics.choose(5), Semantics.choose(-4)])
	report("narrow", [Semantics.narrow(3.99), Semantics.narrow(-3.99), Semantics.narrow(1e30), Semantics.narrow(NAN)])
	report("widen", Semantics.widen(loose_int))
	report("truthiness", [Semantics.truthiness(0, 0.0), Semantics.truthiness(1, 0.0), Semantics.truthiness(2, NAN)])
	report("loop_control", Semantics.loop_control(40))
	report("zero_step", [Semantics.zero_step(0), Semantics.zero_step(3), Semantics.zero_step(-1)])
	report("nested_loops", Semantics.nested_loops(12))
	report("bit_operations", Semantics.bit_operations(0x5A5A, 1234567))
	report("untyped_passthrough", [Semantics.untyped_passthrough("text"), Semantics.untyped_passthrough(42)])
	report("packed_access", Semantics.packed_access(10))
	report("array_access", Semantics.array_access(10))
	report("constant_access", [Semantics.constant_access(0), Semantics.constant_access(-1)])
	report("rest_parameters", [Semantics.rest_parameters(1), Semantics.rest_parameters(1, 2), Semantics.rest_parameters(1, 2, 3, "four")])
	report("empty_values", Semantics.empty_values())
	report("stack", Semantics.stack_probe())
	report("dynamic_numbers", [Semantics.dynamic_numbers(7, 2), Semantics.dynamic_numbers(7, 2.5), Semantics.dynamic_numbers(7.5, 2), Semantics.dynamic_numbers(-9, 4), Semantics.dynamic_numbers(3, 3)])
	report("dynamic_strings", [Semantics.dynamic_strings("a", "b"), Semantics.dynamic_strings([1], [2])])
	report("builtin_calls", Semantics.builtin_calls())
	report("iteration", Semantics.iteration())
	report("matching", [Semantics.matching(1), Semantics.matching(1.0), Semantics.matching(1.5), Semantics.matching("text"), Semantics.matching([1, 2]), Semantics.matching(null), Semantics.matching(Vector2()), Semantics.matching_typed(2), Semantics.matching_typed(5)])
	if OS.is_debug_build():
		report("divide_by_zero", Semantics.divide(1, 0))
		report("modulo_by_zero", Semantics.modulo(1, 0))


func check_async() -> void:
	var checks := AsyncChecks.new()
	checks.run_range_loop(3)
	for value in [10, 20, 30]:
		checks.step.emit(value)
	checks.run_each([Vector2(0.5, 1), Vector2(2, 3), Vector2(4, -1)])
	checks.step.emit(2)
	checks.step.emit(3)
	checks.run_text("a-b-c")
	checks.step.emit(7)
	checks.step.emit(8)
	checks.run_while(7)
	for value in [1, 2, 3, 4]:
		checks.step.emit(value)
	checks.run_chained()
	for value in [5, 6, 7]:
		checks.step.emit(value)
	checks.run_immediate(21)
	var static_output := []
	AsyncChecks.run_static(checks.step, static_output)
	checks.step.emit(11)
	checks.run_lambdas()
	checks.step.emit(99)
	checks.assert_lambdas()
	report("async_log", checks.log)
	report("async_static", static_output)
	report("inner_classes", InnerChecks.new().run())
	report("direct_calls", CallChecks.new().run())
	report("properties", PropertyChecks.new().run())
	var builtin = BuiltinChecks.instantiate()
	report("builtin_script", builtin.run())
	builtin.free()
	var builtin_resource: Variant = load("res://builtin_resource.tres")
	report("builtin_resource", builtin_resource.doubled())


func run_benchmarks() -> void:
	measure("sum_to", func() -> int: return Compute.sum_to(20000000))
	measure("collatz_steps", func() -> int: return Compute.collatz_steps(300000))
	measure("count_primes", func() -> int: return Compute.count_primes(200000))
	measure("mandelbrot", func() -> int: return Compute.mandelbrot(300, 200))

	var simulation := Simulation.new()
	add_child(simulation)
	measure_value("particles", func() -> Variant: return simulation.simulate_particles(2000, 300))
	measure_value("build_text", func() -> Variant: return simulation.build_text(200000))
	measure_value("count_keys", func() -> Variant: return simulation.count_keys(1000000))
	measure_value("spawn_nodes", func() -> Variant: return simulation.spawn_nodes(20000))
	measure_value("node_properties", func() -> Variant: return simulation.move_nodes(1000, 500))
	measure_value("self_properties", func() -> Variant: return simulation.move_self(500000))
	simulation.queue_free()


func report(name: String, value: Variant) -> void:
	print("result %s = %s" % [name, var_to_str(value)])


func is_selected(name: String) -> bool:
	return only.is_empty() or only == name


func measure(name: String, benchmark: Callable) -> void:
	if not is_selected(name):
		return
	var start := Time.get_ticks_usec()
	var value: int = benchmark.call()
	for i in range(repeat - 1):
		benchmark.call()
	var elapsed := (Time.get_ticks_usec() - start) / 1000.0
	print("result %s = %d" % [name, value])
	print("time %s = %.1f ms" % [name, elapsed])


func measure_value(name: String, benchmark: Callable) -> void:
	if not is_selected(name):
		return
	var start := Time.get_ticks_usec()
	var value: Variant = benchmark.call()
	for i in range(repeat - 1):
		benchmark.call()
	var elapsed := (Time.get_ticks_usec() - start) / 1000.0
	print("result %s = %s" % [name, var_to_str(value)])
	print("time %s = %.1f ms" % [name, elapsed])
