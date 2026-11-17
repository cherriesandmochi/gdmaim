extends Node2D

signal scored(score: int)
signal crashed(score: int)

enum State { READY, PLAYING, GAME_OVER }

const WORLD_SIZE := Vector2(480.0, 720.0)
const GROUND_HEIGHT := 80.0
const GRAVITY := 1500.0
const FLAP_VELOCITY := -460.0
const MAX_FALL_SPEED := 720.0
const BIRD_X := 130.0
const BIRD_RADIUS := 15.0
const PIPE_WIDTH := 76.0
const PIPE_GAP := 180.0
const PIPE_SPACING := 230.0
const PIPE_SPEED := 170.0
const GAP_MARGIN := 90.0
const RESTART_DELAY := 0.6

const SIMULATION_SEED := 20260922
const SIMULATION_STEPS := 120000
const SIMULATION_DELTA := 1.0 / 60.0

const SKY_COLOR := Color(0.44, 0.77, 0.93)
const PIPE_COLOR := Color(0.36, 0.75, 0.2)
const PIPE_EDGE_COLOR := Color(0.18, 0.45, 0.1)
const GROUND_COLOR := Color(0.87, 0.8, 0.52)
const GROUND_STRIPE_COLOR := Color(0.72, 0.64, 0.36)
const BIRD_COLOR := Color(0.99, 0.82, 0.2)
const BEAK_COLOR := Color(0.95, 0.45, 0.15)
const TEXT_COLOR := Color.WHITE
const OUTLINE_COLOR := Color(0.1, 0.1, 0.15)


class Pipe:
	var x: float
	var gap_center: float
	var passed := false

	func _init(p_x: float, p_gap_center: float) -> void:
		x = p_x
		gap_center = p_gap_center

	func gap_top() -> float:
		return gap_center - PIPE_GAP * 0.5

	func gap_bottom() -> float:
		return gap_center + PIPE_GAP * 0.5

	func top_rect() -> Rect2:
		return Rect2(x, -WORLD_SIZE.y, PIPE_WIDTH, gap_top() + WORLD_SIZE.y)

	func bottom_rect() -> Rect2:
		return Rect2(x, gap_bottom(), PIPE_WIDTH, WORLD_SIZE.y)

	func hits(center: Vector2, radius: float) -> bool:
		return circle_hits_rect(center, radius, top_rect()) or circle_hits_rect(center, radius, bottom_rect())

	static func circle_hits_rect(center: Vector2, radius: float, rect: Rect2) -> bool:
		var closest := center.clamp(rect.position, rect.end)
		return center.distance_squared_to(closest) < radius * radius


var state := State.READY
var bird_position := Vector2(BIRD_X, WORLD_SIZE.y * 0.4)
var bird_velocity := 0.0
var pipes: Array[Pipe] = []
var score := 0
var best_score := 0
var can_restart := true
var idle_time := 0.0
var ground_offset := 0.0
var score_flash := 0.0
var random := RandomNumberGenerator.new()
var is_autoplay := OS.get_cmdline_user_args().has("--autoplay")
var simulated_games := 0
var simulated_score := 0


func _ready() -> void:
	crashed.connect(func(final_score: int) -> void: best_score = maxi(best_score, final_score))
	scored.connect(func(_value: int) -> void: score_flash = 0.15)
	if DisplayServer.get_name() == "headless":
		run_simulation()
		get_tree().quit()
		return
	random.randomize()
	reset()


func _physics_process(delta: float) -> void:
	if is_autoplay and (autopilot_wants_flap() or state == State.GAME_OVER):
		flap()
	step(delta)
	queue_redraw()


func _unhandled_input(event: InputEvent) -> void:
	var mouse := event as InputEventMouseButton
	var touch := event as InputEventScreenTouch
	var pressed := event.is_action_pressed("ui_accept")
	pressed = pressed or (mouse != null and mouse.pressed and mouse.button_index == MOUSE_BUTTON_LEFT)
	pressed = pressed or (touch != null and touch.pressed)
	if pressed:
		flap()


func reset() -> void:
	state = State.READY
	bird_position = Vector2(BIRD_X, WORLD_SIZE.y * 0.4)
	bird_velocity = 0.0
	score = 0
	idle_time = 0.0
	pipes.clear()
	var x := WORLD_SIZE.x + 120.0
	for i in range(3):
		pipes.append(Pipe.new(x, random_gap_center()))
		x += PIPE_SPACING


func flap() -> void:
	match state:
		State.READY:
			state = State.PLAYING
			bird_velocity = FLAP_VELOCITY
		State.PLAYING:
			bird_velocity = FLAP_VELOCITY
		State.GAME_OVER:
			if can_restart:
				reset()


func step(delta: float) -> void:
	score_flash = maxf(score_flash - delta, 0.0)
	match state:
		State.READY:
			idle_time += delta
			bird_position.y = WORLD_SIZE.y * 0.4 + sin(idle_time * 5.0) * 6.0
			scroll_ground(delta)
		State.PLAYING:
			update_bird(delta)
			update_pipes(delta)
			scroll_ground(delta)
			if is_colliding():
				end_game()
		State.GAME_OVER:
			if bird_position.y + BIRD_RADIUS < ground_level():
				update_bird(delta)
				bird_position.y = minf(bird_position.y, ground_level() - BIRD_RADIUS)


func update_bird(delta: float) -> void:
	bird_velocity = minf(bird_velocity + GRAVITY * delta, MAX_FALL_SPEED)
	bird_position.y = maxf(bird_position.y + bird_velocity * delta, BIRD_RADIUS)


func update_pipes(delta: float) -> void:
	for pipe in pipes:
		pipe.x -= PIPE_SPEED * delta
		if not pipe.passed and pipe.x + PIPE_WIDTH < bird_position.x - BIRD_RADIUS:
			pipe.passed = true
			score += 1
			scored.emit(score)
	if pipes[0].x < -PIPE_WIDTH:
		var last := pipes[pipes.size() - 1]
		pipes.remove_at(0)
		pipes.append(Pipe.new(last.x + PIPE_SPACING, random_gap_center()))


func scroll_ground(delta: float) -> void:
	ground_offset = fposmod(ground_offset + PIPE_SPEED * delta, 24.0)


func is_colliding() -> bool:
	if bird_position.y + BIRD_RADIUS >= ground_level():
		return true
	return pipes.any(func(pipe: Pipe) -> bool: return pipe.hits(bird_position, BIRD_RADIUS))


func end_game() -> void:
	state = State.GAME_OVER
	crashed.emit(score)
	if DisplayServer.get_name() != "headless":
		block_restart()


func block_restart() -> void:
	can_restart = false
	await get_tree().create_timer(RESTART_DELAY).timeout
	can_restart = true


func ground_level() -> float:
	return WORLD_SIZE.y - GROUND_HEIGHT


func random_gap_center() -> float:
	var half_gap := PIPE_GAP * 0.5
	return random.randf_range(GAP_MARGIN + half_gap, ground_level() - GAP_MARGIN - half_gap)


func next_pipe() -> Pipe:
	for pipe in pipes:
		if pipe.x + PIPE_WIDTH >= bird_position.x - BIRD_RADIUS:
			return pipe
	return pipes[0]


func autopilot_wants_flap() -> bool:
	if state == State.READY:
		return true
	var target := next_pipe().gap_center + PIPE_GAP * 0.2
	return bird_position.y > target and bird_velocity > 0.0


func run_simulation() -> void:
	crashed.connect(func(final_score: int) -> void:
		simulated_games += 1
		simulated_score += final_score
	)
	random.seed = SIMULATION_SEED
	reset()
	var flaps := 0
	var start := Time.get_ticks_usec()
	for i in range(SIMULATION_STEPS):
		if state == State.GAME_OVER:
			reset()
		if autopilot_wants_flap():
			flap()
			flaps += 1
		step(SIMULATION_DELTA)
	var elapsed := (Time.get_ticks_usec() - start) / 1000.0
	var summary := [simulated_games, simulated_score, best_score, flaps, bird_position, bird_velocity]
	print("result flappy = %s" % var_to_str(summary))
	print("time flappy = %.1f ms" % elapsed)


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, WORLD_SIZE), SKY_COLOR)
	for pipe in pipes:
		draw_pipe(pipe)
	draw_ground()
	draw_bird()
	draw_hud()


func draw_pipe(pipe: Pipe) -> void:
	var cap_height := 24.0
	var cap_overhang := 5.0
	var top := pipe.top_rect()
	var bottom := pipe.bottom_rect()
	draw_rect(top, PIPE_COLOR)
	draw_rect(bottom, PIPE_COLOR)
	draw_rect(top, PIPE_EDGE_COLOR, false, 3.0)
	draw_rect(bottom, PIPE_EDGE_COLOR, false, 3.0)
	var top_cap := Rect2(pipe.x - cap_overhang, pipe.gap_top() - cap_height, PIPE_WIDTH + cap_overhang * 2.0, cap_height)
	var bottom_cap := Rect2(pipe.x - cap_overhang, pipe.gap_bottom(), PIPE_WIDTH + cap_overhang * 2.0, cap_height)
	for cap in [top_cap, bottom_cap]:
		draw_rect(cap, PIPE_COLOR)
		draw_rect(cap, PIPE_EDGE_COLOR, false, 3.0)


func draw_ground() -> void:
	var top := ground_level()
	draw_rect(Rect2(0.0, top, WORLD_SIZE.x, GROUND_HEIGHT), GROUND_COLOR)
	var x := -ground_offset
	while x < WORLD_SIZE.x:
		draw_rect(Rect2(x, top, 12.0, 10.0), GROUND_STRIPE_COLOR)
		x += 24.0
	draw_line(Vector2(0.0, top), Vector2(WORLD_SIZE.x, top), PIPE_EDGE_COLOR, 3.0)


func draw_bird() -> void:
	var angle := clampf(bird_velocity / MAX_FALL_SPEED, -0.4, 1.0) * 0.9
	draw_set_transform(bird_position, angle)
	draw_circle(Vector2.ZERO, BIRD_RADIUS, BIRD_COLOR)
	draw_circle(Vector2.ZERO, BIRD_RADIUS, OUTLINE_COLOR, false, 2.0)
	draw_circle(Vector2(-4.0, 3.0), 7.0, Color(1.0, 0.93, 0.6))
	draw_circle(Vector2(6.0, -5.0), 4.5, Color.WHITE)
	draw_circle(Vector2(7.5, -5.0), 2.0, OUTLINE_COLOR)
	draw_colored_polygon(PackedVector2Array([Vector2(12.0, -1.0), Vector2(22.0, 3.0), Vector2(12.0, 7.0)]), BEAK_COLOR)
	draw_set_transform(Vector2.ZERO)


func draw_hud() -> void:
	var score_size := 56 if score_flash > 0.0 else 48
	draw_centered_text(str(score), 90.0, score_size)
	match state:
		State.READY:
			draw_centered_text("Press Space or click to flap", WORLD_SIZE.y * 0.62, 22)
		State.GAME_OVER:
			draw_centered_text("Game Over", WORLD_SIZE.y * 0.36, 44)
			draw_centered_text("Best: %d" % best_score, WORLD_SIZE.y * 0.36 + 44.0, 26)
			if can_restart:
				draw_centered_text("Press Space to try again", WORLD_SIZE.y * 0.36 + 84.0, 20)


func draw_centered_text(text: String, y: float, size: int) -> void:
	var font := ThemeDB.fallback_font
	var position := Vector2(0.0, y)
	draw_string_outline(font, position, text, HORIZONTAL_ALIGNMENT_CENTER, WORLD_SIZE.x, size, 6, OUTLINE_COLOR)
	draw_string(font, position, text, HORIZONTAL_ALIGNMENT_CENTER, WORLD_SIZE.x, size, TEXT_COLOR)
