#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "../daily_challenges/daily_stat_tracker.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <algorithm>
#include <optional>
#include <future>

using namespace bronx::db;

namespace commands {
namespace passive {

// ── Stock Market Data Structures ───────────────────────────────────────────

struct Stock {
    std::string symbol;
    std::string company_name;
    int base_price;
    int current_price;
    double price_modifier;
    double trend;
};

struct StockHistory {
    int price;
    std::chrono::system_clock::time_point time;
};

struct UserStock {
    uint64_t user_id;
    std::string symbol;
    int shares;
    int avg_buy_price;
};

// ── Database Operations ─────────────────────────────────────────────────────

static std::vector<Stock> get_all_stocks(Database* db) {
    std::vector<Stock> stocks;
    auto conn = db->get_pool()->acquire();
    if (!conn) return stocks;
    
    std::string sql = "select symbol, company_name, base_price, current_price, price_modifier, trend from stocks order by symbol";
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                Stock s;
                s.symbol = row[0];
                s.company_name = row[1];
                s.base_price = std::stoi(row[2]);
                s.current_price = std::stoi(row[3]);
                s.price_modifier = std::stod(row[4]);
                s.trend = std::stod(row[5]);
                stocks.push_back(s);
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return stocks;
}

static std::optional<Stock> get_stock(Database* db, const std::string& symbol) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return std::nullopt;
    
    char esc_sym[21];
    mysql_real_escape_string(conn->get(), esc_sym, symbol.c_str(), symbol.size());
    
    std::string sql = "select symbol, company_name, base_price, current_price, price_modifier, trend from stocks where symbol = '" + std::string(esc_sym) + "'";
    
    std::optional<Stock> result;
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                Stock s;
                s.symbol = row[0];
                s.company_name = row[1];
                s.base_price = std::stoi(row[2]);
                s.current_price = std::stoi(row[3]);
                s.price_modifier = std::stod(row[4]);
                s.trend = std::stod(row[5]);
                result = s;
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return result;
}

static std::vector<StockHistory> get_stock_history(Database* db, const std::string& symbol) {
    std::vector<StockHistory> history;
    auto conn = db->get_pool()->acquire();
    if (!conn) return history;
    
    char esc_sym[21];
    mysql_real_escape_string(conn->get(), esc_sym, symbol.c_str(), symbol.size());
    
    std::string sql = "select price, recorded_at from stock_price_history where symbol = '" + std::string(esc_sym) + "' order by recorded_at desc limit 24";
    
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                StockHistory sh;
                sh.price = std::stoi(row[0]);
                struct tm tm = {};
                strptime(row[1], "%Y-%m-%d %H:%M:%S", &tm);
                sh.time = std::chrono::system_clock::from_time_t(timegm(&tm));
                history.push_back(sh);
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    // Reverse to chronological order for sparkline
    std::reverse(history.begin(), history.end());
    return history;
}

static std::vector<UserStock> get_user_portfolio(Database* db, uint64_t user_id) {
    std::vector<UserStock> portfolio;
    auto conn = db->get_pool()->acquire();
    if (!conn) return portfolio;
    
    std::string sql = "select user_id, symbol, shares, avg_buy_price from user_stocks where user_id = " + std::to_string(user_id) + " and shares > 0";
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                UserStock us;
                us.user_id = std::stoull(row[0]);
                us.symbol = row[1];
                us.shares = std::stoi(row[2]);
                us.avg_buy_price = std::stoi(row[3]);
                portfolio.push_back(us);
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return portfolio;
}

static std::optional<UserStock> get_user_stock(Database* db, uint64_t user_id, const std::string& symbol) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return std::nullopt;
    
    char esc_sym[21];
    mysql_real_escape_string(conn->get(), esc_sym, symbol.c_str(), symbol.size());
    
    std::string sql = "select user_id, symbol, shares, avg_buy_price from user_stocks where user_id = " + std::to_string(user_id) + " and symbol = '" + std::string(esc_sym) + "'";
    
    std::optional<UserStock> result;
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                UserStock us;
                us.user_id = std::stoull(row[0]);
                us.symbol = row[1];
                us.shares = std::stoi(row[2]);
                us.avg_buy_price = std::stoi(row[3]);
                result = us;
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return result;
}

static bool update_user_stock(Database* db, uint64_t user_id, const std::string& symbol, int shares, int avg_buy_price) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    char esc_sym[21];
    mysql_real_escape_string(conn->get(), esc_sym, symbol.c_str(), symbol.size());
    
    std::string sql;
    if (shares <= 0) {
        sql = "delete from user_stocks where user_id = " + std::to_string(user_id) + " and symbol = '" + std::string(esc_sym) + "'";
    } else {
        sql = "insert into user_stocks (user_id, symbol, shares, avg_buy_price) values (" +
            std::to_string(user_id) + ", '" + std::string(esc_sym) + "', " + std::to_string(shares) + ", " + std::to_string(avg_buy_price) + ") " +
            "on duplicate key update shares = " + std::to_string(shares) + ", avg_buy_price = " + std::to_string(avg_buy_price);
    }
    
    bool success = (mysql_query(conn->get(), sql.c_str()) == 0);
    db->get_pool()->release(conn);
    return success;
}

static void initialize_stocks(Database* db) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return;
    
    std::string check = "select count(*) from stocks";
    int count = 0;
    if (mysql_query(conn->get(), check.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) count = std::stoi(row[0]);
            mysql_free_result(res);
        }
    }
    
    if (count == 0) {
        struct { std::string sym; std::string name; int base; } companies[] = {
            {"brnx", "bronx inc", 150},
            {"fish", "fishing corp", 80},
            {"mine", "mining ltd", 120},
            {"bjhd", "blackjack holdings", 200},
            {"amco", "autominers co", 100}
        };
        for (const auto& c : companies) {
            char esc_name[201];
            mysql_real_escape_string(conn->get(), esc_name, c.name.c_str(), c.name.size());
            std::string sql = "insert ignore into stocks (symbol, company_name, base_price, current_price, price_modifier, trend) values ('" +
                c.sym + "', '" + std::string(esc_name) + "', " + std::to_string(c.base) + ", " + std::to_string(c.base) + ", 1.00, 0.00)";
            mysql_query(conn->get(), sql.c_str());
            
            std::string hist_sql = "insert into stock_price_history (symbol, price) values ('" + c.sym + "', " + std::to_string(c.base) + ")";
            mysql_query(conn->get(), hist_sql.c_str());
        }
    }
    db->get_pool()->release(conn);
}

// ── Price Fluctuation Engine ────────────────────────────────────────────────

static void update_stock_prices(Database* db) {
    initialize_stocks(db);
    
    std::vector<Stock> stocks = get_all_stocks(db);
    if (stocks.empty()) return;
    
    auto conn = db->get_pool()->acquire();
    if (!conn) return;
    
    static std::mt19937 rng(static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count()));
    std::uniform_real_distribution<double> dist_delta(-0.08, 0.08);
    std::uniform_real_distribution<double> dist_event(0.0, 1.0);
    
    for (auto& s : stocks) {
        double random_delta = dist_delta(rng);
        
        // momentum towards base price
        double momentum = (s.base_price - s.current_price) / (s.base_price * 10.0);
        
        // 5% chance of random market events
        double event_roll = dist_event(rng);
        double event_multiplier = 1.0;
        if (event_roll < 0.05) {
            if (dist_event(rng) < 0.5) {
                // Bull run: +15% to +30%
                event_multiplier = 1.15 + (dist_event(rng) * 0.15);
            } else {
                // Crash: -15% to -30%
                event_multiplier = 0.70 + (dist_event(rng) * 0.15);
            }
        }
        
        int new_price = static_cast<int>(s.current_price * (1.0 + random_delta + momentum) * event_multiplier);
        if (new_price < 1) new_price = 1;
        
        double actual_trend = (new_price - s.current_price) / static_cast<double>(s.current_price);
        double new_modifier = static_cast<double>(new_price) / static_cast<double>(s.base_price);
        
        std::string sql = "update stocks set current_price = " + std::to_string(new_price) + 
            ", price_modifier = " + std::to_string(new_modifier) + 
            ", trend = " + std::to_string(actual_trend) + 
            ", last_updated = CURRENT_TIMESTAMP WHERE symbol = '" + s.symbol + "'";
        mysql_query(conn->get(), sql.c_str());
        
        std::string hist_sql = "insert into stock_price_history (symbol, price) values ('" + s.symbol + "', " + std::to_string(new_price) + ")";
        mysql_query(conn->get(), hist_sql.c_str());
        
        std::string prune_sql = "delete from stock_price_history where symbol = '" + s.symbol + "' and id not in ("
            "select id from (select id from stock_price_history where symbol = '" + s.symbol + "' order by recorded_at desc limit 24) tmp)";
        mysql_query(conn->get(), prune_sql.c_str());
    }
    
    db->get_pool()->release(conn);
}

// ── Visual Sparkline & Format Helpers ────────────────────────────────────────

static std::string make_sparkline(const std::vector<StockHistory>& history) {
    if (history.empty()) return "—";
    
    const char* blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int min_p = history[0].price, max_p = history[0].price;
    for (const auto& h : history) {
        min_p = std::min(min_p, h.price);
        max_p = std::max(max_p, h.price);
    }
    if (min_p == max_p) return std::string(history.size(), '-');
    
    std::string spark;
    int step = std::max(1, (int)history.size() / 8);
    for (size_t i = 0; i < history.size(); i += step) {
        int idx = (int)(7.0 * (history[i].price - min_p) / (max_p - min_p));
        idx = std::clamp(idx, 0, 7);
        spark += blocks[idx];
    }
    return spark;
}

static std::string stock_trend_arrow(double trend) {
    if (trend > 0.02) return "📈";
    if (trend < -0.02) return "📉";
    return "➡️";
}

static std::string hourly_pct_change(double trend) {
    double pct = trend * 100.0;
    char buf[32];
    if (pct >= 0) {
        snprintf(buf, sizeof(buf), "+%.2f%%", pct);
    } else {
        snprintf(buf, sizeof(buf), "%.2f%%", pct);
    }
    return std::string(buf);
}

// ── Command Registration ────────────────────────────────────────────────────

inline Command* get_market_command(Database* db) {
    static Command* cmd = new Command(
        "market",
        "view and trade fictional stocks",
        "economy",
        {"stocks", "stock", "shares", "portfolio"},
        true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            // Run db interactions asynchronously on the event thread pool
            std::async(std::launch::async, [&bot, event, args, db]() {
                uint64_t uid = event.msg.author.id;
                db->ensure_user_exists(uid);
                initialize_stocks(db);
                
                std::string sub = args.empty() ? "view" : args[0];
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                
                if (sub == "buy") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `market buy <symbol> <shares>`"));
                        return;
                    }
                    std::string symbol = args[1];
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    
                    int shares = 0;
                    try { shares = std::stoi(args[2]); } catch(...) {}
                    if (shares <= 0) {
                        bronx::send_message(bot, event, bronx::error("specify a valid positive number of shares"));
                        return;
                    }
                    
                    auto s = get_stock(db, symbol);
                    if (!s) {
                        bronx::send_message(bot, event, bronx::error("invalid company symbol"));
                        return;
                    }
                    
                    int64_t total_cost = static_cast<int64_t>(s->current_price) * shares;
                    int64_t wallet = db->get_wallet(uid);
                    if (wallet < total_cost) {
                        bronx::send_message(bot, event, bronx::error("you do not have enough money (cost: $" + format_number(total_cost) + ")"));
                        return;
                    }
                    
                    auto us = get_user_stock(db, uid, symbol);
                    int current_shares = us ? us->shares : 0;
                    int current_avg = us ? us->avg_buy_price : 0;
                    
                    int new_shares = current_shares + shares;
                    int new_avg = static_cast<int>(((static_cast<int64_t>(current_shares) * current_avg) + total_cost) / new_shares);
                    
                    if (db->update_wallet(uid, -total_cost) && update_user_stock(db, uid, symbol, new_shares, new_avg)) {
                        daily_challenges::track_daily_stat(db, uid, "stocks_traded", shares);
                        bronx::send_message(bot, event, bronx::success("successfully bought " + std::to_string(shares) + " shares of " + s->company_name + " for $" + format_number(total_cost)));
                    } else {
                        bronx::send_message(bot, event, bronx::error("transaction failed"));
                    }
                    
                } else if (sub == "sell") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `market sell <symbol> <shares>`"));
                        return;
                    }
                    std::string symbol = args[1];
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    
                    int shares = 0;
                    try { shares = std::stoi(args[2]); } catch(...) {}
                    if (shares <= 0) {
                        bronx::send_message(bot, event, bronx::error("specify a valid positive number of shares"));
                        return;
                    }
                    
                    auto s = get_stock(db, symbol);
                    if (!s) {
                        bronx::send_message(bot, event, bronx::error("invalid company symbol"));
                        return;
                    }
                    
                    auto us = get_user_stock(db, uid, symbol);
                    if (!us || us->shares < shares) {
                        bronx::send_message(bot, event, bronx::error("you do not own enough shares of this stock"));
                        return;
                    }
                    
                    int64_t total_proceeds = static_cast<int64_t>(s->current_price) * shares;
                    int new_shares = us->shares - shares;
                    
                    if (db->update_wallet(uid, total_proceeds) && update_user_stock(db, uid, symbol, new_shares, us->avg_buy_price)) {
                        daily_challenges::track_daily_stat(db, uid, "stocks_traded", shares);
                        bronx::send_message(bot, event, bronx::success("successfully sold " + std::to_string(shares) + " shares of " + s->company_name + " for $" + format_number(total_proceeds)));
                    } else {
                        bronx::send_message(bot, event, bronx::error("transaction failed"));
                    }
                    
                } else if (sub == "portfolio") {
                    auto portfolio = get_user_portfolio(db, uid);
                    if (portfolio.empty()) {
                        bronx::send_message(bot, event, bronx::info("your portfolio is currently empty"));
                        return;
                    }
                    
                    std::string desc = "**your stock portfolio**\n\n";
                    int64_t total_value = 0;
                    int64_t total_cost = 0;
                    
                    for (const auto& us : portfolio) {
                        auto s = get_stock(db, us.symbol);
                        if (!s) continue;
                        
                        int64_t cost = static_cast<int64_t>(us.avg_buy_price) * us.shares;
                        int64_t value = static_cast<int64_t>(s->current_price) * us.shares;
                        int64_t returns = value - cost;
                        double returns_pct = cost > 0 ? (static_cast<double>(returns) / cost) * 100.0 : 0.0;
                        
                        total_value += value;
                        total_cost += cost;
                        
                        char pct_buf[32];
                        snprintf(pct_buf, sizeof(pct_buf), "%+.2f%%", returns_pct);
                        
                        desc += "• **" + s->company_name + "** (`" + us.symbol + "`)\n";
                        desc += "  shares: " + std::to_string(us.shares) + " | avg: $" + format_number(us.avg_buy_price) + "\n";
                        desc += "  value: $" + format_number(value) + " | profit/loss: $" + format_number(returns) + " (" + std::string(pct_buf) + ")\n\n";
                    }
                    
                    int64_t total_profit = total_value - total_cost;
                    double total_profit_pct = total_cost > 0 ? (static_cast<double>(total_profit) / total_cost) * 100.0 : 0.0;
                    char tot_pct_buf[32];
                    snprintf(tot_pct_buf, sizeof(tot_pct_buf), "%+.2f%%", total_profit_pct);
                    
                    desc += "**summary:**\n";
                    desc += "• total value: $" + format_number(total_value) + "\n";
                    desc += "• net return: $" + format_number(total_profit) + " (" + std::string(tot_pct_buf) + ")\n";
                    
                    uint32_t embed_color = bronx::COLOR_DEFAULT;
                    if (total_profit > 0) embed_color = bronx::COLOR_SUCCESS;
                    else if (total_profit < 0) embed_color = bronx::COLOR_ERROR;
                    
                    auto embed = bronx::create_embed(desc, embed_color);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    
                } else if (sub == "history") {
                    if (args.size() < 2) {
                        bronx::send_message(bot, event, bronx::error("usage: `market history <symbol>`"));
                        return;
                    }
                    std::string symbol = args[1];
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    
                    auto s = get_stock(db, symbol);
                    if (!s) {
                        bronx::send_message(bot, event, bronx::error("invalid company symbol"));
                        return;
                    }
                    
                    auto hist = get_stock_history(db, symbol);
                    std::string desc = "**company profile: " + s->company_name + " (" + s->symbol + ")**\n\n";
                    desc += "• current price: $" + format_number(s->current_price) + "\n";
                    desc += "• base price: $" + format_number(s->base_price) + "\n";
                    desc += "• hourly change: " + hourly_pct_change(s->trend) + " " + stock_trend_arrow(s->trend) + "\n\n";
                    
                    desc += "**price history (past 24h):**\n";
                    desc += "```\n" + make_sparkline(hist) + "\n```\n";
                    
                    for (int i = static_cast<int>(hist.size()) - 1; i >= std::max(0, static_cast<int>(hist.size()) - 5); --i) {
                        auto tt = std::chrono::system_clock::to_time_t(hist[i].time);
                        struct tm tm_est = *gmtime(&tt);
                        char time_buf[32];
                        strftime(time_buf, sizeof(time_buf), "%h:%m utc", &tm_est);
                        desc += "• " + std::string(time_buf) + ": $" + format_number(hist[i].price) + "\n";
                    }
                    
                    auto embed = bronx::create_embed(desc, bronx::COLOR_DEFAULT);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    
                } else { // view / overview
                    std::string symbol = args.empty() ? "" : args[0];
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    
                    if (symbol != "view" && !symbol.empty()) {
                        auto s = get_stock(db, symbol);
                        if (s) {
                            auto hist = get_stock_history(db, symbol);
                            std::string desc = "**company profile: " + s->company_name + " (" + s->symbol + ")**\n\n";
                            desc += "• current price: $" + format_number(s->current_price) + "\n";
                            desc += "• base price: $" + format_number(s->base_price) + "\n";
                            desc += "• hourly change: " + hourly_pct_change(s->trend) + " " + stock_trend_arrow(s->trend) + "\n\n";
                            
                            desc += "**price history (past 24h):**\n";
                            desc += "```\n" + make_sparkline(hist) + "\n```\n";
                            
                            for (int i = static_cast<int>(hist.size()) - 1; i >= std::max(0, static_cast<int>(hist.size()) - 5); --i) {
                                auto tt = std::chrono::system_clock::to_time_t(hist[i].time);
                                struct tm tm_est = *gmtime(&tt);
                                char time_buf[32];
                                strftime(time_buf, sizeof(time_buf), "%h:%m utc", &tm_est);
                                desc += "• " + std::string(time_buf) + ": $" + format_number(hist[i].price) + "\n";
                            }
                            
                            auto embed = bronx::create_embed(desc, bronx::COLOR_DEFAULT);
                            bronx::add_invoker_footer(embed, event.msg.author);
                            bronx::send_message(bot, event, embed);
                            return;
                        }
                    }
                    
                    std::vector<Stock> stocks = get_all_stocks(db);
                    std::string desc = "**fictional stock market**\n\n";
                    for (const auto& s : stocks) {
                        auto hist = get_stock_history(db, s.symbol);
                        desc += stock_trend_arrow(s.trend) + " **" + s.company_name + "** (`" + s.symbol + "`) — $" + format_number(s.current_price) + " (" + hourly_pct_change(s.trend) + ") " + make_sparkline(hist) + "\n";
                    }
                    desc += "\nuse `market buy <symbol> <shares>` to invest";
                    
                    auto embed = bronx::create_embed(desc, bronx::COLOR_DEFAULT);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                }
            });
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            std::async(std::launch::async, [&bot, event, db]() {
                uint64_t uid = event.command.get_issuing_user().id;
                db->ensure_user_exists(uid);
                initialize_stocks(db);
                
                std::string sub = "view";
                auto ci_options = event.command.get_command_interaction().options;
                if (ci_options.size() > 0) sub = ci_options[0].name;
                
                if (sub == "buy") {
                    std::string symbol = "";
                    int64_t shares = 0;
                    try {
                        if (ci_options[0].options.size() > 0) {
                            symbol = std::get<std::string>(ci_options[0].options[0].value);
                        }
                        if (ci_options[0].options.size() > 1) {
                            shares = std::get<int64_t>(ci_options[0].options[1].value);
                        }
                    } catch(...) {}
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    
                    if (shares <= 0 || symbol.empty()) {
                        event.reply(dpp::message().add_embed(bronx::error("specify a valid stock symbol and positive number of shares")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    auto s = get_stock(db, symbol);
                    if (!s) {
                        event.reply(dpp::message().add_embed(bronx::error("invalid company symbol")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    int64_t total_cost = static_cast<int64_t>(s->current_price) * shares;
                    int64_t wallet = db->get_wallet(uid);
                    if (wallet < total_cost) {
                        event.reply(dpp::message().add_embed(bronx::error("you do not have enough money (cost: $" + format_number(total_cost) + ")")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    auto us = get_user_stock(db, uid, symbol);
                    int current_shares = us ? us->shares : 0;
                    int current_avg = us ? us->avg_buy_price : 0;
                    
                    int new_shares = current_shares + shares;
                    int new_avg = static_cast<int>(((static_cast<int64_t>(current_shares) * current_avg) + total_cost) / new_shares);
                    
                    if (db->update_wallet(uid, -total_cost) && update_user_stock(db, uid, symbol, new_shares, new_avg)) {
                        daily_challenges::track_daily_stat(db, uid, "stocks_traded", shares);
                        event.reply(dpp::message().add_embed(bronx::success("successfully bought " + std::to_string(shares) + " shares of " + s->company_name + " for $" + format_number(total_cost))));
                    } else {
                        event.reply(dpp::message().add_embed(bronx::error("transaction failed")).set_flags(dpp::m_ephemeral));
                    }
                    
                } else if (sub == "sell") {
                    std::string symbol = "";
                    int64_t shares = 0;
                    try {
                        if (ci_options[0].options.size() > 0) {
                            symbol = std::get<std::string>(ci_options[0].options[0].value);
                        }
                        if (ci_options[0].options.size() > 1) {
                            shares = std::get<int64_t>(ci_options[0].options[1].value);
                        }
                    } catch(...) {}
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    
                    if (shares <= 0 || symbol.empty()) {
                        event.reply(dpp::message().add_embed(bronx::error("specify a valid stock symbol and positive number of shares")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    auto s = get_stock(db, symbol);
                    if (!s) {
                        event.reply(dpp::message().add_embed(bronx::error("invalid company symbol")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    auto us = get_user_stock(db, uid, symbol);
                    if (!us || us->shares < shares) {
                        event.reply(dpp::message().add_embed(bronx::error("you do not own enough shares of this stock")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    int64_t total_proceeds = static_cast<int64_t>(s->current_price) * shares;
                    int new_shares = us->shares - shares;
                    
                    if (db->update_wallet(uid, total_proceeds) && update_user_stock(db, uid, symbol, new_shares, us->avg_buy_price)) {
                        daily_challenges::track_daily_stat(db, uid, "stocks_traded", shares);
                        event.reply(dpp::message().add_embed(bronx::success("successfully sold " + std::to_string(shares) + " shares of " + s->company_name + " for $" + format_number(total_proceeds))));
                    } else {
                        event.reply(dpp::message().add_embed(bronx::error("transaction failed")).set_flags(dpp::m_ephemeral));
                    }
                    
                } else if (sub == "portfolio") {
                    auto portfolio = get_user_portfolio(db, uid);
                    if (portfolio.empty()) {
                        event.reply(dpp::message().add_embed(bronx::info("your portfolio is currently empty")));
                        return;
                    }
                    
                    std::string desc = "**your stock portfolio**\n\n";
                    int64_t total_value = 0;
                    int64_t total_cost = 0;
                    
                    for (const auto& us : portfolio) {
                        auto s = get_stock(db, us.symbol);
                        if (!s) continue;
                        
                        int64_t cost = static_cast<int64_t>(us.avg_buy_price) * us.shares;
                        int64_t value = static_cast<int64_t>(s->current_price) * us.shares;
                        int64_t returns = value - cost;
                        double returns_pct = cost > 0 ? (static_cast<double>(returns) / cost) * 100.0 : 0.0;
                        
                        total_value += value;
                        total_cost += cost;
                        
                        char pct_buf[32];
                        snprintf(pct_buf, sizeof(pct_buf), "%+.2f%%", returns_pct);
                        
                        desc += "• **" + s->company_name + "** (`" + us.symbol + "`)\n";
                        desc += "  shares: " + std::to_string(us.shares) + " | avg: $" + format_number(us.avg_buy_price) + "\n";
                        desc += "  value: $" + format_number(value) + " | profit/loss: $" + format_number(returns) + " (" + std::string(pct_buf) + ")\n\n";
                    }
                    
                    int64_t total_profit = total_value - total_cost;
                    double total_profit_pct = total_cost > 0 ? (static_cast<double>(total_profit) / total_cost) * 100.0 : 0.0;
                    char tot_pct_buf[32];
                    snprintf(tot_pct_buf, sizeof(tot_pct_buf), "%+.2f%%", total_profit_pct);
                    
                    desc += "**summary:**\n";
                    desc += "• total value: $" + format_number(total_value) + "\n";
                    desc += "• net return: $" + format_number(total_profit) + " (" + std::string(tot_pct_buf) + ")\n";
                    
                    uint32_t embed_color = bronx::COLOR_DEFAULT;
                    if (total_profit > 0) embed_color = bronx::COLOR_SUCCESS;
                    else if (total_profit < 0) embed_color = bronx::COLOR_ERROR;
                    
                    auto embed = bronx::create_embed(desc, embed_color);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                    
                } else if (sub == "history") {
                    std::string symbol = "";
                    try {
                        if (ci_options[0].options.size() > 0) {
                            symbol = std::get<std::string>(ci_options[0].options[0].value);
                        }
                    } catch(...) {}
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    
                    auto s = get_stock(db, symbol);
                    if (!s) {
                        event.reply(dpp::message().add_embed(bronx::error("invalid company symbol")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    auto hist = get_stock_history(db, symbol);
                    std::string desc = "**company profile: " + s->company_name + " (" + s->symbol + ")**\n\n";
                    desc += "• current price: $" + format_number(s->current_price) + "\n";
                    desc += "• base price: $" + format_number(s->base_price) + "\n";
                    desc += "• hourly change: " + hourly_pct_change(s->trend) + " " + stock_trend_arrow(s->trend) + "\n\n";
                    
                    desc += "**price history (past 24h):**\n";
                    desc += "```\n" + make_sparkline(hist) + "\n```\n";
                    
                    for (int i = static_cast<int>(hist.size()) - 1; i >= std::max(0, static_cast<int>(hist.size()) - 5); --i) {
                        auto tt = std::chrono::system_clock::to_time_t(hist[i].time);
                        struct tm tm_est = *gmtime(&tt);
                        char time_buf[32];
                        strftime(time_buf, sizeof(time_buf), "%h:%m utc", &tm_est);
                        desc += "• " + std::string(time_buf) + ": $" + format_number(hist[i].price) + "\n";
                    }
                    
                    auto embed = bronx::create_embed(desc, bronx::COLOR_DEFAULT);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                    
                } else { // view
                    std::string symbol = "";
                    try {
                        if (ci_options[0].options.size() > 0) {
                            symbol = std::get<std::string>(ci_options[0].options[0].value);
                        }
                    } catch(...) {}
                    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::tolower);
                    
                    if (!symbol.empty()) {
                        auto s = get_stock(db, symbol);
                        if (s) {
                            auto hist = get_stock_history(db, symbol);
                            std::string desc = "**company profile: " + s->company_name + " (" + s->symbol + ")**\n\n";
                            desc += "• current price: $" + format_number(s->current_price) + "\n";
                            desc += "• base price: $" + format_number(s->base_price) + "\n";
                            desc += "• hourly change: " + hourly_pct_change(s->trend) + " " + stock_trend_arrow(s->trend) + "\n\n";
                            
                            desc += "**price history (past 24h):**\n";
                            desc += "```\n" + make_sparkline(hist) + "\n```\n";
                            
                            for (int i = static_cast<int>(hist.size()) - 1; i >= std::max(0, static_cast<int>(hist.size()) - 5); --i) {
                                auto tt = std::chrono::system_clock::to_time_t(hist[i].time);
                                struct tm tm_est = *gmtime(&tt);
                                char time_buf[32];
                                strftime(time_buf, sizeof(time_buf), "%h:%m utc", &tm_est);
                                desc += "• " + std::string(time_buf) + ": $" + format_number(hist[i].price) + "\n";
                            }
                            
                            auto embed = bronx::create_embed(desc, bronx::COLOR_DEFAULT);
                            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                            event.reply(dpp::message().add_embed(embed));
                            return;
                        }
                    }
                    
                    std::vector<Stock> stocks = get_all_stocks(db);
                    std::string desc = "**fictional stock market**\n\n";
                    for (const auto& s : stocks) {
                        auto hist = get_stock_history(db, s.symbol);
                        desc += stock_trend_arrow(s.trend) + " **" + s.company_name + "** (`" + s.symbol + "`) — $" + format_number(s.current_price) + " (" + hourly_pct_change(s.trend) + ") " + make_sparkline(hist) + "\n";
                    }
                    desc += "\nuse `market buy <symbol> <shares>` to invest";
                    
                    auto embed = bronx::create_embed(desc, bronx::COLOR_DEFAULT);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                }
            });
        },
        // options
        {
            dpp::command_option(dpp::co_sub_command, "view", "view all stocks or a specific one")
                .add_option(dpp::command_option(dpp::co_string, "symbol", "company symbol (optional)", false)),
            dpp::command_option(dpp::co_sub_command, "buy", "buy shares of a stock")
                .add_option(dpp::command_option(dpp::co_string, "symbol", "company symbol", true))
                .add_option(dpp::command_option(dpp::co_integer, "shares", "number of shares to buy", true)),
            dpp::command_option(dpp::co_sub_command, "sell", "sell shares of a stock")
                .add_option(dpp::command_option(dpp::co_string, "symbol", "company symbol", true))
                .add_option(dpp::command_option(dpp::co_integer, "shares", "number of shares to sell", true)),
            dpp::command_option(dpp::co_sub_command, "portfolio", "view your current holdings"),
            dpp::command_option(dpp::co_sub_command, "history", "view detailed stock history")
                .add_option(dpp::command_option(dpp::co_string, "symbol", "company symbol", true))
        }
    );
    return cmd;
}

} // namespace passive
} // namespace commands
