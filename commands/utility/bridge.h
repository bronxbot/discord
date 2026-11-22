#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "../../utils/bridge_client.h"
#include "utility_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>

namespace commands::utility {

inline Command* get_bridge_command(bronx::db::Database* db) {
    static Command bridge_cmd("bridge", "bridge channels using verification codes", "utility", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t channel_id = event.msg.channel_id;
            uint64_t user_id = event.msg.author.id;

            std::cout << "[debug-cmd-bridge] handler entered. args count=" << args.size() << std::endl;

            if (args.empty()) {
                std::string out = "🔗 **bridge commands**\n"
                                  "• `b.bridge add` - starts a bridge connection or redeems a code.\n"
                                  "• `b.bridge remove` - tears down the current channel's bridge.\n"
                                  "• `b.bridge check` - shows active bridges for this channel.\n"
                                  "• `b.bridge info` - shows origin metadata for a replied message.\n"
                                  "• `b.bridge emojis` - shows custom emojis sync status across platforms.";
                bot.message_create(dpp::message(channel_id, bronx::info(out)));
                return;
            }

            std::string subcommand = args[0];
            std::cout << "[debug-cmd-bridge] subcommand='" << subcommand << "'" << std::endl;

            // Helper: check if user has admin/mod rights
            auto check_perm = [&]() -> bool {
                auto* g = dpp::find_guild(guild_id);
                if (g && g->owner_id == user_id) return true;
                if (bronx::db::permission_operations::is_admin(db, user_id, guild_id)) return true;
                if (bronx::db::permission_operations::is_mod(db, user_id, guild_id)) return true;
                // Discord native permissions check
                if (g) {
                    auto member_it = g->members.find(user_id);
                    if (member_it != g->members.end()) {
                        auto perms = g->base_permissions(member_it->second);
                        if (perms.has(dpp::p_administrator) || perms.has(dpp::p_manage_guild)) return true;
                    }
                }
                return false;
            };

            if (subcommand == "add") {
                if (!check_perm()) {
                    bot.message_create(dpp::message(channel_id, bronx::error("permission error: you need to be an administrator or moderator to manage bridges.")));
                    return;
                }

                if (args.size() < 2) {
                    // Generate code
                    bronx::bridge::BridgeClient::get().register_bridge_add_gen(user_id, [&bot, user_id, channel_id](const std::string& code, uint64_t /*cid*/) {
                        std::string code_msg = "🔑 **bridge verification code**\n"
                                               "run `b.bridge add " + code + "` in the target channel on another platform to bridge them.\n"
                                               "this code will expire in 5 minutes.";
                        bot.direct_message_create(user_id, dpp::message(code_msg), [&bot, channel_id](const dpp::confirmation_callback_t& dm_cb) {
                            if (dm_cb.is_error()) {
                                bot.message_create(dpp::message(channel_id, bronx::error("failed to send dm. please ensure your dms are open.")));
                                return;
                            }
                            bot.message_create(dpp::message(channel_id, bronx::info("a bridge verification code has been sent to your dms.")));
                        });
                    });

                    nlohmann::json req = {
                        {"type", "bridge_add_generate"},
                        {"user_id", std::to_string(user_id)},
                        {"channel_id", std::to_string(channel_id)}
                    };
                    bronx::bridge::BridgeClient::get().send_json(req);
                } else {
                    // Redeem code
                    std::string code = args[1];
                    bronx::bridge::BridgeClient::get().register_bridge_add_redeem(user_id, [&bot, channel_id](const std::string& err, uint64_t other_channel_id, const std::string& other_platform) {
                        if (err.empty()) {
                            std::string out = "🎉 **success!** channel has been successfully bridged to `" + std::to_string(other_channel_id) + "` on **" + other_platform + "**!";
                            bot.message_create(dpp::message(channel_id, bronx::info(out)));
                        } else {
                            bot.message_create(dpp::message(channel_id, bronx::error("bridge error: " + err)));
                        }
                    });

                    nlohmann::json req = {
                        {"type", "bridge_add_redeem"},
                        {"user_id", std::to_string(user_id)},
                        {"channel_id", std::to_string(channel_id)},
                        {"code", code}
                    };
                    bronx::bridge::BridgeClient::get().send_json(req);
                }
            }
            else if (subcommand == "remove") {
                if (!check_perm()) {
                    bot.message_create(dpp::message(channel_id, bronx::error("permission error: you need to be an administrator or moderator to manage bridges.")));
                    return;
                }

                bronx::bridge::BridgeClient::get().register_bridge_remove(user_id, [&bot, channel_id](const std::string& err) {
                    if (err.empty()) {
                        bot.message_create(dpp::message(channel_id, bronx::info("success! this channel has been unbridged.")));
                    } else {
                        bot.message_create(dpp::message(channel_id, bronx::error("bridge error: " + err)));
                    }
                });

                nlohmann::json req = {
                    {"type", "bridge_remove"},
                    {"user_id", std::to_string(user_id)},
                    {"channel_id", std::to_string(channel_id)}
                };
                bronx::bridge::BridgeClient::get().send_json(req);
            }
            else if (subcommand == "check") {
                std::cout << "[debug-cmd-bridge] handling check subcommand" << std::endl;
                bronx::bridge::BridgeClient::get().register_bridge_check(user_id, [&bot, channel_id](bool found, uint64_t stoat_ch, uint64_t fluxer_ch, uint64_t discord_ch) {
                    if (found) {
                        std::string out = "🔗 **active bridge status**\n";
                        out += "• **stoat channel**: " + (stoat_ch == 0 ? "*none*" : "`" + std::to_string(stoat_ch) + "`") + "\n";
                        out += "• **fluxer channel**: " + (fluxer_ch == 0 ? "*none*" : "`" + std::to_string(fluxer_ch) + "`") + "\n";
                        out += "• **discord channel**: " + (discord_ch == 0 ? "*none*" : "`" + std::to_string(discord_ch) + "`") + "\n";
                        bot.message_create(dpp::message(channel_id, bronx::info(out)));
                    } else {
                        bot.message_create(dpp::message(channel_id, bronx::info("this channel is not bridged.")));
                    }
                });

                nlohmann::json req = {
                    {"type", "bridge_check"},
                    {"user_id", std::to_string(user_id)},
                    {"channel_id", std::to_string(channel_id)}
                };
                bronx::bridge::BridgeClient::get().send_json(req);
            }
            else if (subcommand == "info") {
                if (event.msg.message_reference.message_id == 0) {
                    bot.message_create(dpp::message(channel_id, bronx::error("please reply to a message to check its bridge info.")));
                    return;
                }

                uint64_t replied_msg_id = event.msg.message_reference.message_id;

                bronx::bridge::BridgeClient::get().register_bridge_info(user_id, [&bot, channel_id, replied_msg_id](bool found, const std::string& src_platform, uint64_t src_channel_id, const std::string& src_channel_name, uint64_t src_server_id, const std::string& src_server_name, uint64_t src_user_id, const std::string& src_username) {
                    if (found) {
                        std::string out = "bridged message information\n";
                        out += "**origin platform:** " + src_platform + "\n";
                        out += "**sender:** " + src_username + " (`" + std::to_string(src_user_id) + "`)\n";
                        out += "**origin channel:** #" + (src_channel_name.empty() ? std::to_string(src_channel_id) : src_channel_name) + " (`" + std::to_string(src_channel_id) + "`)\n";
                        if (src_server_id != 0) {
                            out += "**origin server:** " + (src_server_name.empty() ? std::to_string(src_server_id) : src_server_name) + " (`" + std::to_string(src_server_id) + "`)\n";
                        }
                        bot.message_create(dpp::message(channel_id, bronx::info(out)));
                    } else {
                        // Check local message
                        bot.message_get(replied_msg_id, channel_id, [&bot, channel_id](const dpp::confirmation_callback_t& local_cb) {
                            if (local_cb.is_error()) {
                                bot.message_create(dpp::message(channel_id, bronx::error("message not found.")));
                                return;
                            }
                            auto local_msg = std::get<dpp::message>(local_cb.value);
                            std::string author_name = local_msg.author.format_username();
                            std::string out = "local message information\n";
                            out += "**origin platform:** discord\n";
                            out += "**sender:** " + author_name + " (`" + std::to_string(local_msg.author.id) + "`)\n";
                            out += "**origin channel:** <#" + std::to_string(local_msg.channel_id) + "> (`" + std::to_string(local_msg.channel_id) + "`)\n";
                            if (local_msg.guild_id != 0) {
                                out += "**origin server:** `" + std::to_string(local_msg.guild_id) + "`\n";
                            }
                            bot.message_create(dpp::message(channel_id, bronx::info(out)));
                        });
                    }
                });

                nlohmann::json req = {
                    {"type", "bridge_info_query"},
                    {"user_id", std::to_string(user_id)},
                    {"channel_id", std::to_string(channel_id)},
                    {"message_id", std::to_string(replied_msg_id)}
                };
                bronx::bridge::BridgeClient::get().send_json(req);
            }
            else if (subcommand == "emojis") {
                bronx::bridge::BridgeClient::get().register_bridge_emojis(user_id, [&bot, channel_id](bool found, const nlohmann::json& emojis) {
                    if (found) {
                        std::string out = "🔗 **bridged emojis sync status**\n\n";
                        std::string matches = "";
                        std::string unmatched_discord = "";
                        std::string unmatched_stoat = "";
                        std::string unmatched_fluxer = "";
                        int match_count = 0;

                        for (const auto& item : emojis) {
                            std::string name = item["name"].get<std::string>();
                            bool has_disc = item.contains("discord") && !item["discord"].is_null();
                            bool has_stoat = item.contains("stoat") && !item["stoat"].is_null();
                            bool has_fluxer = item.contains("fluxer") && !item["fluxer"].is_null();

                            std::vector<std::string> platforms;
                            if (has_disc) platforms.push_back("Discord");
                            if (has_stoat) platforms.push_back("Revolt");
                            if (has_fluxer) platforms.push_back("Guilded");

                            if (platforms.size() >= 2) {
                                matches += "• **:" + name + ":** (";
                                for (size_t i = 0; i < platforms.size(); ++i) {
                                    matches += platforms[i] + (i == platforms.size() - 1 ? "" : ", ");
                                }
                                matches += ")\n";
                                match_count++;
                            } else {
                                if (has_disc) unmatched_discord += "• **:" + name + ":**\n";
                                if (has_stoat) unmatched_stoat += "• **:" + name + ":**\n";
                                if (has_fluxer) unmatched_fluxer += "• **:" + name + ":**\n";
                            }
                        }

                        if (match_count > 0) {
                            out += "**Matched Emojis (" + std::to_string(match_count) + ")**:\n" + matches + "\n";
                        } else {
                            out += "**Matched Emojis**: *none*\n\n";
                        }

                        out += "**Unmatched Emojis**:\n";
                        out += "• **Discord**: " + (unmatched_discord.empty() ? "*none*" : "\n" + unmatched_discord) + "\n";
                        out += "• **Revolt (Stoat)**: " + (unmatched_stoat.empty() ? "*none*" : "\n" + unmatched_stoat) + "\n";
                        out += "• **Guilded (Fluxer)**: " + (unmatched_fluxer.empty() ? "*none*" : "\n" + unmatched_fluxer) + "\n";

                        bot.message_create(dpp::message(channel_id, bronx::info(out)));
                    } else {
                        bot.message_create(dpp::message(channel_id, bronx::error("bridge error: this channel is not bridged.")));
                    }
                });

                nlohmann::json req = {
                    {"type", "bridge_emojis_query"},
                    {"user_id", std::to_string(user_id)},
                    {"channel_id", std::to_string(channel_id)}
                };
                bronx::bridge::BridgeClient::get().send_json(req);
            }
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply("this command must be used as a prefix command (`b.bridge`).");
        });

    return &bridge_cmd;
}

} // namespace commands::utility
