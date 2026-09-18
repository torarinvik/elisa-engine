extends SceneTree

# Godot-side embedding probe: the same maze game the native host drives through
# the Elisa C archive, this time through a hand-written GDExtension class.
# The full session (start, moves, hazard rules, world query, win route) must
# agree with the native embedding, which proves one Elisa world behind both
# host families.

func check_equal(what: String, got: int, expected: int) -> bool:
	if got != expected:
		push_error("embed godot: %s is %d, expected %d" % [what, got, expected])
		return false
	return true

func _initialize() -> void:
	call_deferred("_run_probe")

func _run_probe() -> void:
	if not ClassDB.class_exists("ElisaMaze"):
		push_error("embed godot: ElisaMaze extension class is not registered")
		quit(1)
		return
	if not check_equal("start", ElisaMaze.maze_start(), 1):
		quit(1)
		return
	if not check_equal("start_x", ElisaMaze.maze_player_x(), 1):
		quit(1)
		return
	if not check_equal("start_y", ElisaMaze.maze_player_y(), 1):
		quit(1)
		return
	if not check_equal("east moved", ElisaMaze.maze_step(3), 1):
		quit(1)
		return
	if not check_equal("after east x", ElisaMaze.maze_player_x(), 2):
		quit(1)
		return
	if not check_equal("north blocked", ElisaMaze.maze_step(0), 0):
		quit(1)
		return
	if not check_equal("status", ElisaMaze.maze_status(), 1):
		quit(1)
		return
	if not check_equal("lives", ElisaMaze.maze_lives(), 3):
		quit(1)
		return
	if not check_equal("width", ElisaMaze.maze_width(), 8):
		quit(1)
		return
	if not check_equal("height", ElisaMaze.maze_height(), 8):
		quit(1)
		return
	if not check_equal("walls", ElisaMaze.maze_wall_count(), 34):
		quit(1)
		return
	if not check_equal("goal_x", ElisaMaze.maze_goal_x(), 6):
		quit(1)
		return
	if not check_equal("goal_y", ElisaMaze.maze_goal_y(), 6):
		quit(1)
		return
	if not check_equal("wall(0,1)", ElisaMaze.maze_is_wall(0, 1), 1):
		quit(1)
		return
	if not check_equal("wall(1,1)", ElisaMaze.maze_is_wall(1, 1), 0):
		quit(1)
		return

	# Live input, the same path the game uses: a synthetic key event is parsed
	# by the host, mapped to the portable move code, and drives Elisa gameplay
	# through the extension. The native embedding maps SDL keys the same way.
	var keycodes := {"KeyW": KEY_W, "KeyA": KEY_A, "KeyS": KEY_S, "KeyD": KEY_D}
	var move_codes := {"KeyW": 0, "KeyA": 2, "KeyS": 1, "KeyD": 3}
	ElisaMaze.maze_start()
	var pressed := 0
	for name in keycodes:
		var event := InputEventKey.new()
		event.keycode = keycodes[name]
		event.physical_keycode = keycodes[name]
		event.pressed = true
		Input.parse_input_event(event)
		Input.flush_buffered_events()
		if Input.is_key_pressed(keycodes[name]):
			pressed += 1
	if not check_equal("input pressed", pressed, keycodes.size()):
		quit(1)
		return
	if not check_equal("live east moved", ElisaMaze.maze_step(move_codes["KeyD"]), 1):
		quit(1)
		return
	if not check_equal("live after east x", ElisaMaze.maze_player_x(), 2):
		quit(1)
		return
	print("embed godot live input: keys=%d move_code=%d player_x=%d" % [
		pressed, move_codes["KeyD"], ElisaMaze.maze_player_x()])

	ElisaMaze.maze_start()
	for step in range(4):
		ElisaMaze.maze_step(1)
	if not check_equal("hazard moved", ElisaMaze.maze_step(3), 1):
		quit(1)
		return
	if not check_equal("lives after hazard", ElisaMaze.maze_lives(), 2):
		quit(1)
		return
	if not check_equal("reset x", ElisaMaze.maze_player_x(), 1):
		quit(1)
		return
	if not check_equal("reset y", ElisaMaze.maze_player_y(), 1):
		quit(1)
		return
	ElisaMaze.maze_start()
	for step in range(5):
		ElisaMaze.maze_step(1)
	for step in range(5):
		ElisaMaze.maze_step(3)
	if not check_equal("won status", ElisaMaze.maze_status(), 3):
		quit(1)
		return
	if not check_equal("won x", ElisaMaze.maze_player_x(), 6):
		quit(1)
		return
	if not check_equal("won y", ElisaMaze.maze_player_y(), 6):
		quit(1)
		return
	print("embed godot: full session agrees with the native embedding")
	quit(0)
