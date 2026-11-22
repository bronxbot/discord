#pragma once

#include "../../core/database.h"
#include <optional>
#include <string>
#include <vector>

namespace bronx::db::rss_operations {

struct Feed {
    uint64_t id = 0;
    uint64_t guild_id = 0;
    std::string channels;
    std::string url;
    int interval_seconds = 1200;
    uint64_t last_checked = 0;
    std::string last_guid;
};

std::optional<uint64_t> add_feed(Database* db, const Feed& feed);
std::vector<Feed> get_guild_feeds(Database* db, uint64_t guild_id);
std::vector<Feed> get_all_feeds(Database* db);
std::optional<Feed> get_feed(Database* db, uint64_t guild_id, uint64_t feed_id);
bool set_target(Database* db, uint64_t guild_id, uint64_t feed_id, const std::string& channels);
bool update_interval(Database* db, uint64_t guild_id, uint64_t feed_id, int interval_seconds);
bool update_check(Database* db, uint64_t guild_id, uint64_t feed_id, uint64_t checked_at, const std::string& last_guid);
bool remove_feed(Database* db, uint64_t guild_id, uint64_t feed_id);

}
