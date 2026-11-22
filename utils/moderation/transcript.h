#pragma once
#include <dpp/dpp.h>
#include <string>
#include <functional>
#include "../../database/core/database.h"

namespace bronx {
namespace moderation {

//
 // @brief Generates a transcript for a modmail thread and uploads it to the log channel.
 // 
 // @param bot Reference to the dpp::cluster
 // @param db Pointer to the database instance
 // @param thread_id The ID of the modmail thread (channel)
 // @param log_channel_id The ID of the channel to upload the transcript to
 // @param user_id The ID of the user the thread was with
 // @param closed_by The ID of the user who closed the thread
 // @param callback Callback for success/failure
 ///
void create_modmail_transcript(dpp::cluster& bot, db::Database* db, uint64_t thread_id, uint64_t log_channel_id, uint64_t user_id, uint64_t closed_by, std::function<void(bool)> callback = nullptr);

} // namespace moderation
} // namespace bronx
