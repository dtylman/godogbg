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

	await legal_tests()

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


# A board from {point: checkers} for each side, in that side's own
# numbering (25 = bar).
func make_board(mine: Dictionary, opp: Dictionary) -> PackedInt32Array:
	var board := PackedInt32Array()
	board.resize(50)
	for p in opp:
		board[p - 1] = opp[p]
	for p in mine:
		board[25 + p - 1] = mine[p]
	return board


# Steps as sorted "from/to:die" strings ("*" for a hit), for comparing.
func steps_text(steps: Array) -> String:
	var parts := PackedStringArray()
	for s in steps:
		parts.append("%d/%d:%d%s" % [s.from, s.to, s.die, "*" if s.hit else ""])
	parts.sort()
	return " ".join(parts)


func check_steps(board: PackedInt32Array, d1: int, d2: int, played: Array, want: String,
		what: String) -> void:
	var got := steps_text(Gnubg.legal_steps(board, d1, d2, played))
	check(got == want, "%s: [%s]" % [what, got] if got == want
		else "%s: got [%s], want [%s]" % [what, got, want])


func legal_tests() -> void:
	var start := Gnubg.initial_board()

	# Opening rolls.
	check(Gnubg.legal_moves(start, 3, 1).size() == 16, "legal_moves 31: 16 moves")
	check(Gnubg.legal_moves(start, 6, 5).size() == 7, "legal_moves 65: 7 moves")
	check(Gnubg.legal_moves(start, 2, 2).size() == 75, "legal_moves 22: 75 moves")
	check_steps(start, 3, 1, [], "13/10:3 24/21:3 24/23:1 6/3:3 6/5:1 8/5:3 8/7:1", "steps 31")
	check_steps(start, 3, 1, [{"from": 13, "to": 10}], "10/9:1 24/23:1 6/5:1 8/7:1", "steps 31 after 13/10")
	check_steps(start, 1, 3, [{"from": 13, "to": 10}, {"from": 10, "to": 9}], "", "steps 31 done")
	check_steps(start, 6, 5, [], "13/7:6 13/8:5 24/18:6 8/2:6 8/3:5", "steps 65")
	check_steps(start, 6, 5, [{"from": 24, "to": 18}], "13/8:5 18/13:5 8/3:5", "steps 65 after 24/18")
	check_steps(start, 2, 2, [], "13/11:2 24/22:2 6/4:2 8/6:2", "steps 22")
	var three := [{"from": 24, "to": 22}, {"from": 22, "to": 20}, {"from": 13, "to": 11}]
	check_steps(start, 2, 2, three, "11/9:2 13/11:2 20/18:2 24/22:2 6/4:2 8/6:2", "steps 22 after 3")
	check_steps(start, 2, 2, three + [{"from": 6, "to": 4}], "", "steps 22 done after 4")

	# gnubg lists 13/11 11/5 and 13/7 7/5 as one move; both paths are offered.
	var to5 := 0
	for m in Gnubg.legal_moves(start, 6, 2):
		var b := Gnubg.apply_move(start, m)
		if b[25 + 12] == 4 and b[25 + 4] == 1:
			to5 += 1
	check(to5 == 1, "legal_moves 62: 13/5 listed once")
	check_steps(start, 6, 2, [{"from": 13, "to": 11}], "11/5:6 13/7:6 24/18:6 8/2:6", "steps 62 after 13/11")
	check_steps(start, 6, 2, [{"from": 13, "to": 7}], "13/11:2 24/22:2 6/4:2 7/5:2 8/6:2",
		"steps 62 after 13/7")

	# A blot is hit.
	var blot := make_board({24: 2, 13: 5, 8: 3, 6: 5}, {6: 5, 8: 3, 13: 4, 14: 1, 24: 2})
	check_steps(blot, 5, 2, [{"from": 13, "to": 8}], "13/11:2* 24/22:2 6/4:2 8/6:2", "hit flag 52")

	# Bar: one checker, the 6 can't enter (opponent's 6-point).
	var bar1 := make_board({25: 1, 13: 5, 8: 3, 6: 5, 24: 1}, {6: 5, 8: 3, 13: 5, 24: 2})
	check_steps(bar1, 6, 5, [], "25/20:5", "bar: enter with the 5 only")
	check_steps(bar1, 6, 5, [{"from": 25, "to": 20}], "13/7:6 20/14:6 24/18:6 8/2:6", "bar: then any 6")
	# Two on the bar, only the 5 enters: the turn ends after it.
	var bar2 := make_board({25: 2, 13: 5, 8: 3, 6: 5}, {6: 5, 8: 3, 13: 5, 24: 2})
	check_steps(bar2, 6, 5, [], "25/20:5", "bar 2: enter with the 5")
	check_steps(bar2, 6, 5, [{"from": 25, "to": 20}], "", "bar 2: then nothing")
	check(Gnubg.legal_moves(bar2, 6, 5).size() == 1, "bar 2: one legal move")
	# Neither die enters.
	var bar3 := make_board({25: 1, 13: 6, 8: 3, 6: 5}, {5: 4, 6: 4, 8: 2, 13: 5})
	check(Gnubg.legal_steps(bar3, 6, 5).is_empty() and Gnubg.legal_moves(bar3, 6, 5).is_empty(),
		"bar: no entry with 65")
	# Closed board.
	var closed := make_board({25: 1, 13: 14}, {1: 2, 2: 2, 3: 2, 4: 2, 5: 2, 6: 3, 13: 2})
	var none := true
	for d1 in range(1, 7):
		for d2 in range(d1, 7):
			none = none and Gnubg.legal_steps(closed, d1, d2).is_empty() \
				and Gnubg.legal_moves(closed, d1, d2).is_empty()
	check(none, "closed board: no steps or moves for any roll")
	check(Gnubg.is_legal_move(closed, 6, 6, {"from": [], "to": []}), "closed board: empty move is legal")

	# Only one die can be played (13/7/2 and 13/8/2 end on a made point):
	# it must be the 6.
	var one := make_board({13: 1, 1: 14}, {23: 2, 1: 13})
	check_steps(one, 6, 5, [], "13/7:6", "one die only: the larger")
	check(Gnubg.legal_moves(one, 6, 5).size() == 1, "one die only: one legal move")
	check(not Gnubg.is_legal_move(one, 6, 5, {"from": [13], "to": [8]}), "one die only: 13/8 illegal")

	# Both dice play only if the 6 goes 13/7 and the 1 goes 4/3: 13/12 is
	# possible but leaves the 6 unplayable (12/6 and 7/6 are blocked).
	var order := make_board({13: 1, 4: 1, 1: 13}, {19: 2, 1: 13})
	check_steps(order, 6, 1, [], "13/7:6 4/3:1", "one order: 13/12 not offered")
	check_steps(order, 6, 1, [{"from": 4, "to": 3}], "13/7:6", "one order: after 4/3")
	check(Gnubg.legal_steps(order, 6, 1, [{"from": 13, "to": 12}]).is_empty(), "one order: 13/12 refused")

	# Bearing off with a larger die: 3/off can use the 5 or the 6.
	var off := make_board({3: 1, 2: 1}, {19: 5, 1: 10})
	check_steps(off, 6, 5, [], "3/0:5 3/0:6", "bear off: one entry per die")
	check_steps(off, 6, 5, [{"from": 3, "to": 0}], "2/0:6", "bear off: 3/off took the 5")
	check_steps(off, 6, 5, [{"from": 3, "to": 0, "die": 6}], "2/0:5", "bear off: 3/off with the 6")
	# Both dice must be used: 6/1/off, not 6/off.
	var last := make_board({6: 1}, {19: 5, 1: 10})
	check_steps(last, 6, 5, [], "6/1:5", "bear off: use both dice")
	# Either die bears the last checker off: it must be the larger.
	var last1 := make_board({1: 1}, {19: 5, 1: 10})
	check_steps(last1, 6, 2, [], "1/0:6", "bear off: the larger die")

	# Doubles where only 1, 2 or 3 steps can be played.
	for n: int in [1, 2, 3]:
		var blocked: int = 13 - 2 * (n + 1)
		var b := make_board({13: 1, 1: 14}, {25 - blocked: 2, 1: 13})
		var played := []
		var counts := 0
		while true:
			var steps := Gnubg.legal_steps(b, 2, 2, played)
			if steps.is_empty():
				break
			played.append(steps[0])
			counts += 1
		var moves := Gnubg.legal_moves(b, 2, 2)
		check(counts == n and moves.size() == 1 and moves[0].from.size() == n,
			"22 with %d/%d blocked: %d step(s), move %s" % [blocked, blocked, counts,
				moves[0].text if moves.size() else "-"])

	# No legal move without the bar.
	var stuck := make_board({13: 1, 1: 14}, {13: 2, 1: 13})
	check(Gnubg.legal_steps(stuck, 1, 1).is_empty() and Gnubg.legal_moves(stuck, 1, 1).is_empty(),
		"no legal move: 11 against a block")

	# is_legal_move.
	check(Gnubg.is_legal_move(start, 3, 1, {"from": [8, 6], "to": [5, 5]}), "is_legal_move 8/5 6/5")
	check(Gnubg.is_legal_move(start, 3, 1, {"from": [6, 8], "to": [5, 5]}), "is_legal_move 6/5 8/5")
	check(Gnubg.is_legal_move(start, 1, 3, {"from": [8, 7], "to": [7, 4]}), "is_legal_move 8/7 7/4")
	check(Gnubg.is_legal_move(start, 6, 5, {"from": [24], "to": [13]}), "is_legal_move 24/13 as one step")
	check(not Gnubg.is_legal_move(start, 3, 1, {"from": [24], "to": [22]}), "is_legal_move 24/22 illegal")
	check(not Gnubg.is_legal_move(start, 6, 5, {"from": [13], "to": [7]}), "is_legal_move half a move")
	# 13/10 with 21 must go 13/11*/10 (the 12-point is blocked).
	check(not Gnubg.is_legal_move(blot, 2, 1, {"from": [13], "to": [10]}), "is_legal_move 13/10 skips the hit")
	check(Gnubg.is_legal_move(blot, 2, 1, {"from": [13, 11], "to": [11, 10]}), "is_legal_move 13/11* 11/10")
	check(not Gnubg.is_legal_move(start, 3, 1, {"from": [], "to": []}), "is_legal_move empty move")

	# Bad input.
	check(Gnubg.legal_steps(start, 3, 1, [{"from": 24, "to": 20}]).is_empty(), "illegal played step rejected")
	check(Gnubg.legal_steps(start, 3, 1, [{"from": 6, "to": 5}, {"from": 6, "to": 5}]).is_empty(),
		"die used twice rejected")
	check(Gnubg.legal_steps(start, 0, 7).is_empty() and Gnubg.legal_moves(start, 3, 9).is_empty(),
		"legal_*: bad dice rejected")
	check(Gnubg.legal_moves(PackedInt32Array([1]), 3, 1).is_empty(), "legal_moves: bad board rejected")

	# Consistency on a self-play game: the step paths end exactly in the
	# legal_moves positions, for every roll.
	Gnubg.seed(7)
	var b := Gnubg.initial_board()
	var positions := 0
	var mismatches := 0
	for ply in 40:
		for d1 in range(1, 7):
			for d2 in range(d1, 7):
				var want := {}
				for m in Gnubg.legal_moves(b, d1, d2):
					want[Gnubg.position_id(Gnubg.apply_move(b, m))] = true
				var got := {}
				walk_steps(b, d1, d2, [], b, got, {})
				if got != want:
					mismatches += 1
					print("      %s %d%d: steps reach %d positions, legal_moves has %d" % [
						Gnubg.position_id(b), d1, d2, got.size(), want.size()])
		positions += 1
		var dice := Gnubg.roll_dice()
		var move := Gnubg.best_move(b, dice[0], dice[1])
		if move.from.size():
			b = Gnubg.apply_move(b, move)
		if Gnubg.game_status(b) != 0:
			break
		b = Gnubg.swap_sides(b)
	check(mismatches == 0, "steps match legal_moves on %d positions x 21 rolls" % positions)

	# The board helpers don't wait for a running evaluation.
	Gnubg.set_skill(Gnubg.SKILL_GRANDMASTER)
	var busy := Gnubg.board_from_position_id("4HPwATDgc/ABMA")
	var request := Gnubg.best_move_async(busy, 6, 6)
	var t0 := Time.get_ticks_usec()
	var ok := Gnubg.legal_moves(start, 2, 2).size() == 75 \
		and steps_text(Gnubg.legal_steps(start, 3, 1)) == "13/10:3 24/21:3 24/23:1 6/3:3 6/5:1 8/5:3 8/7:1"
	var dt := Time.get_ticks_usec() - t0
	var args: Array = await Gnubg.best_move_completed
	var t1 := Time.get_ticks_usec()
	check(ok and args[0] == request and t1 - t0 > dt,
		"legal_* during best_move_async: %d us (evaluation %d us)" % [dt, t1 - t0])
	Gnubg.set_skill(Gnubg.SKILL_WORLDCLASS)


# Every path of legal_steps from (board, played), each state once; the end
# positions go into out.
func walk_steps(start: PackedInt32Array, d1: int, d2: int, played: Array, board: PackedInt32Array,
		out: Dictionary, seen: Dictionary) -> void:
	var dice := []
	for s in played:
		dice.append(s.die)
	dice.sort()
	var key := "%s %s" % [Gnubg.position_id(board), dice]
	if seen.has(key):
		return
	seen[key] = true
	var steps := Gnubg.legal_steps(start, d1, d2, played)
	if steps.is_empty():
		if played.size():
			out[Gnubg.position_id(board)] = true
		return
	for s in steps:
		var next := Gnubg.apply_move(board, {"from": [s.from], "to": [s.to]})
		walk_steps(start, d1, d2, played + [s], next, out, seen)
