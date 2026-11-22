#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../utils/bridge_client.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>

namespace commands::utility {

inline Command* get_link_command(bronx::db::Database* db) {
    static Command link_cmd("link", "link accounts between Discord, Revolt (Stoat), and Guilded (Fluxer)", "utility", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t channel_id = event.msg.channel_id;
            uint64_t user_id = event.msg.author.id;

            if (args.empty()) {
                auto link_opt = db->get_account_link("discord", std::to_string(user_id));
                if (link_opt) {
                    std::string out = "🔗 **Account Link Status**\n";
                    out += "• **Stoat ID**: " + (link_opt->stoat_id.empty() ? "*None*" : "`" + link_opt->stoat_id + "`") + "\n";
                    out += "• **Fluxer ID**: " + (link_opt->fluxer_id.empty() ? "*None*" : "`" + link_opt->fluxer_id + "`") + "\n";
                    out += "• **Discord ID**: " + (link_opt->discord_id.empty() ? "*None*" : "`" + link_opt->discord_id + "`") + "\n";
                    bronx::send_message(bot, event, bronx::info(out));
                } else {
                    std::string out = "❌ **You do not have any linked accounts.**\n\n"
                                      "To link with another platform:\n"
                                      "1. Type `b.link generate` to get a 6-digit link code.\n"
                                      "2. Go to Revolt (Stoat) or Guilded (Fluxer) and type `.link redeem <code>`.";
                    bronx::send_message(bot, event, bronx::error(out));
                }
            } else if (args[0] == "generate") {
                bronx::bridge::BridgeClient::get().register_link_generate(user_id, [&bot, event, user_id, channel_id](const std::string& code) {
                    std::string code_msg = "🔑 **Link Code Generated!**\n"
                                           "Your code is: `" + code + "`\n"
                                           "Redeem it on Revolt (Stoat) or Guilded (Fluxer) using `.link redeem " + code + "`.\n"
                                           "This code will expire in 5 minutes.";
                    bot.direct_message_create(user_id, dpp::message(code_msg), [&bot, event, channel_id](const dpp::confirmation_callback_t& dm_cb) {
                        if (dm_cb.is_error()) {
                            bronx::send_message(bot, event, bronx::error("failed to send dm. please ensure your dms are open."));
                            return;
                        }
                        bronx::send_message(bot, event, bronx::info("a link verification code has been sent to your dms."));
                    });
                });

                nlohmann::json req = {
                    {"type", "link_generate"},
                    {"user_id", std::to_string(user_id)}
                };
                bronx::bridge::BridgeClient::get().send_json(req);
            } else if (args[0] == "redeem") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("❌ Please specify the code: `b.link redeem <code>`."));
                    return;
                }
                std::string code = args[1];
                bronx::bridge::BridgeClient::get().register_link_redeem(user_id, [&bot, event, channel_id](const std::string& err) {
                    if (err.empty()) {
                        bronx::send_message(bot, event, bronx::info("🎉 **Success!** Your accounts have been linked."));
                    } else {
                        bronx::send_message(bot, event, bronx::error("❌ **Link Error:** " + err));
                    }
                });

                nlohmann::json req = {
                    {"type", "link_redeem"},
                    {"user_id", std::to_string(user_id)},
                    {"code", code}
                };
                bronx::bridge::BridgeClient::get().send_json(req);
            }
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply("this command must be used as a prefix command (`b.link`).");
        });

    return &link_cmd;
}

} // namespace commands::utility
