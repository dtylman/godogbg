/*
 * test-legal - checks the legal-move functions of gnubg_api against the
 * engine itself, on every roll of the positions from self-play games:
 *
 *   - gnubg_legal_moves() returns exactly what GenerateMoves() returns,
 *     in the same order;
 *   - every path built by repeatedly taking gnubg_legal_steps() ends in
 *     one of those positions, and every position is reached;
 *   - every order in which the steps of a legal move can physically be
 *     played (each step one die) is accepted step by step;
 *   - gnubg_is_legal_move() accepts every listed move.
 *
 *   test-legal DATADIR [GAMES]
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 3 or later of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "backgammon.h"
#include "eval.h"
#include "positionid.h"

#include "gnubg_api.h"

static int failures;
static long positions, rolls, paths, orders;

#define FAIL(...) do { failures++; if (failures <= 20) { printf("FAIL  " __VA_ARGS__); putchar('\n'); } } while (0)

static void
to_tan(const gnubg_board board, TanBoard anBoard)
{
    int i, j;

    for (i = 0; i < 2; i++)
        for (j = 0; j < 25; j++)
            anBoard[i][j] = (unsigned int) board[i][j];
}

static void
key_of(const gnubg_board board, positionkey *pkey)
{
    TanBoard anBoard;

    to_tan(board, anBoard);
    PositionKey((ConstTanBoard) anBoard, pkey);
}

static int
find_key(const positionkey *keys, int n, const positionkey *pkey)
{
    int i;

    for (i = 0; i < n; i++)
        if (EqualKeys(keys[i], *pkey))
            return i;
    return -1;
}

typedef struct {
    gnubg_board start;
    int d1, d2;
    const char *id;
    positionkey keys[GNUBG_MAX_MOVES];
    int reached[GNUBG_MAX_MOVES];
    int n;
} roll_case;

/* A turn state: the position and the dice left. */
static char *
state_key(const TanBoard anBoard, const int dice[], int n_dice)
{
    positionkey key;
    GString *s = g_string_new(NULL);
    int i;

    PositionKey(anBoard, &key);
    for (i = 0; i < 7; i++)
        g_string_append_printf(s, "%08x", key.data[i]);
    for (i = 0; i < n_dice; i++)
        g_string_append_printf(s, "%d", dice[i]);
    return g_string_free(s, FALSE);
}

/* Dice left after played, as gnubg_legal_steps() sees them. */
static int
dice_left(const roll_case *rc, const gnubg_step *played, int n_played, int dice[4])
{
    int n, i, k;

    dice[0] = MIN(rc->d1, rc->d2);
    dice[1] = MAX(rc->d1, rc->d2);
    dice[2] = dice[3] = rc->d1;
    n = rc->d1 == rc->d2 ? 4 : 2;
    for (i = 0; i < n_played; i++) {
        for (k = 0; k < n && dice[k] != played[i].die; k++);
        for (; k < n - 1; k++)
            dice[k] = dice[k + 1];
        n--;
    }
    return n;
}

/* Walk every path of gnubg_legal_steps() from played (each state once). */
static void
walk_steps(roll_case *rc, gnubg_board board, gnubg_step *played, int n_played, GHashTable *seen)
{
    gnubg_step steps[GNUBG_MAX_STEPS];
    TanBoard anBoard;
    int dice[4];
    int n, i;
    char *key;

    to_tan(board, anBoard);
    key = state_key((ConstTanBoard) anBoard, dice, dice_left(rc, played, n_played, dice));
    if (g_hash_table_contains(seen, key)) {
        g_free(key);
        return;
    }
    g_hash_table_add(seen, key);

    n = gnubg_legal_steps(rc->start, rc->d1, rc->d2, played, n_played, steps, GNUBG_MAX_STEPS);
    if (n < 0 || n > GNUBG_MAX_STEPS) {
        FAIL("%s %d%d: legal_steps returned %d after %d steps", rc->id, rc->d1, rc->d2, n, n_played);
        return;
    }

    if (n == 0) {
        positionkey key;
        int k;

        paths++;
        if (rc->n == 0) {
            if (n_played)
                FAIL("%s %d%d: steps played but no legal move", rc->id, rc->d1, rc->d2);
            return;
        }
        key_of(board, &key);
        if ((k = find_key(rc->keys, rc->n, &key)) < 0) {
            char id[15];
            gnubg_position_id(board, id);
            FAIL("%s %d%d: step path ends in %s, not a legal move", rc->id, rc->d1, rc->d2, id);
        } else
            rc->reached[k] = TRUE;
        return;
    }

    if (n_played == 4) {
        FAIL("%s %d%d: steps offered after 4", rc->id, rc->d1, rc->d2);
        return;
    }

    for (i = 0; i < n; i++) {
        gnubg_board next;
        gnubg_move m;

        memset(&m, 0, sizeof(m));
        m.count = 1;
        m.from[0] = steps[i].from;
        m.to[0] = steps[i].to;
        memcpy(next, board, sizeof(gnubg_board));
        if ((steps[i].from - steps[i].to != steps[i].die && steps[i].to != 0) ||
            gnubg_apply_move(next, &m) < 0) {
            FAIL("%s %d%d: bad step %d/%d (die %d)", rc->id, rc->d1, rc->d2, steps[i].from, steps[i].to,
                 steps[i].die);
            continue;
        }
        played[n_played] = steps[i];
        walk_steps(rc, next, played, n_played + 1, seen);
    }
}

/*
 * Brute force: play the dice in every order and every way, with only the
 * physical rules. Every step towards a sequence that uses the most pips
 * must be offered. Returns TRUE if such a sequence continues from here.
 */
static int max_pips_seen;

static int
check_orders(roll_case *rc, const TanBoard anBoard, int dice[], int n_dice, gnubg_step *played, int n_played,
             int pips, GHashTable *seen)
{
    gnubg_step steps[GNUBG_MAX_STEPS];
    int k, i, n, any = FALSE, ok = FALSE;
    char *key = state_key(anBoard, dice, n_dice);
    gpointer value;

    if (g_hash_table_lookup_extended(seen, key, NULL, &value)) {
        g_free(key);
        return GPOINTER_TO_INT(value);
    }

    n = gnubg_legal_steps(rc->start, rc->d1, rc->d2, played, n_played, steps, GNUBG_MAX_STEPS);

    for (k = 0; k < n_dice; k++) {
        int d = dice[k];

        if (k && d == dice[k - 1])
            continue;
        for (i = 24; i >= 0; i--) {
            TanBoard anNew;
            int rest[4], j, r = 0, nBack, dest = i - d, m;

            if (!anBoard[1][i] || (anBoard[1][24] && i != 24))
                continue;
            if (dest >= 0) {
                if (anBoard[0][23 - dest] >= 2)
                    continue;
            } else {
                for (nBack = 24; nBack > 0 && !anBoard[1][nBack]; nBack--);
                if (nBack > 5 || (dest != -1 && i != nBack))
                    continue;
            }
            any = TRUE;
            memcpy(anNew, anBoard, sizeof(anNew));
            ApplySubMove(anNew, i, d, TRUE);
            for (j = 0; j < n_dice; j++)
                if (j != k)
                    rest[r++] = dice[j];
            played[n_played].from = i + 1;
            played[n_played].to = dest < 0 ? 0 : dest + 1;
            played[n_played].die = d;
            if (!check_orders(rc, (ConstTanBoard) anNew, rest, n_dice - 1, played, n_played + 1, pips + d, seen))
                continue;
            ok = TRUE;
            orders++;
            for (m = 0; m < n; m++)
                if (steps[m].from == i + 1 && steps[m].to == played[n_played].to && steps[m].die == d)
                    break;
            if (m >= n)
                FAIL("%s %d%d: step %d (%d/%d die %d) of a legal order not offered", rc->id, rc->d1, rc->d2,
                     n_played + 1, i + 1, played[n_played].to, d);
        }
    }

    if (!any)
        ok = n_played && pips == max_pips_seen;
    g_hash_table_insert(seen, key, GINT_TO_POINTER(ok));
    return ok;
}

static void
check_roll(roll_case *rc)
{
    static gnubg_move moves[GNUBG_MAX_MOVES];
    TanBoard anBoard;
    movelist ml;
    gnubg_board board;
    gnubg_step played[4];
    GHashTable *seen;
    int dice[4], i, j;

    rolls++;
    to_tan(rc->start, anBoard);
    GenerateMoves(&ml, (ConstTanBoard) anBoard, rc->d1, rc->d2, FALSE);

    rc->n = gnubg_legal_moves(rc->start, rc->d1, rc->d2, moves, GNUBG_MAX_MOVES);
    if (rc->n != (int) ml.cMoves) {
        FAIL("%s %d%d: %d legal moves, GenerateMoves has %u", rc->id, rc->d1, rc->d2, rc->n, ml.cMoves);
        return;
    }

    max_pips_seen = 0;
    for (i = 0; i < rc->n; i++) {
        const move *pm = &ml.amMoves[i];

        for (j = 0; j < 4 && pm->anMove[2 * j] >= 0; j++)
            if (moves[i].from[j] != pm->anMove[2 * j] + 1 || moves[i].to[j] != pm->anMove[2 * j + 1] + 1)
                break;
        if (j != moves[i].count || (j < 4 && pm->anMove[2 * j] >= 0))
            FAIL("%s %d%d: move %d is %s, GenerateMoves differs", rc->id, rc->d1, rc->d2, i, moves[i].text);
        CopyKey(pm->key, rc->keys[i]);
        rc->reached[i] = FALSE;
        if (gnubg_is_legal_move(rc->start, rc->d1, rc->d2, &moves[i]) != 1)
            FAIL("%s %d%d: is_legal_move rejects %s", rc->id, rc->d1, rc->d2, moves[i].text);
        if ((int) pm->cPips > max_pips_seen)
            max_pips_seen = (int) pm->cPips;
    }

    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    memcpy(board, rc->start, sizeof(gnubg_board));
    walk_steps(rc, board, played, 0, seen);
    g_hash_table_remove_all(seen);
    for (i = 0; i < rc->n; i++)
        if (!rc->reached[i])
            FAIL("%s %d%d: %s not reachable by steps", rc->id, rc->d1, rc->d2, moves[i].text);

    if (rc->n) {
        dice[0] = MIN(rc->d1, rc->d2);
        dice[1] = MAX(rc->d1, rc->d2);
        dice[2] = dice[3] = rc->d1;
        check_orders(rc, (ConstTanBoard) anBoard, dice, rc->d1 == rc->d2 ? 4 : 2, played, 0, 0, seen);
    } else {
        gnubg_move none;

        memset(&none, 0, sizeof(none));
        if (gnubg_is_legal_move(rc->start, rc->d1, rc->d2, &none) != 1)
            FAIL("%s %d%d: empty move rejected with no legal move", rc->id, rc->d1, rc->d2);
    }
    g_hash_table_destroy(seen);
}

static void
check_position(const gnubg_board board)
{
    static roll_case rc;
    char id[15];

    positions++;
    gnubg_position_id(board, id);
    memcpy(rc.start, board, sizeof(gnubg_board));
    rc.id = id;
    for (rc.d1 = 1; rc.d1 <= 6; rc.d1++)
        for (rc.d2 = rc.d1; rc.d2 <= 6; rc.d2++)
            check_roll(&rc);
}

int
main(int argc, char *argv[])
{
    int games, g;

    if (argc < 2) {
        fprintf(stderr, "usage: %s DATADIR [GAMES]\n", argv[0]);
        return 2;
    }
    games = argc > 2 ? atoi(argv[2]) : 20;
    if (gnubg_init(argv[1]) < 0) {
        fprintf(stderr, "gnubg_init(%s) failed\n", argv[1]);
        return 1;
    }
    /* Beginner level: quick, and it reaches odd positions. */
    gnubg_set_skill(GNUBG_SKILL_BEGINNER);

    for (g = 1; g <= games; g++) {
        gnubg_board board;
        gnubg_move move;
        int dice[2];

        gnubg_seed((unsigned int) g);
        gnubg_initial_board(board);
        for (;;) {
            check_position(board);
            gnubg_roll_dice(dice);
            if (gnubg_best_move(board, dice[0], dice[1], NULL, &move) < 0)
                return 1;
            if (move.count)
                gnubg_apply_move(board, &move);
            if (gnubg_game_status(board))
                break;
            gnubg_swap_sides(board);
        }
    }

    printf("%ld positions, %ld rolls, %ld step paths, %ld step orders: %d failure(s)\n",
           positions, rolls, paths, orders, failures);
    gnubg_shutdown();
    return failures != 0;
}
