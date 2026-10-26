/*
 * gnubg_api.c - minimal C API around the GNU Backgammon evaluation engine.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 3 or later of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <glib.h>

#include "backgammon.h"
#include "eval.h"
#include "matchequity.h"
#include "multithread.h"
#include "positionid.h"
/* simd.h first: SFMT.h #undefs HAVE_NEON, which simd.h needs. */
#include "lib/simd.h"
#include "lib/SFMT.h"

#include "gnubg_api.h"

/* port/hooks.c */
extern void gnubg_port_set_datadir(const char *dir);

static int initialised = FALSE;
static evalcontext ecCurrent;
static movefilter (*amfCurrent)[MAX_FILTER_PLIES];
static sfmt_t sfmt;

/*
 * Conversions between the public int board and the engine's TanBoard.
 * They have the same layout; only the element type differs.
 */

static int
to_tan(const gnubg_board board, TanBoard anBoard)
{
    int i, j, n;

    for (i = 0; i < 2; i++) {
        n = 0;
        for (j = 0; j < 25; j++) {
            if (board[i][j] < 0)
                return -1;
            anBoard[i][j] = (unsigned int) board[i][j];
            n += board[i][j];
        }
        if (n > 15)
            return -1;
    }
    return 0;
}

static void
from_tan(const TanBoard anBoard, gnubg_board board)
{
    int i, j;

    for (i = 0; i < 2; i++)
        for (j = 0; j < 25; j++)
            board[i][j] = (int) anBoard[i][j];
}

static int
make_cubeinfo(const gnubg_match *match, cubeinfo *pci)
{
    gnubg_match m;

    if (match)
        m = *match;
    else
        gnubg_match_money(&m);

    if (m.cube_owner < -1 || m.cube_owner > 1)
        return -1;

    return SetCubeInfo(pci, m.cube_value, m.cube_owner, 1, m.match_length, m.score,
                       m.crawford, m.jacoby, m.beavers, VARIATION_STANDARD);
}

/* Cubeful outputs are match winning chances in match play; report equity. */
static float
cubeful_equity(float r, const cubeinfo *pci)
{
    return pci->nMatchTo ? mwc2eq(r, pci) : r;
}

static void
fill_eval(gnubg_eval *pe, const float ar[NUM_ROLLOUT_OUTPUTS], const cubeinfo *pci)
{
    pe->win = ar[OUTPUT_WIN];
    pe->win_gammon = ar[OUTPUT_WINGAMMON];
    pe->win_backgammon = ar[OUTPUT_WINBACKGAMMON];
    pe->lose_gammon = ar[OUTPUT_LOSEGAMMON];
    pe->lose_backgammon = ar[OUTPUT_LOSEBACKGAMMON];
    pe->equity = cubeful_equity(ar[OUTPUT_CUBEFUL_EQUITY], pci);
}

/* "24/18 13/11*": one entry per checker moved, '*' marks a hit. */
static void
format_move(const TanBoard anBoardIn, const gnubg_move *pm, char *sz, size_t cb)
{
    TanBoard anBoard;
    size_t len = 0;
    int i;

    memcpy(anBoard, anBoardIn, sizeof(TanBoard));
    sz[0] = '\0';

    for (i = 0; i < pm->count; i++) {
        int src = pm->from[i] - 1;
        int dst = pm->to[i] - 1;
        int hit = dst >= 0 && anBoard[0][23 - dst] == 1;
        char from[8], to[8];

        if (pm->from[i] == 25)
            strcpy(from, "bar");
        else
            sprintf(from, "%d", pm->from[i]);
        if (pm->to[i] == 0)
            strcpy(to, "off");
        else
            sprintf(to, "%d", pm->to[i]);

        len += (size_t) g_snprintf(sz + len, cb - len, "%s%s/%s%s", i ? " " : "", from, to, hit ? "*" : "");
        if (len >= cb)
            return;

        anBoard[1][src]--;
        if (dst >= 0) {
            anBoard[1][dst]++;
            if (hit) {
                anBoard[0][23 - dst] = 0;
                anBoard[0][24]++;
            }
        }
    }
}

GNUBG_API int
gnubg_init(const char *data_dir)
{
    char *weights, *weights_binary, *met;
    int have_weights;

    if (initialised)
        return 0;
    if (!data_dir)
        return -1;

    weights = g_build_filename(data_dir, "gnubg.weights", NULL);
    weights_binary = g_build_filename(data_dir, "gnubg.wd", NULL);

    /* EvalInitialise() calls exit() if no weights load: check first. */
    have_weights = g_file_test(weights_binary, G_FILE_TEST_IS_REGULAR) ||
        g_file_test(weights, G_FILE_TEST_IS_REGULAR);
    if (!have_weights) {
        g_printerr("gnubg: no gnubg.wd or gnubg.weights in %s\n", data_dir);
        g_free(weights);
        g_free(weights_binary);
        return -1;
    }

    gnubg_port_set_datadir(data_dir);

    /* Falls back to a built-in table if the file is missing. */
    met = g_build_filename(data_dir, "met", "Kazaross-XG2.xml", NULL);
    InitMatchEquity(met);
    g_free(met);

    MT_InitThreads();

    EvalInitialise(weights, weights_binary, FALSE, NULL);

    g_free(weights);
    g_free(weights_binary);

    gnubg_set_skill(GNUBG_SKILL_WORLDCLASS);
    gnubg_seed(0);

    initialised = TRUE;
    return 0;
}

GNUBG_API void
gnubg_shutdown(void)
{
    if (!initialised)
        return;
    MT_Close();
    EvalShutdown();
    initialised = FALSE;
}

GNUBG_API const char *
gnubg_version(void)
{
    return VERSION;
}

GNUBG_API int
gnubg_set_skill(gnubg_skill skill)
{
    int filter;

    if ((int) skill < 0 || (int) skill > SETTINGS_GRANDMASTER)
        return -1;

    ecCurrent = aecSettings[skill];
    filter = aiSettingsMoveFilter[skill];
    amfCurrent = aaamfMoveFilterSettings[filter < 0 ? 2 : filter];
    return 0;
}

GNUBG_API void
gnubg_match_money(gnubg_match *match)
{
    memset(match, 0, sizeof(*match));
    match->cube_value = 1;
    match->cube_owner = -1;
}

GNUBG_API int
gnubg_best_move(const gnubg_board board, int die1, int die2, const gnubg_match *match, gnubg_move *out)
{
    TanBoard anBoard;
    cubeinfo ci;
    movelist ml;
    const move *pm;
    int i;

    if (!initialised || !out || die1 < 1 || die1 > 6 || die2 < 1 || die2 > 6)
        return -1;
    if (to_tan(board, anBoard) < 0 || make_cubeinfo(match, &ci) < 0)
        return -1;

    memset(out, 0, sizeof(*out));

    if (FindnSaveBestMoves(&ml, die1, die2, (ConstTanBoard) anBoard, NULL, 0.0f, &ci, &ecCurrent, amfCurrent) < 0) {
        g_free(ml.amMoves);
        return -1;
    }

    if (ml.cMoves == 0) {
        g_free(ml.amMoves);
        return 0;
    }

    pm = &ml.amMoves[ml.iMoveBest];
    for (i = 0; i < 4 && pm->anMove[2 * i] >= 0; i++) {
        out->from[i] = pm->anMove[2 * i] + 1;
        out->to[i] = pm->anMove[2 * i + 1] + 1;
    }
    out->count = i;
    fill_eval(&out->eval, pm->arEvalMove, &ci);
    format_move((ConstTanBoard) anBoard, out, out->text, sizeof(out->text));

    g_free(ml.amMoves);
    return 0;
}

GNUBG_API int
gnubg_cube_action(const gnubg_board board, const gnubg_match *match, gnubg_cube_decision *out)
{
    TanBoard anBoard;
    cubeinfo ci;
    float aarOutput[2][NUM_ROLLOUT_OUTPUTS];
    float arDouble[4];
    cubedecision cd;

    if (!initialised || !out)
        return -1;
    if (to_tan(board, anBoard) < 0 || make_cubeinfo(match, &ci) < 0)
        return -1;

    memset(out, 0, sizeof(*out));

    if (GeneralCubeDecisionE(aarOutput, (ConstTanBoard) anBoard, &ci, &ecCurrent, NULL) < 0)
        return -1;

    cd = FindCubeDecision(arDouble, aarOutput, &ci);

    out->equity_no_double = cubeful_equity(arDouble[OUTPUT_NODOUBLE], &ci);
    out->equity_double_take = cubeful_equity(arDouble[OUTPUT_TAKE], &ci);
    out->equity_double_pass = cubeful_equity(arDouble[OUTPUT_DROP], &ci);

    /* Same mapping as the computer player in gnubg's play.c */
    switch (cd) {
    case DOUBLE_TAKE:
    case REDOUBLE_TAKE:
    case DOUBLE_PASS:
    case REDOUBLE_PASS:
    case DOUBLE_BEAVER:
        out->should_double = GetDPEq(NULL, NULL, &ci) && ci.nCube < MAX_CUBE;
        break;
    default:
        break;
    }

    switch (cd) {
    case DOUBLE_PASS:
    case TOOGOOD_PASS:
    case REDOUBLE_PASS:
    case TOOGOODRE_PASS:
    case OPTIONAL_DOUBLE_PASS:
    case OPTIONAL_REDOUBLE_PASS:
        out->should_take = FALSE;
        break;
    case DOUBLE_BEAVER:
    case NODOUBLE_BEAVER:
    case NO_REDOUBLE_BEAVER:
    case OPTIONAL_DOUBLE_BEAVER:
        out->should_take = TRUE;
        out->should_beaver = !ci.nMatchTo && ci.fBeavers;
        break;
    default:
        out->should_take = TRUE;
        break;
    }

    return 0;
}

GNUBG_API int
gnubg_evaluate(const gnubg_board board, const gnubg_match *match, gnubg_eval *out)
{
    TanBoard anBoard;
    cubeinfo ci;
    SSE_ALIGN(float arOutput[NUM_ROLLOUT_OUTPUTS]);

    if (!initialised || !out)
        return -1;
    if (to_tan(board, anBoard) < 0 || make_cubeinfo(match, &ci) < 0)
        return -1;

    if (GeneralEvaluationE(arOutput, (ConstTanBoard) anBoard, &ci, &ecCurrent) < 0)
        return -1;

    fill_eval(out, arOutput, &ci);
    return 0;
}

GNUBG_API int
gnubg_apply_move(gnubg_board board, const gnubg_move *move)
{
    TanBoard anBoard;
    int anMove[8];
    int i;

    if (!move || move->count < 0 || move->count > 4 || to_tan(board, anBoard) < 0)
        return -1;

    for (i = 0; i < 8; i++)
        anMove[i] = -1;
    for (i = 0; i < move->count; i++) {
        if (move->from[i] < 1 || move->from[i] > 25 || move->to[i] < 0 || move->to[i] > 24)
            return -1;
        anMove[2 * i] = move->from[i] - 1;
        anMove[2 * i + 1] = move->to[i] - 1;
    }

    if (ApplyMove(anBoard, anMove, TRUE) < 0)
        return -1;

    from_tan((ConstTanBoard) anBoard, board);
    return 0;
}

/*
 * Legal moves.
 *
 * gnubg's GenerateMoves() fills a buffer that the evaluator also uses (one
 * global without USE_MULTITHREAD), so it can't run while gnubg_best_move()
 * is searching on another thread. generate_moves() is a copy of its logic
 * (GenerateMoves, GenerateMovesSub, SaveMoves with fPartial FALSE, and
 * LegalMove in eval.c) with a caller-owned list. It gives the same moves
 * in the same order; tools/test_legal.c checks this.
 */

typedef struct {
    int anMove[8];
    positionkey key;
    unsigned int cMoves, cPips;
} legal_move;

typedef struct {
    unsigned int cMoves, cMaxMoves, cMaxPips;
    legal_move *amMoves;        /* MAX_INCOMPLETE_MOVES entries */
} legal_list;

static void
save_move(legal_list *pml, unsigned int cMoves, unsigned int cPip, const int anMoves[], const TanBoard anBoard)
{
    unsigned int i, j;
    legal_move *pm;
    positionkey key;

    if (cMoves < pml->cMaxMoves || cPip < pml->cMaxPips)
        return;

    if (cMoves > pml->cMaxMoves || cPip > pml->cMaxPips)
        pml->cMoves = 0;

    pml->cMaxMoves = cMoves;
    pml->cMaxPips = cPip;

    PositionKey(anBoard, &key);

    for (i = 0; i < pml->cMoves; i++) {
        pm = &pml->amMoves[i];
        if (EqualKeys(key, pm->key)) {
            if (cMoves > pm->cMoves || cPip > pm->cPips) {
                for (j = 0; j < cMoves * 2; j++)
                    pm->anMove[j] = anMoves[j] > -1 ? anMoves[j] : -1;
                if (cMoves < 4)
                    pm->anMove[cMoves * 2] = -1;
                pm->cMoves = cMoves;
                pm->cPips = cPip;
            }
            return;
        }
    }

    if (pml->cMoves >= MAX_INCOMPLETE_MOVES)
        return;

    pm = &pml->amMoves[pml->cMoves++];
    for (i = 0; i < cMoves * 2; i++)
        pm->anMove[i] = anMoves[i] > -1 ? anMoves[i] : -1;
    if (cMoves < 4)
        pm->anMove[cMoves * 2] = -1;
    CopyKey(key, pm->key);
    pm->cMoves = cMoves;
    pm->cPips = cPip;
}

/* Can a checker on iSrc (not the bar) move nPips? */
static int
legal_sub_move(const TanBoard anBoard, int iSrc, int nPips)
{
    int nBack;
    const int iDest = iSrc - nPips;

    if (iDest >= 0)
        return anBoard[0][23 - iDest] < 2;

    /* bearing off */
    for (nBack = 24; nBack > 0; nBack--)
        if (anBoard[1][nBack] > 0)
            break;

    return nBack <= 5 && (iSrc == nBack || iDest == -1);
}

static int
generate_moves_sub(legal_list *pml, const int anRoll[], int nMoveDepth, int iPip, int cPip,
                   const TanBoard anBoard, int anMoves[])
{
    int i, fUsed = 0;
    TanBoard anBoardNew;

    if (nMoveDepth > 3 || !anRoll[nMoveDepth])
        return TRUE;

    if (anBoard[1][24]) {       /* on bar */
        if (anBoard[0][anRoll[nMoveDepth] - 1] >= 2)
            return TRUE;

        anMoves[nMoveDepth * 2] = 24;
        anMoves[nMoveDepth * 2 + 1] = 24 - anRoll[nMoveDepth];

        memcpy(anBoardNew, anBoard, sizeof(anBoardNew));
        ApplySubMove(anBoardNew, 24, anRoll[nMoveDepth], TRUE);

        if (generate_moves_sub(pml, anRoll, nMoveDepth + 1, 23, cPip + anRoll[nMoveDepth],
                               (ConstTanBoard) anBoardNew, anMoves))
            save_move(pml, nMoveDepth + 1, cPip + anRoll[nMoveDepth], anMoves, (ConstTanBoard) anBoardNew);

        return FALSE;
    }

    for (i = iPip; i >= 0; i--)
        if (anBoard[1][i] && legal_sub_move(anBoard, i, anRoll[nMoveDepth])) {
            anMoves[nMoveDepth * 2] = i;
            anMoves[nMoveDepth * 2 + 1] = i - anRoll[nMoveDepth];

            memcpy(anBoardNew, anBoard, sizeof(anBoardNew));
            ApplySubMove(anBoardNew, i, anRoll[nMoveDepth], TRUE);

            if (generate_moves_sub(pml, anRoll, nMoveDepth + 1, anRoll[0] == anRoll[1] ? i : 23,
                                   cPip + anRoll[nMoveDepth], (ConstTanBoard) anBoardNew, anMoves))
                save_move(pml, nMoveDepth + 1, cPip + anRoll[nMoveDepth], anMoves, (ConstTanBoard) anBoardNew);

            fUsed = 1;
        }

    return !fUsed;
}

/* Fills pml (its buffer allocated here; g_free it) and returns the move count. */
static unsigned int
generate_moves(legal_list *pml, const TanBoard anBoard, int n0, int n1)
{
    int anRoll[4], anMoves[8];

    anRoll[0] = n0;
    anRoll[1] = n1;
    anRoll[2] = anRoll[3] = n0 == n1 ? n0 : 0;

    pml->cMoves = pml->cMaxMoves = pml->cMaxPips = 0;
    pml->amMoves = g_new(legal_move, MAX_INCOMPLETE_MOVES);
    generate_moves_sub(pml, anRoll, 0, 23, 0, anBoard, anMoves);

    if (n0 != n1) {
        anRoll[0] = n1;
        anRoll[1] = n0;
        generate_moves_sub(pml, anRoll, 0, 23, 0, anBoard, anMoves);
    }

    return pml->cMoves;
}

static int
valid_dice(int die1, int die2)
{
    return die1 >= 1 && die1 <= 6 && die2 >= 1 && die2 <= 6;
}

GNUBG_API int
gnubg_legal_moves(const gnubg_board board, int die1, int die2, gnubg_move *out, int max_out)
{
    TanBoard anBoard;
    legal_list ml;
    unsigned int i;
    int j;

    if (!valid_dice(die1, die2) || to_tan(board, anBoard) < 0 || max_out < 0 || (max_out && !out))
        return -1;

    generate_moves(&ml, (ConstTanBoard) anBoard, die1, die2);

    for (i = 0; i < ml.cMoves && (int) i < max_out; i++) {
        const legal_move *pm = &ml.amMoves[i];
        gnubg_move *m = &out[i];

        memset(m, 0, sizeof(*m));
        for (j = 0; j < 4 && pm->anMove[2 * j] >= 0; j++) {
            m->from[j] = pm->anMove[2 * j] + 1;
            m->to[j] = pm->anMove[2 * j + 1] + 1;
        }
        m->count = j;
        format_move((ConstTanBoard) anBoard, m, m->text, sizeof(m->text));
    }

    g_free(ml.amMoves);
    return (int) ml.cMoves;
}

GNUBG_API int
gnubg_is_legal_move(const gnubg_board board, int die1, int die2, const gnubg_move *move)
{
    TanBoard anBoard, anAfter;
    legal_list ml;
    positionkey key;
    int anMove[8];
    unsigned int i;
    int legal = FALSE;

    if (!valid_dice(die1, die2) || !move || to_tan(board, anBoard) < 0)
        return -1;
    if (move->count < 0 || move->count > 4)
        return 0;

    for (i = 0; i < 8; i++)
        anMove[i] = -1;
    for (i = 0; i < (unsigned int) move->count; i++) {
        if (move->from[i] < 1 || move->from[i] > 25 || move->to[i] < 0 || move->to[i] > 24)
            return 0;
        anMove[2 * i] = move->from[i] - 1;
        anMove[2 * i + 1] = move->to[i] - 1;
    }
    /* Steps may span several dice (gnubg writes 24/18 18/13 as 24/13). */
    memcpy(anAfter, anBoard, sizeof(anAfter));
    if (ApplyMove(anAfter, anMove, FALSE) < 0)
        return 0;
    PositionKey((ConstTanBoard) anAfter, &key);

    generate_moves(&ml, (ConstTanBoard) anBoard, die1, die2);
    if (ml.cMoves == 0)
        legal = move->count == 0;
    else
        for (i = 0; i < ml.cMoves && !legal; i++)
            legal = EqualKeys(key, ml.amMoves[i].key);
    g_free(ml.amMoves);
    return legal;
}

/*
 * Legal steps. A full move must play as many pips as possible: that is
 * both rules at once (use as many dice as you can; if only one die can be
 * used, the larger one), as in SaveMoves. So a step is legal when the
 * pips played so far, plus its die, plus the most that can still be
 * played after it, reach the most the turn allows. Every order of every
 * move is searched, so no intermediate landing point is lost.
 */

typedef struct {
    TanBoard anBoard;
    int anDice[4];      /* dice left, ascending */
    int nDice;
    int cPips;          /* pips played so far */
} turn_state;

/* Can a checker on iSrc (24 = bar) move nPips, under the bar rule? */
static int
can_step(const TanBoard anBoard, int iSrc, int nPips)
{
    if (anBoard[1][24] && iSrc != 24)
        return FALSE;
    return anBoard[1][iSrc] > 0 && legal_sub_move(anBoard, iSrc, nPips);
}

/* Play the k-th die left from iSrc (which must be a legal step). */
static void
play_step(const turn_state *ps, int k, int iSrc, turn_state *pnew)
{
    int i;

    *pnew = *ps;
    ApplySubMove(pnew->anBoard, iSrc, ps->anDice[k], TRUE);
    pnew->cPips += ps->anDice[k];
    for (i = k; i < ps->nDice - 1; i++)
        pnew->anDice[i] = ps->anDice[i + 1];
    pnew->nDice--;
}

/* The most pips that can still be played from ps. */
static int
max_pips(const turn_state *ps)
{
    int left = 0, best = 0;
    int i, k;
    turn_state next;

    for (k = 0; k < ps->nDice; k++)
        left += ps->anDice[k];

    for (k = 0; k < ps->nDice; k++) {
        if (k && ps->anDice[k] == ps->anDice[k - 1])
            continue;
        for (i = 24; i >= 0; i--)
            if (can_step((ConstTanBoard) ps->anBoard, i, ps->anDice[k])) {
                int n;

                play_step(ps, k, i, &next);
                n = ps->anDice[k] + max_pips(&next);
                if (n > best)
                    best = n;
                if (best == left)
                    return best;
            }
    }
    return best;
}

/*
 * Follow played from ps, keeping to moves that reach nTarget pips. A step
 * without a die tries the smaller fitting die first. 0 on success.
 */
static int
follow_steps(turn_state *ps, int nTarget, const gnubg_step *played, int n_played)
{
    int iSrc, k;
    turn_state next;

    if (n_played == 0)
        return 0;
    if (played->from < 1 || played->from > 25 || played->to < 0 || played->to > 24)
        return -1;

    iSrc = played->from - 1;
    for (k = 0; k < ps->nDice; k++) {
        int nDie = ps->anDice[k];
        int iDest = iSrc - nDie;

        if ((k && nDie == ps->anDice[k - 1]) || (played->die && played->die != nDie))
            continue;
        if (iDest < 0 ? played->to != 0 : iDest != played->to - 1)
            continue;
        if (!can_step((ConstTanBoard) ps->anBoard, iSrc, nDie))
            continue;
        play_step(ps, k, iSrc, &next);
        if (next.cPips + max_pips(&next) != nTarget)
            continue;
        if (follow_steps(&next, nTarget, played + 1, n_played - 1) == 0) {
            *ps = next;
            return 0;
        }
    }
    return -1;
}

GNUBG_API int
gnubg_legal_steps(const gnubg_board start, int die1, int die2, const gnubg_step *played, int n_played,
                  gnubg_step *out, int max_out)
{
    turn_state ts, next;
    int nTarget, i, k, n = 0;

    if (!valid_dice(die1, die2) || to_tan(start, ts.anBoard) < 0 || n_played < 0 || n_played > 4 ||
        (n_played && !played) || max_out < 0 || (max_out && !out))
        return -1;

    ts.anDice[0] = MIN(die1, die2);
    ts.anDice[1] = MAX(die1, die2);
    ts.anDice[2] = ts.anDice[3] = die1;
    ts.nDice = die1 == die2 ? 4 : 2;
    ts.cPips = 0;

    nTarget = max_pips(&ts);
    if (follow_steps(&ts, nTarget, played, n_played) < 0)
        return -2;

    for (i = 24; i >= 0; i--)
        for (k = 0; k < ts.nDice; k++) {
            int nDie = ts.anDice[k];
            int iDest = i - nDie;

            if (k && nDie == ts.anDice[k - 1])
                continue;
            if (!can_step((ConstTanBoard) ts.anBoard, i, nDie))
                continue;
            play_step(&ts, k, i, &next);
            if (next.cPips + max_pips(&next) != nTarget)
                continue;
            if (n < max_out) {
                out[n].from = i + 1;
                out[n].to = iDest < 0 ? 0 : iDest + 1;
                out[n].die = nDie;
                out[n].hit = iDest >= 0 && ts.anBoard[0][23 - iDest] == 1;
            }
            n++;
        }

    return n;
}

GNUBG_API void
gnubg_swap_sides(gnubg_board board)
{
    int i, n;

    for (i = 0; i < 25; i++) {
        n = board[0][i];
        board[0][i] = board[1][i];
        board[1][i] = n;
    }
}

GNUBG_API int
gnubg_game_status(const gnubg_board board)
{
    TanBoard anBoard;

    if (to_tan(board, anBoard) < 0)
        return -1;
    return GameStatus((ConstTanBoard) anBoard, VARIATION_STANDARD);
}

GNUBG_API void
gnubg_initial_board(gnubg_board board)
{
    int i;

    memset(board, 0, sizeof(gnubg_board));
    for (i = 0; i < 2; i++) {
        board[i][5] = 5;        /* 6-point */
        board[i][7] = 3;        /* 8-point */
        board[i][12] = 5;       /* mid-point */
        board[i][23] = 2;       /* back checkers */
    }
}

GNUBG_API int
gnubg_board_from_position_id(const char *id, gnubg_board board)
{
    TanBoard anBoard;

    if (!id || !PositionFromID(anBoard, id))
        return -1;
    from_tan((ConstTanBoard) anBoard, board);
    return 0;
}

GNUBG_API int
gnubg_position_id(const gnubg_board board, char out[15])
{
    TanBoard anBoard;

    if (to_tan(board, anBoard) < 0)
        return -1;
    g_strlcpy(out, PositionID((ConstTanBoard) anBoard), 15);
    return 0;
}

GNUBG_API void
gnubg_seed(unsigned int seed)
{
    if (seed == 0)
        seed = (unsigned int) time(NULL) ^ (unsigned int) g_get_monotonic_time();
    sfmt_init_gen_rand(&sfmt, seed);
}

static int
roll_die(void)
{
    /* Rejection sampling: 4294967292 is the largest multiple of 6 below 2^32. */
    uint32_t r;

    do {
        r = sfmt_genrand_uint32(&sfmt);
    } while (r >= 4294967292u);
    return (int) (r % 6) + 1;
}

GNUBG_API void
gnubg_roll_dice(int dice[2])
{
    dice[0] = roll_die();
    dice[1] = roll_die();
}
