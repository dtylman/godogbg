# Headless test of the Gnubg singleton. Run from the repo root:
#
#   godot --headless --path godot --import
#   godot --headless --path godot --script res://test/test_gnubg.gd -- [selfplay.txt]
#
# Exits with status 0 if every check passes. If a file name is given, the
# seed-42 self-play is written there in gnubg-cli's format, for diffing
# against `gnubg-cli data selfplay 42`.
extends SceneTree

var failures := 0


func check(cond: bool, what: String) -> void:
	if cond:
		print("ok    ", what)
	else:
		print("FAIL  ", what)
		failures += 1


func _initialize() -> void:
	run()


func run() -> void:
	print("gnubg ", Gnubg.get_version())
	check(Gnubg.init("res://addons/godogbg/data") == OK, "init")
	check(Gnubg.is_ready(), "is_ready")
	if not Gnubg.is_ready():
		quit(1)
		return

	var board := Gnubg.board_from_position_id("4HPwATDgc/ABMA")
	check(board.size() == 50, "board_from_position_id")
	check(Gnubg.position_id(board) == "4HPwATDgc/ABMA", "position_id round trip")

	var move := Gnubg.best_move(board, 3, 1)
	check(move.text == "8/5 6/5", "best_move 31: %s" % move.text)
	check(move.from == PackedInt32Array([8, 6]) and move.to == PackedInt32Array([5, 5]),
		"best_move from/to: %s -> %s" % [move.from, move.to])
	check(is_equal_approx(snappedf(move.eval.equity, 0.001), 0.219),
		"best_move equity %.3f" % move.eval.equity)

	var cube := Gnubg.cube_action(Gnubg.board_from_position_id("2D2GAQaMZ+EANA"))
	check(not cube.should_double and cube.should_take, "cube: no double, take")
	check(is_equal_approx(snappedf(cube.equity_no_double, 0.001), -0.922)
		and is_equal_approx(snappedf(cube.equity_double_take, 0.001), -1.855)
		and is_equal_approx(snappedf(cube.equity_double_pass, 0.001), 1.0),
		"cube equities %.3f %.3f %.3f" % [cube.equity_no_double, cube.equity_double_take,
			cube.equity_double_pass])

	# Upstream gnubg: 2-ply 0.525 win, cubeful +0.105 for the side on roll.
	var e := Gnubg.evaluate(Gnubg.initial_board())
	check(is_equal_approx(snappedf(e.win, 0.001), 0.525)
		and is_equal_approx(snappedf(e.equity, 0.001), 0.105),
		"evaluate opening: win %.3f equity %.3f" % [e.win, e.equity])

	var match := {"match_length": 7, "score": [3, 5], "cube_value": 2, "cube_owner": 0}
	check(not Gnubg.best_move(board, 3, 1, match).is_empty(), "best_move in a match")

	# Bad input: errors, not crashes.
	check(Gnubg.best_move(PackedInt32Array([1, 2, 3]), 3, 1).is_empty(), "bad board rejected")
	check(Gnubg.best_move(board, 0, 7).is_empty(), "bad dice rejected")
	check(Gnubg.apply_move(board, {"from": [24], "to": [3]}).is_empty(), "illegal move rejected")
	check(Gnubg.set_skill(99) != OK, "bad skill rejected")

	# Async: the result arrives through the signal, on the main thread.
	var request := Gnubg.best_move_async(board, 3, 1)
	var args: Array = await Gnubg.best_move_completed
	check(args[0] == request and args[1].text == "8/5 6/5", "best_move_async")
	request = Gnubg.cube_action_async(Gnubg.board_from_position_id("2D2GAQaMZ+EANA"))
	args = await Gnubg.cube_action_completed
	check(args[0] == request and args[1].should_take, "cube_action_async")

	var out := selfplay(42)
	check(out[-1] == "O wins 1 point", "selfplay 42: %s" % out[-1])
	var user_args := OS.get_cmdline_user_args()
	if user_args.size() > 0:
		var f := FileAccess.open(user_args[0], FileAccess.WRITE)
		f.store_string("\n".join(out) + "\n")

	check(Gnubg.set_skill(Gnubg.SKILL_EXPERT) == OK, "set_skill expert")
	check(not Gnubg.best_move(board, 6, 6).is_empty(), "best_move at expert")
	Gnubg.shutdown()
	check(not Gnubg.is_ready(), "shutdown")

	print("%d failure(s)" % failures)
	quit(1 if failures else 0)


# The same game loop as `gnubg-cli DATADIR selfplay SEED`.
func selfplay(seed: int) -> PackedStringArray:
	var lines := PackedStringArray()
	Gnubg.seed(seed)
	var board := Gnubg.initial_board()
	var dice := Gnubg.roll_dice()
	while dice[0] == dice[1]:
		dice = Gnubg.roll_dice()
	var turn := 0 if dice[0] > dice[1] else 1
	var ply := 0
	while true:
		var move := Gnubg.best_move(board, dice[0], dice[1])
		ply += 1
		lines.append("%3d  %s  %s  %d%d: %s" % [ply, "XO"[turn], Gnubg.position_id(board),
			dice[0], dice[1], move.text if move.from.size() else "(no move)"])
		if move.from.size():
			board = Gnubg.apply_move(board, move)
		var status := Gnubg.game_status(board)
		if status != 0:
			lines.append("%s wins %d point%s" % ["XO"[turn], status, "s" if status > 1 else ""])
			return lines
		board = Gnubg.swap_sides(board)
		turn = 1 - turn
		dice = Gnubg.roll_dice()
	return lines
