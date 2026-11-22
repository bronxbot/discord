#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../../database/operations/economy/gambling_verification.h"
#include "../economy_core.h"
#include "gambling_helpers.h"
#include "../daily_challenges/challenges.h"
#include <dpp/dpp.h>
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <chrono>

using namespace bronx::db;

namespace commands {
namespace gambling {

struct HiloCard {
    int value; // 1 to 13 (Ace = 1, Jack = 11, Queen = 12, King = 13)
    std::string suit; // spade, heart, diamond, club

    std::string get_name() const {
        std::string val_str;
        if (value == 1) val_str = "ace";
        else if (value == 11) val_str = "jack";
        else if (value == 12) val_str = "queen";
        else if (value == 13) val_str = "king";
        else val_str = std::to_string(value);
        return val_str + " of " + suit;
    }
};

struct HiloGame {
    uint64_t user_id;
    uint64_t message_id;
    uint64_t channel_id;
    int64_t initial_bet;
    double current_multiplier;
    HiloCard current_card;
    int consecutive_guesses;
    bool active;
    bool game_over;
    bool won;
    std::chrono::steady_clock::time_point created_at;
};

extern std::map<uint64_t, HiloGame> active_hilo_games;
extern std::mutex hilo_games_mutex;

static constexpr int HILO_TIMEOUT_SECONDS = 300;

inline HiloCard draw_hilo_card() {
    static const std::vector<std::string> suits = {"spades ♠", "hearts ♥", "diamonds ♦", "clubs ♣"};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> val_dis(1, 13);
    std::uniform_int_distribution<> suit_dis(0, 3);
    return HiloCard{val_dis(gen), suits[suit_dis(gen)]};
}

inline double get_hilo_multiplier(int val, const std::string& choice) {
    // 3% house edge
    double edge = 0.97;
    if (choice == "higher") {
        int higher_cards = 13 - val;
        if (higher_cards <= 0) return 0.0;
        return edge * (13.0 / higher_cards);
    } else if (choice == "lower") {
        int lower_cards = val - 1;
        if (lower_cards <= 0) return 0.0;
        return edge * (13.0 / lower_cards);
    } else {
        // Equal
        return edge * 13.0;
    }
}

inline dpp::message build_hilo_message(const HiloGame& game) {
    std::string title = "card high-low";
    if (game.game_over) {
        title += game.won ? " - cashed out" : " - game over";
    }

    std::ostringstream desc_ss;
    desc_ss << "**bet:** $" << format_number(game.initial_bet) << "\n"
            << "**current card:** " << game.current_card.get_name() << "\n"
            << "**consecutive guesses:** " << game.consecutive_guesses << "\n";
            
    std::ostringstream mult_ss;
    mult_ss << std::fixed << std::setprecision(2) << game.current_multiplier;
    desc_ss << "**current multiplier:** " << mult_ss.str() << "x\n"
            << "**current payout:** $" << format_number((int64_t)(game.initial_bet * game.current_multiplier)) << "\n\n";

    if (!game.game_over) {
        double mult_high = get_hilo_multiplier(game.current_card.value, "higher");
        double mult_low = get_hilo_multiplier(game.current_card.value, "lower");
        
        desc_ss << "**next card guesses:**\n";
        if (mult_high > 0.0) {
            std::ostringstream h_ss;
            h_ss << std::fixed << std::setprecision(2) << mult_high;
            desc_ss << "• higher: " << h_ss.str() << "x\n";
        }
        if (mult_low > 0.0) {
            std::ostringstream l_ss;
            l_ss << std::fixed << std::setprecision(2) << mult_low;
            desc_ss << "• lower: " << l_ss.str() << "x\n";
        }
        std::ostringstream e_ss;
        e_ss << std::fixed << std::setprecision(2) << get_hilo_multiplier(game.current_card.value, "equal");
        desc_ss << "• equal: " << e_ss.str() << "x\n\n"
                << "predict if the next card will be higher or lower!";
    }

    dpp::embed embed = dpp::embed()
        .set_title(title)
        .set_description(desc_ss.str());

    if (game.game_over) {
        embed.set_color(game.won ? bronx::COLOR_SUCCESS : bronx::COLOR_ERROR);
    } else {
        embed.set_color(bronx::COLOR_DEFAULT);
    }

    dpp::message msg;
    msg.add_embed(embed);

    if (!game.game_over) {
        dpp::component row;
        row.set_type(dpp::cot_action_row);

        row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("higher")
            .set_style(dpp::cos_success)
            .set_id("hilo_higher_" + std::to_string(game.user_id))
            .set_disabled(game.current_card.value == 13));

        row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("lower")
            .set_style(dpp::cos_danger)
            .set_id("hilo_lower_" + std::to_string(game.user_id))
            .set_disabled(game.current_card.value == 1));

        row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("equal")
            .set_style(dpp::cos_primary)
            .set_id("hilo_equal_" + std::to_string(game.user_id)));

        row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("cash out")
            .set_style(dpp::cos_secondary)
            .set_id("hilo_cashout_" + std::to_string(game.user_id))
            .set_disabled(game.consecutive_guesses == 0));

        msg.add_component(row);
    }

    return msg;
}

inline Command* get_hilo_command(Database* db) {
    static Command* hilo = new Command("hilo", "play card high-low game", "gambling", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!db->try_claim_cooldown(event.msg.author.id, "hilo", 3)) {
                bronx::send_message(bot, event, bronx::error("slow down! wait a few seconds between games"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: gamble hilo <amount>"));
                return;
            }

            auto user = db->get_user(event.msg.author.id);
            if (!user) return;

            int64_t bet;
            try {
                bet = parse_amount(args[0], user->wallet);
            } catch (...) {
                bronx::send_message(bot, event, bronx::error("invalid bet amount"));
                return;
            }

            if (bet < 50) {
                bronx::send_message(bot, event, bronx::error("minimum bet is $50"));
                return;
            }

            if (bet > user->wallet) {
                bronx::send_message(bot, event, bronx::error("you don't have that much"));
                return;
            }

            // Deduct bet async
            uint64_t author_id = event.msg.author.id;
            std::thread([db, author_id, bet]() {
                db->update_wallet(author_id, -bet);
            }).detach();

            HiloGame game;
            game.user_id = author_id;
            game.channel_id = event.msg.channel_id;
            game.initial_bet = bet;
            game.current_multiplier = 1.0;
            game.current_card = draw_hilo_card();
            game.consecutive_guesses = 0;
            game.active = true;
            game.game_over = false;
            game.won = false;
            game.created_at = std::chrono::steady_clock::now();

            dpp::message msg = build_hilo_message(game);
            bot.message_create(msg, [game, event](const dpp::confirmation_callback_t& callback) mutable {
                if (!callback.is_error()) {
                    auto sent_msg = std::get<dpp::message>(callback.value);
                    game.message_id = sent_msg.id;
                    std::lock_guard<std::mutex> lock(hilo_games_mutex);
                    active_hilo_games[event.msg.author.id] = game;
                }
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "hilo", 3)) {
                event.reply(dpp::message().add_embed(bronx::error("slow down! wait a few seconds between games")));
                return;
            }

            auto amount_param = event.get_parameter("amount");
            std::string amount_str;
            if (std::holds_alternative<std::string>(amount_param)) {
                amount_str = std::get<std::string>(amount_param);
            } else if (std::holds_alternative<int64_t>(amount_param)) {
                amount_str = std::to_string(std::get<int64_t>(amount_param));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("please provide a bet amount")));
                return;
            }

            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found")));
                return;
            }

            int64_t bet;
            try {
                bet = parse_amount(amount_str, user->wallet);
            } catch (...) {
                event.reply(dpp::message().add_embed(bronx::error("invalid bet amount")));
                return;
            }

            if (bet < 50) {
                event.reply(dpp::message().add_embed(bronx::error("minimum bet is $50")));
                return;
            }

            if (bet > user->wallet) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have that much")));
                return;
            }

            // Deduct bet async
            uint64_t user_id = event.command.get_issuing_user().id;
            std::thread([db, user_id, bet]() {
                db->update_wallet(user_id, -bet);
            }).detach();

            HiloGame game;
            game.user_id = user_id;
            game.channel_id = event.command.channel_id;
            game.initial_bet = bet;
            game.current_multiplier = 1.0;
            game.current_card = draw_hilo_card();
            game.consecutive_guesses = 0;
            game.active = true;
            game.game_over = false;
            game.won = false;
            game.created_at = std::chrono::steady_clock::now();

            event.reply(build_hilo_message(game), [game](const dpp::confirmation_callback_t& callback) mutable {
                if (!callback.is_error()) {
                    auto sent_msg = callback.get<dpp::message>();
                    game.message_id = sent_msg.id;
                    std::lock_guard<std::mutex> lock(hilo_games_mutex);
                    active_hilo_games[game.user_id] = game;
                }
            });
        });
        
    return hilo;
}

inline void handle_hilo_action(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, const std::string& choice) {
    uint64_t user_id = event.command.get_issuing_user().id;
    
    std::lock_guard<std::mutex> lock(hilo_games_mutex);
    auto it = active_hilo_games.find(user_id);
    if (it == active_hilo_games.end()) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::error("game not found or expired")).set_flags(dpp::m_ephemeral));
        return;
    }

    HiloGame& game = it->second;
    if (!game.active) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::error("game is already over")).set_flags(dpp::m_ephemeral));
        return;
    }

    if (choice == "cashout") {
        if (game.consecutive_guesses == 0) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("make at least one guess first")).set_flags(dpp::m_ephemeral));
            return;
        }

        game.active = false;
        game.game_over = true;
        game.won = true;
        
        int64_t payout = (int64_t)(game.initial_bet * game.current_multiplier);
        
        // Apply winnings async
        std::thread([db, user_id, payout, game]() {
            db->update_wallet(user_id, payout);
            int64_t profit = payout - game.initial_bet;
            if (profit > 0) {
                db->increment_stat(user_id, "gambling_profit", profit);
            }
            // Log history
            int64_t balance_after = db->get_wallet(user_id);
            bronx::db::history_operations::log_gambling(db, user_id, "cashed out hilo ($" + format_number(profit) + " profit)", payout, balance_after);
        }).detach();

        // Track stats
        std::string today = daily_challenges::get_today_est();
        db->track_challenge_attempt(user_id, "precision_gambler", true, today);
        db->track_challenge_variety(user_id, "game_master", "hilo", today);

        event.reply(dpp::ir_update_message, build_hilo_message(game));
        active_hilo_games.erase(user_id);
        return;
    }

    // Process guess
    double step_mult = get_hilo_multiplier(game.current_card.value, choice);
    HiloCard next_card = draw_hilo_card();
    
    bool correct = false;
    if (choice == "higher") {
        correct = next_card.value > game.current_card.value;
    } else if (choice == "lower") {
        correct = next_card.value < game.current_card.value;
    } else if (choice == "equal") {
        correct = next_card.value == game.current_card.value;
    }

    if (correct) {
        game.current_multiplier *= step_mult;
        game.current_card = next_card;
        game.consecutive_guesses++;
        event.reply(dpp::ir_update_message, build_hilo_message(game));
    } else {
        game.active = false;
        game.game_over = true;
        game.won = false;
        game.current_multiplier = 0.0;
        
        // Log loss async
        std::thread([db, user_id, game]() {
            db->increment_stat(user_id, "gambling_losses", game.initial_bet);
            int64_t balance_after = db->get_wallet(user_id);
            bronx::db::history_operations::log_gambling(db, user_id, "lost hilo ($" + format_number(game.initial_bet) + ")", 0, balance_after);
        }).detach();

        // Track stats
        std::string today = daily_challenges::get_today_est();
        db->track_challenge_attempt(user_id, "precision_gambler", false, today);
        db->track_challenge_variety(user_id, "game_master", "hilo", today);

        event.reply(dpp::ir_update_message, build_hilo_message(game));
        active_hilo_games.erase(user_id);
    }
}

inline void register_hilo_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        std::string custom_id = event.custom_id;
        if (custom_id.find("hilo_") != 0) return;

        try {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t user_id = std::stoull(custom_id.substr(last_underscore + 1));
            
            if (event.command.get_issuing_user().id != user_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this is not your game!")).set_flags(dpp::m_ephemeral));
                return;
            }

            std::string action;
            if (custom_id.find("hilo_higher_") == 0) action = "higher";
            else if (custom_id.find("hilo_lower_") == 0) action = "lower";
            else if (custom_id.find("hilo_equal_") == 0) action = "equal";
            else if (custom_id.find("hilo_cashout_") == 0) action = "cashout";

            handle_hilo_action(bot, db, event, action);
        } catch (...) {
            try {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("an error occurred processing this interaction")).set_flags(dpp::m_ephemeral));
            } catch (...) {}
        }
    });
}

} // namespace gambling
} // namespace commands
