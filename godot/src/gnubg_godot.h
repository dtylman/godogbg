/*
 * Gnubg: GDScript access to the GNU Backgammon engine (include/gnubg_api.h),
 * registered as the engine singleton "Gnubg".
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 3 or later of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#ifndef GODOGBG_GNUBG_GODOT_H
#define GODOGBG_GNUBG_GODOT_H

#include <mutex>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

class Gnubg : public Object {
    GDCLASS(Gnubg, Object)

public:
    enum Skill {
        SKILL_BEGINNER,
        SKILL_CASUAL,
        SKILL_INTERMEDIATE,
        SKILL_ADVANCED,
        SKILL_EXPERT,
        SKILL_WORLDCLASS,
        SKILL_SUPREMO,
        SKILL_GRANDMASTER,
    };

    /* Board layout: 50 ints, [0..24] opponent, [25..49] player on roll. */
    static constexpr int BOARD_SIZE = 50;

    Gnubg();
    ~Gnubg();

    static Gnubg *get_singleton() { return singleton; }

    Error init(const String &p_data_dir);
    void shutdown();
    bool is_ready() const;
    String get_version() const;
    Error set_skill(Skill p_skill);

    Dictionary best_move(const PackedInt32Array &p_board, int p_die1, int p_die2,
                         const Dictionary &p_match = Dictionary());
    Dictionary cube_action(const PackedInt32Array &p_board, const Dictionary &p_match = Dictionary());
    Dictionary evaluate(const PackedInt32Array &p_board, const Dictionary &p_match = Dictionary());

    int64_t best_move_async(const PackedInt32Array &p_board, int p_die1, int p_die2,
                            const Dictionary &p_match = Dictionary());
    int64_t cube_action_async(const PackedInt32Array &p_board, const Dictionary &p_match = Dictionary());

    PackedInt32Array initial_board() const;
    PackedInt32Array apply_move(const PackedInt32Array &p_board, const Dictionary &p_move);
    PackedInt32Array swap_sides(const PackedInt32Array &p_board) const;
    int game_status(const PackedInt32Array &p_board);
    PackedInt32Array board_from_position_id(const String &p_id);
    String position_id(const PackedInt32Array &p_board);

    void seed(int64_t p_seed);
    PackedInt32Array roll_dice();

protected:
    static void _bind_methods();

private:
    static Gnubg *singleton;

    /* The evaluator (init, skill, best move, cube, eval) is not re-entrant. */
    std::mutex eval_mutex;
    /* Position IDs use a static buffer; the dice use a global generator. */
    std::mutex util_mutex;
    bool ready = false;

    /* Async requests: request id -> WorkerThreadPool task id (main thread only). */
    HashMap<int64_t, int64_t> tasks;
    int64_t next_request = 1;

    void _run_best_move(int64_t p_request, const PackedInt32Array &p_board, int p_die1, int p_die2,
                        const Dictionary &p_match);
    void _run_cube_action(int64_t p_request, const PackedInt32Array &p_board, const Dictionary &p_match);
    void _finish(int64_t p_request, const StringName &p_signal, const Dictionary &p_result);
    void _wait_all();
};

VARIANT_ENUM_CAST(Gnubg::Skill);

#endif
