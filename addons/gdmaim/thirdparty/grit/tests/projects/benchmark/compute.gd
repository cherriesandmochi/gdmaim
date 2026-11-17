extends RefCounted


static func sum_to(n: int) -> int:
	var total := 0
	for i in range(n):
		total += i
	return total


static func collatz_steps(limit: int) -> int:
	var steps := 0
	for start in range(1, limit):
		var value := start
		while value != 1:
			if value % 2 == 0:
				value = value / 2
			else:
				value = 3 * value + 1
			steps += 1
	return steps


static func count_primes(limit: int) -> int:
	var count := 0
	for candidate in range(2, limit):
		var is_prime := true
		var divisor := 2
		while divisor * divisor <= candidate:
			if candidate % divisor == 0:
				is_prime = false
				break
			divisor += 1
		if is_prime:
			count += 1
	return count


static func mandelbrot(size: int, iterations: int) -> int:
	var inside := 0
	for y in range(size):
		for x in range(size):
			var c_real := x * 3.0 / size - 2.0
			var c_imag := y * 2.0 / size - 1.0
			var z_real := 0.0
			var z_imag := 0.0
			var n := 0
			while n < iterations and z_real * z_real + z_imag * z_imag <= 4.0:
				var next_real := z_real * z_real - z_imag * z_imag + c_real
				z_imag = 2.0 * z_real * z_imag + c_imag
				z_real = next_real
				n += 1
			if n == iterations:
				inside += 1
	return inside
