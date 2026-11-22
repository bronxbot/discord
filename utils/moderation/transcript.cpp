#include "transcript.h"
#include "../../embed_style.h"
#include "../../database/operations/moderation/modmail_operations.h"
#include <dpp/nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>

using json = nlohmann::json;

namespace bronx {
namespace moderation {

void create_modmail_transcript(dpp::cluster& bot, db::Database* db, uint64_t thread_id, uint64_t log_channel_id, uint64_t user_id, uint64_t closed_by, std::function<void(bool)> callback) {
    bot.messages_get(thread_id, 0, 0, 0, 100, [&bot, db, thread_id, log_channel_id, user_id, closed_by, callback](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            if (callback) callback(false);
            return;
        }

        auto messages = std::get<dpp::message_map>(cb.value);
        if (messages.empty()) {
            if (callback) callback(true);
            return;
        }

        // Get user's username for DB
        std::string username = "Unknown User";
        dpp::user* target_user = dpp::find_user(user_id);
        if (target_user) {
            username = target_user->username;
        } else {
            // Check messages for username
            for (auto const& [id, msg] : messages) {
                if (msg.author.id == user_id) {
                    username = msg.author.username;
                    break;
                }
            }
        }

        // Construct JSON payload
        json json_arr = json::array();

        // Standard chronological sorting: message_map is naturally sorted by ID (snowflake chronological)
        for (auto const& [id, msg] : messages) {
            std::string timestamp = dpp::utility::current_date_time();
            char mbstr[100];
            time_t sent_time = (time_t)msg.sent;
            if (std::strftime(mbstr, sizeof(mbstr), "%Y-%m-%d %H:%M:%S", std::localtime(&sent_time))) {
                timestamp = mbstr;
            }

            json j_msg = {
                {"id", std::to_string(msg.id)},
                {"timestamp", timestamp},
                {"author_id", std::to_string(msg.author.id)},
                {"username", msg.author.username},
                {"avatar_url", msg.author.get_avatar_url()},
                {"content", msg.content}
            };

            // Embeds
            json embeds_arr = json::array();
            for (auto const& embed : msg.embeds) {
                json j_embed;
                j_embed["title"] = embed.title;
                j_embed["description"] = embed.description;
                
                if (embed.color.has_value()) j_embed["color"] = embed.color.value();
                else j_embed["color"] = nullptr;
                json fields_arr = json::array();
                for (auto const& field : embed.fields) {
                    fields_arr.push_back({
                        {"name", field.name},
                        {"value", field.value},
                        {"inline", field.is_inline}
                    });
                }
                j_embed["fields"] = fields_arr;
                embeds_arr.push_back(j_embed);
            }
            j_msg["embeds"] = embeds_arr;

            // Attachments
            json att_arr = json::array();
            for (auto const& att : msg.attachments) {
                att_arr.push_back({
                    {"id", std::to_string(att.id)},
                    {"filename", att.filename},
                    {"url", att.url},
                    {"size", att.size}
                });
            }
            j_msg["attachments"] = att_arr;

            json_arr.push_back(j_msg);
        }

        // Save transcript to database
        db::ModmailTranscript transcript_record;
        transcript_record.thread_id = thread_id;
        transcript_record.guild_id = messages.begin()->second.guild_id; // Get guild_id from the first message
        if (transcript_record.guild_id == 0) {
            // fallback if guild_id not in messages
            dpp::channel* chan = dpp::find_channel(thread_id);
            if (chan) {
                transcript_record.guild_id = chan->guild_id;
            }
        }
        transcript_record.user_id = user_id;
        transcript_record.username = username;
        transcript_record.closed_by = closed_by;
        transcript_record.messages_json = json_arr.dump();

        bool db_ok = db::save_modmail_transcript(db, transcript_record);
        if (!db_ok) {
            std::cerr << "[Transcript] Warning: Failed to save transcript to DB for thread " << thread_id << std::endl;
        }

        // Create log message with a clean premium Discord embed link
        dpp::message m(log_channel_id, "");
        dpp::embed e;
        e.set_title("modmail transcript")
         .set_color(bronx::COLOR_DEFAULT)
         .set_description("An interactive web-hosted modmail transcript is available for this thread.")
         .add_field("User", "<@" + std::to_string(user_id) + ">", true)
         .add_field("Closed By", "<@" + std::to_string(closed_by) + ">", true)
         .add_field("Thread ID", std::to_string(thread_id), true)
         .add_field("View Transcript", "[Click here to view transcript](https://bronx.siqnole.dev/transcripts/" + std::to_string(thread_id) + ")", false)
         .set_timestamp(time(0));
        m.add_embed(e);

        bot.message_create(m, [callback](const dpp::confirmation_callback_t& res) {
            if (callback) callback(!res.is_error());
        });
    });
}

} // namespace moderation
} // namespace bronx
