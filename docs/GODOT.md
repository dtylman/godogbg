# Godot addon

`godot/addons/godogbg` is a GDExtension that exposes the engine to GDScript as
the singleton `Gnubg`. It needs Godot 4.5 or newer (it's built with godot-cpp
4.5 and tested with Godot 4.7.2). Each platform ships one self-contained
library, with the engine and glib linked in statically.

| Platform | Status |
|---|---|
| Linux x86_64 | Tested headless in Godot 4.7.2, from the editor and from an exported `.pck` |
| Windows x86_64 | Tested with the Windows Godot 4.7.2 editor under wine |
| Android arm64-v8a | Builds (API 21+, 16 KB pages); not yet run inside an exported APK |

In every tested setup the seed-42 self-play game played through GDScript is
identical to `gnubg-cli` on the same platform.

## Building

```bash
scripts/build-godot-addon.sh linux-x86_64
scripts/build-godot-addon.sh windows-x86_64     # after scripts/build-glib.sh mingw-x86_64
ANDROID_NDK_HOME=... scripts/build-godot-addon.sh android-arm64   # after scripts/build-glib.sh android-arm64
```

Each platform is built twice, once per variant (`template_debug` for the
editor and debug exports, `template_release` for release exports), into
`build/godot-<platform>-<variant>`. The libraries land in
`godot/addons/godogbg/bin/`, and the engine data is copied to
`godot/addons/godogbg/data/`. Both directories are gitignored.

godot-cpp (tag `godot-4.5-stable`, pinned by hash) is downloaded at configure
time. `godot/build_profile.json` limits its generated bindings to the classes
the wrapper uses, which keeps each build under a minute. Add a class there if
the wrapper starts using one.

Under the hood this is the normal CMake build with `-DGNUBG_BUILD_GODOT=ON
-DGODOTCPP_TARGET=template_debug|template_release`.

## Using it in a game

1. Copy `godot/addons/godogbg/` (with `bin/` and `data/` built) into your
   project's `addons/`.
2. In every export preset, add `addons/godogbg/data/*` to **Resources >
   Filters to export non-resource files**. Without it the engine data is left
   out of the export and `init()` fails.
3. Call `Gnubg.init("res://addons/godogbg/data")` once at start-up, for
   example from an autoload.

When the project runs from the editor, `init()` reads the files straight from
the project directory. In an exported game `res://` is inside the `.pck` or
`.apk`, but the engine needs real files (it memory-maps the bearoff
databases). So on first run `init()` copies them, about 8.7 MB, to
`user://godogbg/data` and loads them from there. Later runs only check the
file sizes.

```gdscript
func _ready() -> void:
    if Gnubg.init("res://addons/godogbg/data") != OK:
        push_error("backgammon engine failed to start")
        return
    Gnubg.set_skill(Gnubg.SKILL_EXPERT)

    var board := Gnubg.initial_board()
    var move := Gnubg.best_move(board, 3, 1)
    print(move.text, " ", move.eval.equity)     # 8/5 6/5 0.219
    board = Gnubg.apply_move(board, move)
    board = Gnubg.swap_sides(board)             # now the other side is on roll
```

## Board

A board is a `PackedInt32Array` of 50 checker counts, seen from the player
on roll:

| Index | Contents |
|---|---|
| `0..23` | Opponent's checkers on its points 1..24 (counted from the opponent's home) |
| `24` | Opponent's checkers on the bar |
| `25..48` | Player on roll's checkers on its points 1..24 (counted from its own home) |
| `49` | Player on roll's checkers on the bar |

Each side counts from its own home board, so the same physical point has
different indices for the two sides: your point *n* is the opponent's point
*25 − n*. Checkers that have been borne off aren't stored; they're
15 minus the sum. After each turn, call `swap_sides()` to make the other
player the one on roll.

Moves use the mover's numbering: `from` is 1..24, or 25 for the bar, and
`to` is 1..24, or 0 for borne off.

## API

All methods are on the `Gnubg` singleton. Bad input (a wrong-size board,
invalid dice, calling before `init()`) logs an error and returns an empty
`Dictionary` or `PackedInt32Array`.

### Setup

| Method | Returns |
|---|---|
| `init(data_dir: String)` | `Error`. `data_dir` is a `res://`, `user://` or absolute path containing `gnubg.wd`. Calling it again after success does nothing. |
| `shutdown()` | Waits for pending async requests and frees the engine. Also runs automatically when the game exits. |
| `is_ready()` | `bool` |
| `get_version()` | `String`, e.g. `"1.08.003"` |
| `set_skill(skill)` | `Error`. One of `SKILL_BEGINNER`, `SKILL_CASUAL`, `SKILL_INTERMEDIATE`, `SKILL_ADVANCED`, `SKILL_EXPERT`, `SKILL_WORLDCLASS` (default), `SKILL_SUPREMO`, `SKILL_GRANDMASTER`. |

### Engine decisions

These take an optional `match` Dictionary. Leave it out for a money game
(centred cube, Jacoby off, beavers off).

| Method | Returns |
|---|---|
| `best_move(board, die1, die2, match = {})` | `{from: PackedInt32Array, to: PackedInt32Array, text: String, eval: Dictionary}`. `from` is empty if there is no legal move. `eval` is the position after the move, from the mover's side. |
| `cube_action(board, match = {})` | `{should_double, should_take, should_beaver: bool, equity_no_double, equity_double_take, equity_double_pass: float}` for the player on roll, before rolling. |
| `evaluate(board, match = {})` | `{win, win_gammon, win_backgammon, lose_gammon, lose_backgammon, equity: float}` for the player on roll, before rolling. `equity` is cubeful: money equity, or normalised match-winning chances in a match. |
| `best_move_async(board, die1, die2, match = {})` | `int` request id. The result arrives through the `best_move_completed(request_id, result)` signal. |
| `cube_action_async(board, match = {})` | `int` request id. The result arrives through `cube_action_completed(request_id, result)`. |

The `match` keys, all optional, follow `gnubg_match` in
[include/gnubg_api.h](../include/gnubg_api.h):

| Key | Meaning |
|---|---|
| `match_length` | `0` for a money game |
| `score` | `[opponent, player_on_roll]` |
| `cube_value` | 1, 2, 4, ... |
| `cube_owner` | `-1` centred, `0` opponent, `1` player on roll |
| `crawford` | `bool`: this is the Crawford game |
| `jacoby`, `beavers` | `bool`: money-game rules |

### Board helpers

| Method | Returns |
|---|---|
| `initial_board()` | The starting position |
| `apply_move(board, move)` | The new board. `move` is a `best_move()` result or any `{from, to}` Dictionary. Returns an empty array if the move is illegal. |
| `swap_sides(board)` | The board with the other player on roll |
| `game_status(board)` | `0` while the game is running. Otherwise 1, 2 or 3 points (single, gammon or backgammon) for whichever side has finished. |
| `position_id(board)` / `board_from_position_id(id)` | gnubg's 14-character position IDs, e.g. `"4HPwATDgc/ABMA"` |
| `seed(seed: int)` / `roll_dice()` | Built-in dice, `[d1, d2]`. Seed `0` seeds from the clock, and `init()` does that too. |

### Legal moves

`apply_move()` only checks that each step is physically possible. These
apply the full rules: as many dice as possible, the larger die if only one
can be played, four steps for a double. They don't need `init()`.

| Method | Returns |
|---|---|
| `legal_steps(start_board, die1, die2, played: Array = [])` | `Array` of `{from: int, to: int, die: int, hit: bool}`: every single step that can come next. `start_board` is the board at the **start of the turn**, and `played` lists the steps already made this turn, in order, as `{from, to}` Dictionaries (add `die` to say which die a step used; a `legal_steps()` entry works as is). Empty when the turn is over. If `played` isn't a legal start of the turn, logs an error and returns `[]`. |
| `legal_moves(board, die1, die2)` | `Array` of `{from: PackedInt32Array, to: PackedInt32Array, text: String}`: gnubg's list of legal full moves, one per resulting position. Empty when there is no legal move. |
| `is_legal_move(board, die1, die2, move: Dictionary)` | `bool`: whether `{from, to}` leads to the same position as one of `legal_moves()`. A step may cover several dice (`24/13` for 6-5), but a blot hit on the way needs its own step. An empty move is legal only when there is no legal move. |

`legal_steps()` is for a UI that plays one die at a time. A step is listed
if **any** order of a legal full move starts with `played` followed by that
step. gnubg's `legal_moves()` keeps one move per resulting position, so it
has `13/7 7/5` but not `13/11 11/5`; `legal_steps()` offers both paths.
Legality depends on the start of the turn: if both dice can only be used
when one is played first, the other one isn't offered first.

About `die`: a step bearing a checker off can use a die larger than the
point (from the highest point), so the same `from`/`to` can be listed once
for each die. Pass the one the player picked back in `played` (if `die` is
left out there, the smaller die that keeps the turn legal is assumed).
`to` is `0` for borne off and `from` is `25` for the bar. There are at most
30 steps at a time (15 points times 2 dice); 20 is the most seen in
practice. `legal_moves()` can return up to 3060 moves (618 seen in
practice).

```gdscript
var turn_start: PackedInt32Array   # board when the dice were rolled
var played := []                   # steps made this turn, for undo too
var board: PackedInt32Array        # what the player sees

# Targets for the checker the player tapped.
func targets(point: int) -> Array:
    return Gnubg.legal_steps(turn_start, dice[0], dice[1], played) \
        .filter(func(s): return s.from == point)

func play_step(step: Dictionary) -> void:     # one of targets()
    played.append(step)
    board = Gnubg.apply_move(board, {"from": [step.from], "to": [step.to]})
    if Gnubg.legal_steps(turn_start, dice[0], dice[1], played).is_empty():
        end_turn()                            # dice used as far as the rules allow

func undo_step() -> void:
    played.pop_back()
    board = turn_start
    for s in played:
        board = Gnubg.apply_move(board, {"from": [s.from], "to": [s.to]})
```

If `targets()` lists two steps to the same point (bearing off with either
die), prefer the smaller die.

## Threading

At world-class level a move takes tens of milliseconds, and some positions
take longer, so calling `best_move()` from `_process` can drop frames. Use the
async methods instead:

```gdscript
func _ready() -> void:
    Gnubg.best_move_completed.connect(_on_best_move)

func computer_turn() -> void:
    Gnubg.best_move_async(board, dice[0], dice[1])

func _on_best_move(_request_id: int, move: Dictionary) -> void:
    board = Gnubg.apply_move(board, move)
```

The async methods run on Godot's `WorkerThreadPool`, and their signals are
emitted on the main thread. Call them from the main thread. The engine
itself handles one request at a time, so further requests queue up behind
the running one. The synchronous methods are safe to call from any thread.
They wait if the engine is busy, except the board helpers, which never wait
for an evaluation.

## Tests

[godot/test/test_gnubg.gd](../godot/test/test_gnubg.gd) checks the reference
positions from `gnubg-cli`, the error handling and the async path, the
legal-move rules (bar, bearing off, one die only, doubles cut short, step
order) and step paths against `legal_moves()` on a self-play game, then
plays a full seed-42 game:

```bash
G=~/godot/Godot_v4.7.2-stable_linux.x86_64
$G --headless --path godot --import         # once: registers the extension
$G --headless --path godot --script res://test/test_gnubg.gd -- /tmp/selfplay.txt
diff /tmp/selfplay.txt <(build/linux-x86_64/gnubg-cli data selfplay 42)
```

On Windows, the same test runs with the Windows editor under wine. The
self-play matches `gnubg-cli.exe`, not Linux, because of the `qsort`
tie-break noted in [BUILDING.md](BUILDING.md):

```bash
export WINEDEBUG=-all WINEPREFIX=$PWD/build/wineprefix
wine Godot_v4.7.2-stable_win64_console.exe --headless --path "Z:$PWD/godot" \
    --script res://test/test_gnubg.gd -- 'Z:/tmp/selfplay-win.txt'
```

To test the exported-game path (data unpacked from the `.pck`) without
export templates, export only the pack and run it with a copy of the editor
binary placed next to the libraries:

```bash
mkdir -p /tmp/gtest && $G --headless --path godot --export-pack Linux /tmp/gtest/game.pck
cp $G /tmp/gtest/game && cp godot/addons/godogbg/bin/*.so /tmp/gtest/
(cd /tmp/gtest && ./game --headless --main-pack game.pck --script res://test/test_gnubg.gd)
```

## Licence

The addon contains GNU Backgammon, so it is GPL-3.0-or-later, and so is any
game that ships it.
