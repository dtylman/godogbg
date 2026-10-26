/*
 * Gnubg: GDScript access to the GNU Backgammon engine.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 3 or later of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include "gnubg_godot.h"

#include <cstring>
#include <vector>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include "gnubg_api.h"

Gnubg *Gnubg::singleton = nullptr;

/* Engine files under the data directory; only gnubg.wd is required. */
static const char *const DATA_FILES[] = {
    "gnubg.wd",
    "gnubg_os0.bd",
    "gnubg_ts0.bd",
    "met/Kazaross-XG2.xml",
};

/* Where res:// data is unpacked in exported games. */
static const char *const USER_DATA_DIR = "user://godogbg/data";

static bool
to_board(const PackedInt32Array &p_board, gnubg_board r_board)
{
    ERR_FAIL_COND_V_MSG(p_board.size() != Gnubg::BOARD_SIZE, false,
                        "Gnubg: a board must be a PackedInt32Array of 50 ints.");
    const int32_t *src = p_board.ptr();
    for (int side = 0; side < 2; side++)
        for (int i = 0; i < 25; i++)
            r_board[side][i] = src[side * 25 + i];
    return true;
}

static PackedInt32Array
from_board(const gnubg_board p_board)
{
    PackedInt32Array board;
    board.resize(Gnubg::BOARD_SIZE);
    int32_t *dst = board.ptrw();
    for (int side = 0; side < 2; side++)
        for (int i = 0; i < 25; i++)
            dst[side * 25 + i] = p_board[side][i];
    return board;
}

/* Missing keys keep the money-game defaults of gnubg_match_money(). */
static void
to_match(const Dictionary &p_match, gnubg_match &r_match)
{
    gnubg_match_money(&r_match);
    if (p_match.is_empty())
        return;

    r_match.match_length = p_match.get("match_length", r_match.match_length);
    if (p_match.has("score")) {
        Array score = p_match["score"];
        ERR_FAIL_COND_MSG(score.size() != 2, "Gnubg: match \"score\" must have 2 entries.");
        r_match.score[0] = score[0];
        r_match.score[1] = score[1];
    }
    r_match.cube_value = p_match.get("cube_value", r_match.cube_value);
    r_match.cube_owner = p_match.get("cube_owner", r_match.cube_owner);
    r_match.crawford = bool(p_match.get("crawford", false));
    r_match.jacoby = bool(p_match.get("jacoby", false));
    r_match.beavers = bool(p_match.get("beavers", false));
}

static Dictionary
eval_dict(const gnubg_eval &p_eval)
{
    Dictionary d;
    d["win"] = p_eval.win;
    d["win_gammon"] = p_eval.win_gammon;
    d["win_backgammon"] = p_eval.win_backgammon;
    d["lose_gammon"] = p_eval.lose_gammon;
    d["lose_backgammon"] = p_eval.lose_backgammon;
    d["equity"] = p_eval.equity;
    return d;
}

static bool
to_move(const Dictionary &p_move, gnubg_move &r_move)
{
    PackedInt32Array from = p_move.get("from", PackedInt32Array());
    PackedInt32Array to = p_move.get("to", PackedInt32Array());
    ERR_FAIL_COND_V_MSG(from.size() != to.size() || from.size() > 4, false,
                        "Gnubg: a move needs \"from\" and \"to\" arrays of equal length (at most 4).");

    std::memset(&r_move, 0, sizeof(r_move));
    r_move.count = from.size();
    for (int i = 0; i < r_move.count; i++) {
        r_move.from[i] = from[i];
        r_move.to[i] = to[i];
    }
    return true;
}

static bool
valid_dice(int p_die1, int p_die2)
{
    ERR_FAIL_COND_V_MSG(p_die1 < 1 || p_die1 > 6 || p_die2 < 1 || p_die2 > 6, false,
                        "Gnubg: dice must be 1..6.");
    return true;
}

/* Copy one file out of res:// unless an identical-size copy is there. */
static Error
unpack_file(const String &p_src, const String &p_dst)
{
    if (!FileAccess::file_exists(p_src))
        return ERR_FILE_NOT_FOUND;
    PackedByteArray data = FileAccess::get_file_as_bytes(p_src);
    if (FileAccess::file_exists(p_dst)) {
        Ref<FileAccess> f = FileAccess::open(p_dst, FileAccess::READ);
        if (f.is_valid() && f->get_length() == uint64_t(data.size()))
            return OK;
    }
    Error err = DirAccess::make_dir_recursive_absolute(p_dst.get_base_dir());
    ERR_FAIL_COND_V_MSG(err != OK, err, "Gnubg: can't create " + p_dst.get_base_dir());
    Ref<FileAccess> f = FileAccess::open(p_dst, FileAccess::WRITE);
    ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "Gnubg: can't write " + p_dst);
    f->store_buffer(data);
    return OK;
}

Gnubg::Gnubg()
{
    singleton = this;
}

Gnubg::~Gnubg()
{
    shutdown();
    if (singleton == this)
        singleton = nullptr;
}

Error
Gnubg::init(const String &p_data_dir)
{
    String dir = p_data_dir;

    /*
     * The engine opens and memory-maps its files with plain C I/O, so it
     * needs a real directory. res:// is one when running the project from
     * the editor; in an exported game it lives inside the .pck/.apk, so
     * unpack the files to user:// first. (From a pack, globalize_path()
     * returns a relative path, which FileAccess would look up in the pack.)
     */
    if (dir.begins_with("res://")) {
        String real = ProjectSettings::get_singleton()->globalize_path(dir);
        if (real.is_absolute_path() && !real.contains("://") &&
            FileAccess::file_exists(real.path_join("gnubg.wd")))
            dir = real;
    }
    if (dir.begins_with("res://")) {
        for (const char *name : DATA_FILES) {
            Error err = unpack_file(dir.path_join(name), String(USER_DATA_DIR).path_join(name));
            ERR_FAIL_COND_V_MSG(err != OK && String(name) == "gnubg.wd", err,
                                "Gnubg: can't unpack " + dir.path_join(name) +
                                " (is it in the export filter?)");
        }
        dir = USER_DATA_DIR;
    }
    if (dir.begins_with("res://") || dir.begins_with("user://"))
        dir = ProjectSettings::get_singleton()->globalize_path(dir);

    std::lock_guard<std::mutex> lock(eval_mutex);
    if (ready)
        return OK;
    ERR_FAIL_COND_V_MSG(gnubg_init(dir.utf8().get_data()) < 0, ERR_FILE_NOT_FOUND,
                        "Gnubg: no engine weights (gnubg.wd) in " + dir);
    ready = true;
    return OK;
}

void
Gnubg::shutdown()
{
    _wait_all();
    std::lock_guard<std::mutex> lock(eval_mutex);
    if (!ready)
        return;
    gnubg_shutdown();
    ready = false;
}

bool
Gnubg::is_ready() const
{
    return ready;
}

String
Gnubg::get_version() const
{
    return String(gnubg_version());
}

Error
Gnubg::set_skill(Skill p_skill)
{
    std::lock_guard<std::mutex> lock(eval_mutex);
    ERR_FAIL_COND_V_MSG(gnubg_set_skill(gnubg_skill(p_skill)) < 0, ERR_INVALID_PARAMETER,
                        "Gnubg: invalid skill level.");
    return OK;
}

Dictionary
Gnubg::best_move(const PackedInt32Array &p_board, int p_die1, int p_die2, const Dictionary &p_match)
{
    gnubg_board board;
    gnubg_match match;
    gnubg_move move;

    ERR_FAIL_COND_V_MSG(!ready, Dictionary(), "Gnubg: call init() first.");
    if (!to_board(p_board, board))
        return Dictionary();
    to_match(p_match, match);
    {
        std::lock_guard<std::mutex> lock(eval_mutex);
        ERR_FAIL_COND_V_MSG(gnubg_best_move(board, p_die1, p_die2, &match, &move) < 0, Dictionary(),
                            "Gnubg: best_move failed (bad board or dice?).");
    }

    PackedInt32Array from, to;
    for (int i = 0; i < move.count; i++) {
        from.push_back(move.from[i]);
        to.push_back(move.to[i]);
    }
    Dictionary d;
    d["from"] = from;
    d["to"] = to;
    d["text"] = String(move.text);
    d["eval"] = eval_dict(move.eval);
    return d;
}

Dictionary
Gnubg::cube_action(const PackedInt32Array &p_board, const Dictionary &p_match)
{
    gnubg_board board;
    gnubg_match match;
    gnubg_cube_decision cd;

    ERR_FAIL_COND_V_MSG(!ready, Dictionary(), "Gnubg: call init() first.");
    if (!to_board(p_board, board))
        return Dictionary();
    to_match(p_match, match);
    {
        std::lock_guard<std::mutex> lock(eval_mutex);
        ERR_FAIL_COND_V_MSG(gnubg_cube_action(board, &match, &cd) < 0, Dictionary(),
                            "Gnubg: cube_action failed.");
    }

    Dictionary d;
    d["should_double"] = bool(cd.should_double);
    d["should_take"] = bool(cd.should_take);
    d["should_beaver"] = bool(cd.should_beaver);
    d["equity_no_double"] = cd.equity_no_double;
    d["equity_double_take"] = cd.equity_double_take;
    d["equity_double_pass"] = cd.equity_double_pass;
    return d;
}

Dictionary
Gnubg::evaluate(const PackedInt32Array &p_board, const Dictionary &p_match)
{
    gnubg_board board;
    gnubg_match match;
    gnubg_eval e;

    ERR_FAIL_COND_V_MSG(!ready, Dictionary(), "Gnubg: call init() first.");
    if (!to_board(p_board, board))
        return Dictionary();
    to_match(p_match, match);
    {
        std::lock_guard<std::mutex> lock(eval_mutex);
        ERR_FAIL_COND_V_MSG(gnubg_evaluate(board, &match, &e) < 0, Dictionary(),
                            "Gnubg: evaluate failed.");
    }
    return eval_dict(e);
}

int64_t
Gnubg::best_move_async(const PackedInt32Array &p_board, int p_die1, int p_die2, const Dictionary &p_match)
{
    int64_t request = next_request++;
    Callable task = callable_mp(this, &Gnubg::_run_best_move).bind(request, p_board, p_die1, p_die2, p_match);
    tasks[request] = WorkerThreadPool::get_singleton()->add_task(task, true, "Gnubg.best_move");
    return request;
}

int64_t
Gnubg::cube_action_async(const PackedInt32Array &p_board, const Dictionary &p_match)
{
    int64_t request = next_request++;
    Callable task = callable_mp(this, &Gnubg::_run_cube_action).bind(request, p_board, p_match);
    tasks[request] = WorkerThreadPool::get_singleton()->add_task(task, true, "Gnubg.cube_action");
    return request;
}

void
Gnubg::_run_best_move(int64_t p_request, const PackedInt32Array &p_board, int p_die1, int p_die2,
                      const Dictionary &p_match)
{
    Dictionary result = best_move(p_board, p_die1, p_die2, p_match);
    callable_mp(this, &Gnubg::_finish).call_deferred(p_request, StringName("best_move_completed"), result);
}

void
Gnubg::_run_cube_action(int64_t p_request, const PackedInt32Array &p_board, const Dictionary &p_match)
{
    Dictionary result = cube_action(p_board, p_match);
    callable_mp(this, &Gnubg::_finish).call_deferred(p_request, StringName("cube_action_completed"), result);
}

/* Main thread, after the task ran: reap it and report. */
void
Gnubg::_finish(int64_t p_request, const StringName &p_signal, const Dictionary &p_result)
{
    if (!tasks.has(p_request))
        return;     /* already reaped by shutdown() */
    WorkerThreadPool::get_singleton()->wait_for_task_completion(tasks[p_request]);
    tasks.erase(p_request);
    emit_signal(p_signal, p_request, p_result);
}

void
Gnubg::_wait_all()
{
    for (const KeyValue<int64_t, int64_t> &kv : tasks)
        WorkerThreadPool::get_singleton()->wait_for_task_completion(kv.value);
    tasks.clear();
}

PackedInt32Array
Gnubg::initial_board() const
{
    gnubg_board board;
    gnubg_initial_board(board);
    return from_board(board);
}

PackedInt32Array
Gnubg::apply_move(const PackedInt32Array &p_board, const Dictionary &p_move)
{
    gnubg_board board;
    gnubg_move move;

    if (!to_board(p_board, board) || !to_move(p_move, move))
        return PackedInt32Array();

    std::lock_guard<std::mutex> lock(util_mutex);
    if (gnubg_apply_move(board, &move) < 0)
        return PackedInt32Array();
    return from_board(board);
}

PackedInt32Array
Gnubg::swap_sides(const PackedInt32Array &p_board) const
{
    gnubg_board board;

    if (!to_board(p_board, board))
        return PackedInt32Array();
    gnubg_swap_sides(board);
    return from_board(board);
}

int
Gnubg::game_status(const PackedInt32Array &p_board)
{
    gnubg_board board;

    if (!to_board(p_board, board))
        return -1;
    std::lock_guard<std::mutex> lock(util_mutex);
    return gnubg_game_status(board);
}

/*
 * The legal-move functions don't use the evaluator or any shared state,
 * so they take no lock and never wait for a running best_move_async().
 */

Array
Gnubg::legal_moves(const PackedInt32Array &p_board, int p_die1, int p_die2) const
{
    gnubg_board board;
    Array result;

    if (!to_board(p_board, board) || !valid_dice(p_die1, p_die2))
        return result;

    std::vector<gnubg_move> moves(64);
    int n = gnubg_legal_moves(board, p_die1, p_die2, moves.data(), int(moves.size()));
    if (n > int(moves.size())) {
        moves.resize(n);
        n = gnubg_legal_moves(board, p_die1, p_die2, moves.data(), n);
    }
    ERR_FAIL_COND_V_MSG(n < 0, result, "Gnubg: legal_moves failed (bad board?).");

    for (int i = 0; i < n; i++) {
        PackedInt32Array from, to;
        for (int j = 0; j < moves[i].count; j++) {
            from.push_back(moves[i].from[j]);
            to.push_back(moves[i].to[j]);
        }
        Dictionary d;
        d["from"] = from;
        d["to"] = to;
        d["text"] = String(moves[i].text);
        result.push_back(d);
    }
    return result;
}

bool
Gnubg::is_legal_move(const PackedInt32Array &p_board, int p_die1, int p_die2, const Dictionary &p_move) const
{
    gnubg_board board;
    gnubg_move move;

    if (!to_board(p_board, board) || !valid_dice(p_die1, p_die2) || !to_move(p_move, move))
        return false;
    int r = gnubg_is_legal_move(board, p_die1, p_die2, &move);
    ERR_FAIL_COND_V_MSG(r < 0, false, "Gnubg: is_legal_move failed (bad board?).");
    return r == 1;
}

Array
Gnubg::legal_steps(const PackedInt32Array &p_start_board, int p_die1, int p_die2, const Array &p_played) const
{
    gnubg_board board;
    gnubg_step played[4], steps[GNUBG_MAX_STEPS];
    Array result;

    if (!to_board(p_start_board, board) || !valid_dice(p_die1, p_die2))
        return result;
    ERR_FAIL_COND_V_MSG(p_played.size() > 4, result, "Gnubg: legal_steps: at most 4 steps can be played.");
    for (int i = 0; i < p_played.size(); i++) {
        ERR_FAIL_COND_V_MSG(p_played[i].get_type() != Variant::DICTIONARY, result,
                            "Gnubg: legal_steps: each played step must be a {from, to} Dictionary.");
        Dictionary step = p_played[i];
        ERR_FAIL_COND_V_MSG(!step.has("from") || !step.has("to"), result,
                            "Gnubg: legal_steps: each played step needs \"from\" and \"to\".");
        played[i].from = step["from"];
        played[i].to = step["to"];
        played[i].die = step.get("die", 0);
        played[i].hit = 0;
    }

    int n = gnubg_legal_steps(board, p_die1, p_die2, played, int(p_played.size()), steps, GNUBG_MAX_STEPS);
    ERR_FAIL_COND_V_MSG(n == -2, result, "Gnubg: legal_steps: the played steps are not a legal start of this turn.");
    ERR_FAIL_COND_V_MSG(n < 0, result, "Gnubg: legal_steps failed (bad board?).");

    for (int i = 0; i < n; i++) {
        Dictionary d;
        d["from"] = steps[i].from;
        d["to"] = steps[i].to;
        d["die"] = steps[i].die;
        d["hit"] = bool(steps[i].hit);
        result.push_back(d);
    }
    return result;
}

PackedInt32Array
Gnubg::board_from_position_id(const String &p_id)
{
    gnubg_board board;

    std::lock_guard<std::mutex> lock(util_mutex);
    if (gnubg_board_from_position_id(p_id.utf8().get_data(), board) < 0)
        return PackedInt32Array();
    return from_board(board);
}

String
Gnubg::position_id(const PackedInt32Array &p_board)
{
    gnubg_board board;
    char id[15];

    if (!to_board(p_board, board))
        return String();
    std::lock_guard<std::mutex> lock(util_mutex);
    if (gnubg_position_id(board, id) < 0)
        return String();
    return String(id);
}

void
Gnubg::seed(int64_t p_seed)
{
    std::lock_guard<std::mutex> lock(util_mutex);
    gnubg_seed((unsigned int) p_seed);
}

PackedInt32Array
Gnubg::roll_dice()
{
    int dice[2];
    {
        std::lock_guard<std::mutex> lock(util_mutex);
        gnubg_roll_dice(dice);
    }
    PackedInt32Array r;
    r.push_back(dice[0]);
    r.push_back(dice[1]);
    return r;
}

void
Gnubg::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("init", "data_dir"), &Gnubg::init);
    ClassDB::bind_method(D_METHOD("shutdown"), &Gnubg::shutdown);
    ClassDB::bind_method(D_METHOD("is_ready"), &Gnubg::is_ready);
    ClassDB::bind_method(D_METHOD("get_version"), &Gnubg::get_version);
    ClassDB::bind_method(D_METHOD("set_skill", "skill"), &Gnubg::set_skill);

    ClassDB::bind_method(D_METHOD("best_move", "board", "die1", "die2", "match"), &Gnubg::best_move,
                         DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("cube_action", "board", "match"), &Gnubg::cube_action, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("evaluate", "board", "match"), &Gnubg::evaluate, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("best_move_async", "board", "die1", "die2", "match"), &Gnubg::best_move_async,
                         DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("cube_action_async", "board", "match"), &Gnubg::cube_action_async,
                         DEFVAL(Dictionary()));

    ClassDB::bind_method(D_METHOD("initial_board"), &Gnubg::initial_board);
    ClassDB::bind_method(D_METHOD("apply_move", "board", "move"), &Gnubg::apply_move);
    ClassDB::bind_method(D_METHOD("swap_sides", "board"), &Gnubg::swap_sides);
    ClassDB::bind_method(D_METHOD("game_status", "board"), &Gnubg::game_status);
    ClassDB::bind_method(D_METHOD("legal_moves", "board", "die1", "die2"), &Gnubg::legal_moves);
    ClassDB::bind_method(D_METHOD("is_legal_move", "board", "die1", "die2", "move"), &Gnubg::is_legal_move);
    ClassDB::bind_method(D_METHOD("legal_steps", "start_board", "die1", "die2", "played"), &Gnubg::legal_steps,
                         DEFVAL(Array()));
    ClassDB::bind_method(D_METHOD("board_from_position_id", "id"), &Gnubg::board_from_position_id);
    ClassDB::bind_method(D_METHOD("position_id", "board"), &Gnubg::position_id);

    ClassDB::bind_method(D_METHOD("seed", "seed"), &Gnubg::seed);
    ClassDB::bind_method(D_METHOD("roll_dice"), &Gnubg::roll_dice);

    ADD_SIGNAL(MethodInfo("best_move_completed", PropertyInfo(Variant::INT, "request_id"),
                          PropertyInfo(Variant::DICTIONARY, "result")));
    ADD_SIGNAL(MethodInfo("cube_action_completed", PropertyInfo(Variant::INT, "request_id"),
                          PropertyInfo(Variant::DICTIONARY, "result")));

    BIND_ENUM_CONSTANT(SKILL_BEGINNER);
    BIND_ENUM_CONSTANT(SKILL_CASUAL);
    BIND_ENUM_CONSTANT(SKILL_INTERMEDIATE);
    BIND_ENUM_CONSTANT(SKILL_ADVANCED);
    BIND_ENUM_CONSTANT(SKILL_EXPERT);
    BIND_ENUM_CONSTANT(SKILL_WORLDCLASS);
    BIND_ENUM_CONSTANT(SKILL_SUPREMO);
    BIND_ENUM_CONSTANT(SKILL_GRANDMASTER);
}
