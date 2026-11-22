#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/moderation/permission_operations.h"
#include <dpp/dpp.h>
#include <random>
#include <chrono>
#include <algorithm>
#include "../utils/string_utils.h"
#include "../utils/colors.h"
#include "leveling/xp_handler.h"

using namespace bronx::db;

namespace commands {

inline std::vector<Command*> get_leveling_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    // rank/level command to show user's current level
    static Command* rank_cmd = new Command("rank", "view your or someone else's level and XP", "leveling", {"xp"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t target_id = event.msg.author.id;
            if (!event.msg.mentions.empty()) {
                target_id = event.msg.mentions.begin()->first.id;
            }
            
            auto xp_data = db->get_user_xp(target_id, event.msg.guild_id);
            if (!xp_data) {
                bronx::send_message(bot, event, bronx::error("could not find any XP data for that user."));
                return;
            }

            dpp::user* target_user = dpp::find_user(target_id);
            std::string username = target_user ? target_user->username : std::to_string(target_id);
            
            std::string desc = "📊 **rank summary**\n\n"
                               "✨ **level**: " + std::to_string(xp_data->level) + "\n"
                               "📈 **xp**: " + std::to_string(xp_data->total_xp) + " / " + std::to_string(db->calculate_xp_for_next_level(xp_data->level));
            
            auto embed = bronx::create_embed(desc);
            embed.set_title(username + "'s leveling progress");
            embed.set_color(bronx::COLOR_INFO);
            if (target_user) embed.set_thumbnail(target_user->get_avatar_url());
            
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            dpp::snowflake target_id = event.command.usr.id;
            auto it = event.get_parameter("user");
            if (it.index() != 0) {
                target_id = std::get<dpp::snowflake>(it);
            }

            auto xp_data = db->get_user_xp(target_id, event.command.guild_id);
            if (!xp_data) {
                event.reply(dpp::message().add_embed(bronx::error("could not find any XP data for that user.")).set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::user* target_user = dpp::find_user(target_id);
            std::string username = target_user ? target_user->username : std::to_string(target_id);
            
            std::string desc = "📊 **rank summary**\n\n"
                               "✨ **level**: " + std::to_string(xp_data->level) + "\n"
                               "📈 **xp**: " + std::to_string(xp_data->total_xp) + " / " + std::to_string(db->calculate_xp_for_next_level(xp_data->level));
            
            auto embed = bronx::create_embed(desc);
            embed.set_title(username + "'s leveling progress");
            embed.set_color(bronx::COLOR_INFO);
            if (target_user) embed.set_thumbnail(target_user->get_avatar_url());
            
            event.reply(dpp::message().add_embed(embed));
        },
        { dpp::command_option(dpp::co_user, "user", "the user to view rank for", false) }
    );
    cmds.push_back(rank_cmd);
    
    // Combined level command group
    static Command* level_cmd = new Command("level", "configure and manage server leveling", "leveling", {"lvl"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("this command only works in a server"));
                return;
            }
            
            uint64_t guild_id = event.msg.guild_id;
            
            // Check if we should delegate to rank/level progress card
            bool is_admin_command = false;
            std::string sub = "";
            if (!args.empty()) {
                sub = args[0];
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                if (sub == "enable" || sub == "disable" || sub == "xprange" || 
                    sub == "config" || sub == "cfg" || sub == "roles" || sub == "role" || sub == "reward") {
                    is_admin_command = true;
                }
            }

            if (!is_admin_command) {
                // Delegate to rank progress logic (anyone can run this)
                uint64_t target_id = event.msg.author.id;
                if (!event.msg.mentions.empty()) {
                    target_id = event.msg.mentions.begin()->first.id;
                } else if (!args.empty()) {
                    try {
                        target_id = std::stoull(args[0]);
                    } catch (...) {}
                }
                
                auto xp_data = db->get_user_xp(target_id, guild_id);
                if (!xp_data) {
                    bronx::send_message(bot, event, bronx::error("could not find any XP data for that user."));
                    return;
                }

                dpp::user* target_user = dpp::find_user(target_id);
                std::string username = target_user ? target_user->username : std::to_string(target_id);
                
                std::string desc = "📊 **rank summary**\n\n"
                                   "✨ **level**: " + std::to_string(xp_data->level) + "\n"
                                   "📈 **xp**: " + std::to_string(xp_data->total_xp) + " / " + std::to_string(db->calculate_xp_for_next_level(xp_data->level));
                
                auto embed = bronx::create_embed(desc);
                embed.set_title(username + "'s leveling progress");
                embed.set_color(bronx::COLOR_INFO);
                if (target_user) embed.set_thumbnail(target_user->get_avatar_url());
                
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Check admin permission (only for config subcommands)
            bool is_allowed = permission_operations::is_admin(db, event.msg.author.id, guild_id);
            if (!is_allowed) {
                dpp::guild* g = dpp::find_guild(guild_id);
                if (g && g->base_permissions(event.msg.member).can(dpp::p_administrator)) {
                    is_allowed = true;
                }
            }
            if (!is_allowed) {
                bronx::send_message(bot, event, bronx::error("administrator permission required"));
                return;
            }

            // Invalidation helper
            auto invalidate_cache = [](uint64_t gid) {
                std::lock_guard<std::mutex> lock(leveling::leveling_config_mutex);
                leveling::leveling_config_cache.erase(gid);
            };


            // Direct Enable/Disable at top level
            if (sub == "enable") {
                auto config = db->get_guild_leveling_config(guild_id);
                if (!config) {
                    db->create_guild_leveling_config(guild_id);
                    config = db->get_guild_leveling_config(guild_id);
                }
                if (config) {
                    config->enabled = true;
                    db->update_guild_leveling_config(*config);
                    invalidate_cache(guild_id);
                    bronx::send_message(bot, event, bronx::success("leveling system enabled"));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to enable leveling"));
                }
                return;
            }
            else if (sub == "disable") {
                auto config = db->get_guild_leveling_config(guild_id);
                if (!config) {
                    db->create_guild_leveling_config(guild_id);
                    config = db->get_guild_leveling_config(guild_id);
                }
                if (config) {
                    config->enabled = false;
                    db->update_guild_leveling_config(*config);
                    invalidate_cache(guild_id);
                    bronx::send_message(bot, event, bronx::success("leveling system disabled"));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to disable leveling"));
                }
                return;
            }
            // Direct XP range configuration
            else if (sub == "xprange") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `level xprange <min-max | single_value | -1 | 0>`\ne.g., `level xprange 3-5`, `level xprange 5`, or `level xprange 0` (reset to default)"));
                    return;
                }
                std::string val = args[1];
                int min_xp = 15;
                int max_xp = 25;
                bool is_default = (val == "0" || val == "-1");
                if (!is_default) {
                    size_t dash = val.find('-');
                    if (dash != std::string::npos) {
                        try {
                            min_xp = std::stoi(val.substr(0, dash));
                            max_xp = std::stoi(val.substr(dash + 1));
                        } catch (...) {
                            bronx::send_message(bot, event, bronx::error("invalid xp range format. use e.g. `3-5`"));
                            return;
                        }
                    } else {
                        try {
                            min_xp = std::stoi(val);
                            max_xp = min_xp;
                        } catch (...) {
                            bronx::send_message(bot, event, bronx::error("invalid xp value. use e.g. `5` or `3-5`"));
                            return;
                        }
                    }
                }
                if (min_xp <= 0 || max_xp <= 0) {
                    min_xp = 15;
                    max_xp = 25;
                    is_default = true;
                }
                if (min_xp > max_xp) {
                    std::swap(min_xp, max_xp);
                }
                auto config = db->get_guild_leveling_config(guild_id);
                if (!config) {
                    db->create_guild_leveling_config(guild_id);
                    config = db->get_guild_leveling_config(guild_id);
                }
                if (config) {
                    config->min_xp_per_message = min_xp;
                    config->max_xp_per_message = max_xp;
                    db->update_guild_leveling_config(*config);
                    invalidate_cache(guild_id);
                    if (is_default) {
                        bronx::send_message(bot, event, bronx::success("xp range reset to default (**15-25**)"));
                    } else {
                        bronx::send_message(bot, event, bronx::success("xp range updated to **" + std::to_string(min_xp) + "–" + std::to_string(max_xp) + "**"));
                    }
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to update xp range"));
                }
                return;
            }
            // --- Level Config Subcommand ---
            else if (sub == "config" || sub == "cfg") {
                auto config = db->get_guild_leveling_config(guild_id);
                if (!config) {
                    db->create_guild_leveling_config(guild_id);
                    config = db->get_guild_leveling_config(guild_id);
                }
                if (!config) {
                    bronx::send_message(bot, event, bronx::error("failed to load leveling config — database error"));
                    return;
                }

                if (args.size() == 1) { // Show dashboard
                    std::string desc = "**leveling configuration dashboard**\n\n";
                    desc += "✨ **status**: " + std::string(config->enabled ? "✅ enabled" : "❌ disabled") + "\n";
                    desc += "💰 **coin rewards**: " + std::string(config->reward_coins ? "enabled ($" + std::to_string(config->coins_per_message) + "/msg)" : "disabled") + "\n";
                    desc += "📈 **xp gain**: " + std::to_string(config->min_xp_per_message) + "–" + std::to_string(config->max_xp_per_message) + " xp per message\n";
                    desc += "⏱️ **cooldown**: " + std::to_string(config->xp_cooldown_seconds) + " seconds\n";
                    desc += "📏 **min chars**: " + std::to_string(config->min_message_chars) + " characters\n";
                    desc += "📌 **sticky roles**: " + std::string(config->sticky_roles ? "on" : "off") + "\n\n";
                    
                    desc += "**announcements**\n";
                    desc += "📣 **enabled**: " + std::string(config->announce_levelup ? "yes" : "no") + "\n";
                    desc += "📍 **channel**: " + (config->announcement_channel.has_value() ? "<#" + std::to_string(*config->announcement_channel) + ">" : "*not set (uses current channel)*") + "\n";
                    desc += "✉️ **message**: `" + config->announcement_message + "`\n\n";
                    
                    desc += "**commands**\n";
                    desc += "• `level enable/disable` — toggle leveling\n";
                    desc += "• `level xprange <range>` — set xp gain range\n";
                    desc += "• `level config xp <min> <max>`\n";
                    desc += "• `level config cooldown <seconds>`\n";
                    desc += "• `level config channel <#channel>`\n";
                    desc += "• `level config message <template>`\n";
                    desc += "• `level config preview` — preview announcement\n";
                    desc += "• `level config reset` — resets all server xp\n";
                    
                    auto embed = bronx::create_embed(desc);
                    embed.set_title("⚙️ server leveling dashboard");
                    embed.set_color(bronx::COLOR_INFO);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }

                std::string sub2 = args[1];
                std::transform(sub2.begin(), sub2.end(), sub2.begin(), ::tolower);

                if (sub2 == "enable") {
                    config->enabled = true;
                    db->update_guild_leveling_config(*config);
                    invalidate_cache(guild_id);
                    bronx::send_message(bot, event, bronx::success("leveling system enabled"));
                } else if (sub2 == "disable") {
                    config->enabled = false;
                    db->update_guild_leveling_config(*config);
                    invalidate_cache(guild_id);
                    bronx::send_message(bot, event, bronx::success("leveling system disabled"));
                } else if (sub2 == "message") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level config message <text>`\n\n**placeholders:**\n`{name}` — user mention\n`{level}` — new level\n`{members}` — server members\n`{date}` — current date\n`{createdat}` — server creation date"));
                        return;
                    }
                    std::string new_msg;
                    for (size_t i = 2; i < args.size(); i++) {
                        if (i > 2) new_msg += " ";
                        new_msg += args[i];
                    }
                    if (new_msg.length() > 500) {
                        bronx::send_message(bot, event, bronx::error("message template too long (max 500 chars)"));
                        return;
                    }
                    config->announcement_message = new_msg;
                    db->update_guild_leveling_config(*config);
                    invalidate_cache(guild_id);
                    bronx::send_message(bot, event, bronx::success("level-up message updated!"));
                } else if (sub2 == "preview") {
                    std::unordered_map<std::string, std::string> placeholders;
                    placeholders["name"] = event.msg.author.get_mention();
                    placeholders["level"] = "10";
                    placeholders["members"] = "420";
                    placeholders["date"] = "2026-04-22";
                    placeholders["createdat"] = "2024-01-01";

                    std::string preview = bronx::utils::replace_placeholders(config->announcement_message, placeholders);
                    
                    auto embed = bronx::create_embed("### 📣 announcement preview\n\n" + preview);
                    embed.set_title("✨ level up preview");
                    embed.set_footer(dpp::embed_footer().set_text("tip: placeholders are {name}, {level}, {members}, {date}, {createdat}"));
                    bronx::send_message(bot, event, embed);
                } else if (sub2 == "channel") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level config channel <#channel | reset>`"));
                        return;
                    }
                    if (args[2] == "reset" || args[2] == "none") {
                        config->announcement_channel = std::nullopt;
                        db->update_guild_leveling_config(*config);
                        invalidate_cache(guild_id);
                        bronx::send_message(bot, event, bronx::success("announcement channel reset (now uses original channel)"));
                    } else if (!event.msg.mention_channels.empty()) {
                        config->announcement_channel = event.msg.mention_channels[0].id;
                        db->update_guild_leveling_config(*config);
                        invalidate_cache(guild_id);
                        bronx::send_message(bot, event, bronx::success("announcements set to <#" + std::to_string(*config->announcement_channel) + ">"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("please mention a valid channel (e.g. #logs)"));
                    }
                } else if (sub2 == "xp") {
                    if (args.size() < 4) {
                        bronx::send_message(bot, event, bronx::error("usage: `level config xp <min> <max>`"));
                        return;
                    }
                    try {
                        int min_xp = std::stoi(args[2]);
                        int max_xp = std::stoi(args[3]);
                        if (min_xp < 1) min_xp = 1;
                        if (max_xp < min_xp) max_xp = min_xp;
                        config->min_xp_per_message = min_xp;
                        config->max_xp_per_message = max_xp;
                        db->update_guild_leveling_config(*config);
                        invalidate_cache(guild_id);
                        bronx::send_message(bot, event, bronx::success("xp range set to **" + std::to_string(min_xp) + "–" + std::to_string(max_xp) + "**"));
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid numbers")); }
                } else if (sub2 == "cooldown") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level config cooldown <seconds>`"));
                        return;
                    }
                    try {
                        int seconds = std::stoi(args[2]);
                        config->xp_cooldown_seconds = std::max(0, seconds);
                        db->update_guild_leveling_config(*config);
                        invalidate_cache(guild_id);
                        bronx::send_message(bot, event, bronx::success("xp cooldown set to **" + std::to_string(config->xp_cooldown_seconds) + " seconds**"));
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid number")); }
                } else if (sub2 == "reset") {
                    if (db->reset_guild_xp(guild_id)) {
                        bronx::send_message(bot, event, bronx::success("reset all server XP successfully"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to reset server XP"));
                    }
                } else {
                    bronx::send_message(bot, event, bronx::error("unknown config option. use `level config` to see all settings."));
                }
            } 
            // --- Level Roles Subcommand ---
            else if (sub == "roles" || sub == "role" || sub == "reward") {
                if (args.size() == 1) { // List roles
                    auto level_roles = db->get_level_roles(guild_id);
                    std::string desc = "**level role rewards**\n\n";
                    if (level_roles.empty()) {
                        desc += "*no role rewards configured yet.*\n\n";
                    } else {
                        for (const auto& lr : level_roles) {
                            desc += "🏅 **level " + std::to_string(lr.level) + "**: <@&" + std::to_string(lr.role_id) + ">\n";
                        }
                        desc += "\n";
                    }
                    desc += "**commands**\n";
                    desc += "• `level role add <level> <@role>`\n";
                    desc += "• `level role edit <level> <@role>`\n";
                    desc += "• `level role remove <level>`\n";
                    desc += "• `level role sticky on|off`\n";
                    
                    auto embed = bronx::create_embed(desc);
                    embed.set_title("🏆 level rewards");
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }

                std::string sub2 = args[1];
                std::transform(sub2.begin(), sub2.end(), sub2.begin(), ::tolower);

                if (sub2 == "add") {
                    if (args.size() < 4 || event.msg.mention_roles.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `level role add <level> <@role>`"));
                        return;
                    }
                    try {
                        uint32_t level = std::stoul(args[2]);
                        uint64_t role_id = event.msg.mention_roles[0];
                        dpp::role* role_ptr = dpp::find_role(role_id);
                        
                        LevelRole lr;
                        lr.guild_id = guild_id;
                        lr.level = level;
                        lr.role_id = role_id;
                        lr.role_name = role_ptr ? role_ptr->name : "unknown role";
                        lr.remove_previous = false;
                        
                        if (db->create_level_role(lr)) {
                            bronx::send_message(bot, event, bronx::success("added reward info: Level **" + std::to_string(level) + "** -> <@&" + std::to_string(role_id) + ">"));
                        } else {
                            bronx::send_message(bot, event, bronx::error("failed to add reward (level may already have a role)"));
                        }
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid level number")); }
                } else if (sub2 == "edit") {
                    if (args.size() < 4 || event.msg.mention_roles.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `level role edit <level> <@role>`"));
                        return;
                    }
                    try {
                        uint32_t level = std::stoul(args[2]);
                        uint64_t role_id = event.msg.mention_roles[0];
                        dpp::role* role_ptr = dpp::find_role(role_id);
                        
                        auto existing = db->get_level_role_at_level(guild_id, level);
                        if (!existing) {
                            bronx::send_message(bot, event, bronx::error("no role reward exists for level **" + std::to_string(level) + "**. use `add` instead."));
                            return;
                        }
                        
                        LevelRole lr;
                        lr.guild_id = guild_id;
                        lr.level = level;
                        lr.role_id = role_id;
                        lr.role_name = role_ptr ? role_ptr->name : "unknown role";
                        lr.remove_previous = false;
                        
                        if (db->create_level_role(lr)) {
                            bronx::send_message(bot, event, bronx::success("updated reward info: Level **" + std::to_string(level) + "** -> <@&" + std::to_string(role_id) + ">"));
                        } else {
                            bronx::send_message(bot, event, bronx::error("failed to update role reward"));
                        }
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid level number")); }
                } else if (sub2 == "remove") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level role remove <level>`"));
                        return;
                    }
                    try {
                        uint32_t level = std::stoul(args[2]);
                        if (db->delete_level_role(guild_id, level)) {
                            bronx::send_message(bot, event, bronx::success("removed reward for level **" + std::to_string(level) + "**"));
                        } else {
                            bronx::send_message(bot, event, bronx::error("no reward found for that level"));
                        }
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid level number")); }
                } else if (sub2 == "sticky") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level role sticky on|off`"));
                        return;
                    }
                    std::string sticky_val = args[2];
                    std::transform(sticky_val.begin(), sticky_val.end(), sticky_val.begin(), ::tolower);
                    bool sticky_on = true;
                    if (sticky_val == "off" || sticky_val == "no" || sticky_val == "false" || sticky_val == "0") {
                        sticky_on = false;
                    } else if (sticky_val == "on" || sticky_val == "yes" || sticky_val == "true" || sticky_val == "1") {
                        sticky_on = true;
                    } else {
                        bronx::send_message(bot, event, bronx::error("invalid value. use `on` or `off`"));
                        return;
                    }
                    auto config = db->get_guild_leveling_config(guild_id);
                    if (!config) {
                        db->create_guild_leveling_config(guild_id);
                        config = db->get_guild_leveling_config(guild_id);
                    }
                    if (config) {
                        config->sticky_roles = sticky_on;
                        db->update_guild_leveling_config(*config);
                        invalidate_cache(guild_id);
                        bronx::send_message(bot, event, bronx::success("sticky roles are now **" + std::string(sticky_on ? "on" : "off") + "**"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to update sticky roles setting"));
                    }
                } else {
                    bronx::send_message(bot, event, bronx::error("unknown role command. use `level role` to see help."));
                }
            } else {
                bronx::send_message(bot, event, bronx::error("unknown subcommand. use `level config` or `level role`."));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply(dpp::message().add_embed(bronx::error("please use text commands for leveling configuration")));
        },
        {}
    );
    cmds.push_back(level_cmd);
    
    return cmds;
}

} // namespace commands
