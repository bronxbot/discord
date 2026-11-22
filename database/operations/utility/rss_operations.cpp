#include "rss_operations.h"
#include "../../core/connection_pool.h"
#include <cstring>
#include <iostream>

namespace bronx::db::rss_operations {
namespace {

class Statement {
public:
    explicit Statement(Database* db) : db_(db) {
        if (db_) {
            connection_ = db_->get_pool()->acquire();
            if (connection_) {
                statement_ = mysql_stmt_init(connection_->get());
            }
        }
    }

    ~Statement() {
        if (statement_) {
            mysql_stmt_close(statement_);
        }
        if (connection_) {
            db_->get_pool()->release(connection_);
        }
    }

    bool prepare(const char* query, const char* operation) {
        if (!statement_) {
            std::cerr << "rss database error [" << operation << "]: could not initialize statement\n";
            return false;
        }
        if (mysql_stmt_prepare(statement_, query, std::strlen(query)) != 0) {
            std::cerr << "rss database error [" << operation << " prepare]: "
                      << mysql_stmt_error(statement_) << '\n';
            return false;
        }
        return true;
    }

    MYSQL_STMT* get() const { return statement_; }

private:
    Database* db_ = nullptr;
    std::shared_ptr<Connection> connection_;
    MYSQL_STMT* statement_ = nullptr;
};

void bind_id(MYSQL_BIND& bind, uint64_t& value) {
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &value;
    bind.is_unsigned = true;
}

void bind_text(MYSQL_BIND& bind, const std::string& value) {
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char*>(value.data());
    bind.buffer_length = static_cast<unsigned long>(value.size());
}

struct FeedResult {
    uint64_t id = 0;
    uint64_t guild_id = 0;
    int interval_seconds = 0;
    uint64_t last_checked = 0;
    char url[2049]{};
    char channels[2049]{};
    char last_guid[1025]{};
    unsigned long url_length = 0;
    unsigned long channels_length = 0;
    unsigned long guid_length = 0;
};

bool bind_feed_result(MYSQL_STMT* statement, FeedResult& row, MYSQL_BIND result[7]) {
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &row.id;
    result[0].is_unsigned = true;
    result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result[1].buffer = &row.guild_id;
    result[1].is_unsigned = true;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = row.channels;
    result[2].buffer_length = sizeof(row.channels);
    result[2].length = &row.channels_length;
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = row.url;
    result[3].buffer_length = sizeof(row.url);
    result[3].length = &row.url_length;
    result[4].buffer_type = MYSQL_TYPE_LONG;
    result[4].buffer = &row.interval_seconds;
    result[5].buffer_type = MYSQL_TYPE_LONGLONG;
    result[5].buffer = &row.last_checked;
    result[5].is_unsigned = true;
    result[6].buffer_type = MYSQL_TYPE_STRING;
    result[6].buffer = row.last_guid;
    result[6].buffer_length = sizeof(row.last_guid);
    result[6].length = &row.guid_length;
    if (mysql_stmt_bind_result(statement, result) != 0) {
        std::cerr << "rss database error [bind result]: "
                  << mysql_stmt_error(statement) << '\n';
        return false;
    }
    return true;
}

Feed to_feed(const FeedResult& row) {
    Feed feed;
    feed.id = row.id;
    feed.guild_id = row.guild_id;
    feed.channels.assign(row.channels,
        std::min<unsigned long>(row.channels_length, sizeof(row.channels)));
    feed.url.assign(row.url, std::min<unsigned long>(row.url_length, sizeof(row.url)));
    feed.interval_seconds = row.interval_seconds;
    feed.last_checked = row.last_checked;
    feed.last_guid.assign(row.last_guid, std::min<unsigned long>(row.guid_length, sizeof(row.last_guid)));
    return feed;
}

std::vector<Feed> fetch_feeds(Database* db, const char* query, uint64_t* guild_id) {
    std::vector<Feed> feeds;
    Statement statement(db);
    if (!statement.prepare(query, "fetch feeds")) {
        return feeds;
    }

    if (guild_id) {
        MYSQL_BIND param[1]{};
        bind_id(param[0], *guild_id);
        if (mysql_stmt_bind_param(statement.get(), param) != 0) {
            std::cerr << "rss database error [fetch feeds bind]: "
                      << mysql_stmt_error(statement.get()) << '\n';
            return feeds;
        }
    }
    if (mysql_stmt_execute(statement.get()) != 0 || mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "rss database error [fetch feeds execute]: "
                  << mysql_stmt_error(statement.get()) << '\n';
        return feeds;
    }

    FeedResult row;
    MYSQL_BIND result[7]{};
    if (!bind_feed_result(statement.get(), row, result)) {
        return feeds;
    }
    int status = 0;
    while ((status = mysql_stmt_fetch(statement.get())) == 0 || status == MYSQL_DATA_TRUNCATED) {
        feeds.push_back(to_feed(row));
    }
    if (status != MYSQL_NO_DATA) {
        std::cerr << "rss database error [fetch feeds read]: "
                  << mysql_stmt_error(statement.get()) << '\n';
    }
    return feeds;
}

}

std::optional<uint64_t> add_feed(Database* db, const Feed& feed) {
    const char* query = "INSERT INTO guild_rss_feeds "
        "(guild_id, target_channels, feed_url, check_interval, last_checked, last_guid) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    Statement statement(db);
    if (!statement.prepare(query, "add feed")) {
        return std::nullopt;
    }

    uint64_t guild_id = feed.guild_id;
    int interval = feed.interval_seconds;
    uint64_t last_checked = feed.last_checked;
    MYSQL_BIND params[6]{};
    bind_id(params[0], guild_id);
    bind_text(params[1], feed.channels);
    bind_text(params[2], feed.url);
    params[3].buffer_type = MYSQL_TYPE_LONG;
    params[3].buffer = &interval;
    bind_id(params[4], last_checked);
    bind_text(params[5], feed.last_guid);

    if (mysql_stmt_bind_param(statement.get(), params) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "rss database error [add feed execute]: "
                  << mysql_stmt_error(statement.get()) << '\n';
        return std::nullopt;
    }
    return mysql_stmt_insert_id(statement.get());
}

std::vector<Feed> get_guild_feeds(Database* db, uint64_t guild_id) {
    return fetch_feeds(db,
        "SELECT id, guild_id, target_channels, feed_url, check_interval, last_checked, last_guid "
        "FROM guild_rss_feeds WHERE guild_id = ? ORDER BY id",
        &guild_id);
}

std::vector<Feed> get_all_feeds(Database* db) {
    return fetch_feeds(db,
        "SELECT id, guild_id, target_channels, feed_url, check_interval, last_checked, last_guid "
        "FROM guild_rss_feeds ORDER BY id",
        nullptr);
}

std::optional<Feed> get_feed(Database* db, uint64_t guild_id, uint64_t feed_id) {
    const char* query = "SELECT id, guild_id, target_channels, feed_url, check_interval, last_checked, last_guid "
        "FROM guild_rss_feeds WHERE guild_id = ? AND id = ? LIMIT 1";
    Statement statement(db);
    if (!statement.prepare(query, "get feed")) {
        return std::nullopt;
    }
    MYSQL_BIND params[2]{};
    bind_id(params[0], guild_id);
    bind_id(params[1], feed_id);
    if (mysql_stmt_bind_param(statement.get(), params) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "rss database error [get feed execute]: "
                  << mysql_stmt_error(statement.get()) << '\n';
        return std::nullopt;
    }
    FeedResult row;
    MYSQL_BIND result[7]{};
    if (!bind_feed_result(statement.get(), row, result)) {
        return std::nullopt;
    }
    if (mysql_stmt_fetch(statement.get()) != 0) {
        return std::nullopt;
    }
    return to_feed(row);
}

bool set_target(Database* db, uint64_t guild_id, uint64_t feed_id, const std::string& channels) {
    const char* query = "UPDATE guild_rss_feeds SET target_channels = ? WHERE guild_id = ? AND id = ?";
    Statement statement(db);
    if (!statement.prepare(query, "set target")) {
        return false;
    }
    MYSQL_BIND params[3]{};
    bind_text(params[0], channels);
    bind_id(params[1], guild_id);
    bind_id(params[2], feed_id);
    if (mysql_stmt_bind_param(statement.get(), params) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "rss database error [set target execute]: "
                  << mysql_stmt_error(statement.get()) << '\n';
        return false;
    }
    return mysql_stmt_affected_rows(statement.get()) > 0;
}

bool update_interval(Database* db, uint64_t guild_id, uint64_t feed_id, int interval_seconds) {
    const char* query = "UPDATE guild_rss_feeds SET check_interval = ? WHERE guild_id = ? AND id = ?";
    Statement statement(db);
    if (!statement.prepare(query, "update interval")) {
        return false;
    }
    MYSQL_BIND params[3]{};
    params[0].buffer_type = MYSQL_TYPE_LONG;
    params[0].buffer = &interval_seconds;
    bind_id(params[1], guild_id);
    bind_id(params[2], feed_id);
    if (mysql_stmt_bind_param(statement.get(), params) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "rss database error [update interval execute]: "
                  << mysql_stmt_error(statement.get()) << '\n';
        return false;
    }
    return mysql_stmt_affected_rows(statement.get()) > 0;
}

bool update_check(Database* db, uint64_t guild_id, uint64_t feed_id, uint64_t checked_at, const std::string& last_guid) {
    const char* query = "UPDATE guild_rss_feeds SET last_checked = ?, last_guid = ? WHERE guild_id = ? AND id = ?";
    Statement statement(db);
    if (!statement.prepare(query, "update check")) {
        return false;
    }
    MYSQL_BIND params[4]{};
    bind_id(params[0], checked_at);
    bind_text(params[1], last_guid);
    bind_id(params[2], guild_id);
    bind_id(params[3], feed_id);
    if (mysql_stmt_bind_param(statement.get(), params) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "rss database error [update check execute]: "
                  << mysql_stmt_error(statement.get()) << '\n';
        return false;
    }
    return mysql_stmt_affected_rows(statement.get()) > 0;
}

bool remove_feed(Database* db, uint64_t guild_id, uint64_t feed_id) {
    const char* query = "DELETE FROM guild_rss_feeds WHERE guild_id = ? AND id = ?";
    Statement statement(db);
    if (!statement.prepare(query, "remove feed")) {
        return false;
    }
    MYSQL_BIND params[2]{};
    bind_id(params[0], guild_id);
    bind_id(params[1], feed_id);
    if (mysql_stmt_bind_param(statement.get(), params) != 0 ||
        mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "rss database error [remove feed execute]: "
                  << mysql_stmt_error(statement.get()) << '\n';
        return false;
    }
    return mysql_stmt_affected_rows(statement.get()) > 0;
}

}
