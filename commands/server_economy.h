#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/economy/server_economy_operations.h"
#include "../database/operations/moderation/permission_operations.h"
#include "../performance/async_stat_writer.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <sstream>

using namespace bronx::db;
using namespace bronx::db::server_economy_operations;

namespace commands {
namespace server_economy {

inline Command* create_servereconomy_command(Database* db) {
    return new Command(
        "servereconomy",
        "manage server-specific economy settings",
        "admin",
        {"se", "serverecon"},
        true,
        // Text handler (not used for this command)
        nullptr,
        // Slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!event.command.guild_id) {
                event.reply(dpp::message("this command can only be used in servers!").set_flags(dpp::m_ephemeral));
                return;
            }

            uint64_t guild_id = event.command.guild_id;
            uint64_t user_id = event.command.usr.id;

            // Check admin permissions via Discord perms
            dpp::guild* g = dpp::find_guild(guild_id);
            if (!g) {
                event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " could not find guild.").set_flags(dpp::m_ephemeral));
                return;
            }
            auto member_it = g->members.find(user_id);
            if (member_it == g->members.end()) {
                event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " member data not found.").set_flags(dpp::m_ephemeral));
                return;
            }
            dpp::permission perms = g->base_permissions(member_it->second);
            if (!perms.can(dpp::p_administrator)) {
                event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " you need administrator permissions to use this command!").set_flags(dpp::m_ephemeral));
                return;
            }

            auto subcommand = event.command.get_command_interaction().options[0];

            if (subcommand.name == "toggle") {
                // --- TOGGLE ---
                std::string mode = std::get<std::string>(subcommand.options[0].value);

                if (set_economy_mode(db, guild_id, mode)) {
                    // Log activity for dashboard
                    if (bronx::perf::g_stat_writer) {
                        std::string user_name = event.command.usr.global_name.empty()
                            ? event.command.usr.username
                            : event.command.usr.global_name;
                        std::string action = mode == "server"
                            ? "switched to <b>server economy</b> mode"
                            : "switched to <b>global economy</b> mode";
                        bronx::perf::g_stat_writer->enqueue_activity_log(
                            guild_id, event.command.usr.id, user_name, action);
                    }

                    dpp::embed embed = dpp::embed()
                        .set_color(bronx::COLOR_SUCCESS)
                        .set_title(std::string(bronx::EMOJI_CHECK) + " economy mode updated")
                        .set_description(mode == "server"
                            ? "this server now uses a **server-specific economy**!\n\n"
                              "all economy commands will now operate on server-specific balances.\n"
                              "users start fresh with configured starting amounts.\n\n"
                              "use `/servereconomy config` to customize settings."
                            : "this server now uses the **global economy**!\n\n"
                              "all economy commands will use global user balances shared across all servers.")
                        .set_footer(dpp::embed_footer().set_text("server economy system"))
                        .set_timestamp(time(0));

                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " failed to update economy mode. please try again.").set_flags(dpp::m_ephemeral));
                }

            } else if (subcommand.name == "status") {
                // --- STATUS ---
                auto settings = get_guild_economy_settings(db, guild_id);

                if (!settings) {
                    event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " failed to retrieve economy settings.").set_flags(dpp::m_ephemeral));
                    return;
                }

                dpp::embed embed = dpp::embed()
                    .set_color(settings->economy_mode == "server" ? bronx::COLOR_INFO : bronx::COLOR_DEFAULT)
                    .set_title("\xf0\x9f\x8f\xa6 server economy settings")
                    .add_field(
                        "economy mode",
                        settings->economy_mode == "server" ? "\xF0\x9F\x8C\x90 **server economy**" : "\xF0\x9F\x8C\x8D **global economy**",
                        false
                    );

                if (settings->economy_mode == "server") {
                    std::ostringstream starting_vals;
                    starting_vals << "\xF0\x9F\x92\xB5 wallet: **" << settings->starting_wallet << "**\n"
                                  << "\xF0\x9F\x8F\xA6 bank limit: **" << settings->starting_bank_limit << "**\n"
                                  << "\xF0\x9F\x93\x88 interest rate: **" << settings->default_interest_rate << "%**";
                    embed.add_field("starting values", starting_vals.str(), true);

                    std::ostringstream multipliers;
                    multipliers << "\xF0\x9F\x92\xBC work: **" << settings->work_multiplier << "x**\n"
                                << "\xF0\x9F\x8E\xB0 gambling: **" << settings->gambling_multiplier << "x**\n"
                                << "\xF0\x9F\x8E\xA3 fishing: **" << settings->fishing_multiplier << "x**";
                    embed.add_field("multipliers", multipliers.str(), true);

                    std::string features;
                    features += std::string(settings->allow_gambling ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " gambling\n";
                    features += std::string(settings->allow_fishing ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " fishing\n";
                    features += std::string(settings->allow_trading ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " trading\n";
                    features += std::string(settings->allow_robbery ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " robbery";
                    embed.add_field("features", features, true);

                    if (settings->enable_tax) {
                        std::ostringstream tax_info;
                        tax_info << bronx::EMOJI_CHECK << " enabled\n\xF0\x9F\x92\xB8 rate: **" << settings->transaction_tax_percent << "%**";
                        embed.add_field("tax system", tax_info.str(), false);
                    }
                }

                embed.set_footer(dpp::embed_footer().set_text("use /servereconomy config to modify settings"))
                    .set_timestamp(time(0));

                event.reply(dpp::message().add_embed(embed));

            } else if (subcommand.name == "config") {
                // --- CONFIG ---
                auto settings = get_guild_economy_settings(db, guild_id);

                if (!settings) {
                    event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " failed to retrieve economy settings.").set_flags(dpp::m_ephemeral));
                    return;
                }

                auto conn = db->get_pool()->acquire();
                std::string query = "UPDATE guild_settings SET ";
                std::vector<std::string> updates;

                for (const auto& opt : subcommand.options) {
                    if (opt.name == "starting_wallet") {
                        int64_t value = std::get<int64_t>(opt.value);
                        updates.push_back("starting_wallet = " + std::to_string(value));
                    } else if (opt.name == "starting_bank_limit") {
                        int64_t value = std::get<int64_t>(opt.value);
                        updates.push_back("starting_bank_limit = " + std::to_string(value));
                    } else if (opt.name == "work_multiplier") {
                        double value = std::get<double>(opt.value);
                        updates.push_back("work_multiplier = " + std::to_string(value));
                    } else if (opt.name == "gambling_multiplier") {
                        double value = std::get<double>(opt.value);
                        updates.push_back("gambling_multiplier = " + std::to_string(value));
                    } else if (opt.name == "fishing_multiplier") {
                        double value = std::get<double>(opt.value);
                        updates.push_back("fishing_multiplier = " + std::to_string(value));
                    }
                }

                if (updates.empty()) {
                    event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " no settings to update.").set_flags(dpp::m_ephemeral));
                    db->get_pool()->release(conn);
                    return;
                }

                query += updates[0];
                for (size_t i = 1; i < updates.size(); i++) {
                    query += ", " + updates[i];
                }
                query += " WHERE guild_id = " + std::to_string(guild_id);

                if (mysql_query(conn->get(), query.c_str()) == 0) {
                    dpp::embed embed = dpp::embed()
                        .set_color(bronx::COLOR_SUCCESS)
                        .set_title(std::string(bronx::EMOJI_CHECK) + " economy settings updated")
                        .set_description("server economy configuration has been updated successfully!")
                        .set_footer(dpp::embed_footer().set_text("use /servereconomy status to view all settings"))
                        .set_timestamp(time(0));

                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " failed to update settings.").set_flags(dpp::m_ephemeral));
                }

                db->get_pool()->release(conn);

            } else if (subcommand.name == "features") {
                // --- FEATURES ---
                auto conn = db->get_pool()->acquire();
                std::string query = "UPDATE guild_settings SET ";
                std::vector<std::string> updates;
                std::vector<std::string> changes;

                for (const auto& opt : subcommand.options) {
                    bool value = std::get<bool>(opt.value);
                    std::string db_value = value ? "TRUE" : "FALSE";

                    if (opt.name == "gambling") {
                        updates.push_back("allow_gambling = " + db_value);
                        changes.push_back(std::string(value ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + (value ? " enabled" : " disabled") + " gambling");
                    } else if (opt.name == "fishing") {
                        updates.push_back("allow_fishing = " + db_value);
                        changes.push_back(std::string(value ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + (value ? " enabled" : " disabled") + " fishing");
                    } else if (opt.name == "trading") {
                        updates.push_back("allow_trading = " + db_value);
                        changes.push_back(std::string(value ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + (value ? " enabled" : " disabled") + " trading");
                    } else if (opt.name == "robbery") {
                        updates.push_back("allow_robbery = " + db_value);
                        changes.push_back(std::string(value ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + (value ? " enabled" : " disabled") + " robbery");
                    }
                }

                if (updates.empty()) {
                    event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " no features to update.").set_flags(dpp::m_ephemeral));
                    db->get_pool()->release(conn);
                    return;
                }

                query += updates[0];
                for (size_t i = 1; i < updates.size(); i++) {
                    query += ", " + updates[i];
                }
                query += " WHERE guild_id = " + std::to_string(guild_id);

                if (mysql_query(conn->get(), query.c_str()) == 0) {
                    std::string description;
                    for (const auto& change : changes) {
                        description += change + "\n";
                    }

                    dpp::embed embed = dpp::embed()
                        .set_color(bronx::COLOR_SUCCESS)
                        .set_title(std::string(bronx::EMOJI_CHECK) + " feature settings updated")
                        .set_description(description)
                        .set_footer(dpp::embed_footer().set_text("server economy system"))
                        .set_timestamp(time(0));

                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " failed to update feature settings.").set_flags(dpp::m_ephemeral));
                }

                db->get_pool()->release(conn);

            } else if (subcommand.name == "tax") {
                // --- TAX ---
                bool enabled = std::get<bool>(subcommand.options[0].value);
                double rate = 0.0;

                if (subcommand.options.size() > 1) {
                    rate = std::get<double>(subcommand.options[1].value);
                    if (rate < 0 || rate > 100) {
                        event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " tax rate must be between 0 and 100.").set_flags(dpp::m_ephemeral));
                        return;
                    }
                }

                auto conn = db->get_pool()->acquire();
                std::string query = "UPDATE guild_settings SET enable_tax = " +
                                  std::string(enabled ? "TRUE" : "FALSE");

                if (subcommand.options.size() > 1) {
                    query += ", transaction_tax_percent = " + std::to_string(rate);
                }

                query += " WHERE guild_id = " + std::to_string(guild_id);

                if (mysql_query(conn->get(), query.c_str()) == 0) {
                    std::ostringstream desc;
                    if (enabled) {
                        desc << "transaction tax is now **enabled**\n\xF0\x9F\x92\xB8 rate: **" << rate << "%**\n\n"
                             << "this tax will be applied to all player-to-player transfers.";
                    } else {
                        desc << "transaction tax has been **disabled**.";
                    }

                    dpp::embed embed = dpp::embed()
                        .set_color(bronx::COLOR_SUCCESS)
                        .set_title(enabled ? std::string(bronx::EMOJI_CHECK) + " tax system enabled" : "\xF0\x9F\x94\x93 tax system disabled")
                        .set_description(desc.str())
                        .set_footer(dpp::embed_footer().set_text("server economy system"))
                        .set_timestamp(time(0));

                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message(std::string(bronx::EMOJI_DENY) + " failed to update tax settings.").set_flags(dpp::m_ephemeral));
                }

                db->get_pool()->release(conn);
            }
        },
        // Slash command options (subcommands)
        {
            dpp::command_option(dpp::co_sub_command, "toggle", "toggle between global and server economy")
                .add_option(dpp::command_option(dpp::co_string, "mode", "economy mode", true)
                    .add_choice(dpp::command_option_choice("global economy", std::string("global")))
                    .add_choice(dpp::command_option_choice("server economy", std::string("server")))
                ),
            dpp::command_option(dpp::co_sub_command, "status", "view current server economy settings"),
            dpp::command_option(dpp::co_sub_command, "config", "configure server economy settings")
                .add_option(dpp::command_option(dpp::co_integer, "starting_wallet", "starting wallet amount", false))
                .add_option(dpp::command_option(dpp::co_integer, "starting_bank_limit", "starting bank limit", false))
                .add_option(dpp::command_option(dpp::co_number, "work_multiplier", "work earnings multiplier", false))
                .add_option(dpp::command_option(dpp::co_number, "gambling_multiplier", "gambling multiplier", false))
                .add_option(dpp::command_option(dpp::co_number, "fishing_multiplier", "fishing rewards multiplier", false)),
            dpp::command_option(dpp::co_sub_command, "features", "toggle economy features")
                .add_option(dpp::command_option(dpp::co_boolean, "gambling", "allow gambling", false))
                .add_option(dpp::command_option(dpp::co_boolean, "fishing", "allow fishing", false))
                .add_option(dpp::command_option(dpp::co_boolean, "trading", "allow trading", false))
                .add_option(dpp::command_option(dpp::co_boolean, "robbery", "allow robbery", false)),
            dpp::command_option(dpp::co_sub_command, "tax", "configure transaction tax")
                .add_option(dpp::command_option(dpp::co_boolean, "enabled", "enable tax system", true))
                .add_option(dpp::command_option(dpp::co_number, "rate", "tax percentage (0-100)", false))
        }
    );
}

// Get all server economy commands
inline std::vector<Command*> get_server_economy_commands(Database* db) {
    static std::vector<Command*> cmds;
    static bool initialized = false;

    if (!initialized) {
        cmds.push_back(create_servereconomy_command(db));
        initialized = true;
    }

    return cmds;
}

} // namespace server_economy
} // namespace commands
