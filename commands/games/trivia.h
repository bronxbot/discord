#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "database/core/database.h"
#include "utils/moderation/mod_utility.h"
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <atomic>
#include <shared_mutex>

namespace commands {
namespace games {

struct TriviaLobby {
    uint64_t game_id;
    uint64_t author_id;
    uint64_t guild_id;
    uint64_t channel_id;
    uint64_t message_id = 0; // lobby or current question message id
    bool active = false; // lobby active vs game active

    std::vector<uint64_t> players; // joined players
    int64_t bet_amount = 0;
    bool bet_escrowed = false;

    // Game state
    int round = 0; // current question index (1 to 5)
    std::vector<bronx::db::TriviaQuestion> questions;
    
    // Answers for the current question
    std::map<uint64_t, char> current_round_answers; // player -> choice ('a', 'b', 'c', 'd')
    std::map<uint64_t, double> current_round_response_times; // player -> elapsed seconds
    std::chrono::steady_clock::time_point question_start_time;

    // Player scores during the game
    std::map<uint64_t, int> player_scores;
    std::map<uint64_t, int> player_correct_counts;

    bool force_stop = false;
    int64_t start_timestamp = 0;
    std::set<uint64_t> ever_joined;
    std::atomic<uint32_t> timer_generation{0};
    bool lobby_paused = false;

    std::string interaction_token; // latest interaction token
};

extern std::map<uint64_t, TriviaLobby> active_trivia_lobbies;
extern std::shared_mutex trivia_mutex;

static dpp::embed build_trivia_lobby_embed(const TriviaLobby& game) {
    std::string desc;
    if (game.lobby_paused) {
        desc = ":pause_button: lobby **paused** by the host. click the button to join.";
    } else {
        std::string ts = "<t:" + std::to_string(game.start_timestamp) + ":R>";
        desc = ":alarm_clock: waiting for players. the game will begin " + ts + ".";
    }
    
    std::string players_list = "";
    if (game.players.empty()) {
        players_list = "none";
    } else {
        for (auto uid : game.players) {
            players_list += "<@" + std::to_string(uid) + "> ";
        }
    }

    std::string wager_str = game.bet_amount > 0 ? "$" + economy::format_number(game.bet_amount) : "free to play";

    return dpp::embed()
        .set_color(bronx::COLOR_DEFAULT)
        .set_title("🏆 trivia battle - lobby")
        .set_description(desc)
        .add_field("wager", wager_str, true)
        .add_field("host", "<@" + std::to_string(game.author_id) + ">", true)
        .add_field("joined players", players_list, false)
        .set_footer("5 questions. correct answers give up to 150 points (speed bonus).", "");
}

static void edit_trivia_lobby_message(dpp::cluster& bot, const TriviaLobby& game) {
    std::string gid_str = std::to_string(game.game_id);
    dpp::message msg(game.channel_id, build_trivia_lobby_embed(game));
    
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    row.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_success)
        .set_label("join")
        .set_id("trivia_lobby_join:" + gid_str));
    row.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_secondary)
        .set_label("leave")
        .set_id("trivia_lobby_leave:" + gid_str));
    row.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_primary)
        .set_label("start early")
        .set_id("trivia_lobby_start:" + gid_str));
    row.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_danger)
        .set_label("cancel")
        .set_id("trivia_lobby_cancel:" + gid_str));

    msg.add_component(row);
    msg.id = game.message_id;

    if (!game.interaction_token.empty()) {
        bot.interaction_response_edit(game.interaction_token, msg);
    } else {
        bronx::safe_message_edit(bot, msg);
    }
}

static void run_trivia_game_loop(dpp::cluster& bot, bronx::db::Database* db, uint64_t game_id) {
    std::vector<uint64_t> current_players;
    int64_t bet = 0;
    bool escrowed = false;
    uint64_t channel_id = 0;
    uint64_t message_id = 0;

    {
        std::unique_lock<std::shared_mutex> lock(trivia_mutex);
        auto it = active_trivia_lobbies.find(game_id);
        if (it == active_trivia_lobbies.end()) return;
        it->second.active = true;
        current_players = it->second.players;
        bet = it->second.bet_amount;
        escrowed = it->second.bet_escrowed;
        channel_id = it->second.channel_id;
        message_id = it->second.message_id;
    }

    if (current_players.size() < 2) {
        if (bet > 0 && escrowed) {
            for (auto uid : current_players) {
                db->update_wallet(uid, bet);
            }
        }
        
        dpp::message cancel_msg(channel_id, dpp::embed()
            .set_color(bronx::COLOR_ERROR)
            .set_title("trivia battle - cancelled")
            .set_description("not enough players to start the game (minimum 2 players)."));
        cancel_msg.id = message_id;
        
        {
            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it != active_trivia_lobbies.end() && !it->second.interaction_token.empty()) {
                bot.interaction_response_edit(it->second.interaction_token, cancel_msg);
            } else {
                bronx::safe_message_edit(bot, cancel_msg);
            }
        }

        std::unique_lock<std::shared_mutex> lock(trivia_mutex);
        active_trivia_lobbies.erase(game_id);
        return;
    }

    // Get random questions
    auto questions = db->get_random_trivia_questions(5);
    if (questions.size() < 5) {
        std::vector<bronx::db::TriviaQuestion> fallback = {
            {1, "general", "who is known as the father of computer science?", "alan turing", "charles babbage", "ada lovelace", "steve jobs", 'a', "easy"},
            {2, "general", "what is the capital of france?", "berlin", "london", "madrid", "paris", 'd', "easy"},
            {3, "general", "which planet is known as the red planet?", "earth", "mars", "jupiter", "saturn", 'b', "easy"},
            {4, "general", "what is the chemical symbol for gold?", "ag", "au", "fe", "pb", 'b', "easy"},
            {5, "general", "how many bones are in the adult human body?", "106", "206", "306", "406", 'b', "easy"}
        };
        for (const auto& q : fallback) {
            if (questions.size() < 5) {
                questions.push_back(q);
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(trivia_mutex);
        auto it = active_trivia_lobbies.find(game_id);
        if (it != active_trivia_lobbies.end()) {
            it->second.questions = questions;
        }
    }

    for (int q_idx = 0; q_idx < 5; ++q_idx) {
        // Check if game was force-stopped
        {
            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.force_stop) return;
        }

        bronx::db::TriviaQuestion q = questions[q_idx];
        
        {
            std::unique_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.force_stop) return;
            
            it->second.round = q_idx + 1;
            it->second.current_round_answers.clear();
            it->second.current_round_response_times.clear();
            it->second.question_start_time = std::chrono::steady_clock::now();
        }

        // Build question embed (lowercase labels and footers)
        dpp::embed q_embed = dpp::embed()
            .set_color(bronx::COLOR_DEFAULT)
            .set_title("question " + std::to_string(q_idx + 1) + " of 5")
            .set_description("**category: " + q.category + "**\n\n" + q.question_text)
            .add_field("a", q.option_a, true)
            .add_field("b", q.option_b, true)
            .add_field("c", q.option_c, true)
            .add_field("d", q.option_d, true)
            .set_footer("you have 15 seconds to answer. answer quickly for a speed bonus!", "");

        dpp::component ans_row;
        ans_row.set_type(dpp::cot_action_row);
        std::string gid_str = std::to_string(game_id);
        ans_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_label("a")
            .set_id("trivia_ans:" + gid_str + ":a"));
        ans_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_label("b")
            .set_id("trivia_ans:" + gid_str + ":b"));
        ans_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_label("c")
            .set_id("trivia_ans:" + gid_str + ":c"));
        ans_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_label("d")
            .set_id("trivia_ans:" + gid_str + ":d"));

        dpp::message q_msg(channel_id, q_embed);
        q_msg.add_component(ans_row);
        q_msg.id = message_id;

        // Edit lobby message
        {
            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it != active_trivia_lobbies.end() && !it->second.interaction_token.empty() && q_idx == 0) {
                bot.interaction_response_edit(it->second.interaction_token, q_msg);
            } else {
                bronx::safe_message_edit(bot, q_msg);
            }
        }

        // Wait 15 seconds or until everyone answered
        int elapsed_100ms = 0;
        while (elapsed_100ms < 150) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            elapsed_100ms++;
            
            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.force_stop) return;
            
            bool all_answered = true;
            for (auto pid : it->second.players) {
                if (it->second.current_round_answers.find(pid) == it->second.current_round_answers.end()) {
                    all_answered = false;
                    break;
                }
            }
            if (all_answered) break;
        }

        // Score the round
        char correct_opt = std::tolower(q.correct_option);
        std::string correct_text;
        if (correct_opt == 'a') correct_text = q.option_a;
        else if (correct_opt == 'b') correct_text = q.option_b;
        else if (correct_opt == 'c') correct_text = q.option_c;
        else if (correct_opt == 'd') correct_text = q.option_d;

        std::string round_summary = "the correct answer was **" + std::string(1, correct_opt) + ") " + correct_text + "**\n\n";

        std::vector<std::pair<uint64_t, int>> sorted_scores;
        {
            std::unique_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.force_stop) return;

            for (auto p_id : it->second.players) {
                auto ans_it = it->second.current_round_answers.find(p_id);
                if (ans_it != it->second.current_round_answers.end() && ans_it->second == correct_opt) {
                    double resp_time = it->second.current_round_response_times[p_id];
                    double speed_bonus = std::max(0.0, 50.0 * (1.0 - (resp_time / 15.0)));
                    int points = 100 + static_cast<int>(speed_bonus);

                    it->second.player_scores[p_id] += points;
                    it->second.player_correct_counts[p_id]++;

                    round_summary += "<@" + std::to_string(p_id) + ">: **correct** (+" + std::to_string(points) + " pts, " + economy::format_number(resp_time) + "s)\n";
                    db->update_trivia_stats(p_id, false, 1, 1, 0);
                } else {
                    round_summary += "<@" + std::to_string(p_id) + ">: **incorrect**\n";
                    db->update_trivia_stats(p_id, false, 0, 1, 0);
                }
            }

            sorted_scores.assign(it->second.player_scores.begin(), it->second.player_scores.end());
        }

        std::sort(sorted_scores.begin(), sorted_scores.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        round_summary += "\n**scoreboard:**\n";
        int rank = 1;
        for (const auto& entry : sorted_scores) {
            round_summary += std::to_string(rank++) + ". <@" + std::to_string(entry.first) + "> — " + std::to_string(entry.second) + " pts\n";
        }

        dpp::embed summary_embed = dpp::embed()
            .set_color(bronx::COLOR_DEFAULT)
            .set_title("round " + std::to_string(q_idx + 1) + " summary")
            .set_description(round_summary);

        dpp::message summary_msg(channel_id, summary_embed);
        summary_msg.id = message_id;
        
        bronx::safe_message_edit(bot, summary_msg);

        // Sleep for 5 seconds between rounds
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.force_stop) return;
        }
    }

    // Game finished!
    std::vector<uint64_t> winners;
    std::vector<std::pair<uint64_t, int>> final_scores;
    {
        std::shared_lock<std::shared_mutex> lock(trivia_mutex);
        auto it = active_trivia_lobbies.find(game_id);
        if (it != active_trivia_lobbies.end()) {
            final_scores.assign(it->second.player_scores.begin(), it->second.player_scores.end());
        }
    }

    if (final_scores.empty()) {
        std::unique_lock<std::shared_mutex> lock(trivia_mutex);
        active_trivia_lobbies.erase(game_id);
        return;
    }

    std::sort(final_scores.begin(), final_scores.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    int highest_score = final_scores[0].second;
    for (const auto& entry : final_scores) {
        if (entry.second == highest_score) {
            winners.push_back(entry.first);
        }
    }

    int64_t total_pool = current_players.size() * bet;
    int64_t total_payout = static_cast<int64_t>(total_pool * 0.95);
    int64_t split_payout = winners.empty() ? 0 : total_payout / winners.size();

    std::string winner_description = "";
    if (winners.size() == 1) {
        winner_description = "🏆 **winner: <@" + std::to_string(winners[0]) + ">** with **" + std::to_string(highest_score) + " pts**!\n";
        if (bet > 0) {
            winner_description += "payout: **$" + economy::format_number(total_payout) + "** (after 5% rake)\n";
            db->update_wallet(winners[0], total_payout);
        }
    } else {
        winner_description = "🏆 **winners (tie):** ";
        for (auto w_id : winners) {
            winner_description += "<@" + std::to_string(w_id) + "> ";
        }
        winner_description += "with **" + std::to_string(highest_score) + " pts**!\n";
        if (bet > 0) {
            winner_description += "split payout: **$" + economy::format_number(split_payout) + "** each (after 5% rake)\n";
            for (auto w_id : winners) {
                db->update_wallet(w_id, split_payout);
            }
        }
    }

    // Challenge and user stats tracking
    std::string today = dpp::utility::current_date_time().substr(0, 10);
    for (auto p_id : current_players) {
        bool won = (std::find(winners.begin(), winners.end(), p_id) != winners.end());
        db->track_challenge_attempt(p_id, "precision_gambler", won, today);
        db->track_challenge_variety(p_id, "game_master", "trivia", today);
        
        int64_t stat_winnings = 0;
        if (won) {
            stat_winnings = bet > 0 ? (split_payout - bet) : 0;
        } else {
            stat_winnings = bet > 0 ? -bet : 0;
        }
        db->update_trivia_stats(p_id, won, 0, 0, stat_winnings);
    }

    winner_description += "\n**final scoreboard:**\n";
    int rank = 1;
    for (const auto& entry : final_scores) {
        int correct_count = 0;
        {
            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it != active_trivia_lobbies.end()) {
                correct_count = it->second.player_correct_counts[entry.first];
            }
        }
        winner_description += std::to_string(rank++) + ". <@" + std::to_string(entry.first) + "> — " + std::to_string(entry.second) + " pts (" + std::to_string(correct_count) + "/5 correct)\n";
    }

    dpp::embed final_embed = dpp::embed()
        .set_color(bronx::COLOR_SUCCESS)
        .set_title("trivia battle - game over")
        .set_description(winner_description);

    dpp::message final_msg(channel_id, final_embed);
    final_msg.id = message_id;
    bronx::safe_message_edit(bot, final_msg);

    std::unique_lock<std::shared_mutex> lock(trivia_mutex);
    active_trivia_lobbies.erase(game_id);
}

inline void register_trivia_handlers(dpp::cluster& bot, bronx::db::Database* db) {
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        const std::string& cid = event.custom_id;

        // Answer processing
        if (cid.rfind("trivia_ans:", 0) == 0) {
            size_t last_colon = cid.find_last_of(':');
            if (last_colon == std::string::npos || last_colon <= 11) {
                event.reply(dpp::ir_deferred_update_message, "");
                return;
            }
            std::string game_id_str = cid.substr(11, last_colon - 11);
            std::string choice_str = cid.substr(last_colon + 1);
            if (choice_str.empty()) {
                event.reply(dpp::ir_deferred_update_message, "");
                return;
            }
            char choice = std::tolower(choice_str[0]);
            uint64_t game_id = 0;
            try { game_id = std::stoull(game_id_str); } catch (...) { return; }

            std::unique_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || !it->second.active || it->second.force_stop) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("this trivia battle is not active.").set_flags(dpp::m_ephemeral));
                return;
            }

            uint64_t clicker_id = event.command.get_issuing_user().id;
            auto p_it = std::find(it->second.players.begin(), it->second.players.end(), clicker_id);
            if (p_it == it->second.players.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("you are not a participant in this game.").set_flags(dpp::m_ephemeral));
                return;
            }

            if (it->second.current_round_answers.find(clicker_id) != it->second.current_round_answers.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("you have already answered this question.").set_flags(dpp::m_ephemeral));
                return;
            }

            auto now = std::chrono::steady_clock::now();
            double resp_time = std::chrono::duration<double>(now - it->second.question_start_time).count();
            it->second.current_round_answers[clicker_id] = choice;
            it->second.current_round_response_times[clicker_id] = resp_time;

            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().set_content("your answer (" + std::string(1, choice) + ") has been recorded!").set_flags(dpp::m_ephemeral));
            return;
        }

        auto parse_lobby_cmd = [&](const std::string& prefix) -> uint64_t {
            if (cid.rfind(prefix, 0) != 0) return 0;
            try { return std::stoull(cid.substr(prefix.size())); } catch (...) { return 0; }
        };

        if (uint64_t game_id = parse_lobby_cmd("trivia_lobby_join:")) {
            std::unique_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.active || it->second.force_stop) {
                event.reply(dpp::ir_deferred_update_message, "");
                return;
            }

            uint64_t clicker_id = event.command.get_issuing_user().id;
            if (std::find(it->second.players.begin(), it->second.players.end(), clicker_id) != it->second.players.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("you are already in this lobby.").set_flags(dpp::m_ephemeral));
                return;
            }

            if (it->second.bet_amount > 0) {
                auto player_wallet = db->update_wallet(clicker_id, -it->second.bet_amount);
                if (!player_wallet || *player_wallet < 0) {
                    if (player_wallet) {
                        db->update_wallet(clicker_id, it->second.bet_amount);
                    }
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().set_content("you don't have enough money in your wallet to join this wager.").set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            it->second.players.push_back(clicker_id);
            it->second.ever_joined.insert(clicker_id);
            it->second.timer_generation++;
            
            uint32_t new_gen = it->second.timer_generation;
            std::thread([game_id, new_gen, &bot, db]() mutable {
                for (int i = 0; i < 30; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                    auto jt = active_trivia_lobbies.find(game_id);
                    if (jt == active_trivia_lobbies.end()) return;
                    if (jt->second.timer_generation != new_gen) return;
                    if (jt->second.lobby_paused) { --i; continue; }
                }
                run_trivia_game_loop(bot, db, game_id);
            }).detach();

            edit_trivia_lobby_message(bot, it->second);
            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }

        if (uint64_t game_id = parse_lobby_cmd("trivia_lobby_leave:")) {
            std::unique_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.active || it->second.force_stop) {
                event.reply(dpp::ir_deferred_update_message, "");
                return;
            }

            uint64_t clicker_id = event.command.get_issuing_user().id;
            auto p_it = std::find(it->second.players.begin(), it->second.players.end(), clicker_id);
            if (p_it == it->second.players.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("you are not in this lobby.").set_flags(dpp::m_ephemeral));
                return;
            }

            it->second.players.erase(p_it);
            if (it->second.bet_amount > 0) {
                db->update_wallet(clicker_id, it->second.bet_amount);
            }

            if (clicker_id == it->second.author_id) {
                it->second.force_stop = true;
                for (auto pid : it->second.players) {
                    if (it->second.bet_amount > 0) {
                        db->update_wallet(pid, it->second.bet_amount);
                    }
                }
                dpp::message cancel_msg(it->second.channel_id, dpp::embed()
                    .set_color(bronx::COLOR_ERROR)
                    .set_title("trivia battle - cancelled")
                    .set_description("lobby cancelled because the host left."));
                cancel_msg.id = it->second.message_id;
                
                if (!it->second.interaction_token.empty()) {
                    bot.interaction_response_edit(it->second.interaction_token, cancel_msg);
                } else {
                    bronx::safe_message_edit(bot, cancel_msg);
                }
                
                active_trivia_lobbies.erase(game_id);
                event.reply(dpp::ir_deferred_update_message, "");
                return;
            }

            it->second.timer_generation++;
            uint32_t new_gen = it->second.timer_generation;
            std::thread([game_id, new_gen, &bot, db]() mutable {
                for (int i = 0; i < 30; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                    auto jt = active_trivia_lobbies.find(game_id);
                    if (jt == active_trivia_lobbies.end()) return;
                    if (jt->second.timer_generation != new_gen) return;
                    if (jt->second.lobby_paused) { --i; continue; }
                }
                run_trivia_game_loop(bot, db, game_id);
            }).detach();

            edit_trivia_lobby_message(bot, it->second);
            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }

        if (uint64_t game_id = parse_lobby_cmd("trivia_lobby_pause:")) {
            std::unique_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.active || it->second.force_stop) {
                event.reply(dpp::ir_deferred_update_message, "");
                return;
            }

            uint64_t clicker_id = event.command.get_issuing_user().id;
            if (clicker_id != it->second.author_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("only the lobby host can pause/resume the lobby.").set_flags(dpp::m_ephemeral));
                return;
            }

            it->second.lobby_paused = !it->second.lobby_paused;
            if (!it->second.lobby_paused) {
                it->second.timer_generation++;
                it->second.start_timestamp = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count() + 30;
                
                uint32_t new_gen = it->second.timer_generation;
                std::thread([game_id, new_gen, &bot, db]() mutable {
                    for (int i = 0; i < 30; ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                        auto jt = active_trivia_lobbies.find(game_id);
                        if (jt == active_trivia_lobbies.end()) return;
                        if (jt->second.timer_generation != new_gen) return;
                        if (jt->second.lobby_paused) { --i; continue; }
                    }
                    run_trivia_game_loop(bot, db, game_id);
                }).detach();
            }

            edit_trivia_lobby_message(bot, it->second);
            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }

        if (uint64_t game_id = parse_lobby_cmd("trivia_lobby_cancel:")) {
            std::unique_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.active || it->second.force_stop) {
                event.reply(dpp::ir_deferred_update_message, "");
                return;
            }

            uint64_t clicker_id = event.command.get_issuing_user().id;
            if (clicker_id != it->second.author_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("only the lobby host can cancel the lobby.").set_flags(dpp::m_ephemeral));
                return;
            }

            it->second.force_stop = true;
            for (auto pid : it->second.players) {
                if (it->second.bet_amount > 0) {
                    db->update_wallet(pid, it->second.bet_amount);
                }
            }

            dpp::message cancel_msg(it->second.channel_id, dpp::embed()
                .set_color(bronx::COLOR_ERROR)
                .set_title("trivia battle - cancelled")
                .set_description("lobby cancelled by the host."));
            cancel_msg.id = it->second.message_id;
            
            if (!it->second.interaction_token.empty()) {
                bot.interaction_response_edit(it->second.interaction_token, cancel_msg);
            } else {
                bronx::safe_message_edit(bot, cancel_msg);
            }

            active_trivia_lobbies.erase(game_id);
            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }

        if (uint64_t game_id = parse_lobby_cmd("trivia_lobby_start:")) {
            std::unique_lock<std::shared_mutex> lock(trivia_mutex);
            auto it = active_trivia_lobbies.find(game_id);
            if (it == active_trivia_lobbies.end() || it->second.active || it->second.force_stop) {
                event.reply(dpp::ir_deferred_update_message, "");
                return;
            }

            uint64_t clicker_id = event.command.get_issuing_user().id;
            if (clicker_id != it->second.author_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("only the lobby host can start the game early.").set_flags(dpp::m_ephemeral));
                return;
            }

            if (it->second.players.size() < 2) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("need at least 2 players to start.").set_flags(dpp::m_ephemeral));
                return;
            }

            it->second.timer_generation++;
            std::thread([game_id, &bot, db]() mutable {
                run_trivia_game_loop(bot, db, game_id);
            }).detach();

            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }
    });
}

inline Command* get_trivia_command(bronx::db::Database* db) {
    // Slash command options
    std::vector<dpp::command_option> options = {
        dpp::command_option(dpp::co_sub_command, "start", "start a multiplayer trivia battle")
            .add_option(dpp::command_option(dpp::co_integer, "wager", "optional bet amount (default 0)")),
        dpp::command_option(dpp::co_sub_command, "stats", "view trivia stats for a user")
            .add_option(dpp::command_option(dpp::co_user, "player", "user to view stats for")),
        dpp::command_option(dpp::co_sub_command, "categories", "list all available trivia categories")
    };

    static Command* trivia = new Command("trivia", "play trivia battle — multiplayer trivia game with wagers", "games", {"tv", "trivia-battle"}, true,
        // Prefix handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            std::string subcommand = "start";
            int64_t bet = 0;
            uint64_t target_user = event.msg.author.id;

            if (!args.empty()) {
                std::string arg0 = args[0];
                std::transform(arg0.begin(), arg0.end(), arg0.begin(), ::tolower);

                if (arg0 == "stats") {
                    subcommand = "stats";
                    if (args.size() > 1) {
                        target_user = bronx::moderation::parse_mention(args[1]);
                        if (target_user == 0) target_user = event.msg.author.id;
                    }
                } else if (arg0 == "categories" || arg0 == "cats") {
                    subcommand = "categories";
                } else if (arg0 == "start") {
                    subcommand = "start";
                    if (args.size() > 1) {
                        try {
                            bet = economy::parse_amount(args[1], db->get_wallet(event.msg.author.id));
                        } catch (...) {
                            bet = 0;
                        }
                    }
                } else {
                    // Try parsing as wager directly
                    try {
                        bet = economy::parse_amount(arg0, db->get_wallet(event.msg.author.id));
                        subcommand = "start";
                    } catch (...) {
                        bet = 0;
                        subcommand = "start";
                    }
                }
            }

            if (subcommand == "stats") {
                bronx::db::TriviaUserStats stats;
                db->get_trivia_stats(target_user, stats);

                double win_rate = stats.games_played > 0 ? (double)stats.games_won / stats.games_played * 100.0 : 0.0;
                double accuracy = stats.total_answers > 0 ? (double)stats.correct_answers / stats.total_answers * 100.0 : 0.0;

                std::string desc = "stats for <@" + std::to_string(target_user) + ">:\n\n"
                                   "• **games played:** " + std::to_string(stats.games_played) + "\n"
                                   "• **games won:** " + std::to_string(stats.games_won) + " (" + economy::format_number(win_rate) + "% win rate)\n"
                                   "• **accuracy:** " + std::to_string(stats.correct_answers) + "/" + std::to_string(stats.total_answers) + " (" + economy::format_number(accuracy) + "% correct)\n"
                                   "• **total earnings:** $" + economy::format_number(stats.total_winnings) + "\n";

                dpp::embed stats_embed = dpp::embed()
                    .set_color(bronx::COLOR_DEFAULT)
                    .set_title("🏆 trivia battle - user stats")
                    .set_description(desc);

                bot.message_create(dpp::message(event.msg.channel_id, stats_embed));
                return;
            }

            if (subcommand == "categories") {
                auto categories = db->get_trivia_categories();
                std::string desc = "available trivia categories:\n\n";
                if (categories.empty()) {
                    desc += "no categories available.";
                } else {
                    for (const auto& cat : categories) {
                        desc += "• " + cat + "\n";
                    }
                }

                dpp::embed cats_embed = dpp::embed()
                    .set_color(bronx::COLOR_DEFAULT)
                    .set_title("🏆 trivia battle - categories")
                    .set_description(desc);

                bot.message_create(dpp::message(event.msg.channel_id, cats_embed));
                return;
            }

            if (subcommand == "start") {
                uint64_t channel_id = event.msg.channel_id;
                uint64_t author_id = event.msg.author.id;

                // Check active games in the channel
                {
                    std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                    for (const auto& pair : active_trivia_lobbies) {
                        if (pair.second.channel_id == channel_id && !pair.second.force_stop) {
                            bot.message_create(dpp::message(channel_id, bronx::error("a trivia battle is already running in this channel.")));
                            return;
                        }
                    }
                }

                // Check bet
                if (bet < 0) {
                    bot.message_create(dpp::message(channel_id, bronx::error("wager must be a positive amount.")));
                    return;
                }

                if (bet > 0) {
                    auto author_wallet = db->update_wallet(author_id, -bet);
                    if (!author_wallet || *author_wallet < 0) {
                        if (author_wallet) {
                            db->update_wallet(author_id, bet);
                        }
                        bot.message_create(dpp::message(channel_id, bronx::error("you don't have enough money in your wallet to start this wager.")));
                        return;
                    }
                }

                TriviaLobby game;
                uint64_t game_id = (uint64_t)std::chrono::system_clock::now().time_since_epoch().count() + author_id;
                game.game_id = game_id;
                game.author_id = author_id;
                game.channel_id = channel_id;
                game.guild_id = event.msg.guild_id;
                game.bet_amount = bet;
                game.bet_escrowed = (bet > 0);
                game.players.push_back(author_id);
                game.ever_joined.insert(author_id);
                game.start_timestamp = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count() + 30;

                std::string gid_str = std::to_string(game_id);
                dpp::message msg(channel_id, build_trivia_lobby_embed(game));
                
                dpp::component row;
                row.set_type(dpp::cot_action_row);
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_success)
                    .set_label("join")
                    .set_id("trivia_lobby_join:" + gid_str));
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_secondary)
                    .set_label("leave")
                    .set_id("trivia_lobby_leave:" + gid_str));
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_primary)
                    .set_label("start early")
                    .set_id("trivia_lobby_start:" + gid_str));
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_danger)
                    .set_label("cancel")
                    .set_id("trivia_lobby_cancel:" + gid_str));
                msg.add_component(row);

                active_trivia_lobbies.emplace(std::piecewise_construct,
                    std::forward_as_tuple(game_id), std::tuple<>());
                {
                    auto& g = active_trivia_lobbies[game_id];
                    g.game_id = game.game_id;
                    g.author_id = game.author_id;
                    g.guild_id = game.guild_id;
                    g.channel_id = game.channel_id;
                    g.active = game.active;
                    g.bet_amount = game.bet_amount;
                    g.bet_escrowed = game.bet_escrowed;
                    g.players = game.players;
                    g.ever_joined = game.ever_joined;
                    g.start_timestamp = game.start_timestamp;
                }

                bot.message_create(msg, [game_id, &bot, db](const dpp::confirmation_callback_t& callback) mutable {
                    if (callback.is_error()) {
                        std::unique_lock<std::shared_mutex> lock(trivia_mutex);
                        active_trivia_lobbies.erase(game_id);
                        return;
                    }
                    auto sent_msg = callback.get<dpp::message>();
                    {
                        std::unique_lock<std::shared_mutex> lock(trivia_mutex);
                        auto it = active_trivia_lobbies.find(game_id);
                        if (it != active_trivia_lobbies.end()) {
                            it->second.message_id = sent_msg.id;
                        }
                    }

                    ::std::thread([game_id, &bot, db]() mutable {
                        uint32_t my_gen = 0;
                        {
                            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                            auto it = active_trivia_lobbies.find(game_id);
                            if (it != active_trivia_lobbies.end()) {
                                my_gen = it->second.timer_generation.load();
                            }
                        }
                        for (int i = 0; i < 30; ++i) {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                            auto jt = active_trivia_lobbies.find(game_id);
                            if (jt == active_trivia_lobbies.end()) return;
                            if (jt->second.timer_generation != my_gen) return;
                            if (jt->second.lobby_paused) { --i; continue; }
                        }
                        auto jt = active_trivia_lobbies.find(game_id);
                        if (jt == active_trivia_lobbies.end()) return;
                        if (jt->second.timer_generation != my_gen) return;
                        run_trivia_game_loop(bot, db, game_id);
                    }).detach();
                });
            }
        },
        // Slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto options = event.command.get_command_interaction().options;
            if (options.empty()) {
                event.reply(bronx::error("missing subcommand."));
                return;
            }

            auto subcommand_it = options.begin();
            std::string subcommand = subcommand_it->name;
            uint64_t target_user = event.command.get_issuing_user().id;

            if (subcommand == "stats") {
                if (!subcommand_it->options.empty()) {
                    for (const auto& opt : subcommand_it->options) {
                        if (opt.name == "player") {
                            target_user = std::get<dpp::snowflake>(opt.value);
                        }
                    }
                }
                
                bronx::db::TriviaUserStats stats;
                db->get_trivia_stats(target_user, stats);

                double win_rate = stats.games_played > 0 ? (double)stats.games_won / stats.games_played * 100.0 : 0.0;
                double accuracy = stats.total_answers > 0 ? (double)stats.correct_answers / stats.total_answers * 100.0 : 0.0;

                std::string desc = "stats for <@" + std::to_string(target_user) + ">:\n\n"
                                   "• **games played:** " + std::to_string(stats.games_played) + "\n"
                                   "• **games won:** " + std::to_string(stats.games_won) + " (" + economy::format_number(win_rate) + "% win rate)\n"
                                   "• **accuracy:** " + std::to_string(stats.correct_answers) + "/" + std::to_string(stats.total_answers) + " (" + economy::format_number(accuracy) + "% correct)\n"
                                   "• **total earnings:** $" + economy::format_number(stats.total_winnings) + "\n";

                dpp::embed stats_embed = dpp::embed()
                    .set_color(bronx::COLOR_DEFAULT)
                    .set_title("🏆 trivia battle - user stats")
                    .set_description(desc);

                event.reply(dpp::message().add_embed(stats_embed));
                return;
            }

            if (subcommand == "categories") {
                auto categories = db->get_trivia_categories();
                std::string desc = "available trivia categories:\n\n";
                if (categories.empty()) {
                    desc += "no categories available.";
                } else {
                    for (const auto& cat : categories) {
                        desc += "• " + cat + "\n";
                    }
                }

                dpp::embed cats_embed = dpp::embed()
                    .set_color(bronx::COLOR_DEFAULT)
                    .set_title("🏆 trivia battle - categories")
                    .set_description(desc);

                event.reply(dpp::message().add_embed(cats_embed));
                return;
            }

            if (subcommand == "start") {
                int64_t bet = 0;
                if (!subcommand_it->options.empty()) {
                    for (const auto& opt : subcommand_it->options) {
                        if (opt.name == "wager") {
                            bet = std::get<int64_t>(opt.value);
                        }
                    }
                }

                uint64_t channel_id = event.command.channel_id;
                uint64_t author_id = event.command.get_issuing_user().id;

                {
                    std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                    for (const auto& pair : active_trivia_lobbies) {
                        if (pair.second.channel_id == channel_id && !pair.second.force_stop) {
                            event.reply(dpp::message().add_embed(bronx::error("a trivia battle is already running in this channel.")));
                            return;
                        }
                    }
                }

                if (bet < 0) {
                    event.reply(dpp::message().add_embed(bronx::error("wager must be a positive amount.")));
                    return;
                }

                if (bet > 0) {
                    auto author_wallet = db->update_wallet(author_id, -bet);
                    if (!author_wallet || *author_wallet < 0) {
                        if (author_wallet) {
                            db->update_wallet(author_id, bet);
                        }
                        event.reply(dpp::message().add_embed(bronx::error("you don't have enough money in your wallet to start this wager.")));
                        return;
                    }
                }

                TriviaLobby game;
                uint64_t game_id = (uint64_t)std::chrono::system_clock::now().time_since_epoch().count() + author_id;
                game.game_id = game_id;
                game.author_id = author_id;
                game.channel_id = channel_id;
                game.guild_id = event.command.guild_id;
                game.bet_amount = bet;
                game.bet_escrowed = (bet > 0);
                game.players.push_back(author_id);
                game.ever_joined.insert(author_id);
                game.start_timestamp = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count() + 30;
                game.interaction_token = event.command.token;

                std::string gid_str = std::to_string(game_id);
                dpp::message msg(channel_id, build_trivia_lobby_embed(game));
                
                dpp::component row;
                row.set_type(dpp::cot_action_row);
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_success)
                    .set_label("join")
                    .set_id("trivia_lobby_join:" + gid_str));
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_secondary)
                    .set_label("leave")
                    .set_id("trivia_lobby_leave:" + gid_str));
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_primary)
                    .set_label("start early")
                    .set_id("trivia_lobby_start:" + gid_str));
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_danger)
                    .set_label("cancel")
                    .set_id("trivia_lobby_cancel:" + gid_str));
                msg.add_component(row);

                active_trivia_lobbies.emplace(std::piecewise_construct,
                    std::forward_as_tuple(game_id), std::tuple<>());
                {
                    auto& g = active_trivia_lobbies[game_id];
                    g.game_id = game.game_id;
                    g.author_id = game.author_id;
                    g.guild_id = game.guild_id;
                    g.channel_id = game.channel_id;
                    g.active = game.active;
                    g.bet_amount = game.bet_amount;
                    g.bet_escrowed = game.bet_escrowed;
                    g.players = game.players;
                    g.ever_joined = game.ever_joined;
                    g.start_timestamp = game.start_timestamp;
                    g.interaction_token = game.interaction_token;
                }

                auto interaction_token = event.command.token;
                event.reply(msg, [game_id, &bot, channel_id, interaction_token, db](const dpp::confirmation_callback_t& callback) mutable {
                    if (callback.is_error()) {
                        std::unique_lock<std::shared_mutex> lock(trivia_mutex);
                        active_trivia_lobbies.erase(game_id);
                        return;
                    }

                    bot.interaction_response_get_original(interaction_token, [game_id, channel_id, &bot](const dpp::confirmation_callback_t& get_callback) {
                        if (!get_callback.is_error()) {
                            try {
                                auto sent_msg = get_callback.get<dpp::message>();
                                std::unique_lock<std::shared_mutex> lock(trivia_mutex);
                                auto it = active_trivia_lobbies.find(game_id);
                                if (it != active_trivia_lobbies.end()) {
                                    it->second.message_id = sent_msg.id;
                                    it->second.channel_id = channel_id;
                                }
                            } catch (...) {}
                        }
                    });

                    std::thread([game_id, &bot, db]() mutable {
                        uint32_t my_gen = 0;
                        {
                            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                            auto it = active_trivia_lobbies.find(game_id);
                            if (it != active_trivia_lobbies.end()) {
                                my_gen = it->second.timer_generation.load();
                            }
                        }
                        for (int i = 0; i < 30; ++i) {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            std::shared_lock<std::shared_mutex> lock(trivia_mutex);
                            auto jt = active_trivia_lobbies.find(game_id);
                            if (jt == active_trivia_lobbies.end()) return;
                            if (jt->second.timer_generation != my_gen) return;
                            if (jt->second.lobby_paused) { --i; continue; }
                        }
                        auto jt = active_trivia_lobbies.find(game_id);
                        if (jt == active_trivia_lobbies.end()) return;
                        if (jt->second.timer_generation != my_gen) return;
                        run_trivia_game_loop(bot, db, game_id);
                    }).detach();
                });
            }
        },
        options
    );

    return trivia;
}

} // namespace games
} // namespace commands
