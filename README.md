# godogbg

The [GNU Backgammon](https://www.gnu.org/software/gnubg/) evaluation engine
packaged as a small, self-contained C library, intended to be wrapped as a
Godot GDExtension.

Give it a board and dice and it returns the best move. It also returns the
cube decision and the win/gammon probabilities. This is the same neural-net
engine and the same strength as gnubg itself (beginner through grandmaster).

- **No GUI and no external runtime dependencies.** glib is linked statically
  and its symbols are hidden. On Linux the shared library depends only on glibc.
- **Small C API** ([include/gnubg_api.h](include/gnubg_api.h)) with 16 functions.
- [gnubg/](gnubg/) is the **unmodified** GNU Backgammon 1.08.003 release.
  The CMake build compiles only the 17 engine files it needs, and all
  adaptation is in [port/](port/) and [src/](src/).

## Status

| Platform | Status |
|---|---|
| Linux x86_64 | Builds and is tested. Results are identical to gnubg 1.08.003. |
| Windows x86_64 (mingw-w64) | Planned |
| Android arm64 (NDK) | Planned |
| Godot GDExtension wrapper | Planned |

## Quick start (Linux)

```bash
sudo apt install build-essential cmake pkg-config libglib2.0-dev
cmake -S . -B build/linux-x86_64
cmake --build build/linux-x86_64 -j
build/linux-x86_64/gnubg-cli data best 4HPwATDgc/ABMA 3 1
#   31: 8/5 6/5
build/linux-x86_64/gnubg-cli data selfplay 42
```

See [docs/BUILDING.md](docs/BUILDING.md) for build options and the other
platforms.

## Using the API

```c
#include "gnubg_api.h"

gnubg_init("path/to/data");            /* directory containing gnubg.wd etc. */
gnubg_set_skill(GNUBG_SKILL_WORLDCLASS);

gnubg_board board;
gnubg_initial_board(board);

int dice[2];
gnubg_roll_dice(dice);                 /* or supply your own */

gnubg_move move;
gnubg_best_move(board, dice[0], dice[1], NULL, &move);
/* move.text = "8/5 6/5", move.from[] / move.to[] / move.count */

gnubg_apply_move(board, &move);
gnubg_swap_sides(board);               /* the other player is on roll */
```

### Board representation

`gnubg_board` is `int[2][25]`, the same as gnubg's internal board:

- `board[1]` holds the **player on roll** and `board[0]` the opponent.
- Each side counts from its own home: index `0..23` is that player's 1-point to
  24-point, and index `24` is the bar. Checkers not on the board are borne off.
- So `board[0][i]` sits on the same physical point as `board[1][23 - i]`.

After every turn, call `gnubg_swap_sides()` so that `board[1]` is again the
side on roll. Moves use the mover's numbering: `from` is 1..24 or 25 (bar),
and `to` is 1..24 or 0 (off).

The match and cube fields in `gnubg_match` follow the same convention: index or
owner `1` is the player on roll. Passing `NULL` means a money game with a
centred cube and with Jacoby and beavers **off**. Note that gnubg's own default
has both on.

### Runtime data

`gnubg_init()` loads files from the given directory. Ship [data/](data/) with
your game. In Godot, copy it from `res://` to `user://` first, because the
engine reads with plain `fopen`.

| File | Size | Required |
|---|---|---|
| `gnubg.wd` | 400 KB | yes: the neural-net weights |
| `gnubg_os0.bd` | 1.4 MB | recommended: one-sided bearoff DB (built in memory if missing, at a startup cost) |
| `gnubg_ts0.bd` | 6.6 MB | recommended: two-sided bearoff DB (exact bearoff play) |
| `met/Kazaross-XG2.xml` | 24 KB | match play only (falls back to a built-in table) |

### Threading

The library is single-threaded and not re-entrant. Call it from one thread at a
time, for example from a Godot `WorkerThreadPool` task so the UI doesn't stall.
A World Class move takes roughly 10–100 ms on a desktop CPU.

## License

GPL-3.0-or-later; see [LICENSE](LICENSE). This project is a derivative of
GNU Backgammon, Copyright (C) 1999–2024 Gary Wong and the gnubg authors. The
upstream sources, copyright notices and AUTHORS are kept in [gnubg/](gnubg/).

Because the engine is GPL, a game that links this library must also be
distributed under GPL-compatible terms, with source available.
