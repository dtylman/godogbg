/*
 * gnubg_api.h - minimal C API around the GNU Backgammon evaluation engine.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 3 or later of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * Board convention (same as GNU Backgammon's internal TanBoard):
 *
 *   board[0][i]  checkers of the opponent (the player NOT on roll)
 *   board[1][i]  checkers of the player on roll
 *
 *   Each side is indexed from its own point of view:
 *     i = 0 .. 23  that player's 1-point .. 24-point
 *     i = 24       that player's bar
 *   Checkers not on the board are borne off.
 *
 *   So the opponent's 1-point is the on-roll player's 24-point:
 *   board[0][i] faces board[1][23 - i].
 *
 * All match/cube fields use the same convention: index/owner 1 is the
 * player on roll, 0 is the opponent.
 *
 * The library is not thread safe: call it from one thread at a time.
 */

#ifndef GNUBG_API_H
#define GNUBG_API_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(GNUBG_API_BUILD_SHARED)
#define GNUBG_API __declspec(dllexport)
#elif defined(GNUBG_API_USE_SHARED)
#define GNUBG_API __declspec(dllimport)
#else
#define GNUBG_API
#endif
#else
#define GNUBG_API __attribute__((visibility("default")))
#endif

typedef int gnubg_board[2][25];

typedef enum {
    GNUBG_SKILL_BEGINNER = 0,
    GNUBG_SKILL_CASUAL,
    GNUBG_SKILL_INTERMEDIATE,
    GNUBG_SKILL_ADVANCED,
    GNUBG_SKILL_EXPERT,
    GNUBG_SKILL_WORLDCLASS,
    GNUBG_SKILL_SUPREMO,
    GNUBG_SKILL_GRANDMASTER
} gnubg_skill;

typedef struct {
    int match_length;   /* 0 = money game */
    int score[2];       /* match score; [1] = player on roll */
    int cube_value;     /* 1, 2, 4, ... */
    int cube_owner;     /* -1 = centred, 0 = opponent, 1 = player on roll */
    int crawford;       /* non-zero if this is the Crawford game */
    int jacoby;         /* money game: Jacoby rule */
    int beavers;        /* money game: beavers allowed */
} gnubg_match;

typedef struct {
    float win;              /* probability the player on roll wins */
    float win_gammon;       /* ... wins a gammon or better */
    float win_backgammon;   /* ... wins a backgammon */
    float lose_gammon;      /* ... loses a gammon or worse */
    float lose_backgammon;  /* ... loses a backgammon */
    float equity;           /* cubeful equity (money) or normalised MWC equity (match) */
} gnubg_eval;

typedef struct {
    int count;          /* checker moves (0 = no legal move) */
    int from[4];        /* 1..24, or 25 = bar (player-on-roll numbering) */
    int to[4];          /* 1..24, or 0 = borne off */
    char text[48];      /* e.g. "24/18 13/11*" */
    gnubg_eval eval;    /* evaluation after the move, from the mover's view */
} gnubg_move;

/* One checker step, using one die. */
typedef struct {
    int from;           /* 1..24, or 25 = bar */
    int to;             /* 1..24, or 0 = borne off */
    int die;            /* die value used, 1..6 (0 in input = any die that fits) */
    int hit;            /* output only: the step hits a blot */
} gnubg_step;

/* gnubg's bound on the number of legal moves for one roll. */
#define GNUBG_MAX_MOVES 3060
/* At most 15 occupied points times 2 die values. */
#define GNUBG_MAX_STEPS 30

typedef struct {
    int should_double;  /* the player on roll should offer a double now */
    int should_take;    /* the opponent should take if doubled */
    int should_beaver;  /* money game, beavers on: the opponent should beaver */
    float equity_no_double;
    float equity_double_take;
    float equity_double_pass;
} gnubg_cube_decision;

/*
 * Load weights and databases from data_dir, which must contain gnubg.wd
 * (or gnubg.weights). gnubg_os0.bd, gnubg_ts0.bd and met/Kazaross-XG2.xml
 * are optional but recommended. Returns 0 on success, <0 on error.
 */
GNUBG_API int gnubg_init(const char *data_dir);
GNUBG_API void gnubg_shutdown(void);
GNUBG_API const char *gnubg_version(void);

/* Default GNUBG_SKILL_WORLDCLASS. Returns 0, or -1 for an invalid level. */
GNUBG_API int gnubg_set_skill(gnubg_skill skill);

/* match may be NULL: money game, centred cube, no Jacoby, no beavers. */
GNUBG_API void gnubg_match_money(gnubg_match *match);

/*
 * Best checker play for the player on roll (board[1]) with the given dice.
 * Returns 0 on success (out->count == 0 means no legal move), <0 on error.
 */
GNUBG_API int gnubg_best_move(const gnubg_board board, int die1, int die2,
                              const gnubg_match *match, gnubg_move *out);

/* Cube action for the player on roll, before rolling. */
GNUBG_API int gnubg_cube_action(const gnubg_board board, const gnubg_match *match,
                                gnubg_cube_decision *out);

/* Evaluate the position for the player on roll, before rolling. */
GNUBG_API int gnubg_evaluate(const gnubg_board board, const gnubg_match *match, gnubg_eval *out);

/* Apply a move found by gnubg_best_move (or built by the caller). 0 = ok, -1 = illegal. */
GNUBG_API int gnubg_apply_move(gnubg_board board, const gnubg_move *move);

/*
 * Legal moves. These don't need gnubg_init(), and they only touch their
 * arguments, so they may run while another thread is in gnubg_best_move().
 */

/*
 * All legal moves for the player on roll, one per resulting position, in
 * the order gnubg generates them (from, to and text filled in; eval zero).
 * Returns the number of legal moves (0 = no legal move) and writes the
 * first max_out of them to out (which may be NULL if max_out is 0), or
 * returns -1 for a bad board or dice.
 */
GNUBG_API int gnubg_legal_moves(const gnubg_board board, int die1, int die2,
                                gnubg_move *out, int max_out);

/*
 * 1 if move leads to the same position as one of gnubg_legal_moves() (an
 * empty move is legal only when there is no legal move), 0 if not, -1 for
 * a bad board or dice. A step may cover several dice, e.g. 24/13 for 6-5,
 * but a blot hit on the way needs its own step (24/18 18/13).
 */
GNUBG_API int gnubg_is_legal_move(const gnubg_board board, int die1, int die2,
                                 const gnubg_move *move);

/*
 * The single steps that may come next in a turn played one die at a time.
 * start is the board at the start of the turn and played the n_played
 * steps already made (from, to, and die or 0 for any). A step is listed
 * if some legal full move starts with played followed by that step, in
 * any order of its steps; a from/to pair that two dice can make (bearing
 * off) is listed once per die. Steps are sorted by from (high first),
 * then die (low first).
 *
 * Returns the number of steps (0 = the turn is over, at most
 * GNUBG_MAX_STEPS) and writes the first max_out of them to out; -1 for a
 * bad board or dice, -2 if played is not a legal start of the turn.
 */
GNUBG_API int gnubg_legal_steps(const gnubg_board start, int die1, int die2,
                                const gnubg_step *played, int n_played,
                                gnubg_step *out, int max_out);

/* Swap sides, i.e. pass the turn: board[0] <-> board[1]. */
GNUBG_API void gnubg_swap_sides(gnubg_board board);

/* 0 = game running, 1 = single win, 2 = gammon, 3 = backgammon (for whichever side finished). */
GNUBG_API int gnubg_game_status(const gnubg_board board);

/* Standard starting position. */
GNUBG_API void gnubg_initial_board(gnubg_board board);

/* GNU Backgammon position IDs (14 characters, e.g. "4HPwATDgc/ABMA"). */
GNUBG_API int gnubg_board_from_position_id(const char *id, gnubg_board board);
GNUBG_API int gnubg_position_id(const gnubg_board board, char out[15]);

/* Built-in dice (Mersenne Twister). Seed 0 seeds from the clock. */
GNUBG_API void gnubg_seed(unsigned int seed);
GNUBG_API void gnubg_roll_dice(int dice[2]);

#ifdef __cplusplus
}
#endif

#endif /* GNUBG_API_H */
