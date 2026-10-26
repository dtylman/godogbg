/*
 * gnubg-cli - test driver and usage example for the gnubg_api library.
 *
 *   gnubg-cli DATADIR best POSITION_ID DIE1 DIE2
 *   gnubg-cli DATADIR cube POSITION_ID
 *   gnubg-cli DATADIR eval POSITION_ID
 *   gnubg-cli DATADIR selfplay [SEED]
 *
 * Set GNUBG_SKILL=0..7 (beginner..grandmaster) to change the playing level.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 3 or later of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gnubg_api.h"

static void
print_eval(const gnubg_eval *e)
{
    printf("  win %.3f  W(g) %.3f  W(bg) %.3f  L(g) %.3f  L(bg) %.3f  equity %+.3f\n",
           e->win, e->win_gammon, e->win_backgammon, e->lose_gammon, e->lose_backgammon, e->equity);
}

static int
cmd_best(const char *id, int d1, int d2)
{
    gnubg_board board;
    gnubg_move move;

    if (gnubg_board_from_position_id(id, board) < 0) {
        fprintf(stderr, "invalid position id: %s\n", id);
        return 1;
    }
    if (gnubg_best_move(board, d1, d2, NULL, &move) < 0) {
        fprintf(stderr, "gnubg_best_move failed\n");
        return 1;
    }
    printf("%d%d: %s\n", d1, d2, move.count ? move.text : "(no legal move)");
    if (move.count)
        print_eval(&move.eval);
    return 0;
}

static int
cmd_cube(const char *id)
{
    gnubg_board board;
    gnubg_cube_decision cd;

    if (gnubg_board_from_position_id(id, board) < 0) {
        fprintf(stderr, "invalid position id: %s\n", id);
        return 1;
    }
    if (gnubg_cube_action(board, NULL, &cd) < 0) {
        fprintf(stderr, "gnubg_cube_action failed\n");
        return 1;
    }
    printf("double: %s, take: %s%s\n", cd.should_double ? "yes" : "no",
           cd.should_take ? "yes" : "no", cd.should_beaver ? " (beaver)" : "");
    printf("  no double %+.3f  double/take %+.3f  double/pass %+.3f\n",
           cd.equity_no_double, cd.equity_double_take, cd.equity_double_pass);
    return 0;
}

static int
cmd_eval(const char *id)
{
    gnubg_board board;
    gnubg_eval e;

    if (gnubg_board_from_position_id(id, board) < 0) {
        fprintf(stderr, "invalid position id: %s\n", id);
        return 1;
    }
    if (gnubg_evaluate(board, NULL, &e) < 0) {
        fprintf(stderr, "gnubg_evaluate failed\n");
        return 1;
    }
    print_eval(&e);
    return 0;
}

/* Computer vs computer money game without cube: the loop a game front-end runs. */
static int
cmd_selfplay(unsigned int seed)
{
    gnubg_board board;
    gnubg_move move;
    char id[15];
    int dice[2];
    int turn = 0, ply = 0, status;

    gnubg_seed(seed);
    gnubg_initial_board(board);

    /* Opening roll: no doubles, higher die moves first. */
    do
        gnubg_roll_dice(dice);
    while (dice[0] == dice[1]);
    turn = dice[0] > dice[1] ? 0 : 1;

    for (;;) {
        if (gnubg_best_move(board, dice[0], dice[1], NULL, &move) < 0)
            return 1;
        gnubg_position_id(board, id);
        printf("%3d  %c  %s  %d%d: %s\n", ++ply, "XO"[turn], id, dice[0], dice[1],
               move.count ? move.text : "(no move)");
        if (move.count && gnubg_apply_move(board, &move) < 0) {
            fprintf(stderr, "engine produced an illegal move\n");
            return 1;
        }
        if ((status = gnubg_game_status(board)) != 0) {
            printf("%c wins %d point%s\n", "XO"[turn], status, status > 1 ? "s" : "");
            return 0;
        }
        gnubg_swap_sides(board);
        turn = !turn;
        gnubg_roll_dice(dice);
    }
}

int
main(int argc, char *argv[])
{
    const char *skill;
    int rc;

    if (argc < 3) {
        fprintf(stderr,
                "usage: %s DATADIR best POSITION_ID DIE1 DIE2\n"
                "       %s DATADIR cube POSITION_ID\n"
                "       %s DATADIR eval POSITION_ID\n"
                "       %s DATADIR selfplay [SEED]\n", argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }

    if (gnubg_init(argv[1]) < 0) {
        fprintf(stderr, "gnubg_init(%s) failed\n", argv[1]);
        return 1;
    }
    if ((skill = getenv("GNUBG_SKILL")) && gnubg_set_skill((gnubg_skill) atoi(skill)) < 0) {
        fprintf(stderr, "invalid GNUBG_SKILL: %s\n", skill);
        return 2;
    }

    if (!strcmp(argv[2], "best") && argc == 6)
        rc = cmd_best(argv[3], atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(argv[2], "cube") && argc == 4)
        rc = cmd_cube(argv[3]);
    else if (!strcmp(argv[2], "eval") && argc == 4)
        rc = cmd_eval(argv[3]);
    else if (!strcmp(argv[2], "selfplay") && argc <= 4)
        rc = cmd_selfplay(argc == 4 ? (unsigned int) strtoul(argv[3], NULL, 10) : 0);
    else {
        fprintf(stderr, "bad command\n");
        rc = 2;
    }

    gnubg_shutdown();
    return rc;
}
