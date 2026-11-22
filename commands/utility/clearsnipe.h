#pragma once
// clearsnipe.h — clear the recently deleted messages cache for channel or guild.

#include "../../command.h"
#include "../../embed_style.h"
#include "../../performance/snipe_cache.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <algorithm>

namespace commands {
namespace utility {

inline Command* get_clearsnipe_command(bronx::snipe::SnipeCache* cache) {
    static Command cmd("clearsnipe",
        "clear recently deleted messages in this channel or server",
        "utility",
        {"cs"},  // alias
        false,   // is_slash_command
        // ── text handler ──
        [cache](dpp::cluster& bot, const dpp::message_create_t& event,
                const std::vector<std::string>& args)
        {
            // Permission check: require manage messages
            dpp::guild* g = dpp::find_guild(event.msg.guild_id);
            if (g) {
                auto perms = g->base_permissions(event.msg.member);
                if (!(perms & dpp::p_manage_messages) && !(perms & dpp::p_administrator)) {
                    bronx::send_message(bot, event, bronx::error("you need **manage messages** to use this"));
                    return;
                }
            }

            uint64_t channel_filter = event.msg.channel_id;
            bool guild_wide = false;

            for (const auto& arg : args) {
                // Check for channel mention
                if (arg.find("<#") == 0 && arg.back() == '>') {
                    try {
                        channel_filter = std::stoull(arg.substr(2, arg.size() - 3));
                    } catch (...) {}
                }
                // Check for "server" / "guild" / "all" keyword
                else if (arg == "server" || arg == "guild" || arg == "all") {
                    guild_wide = true;
                }
            }

            if (guild_wide) {
                cache->clear(event.msg.guild_id, 0);
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::success("successfully cleared **all** sniped messages for this server")));
            } else {
                cache->clear(event.msg.guild_id, channel_filter);
                if (channel_filter == event.msg.channel_id) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("successfully cleared sniped messages for this channel")));
                } else {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("successfully cleared sniped messages for <#" + std::to_string(channel_filter) + ">")));
                }
            }
        }
    );

    cmd.extended_description = "clears the deleted messages cache. defaults to clearing the current channel. "
                               "provide a channel mention to clear a specific channel, or use 'server' to clear the entire server.";
    cmd.examples = {
        "b.clearsnipe",
        "b.cs",
        "b.clearsnipe #general",
        "b.cs server",
        "b.cs all"
    };
    cmd.notes = "requires manage messages permission. configurable via b.command.";

    return &cmd;
}

} // namespace utility
} // namespace commands
