#include "modmail_operations.h"
#include "../../core/database.h"
#include "../../core/connection_pool.h"
#include <mariadb/mysql.h>
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// ---------------------------------------------------------------------------
// get_modmail_config
// ---------------------------------------------------------------------------
std::optional<ModmailConfig> Database::get_modmail_config(uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT category_id, staff_role_id, log_channel_id, enabled "
                       "FROM guild_modmail_config WHERE guild_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_in[0].buffer = &guild_id;
    bind_in[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind_in);
    
    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t category_id, staff_role_id, log_channel_id;
    int8_t enabled;
    MYSQL_BIND bind_out[4];
    memset(bind_out, 0, sizeof(bind_out));

    bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_out[0].buffer = &category_id;
    bind_out[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_out[1].buffer = &staff_role_id;
    bind_out[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_out[2].buffer = &log_channel_id;
    bind_out[3].buffer_type = MYSQL_TYPE_TINY;
    bind_out[3].buffer = &enabled;

    mysql_stmt_bind_result(stmt, bind_out);
    
    std::optional<ModmailConfig> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ModmailConfig config;
        config.guild_id = guild_id;
        config.category_id = category_id;
        config.staff_role_id = staff_role_id;
        config.log_channel_id = log_channel_id;
        config.enabled = (enabled != 0);
        result = config;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ---------------------------------------------------------------------------
// set_modmail_config
// ---------------------------------------------------------------------------
bool Database::set_modmail_config(const ModmailConfig& config) {
    auto conn = pool_->acquire();
    if (!conn) return false;

    const char* query = "INSERT INTO guild_modmail_config (guild_id, category_id, staff_role_id, log_channel_id, enabled) "
                       "VALUES (?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE "
                       "category_id = VALUES(category_id), staff_role_id = VALUES(staff_role_id), "
                       "log_channel_id = VALUES(log_channel_id), enabled = VALUES(enabled)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    uint64_t gid = config.guild_id;
    uint64_t cid = config.category_id;
    uint64_t srid = config.staff_role_id;
    uint64_t lcid = config.log_channel_id;
    int8_t en = config.enabled ? 1 : 0;

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = &gid; bind[0].is_unsigned = true;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &cid; bind[1].is_unsigned = true;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG; bind[2].buffer = &srid; bind[2].is_unsigned = true;
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG; bind[3].buffer = &lcid; bind[3].is_unsigned = true;
    bind[4].buffer_type = MYSQL_TYPE_TINY;     bind[4].buffer = &en;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ---------------------------------------------------------------------------
// get_any_active_modmail_thread
// ---------------------------------------------------------------------------
std::optional<ModmailThread> Database::get_any_active_modmail_thread(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT id, guild_id, thread_id, created_at FROM modmail_threads "
                       "WHERE user_id = ? AND status = 'open' LIMIT 1";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_in[0].buffer = &user_id; bind_in[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind_in);
    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t id, guild_id, thread_id;
    MYSQL_TIME created_at;
    MYSQL_BIND bind_out[4];
    memset(bind_out, 0, sizeof(bind_out));
    bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[0].buffer = &id;
    bind_out[1].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[1].buffer = &guild_id;
    bind_out[2].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[2].buffer = &thread_id;
    bind_out[3].buffer_type = MYSQL_TYPE_TIMESTAMP; bind_out[3].buffer = &created_at;

    mysql_stmt_bind_result(stmt, bind_out);
    
    std::optional<ModmailThread> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ModmailThread thread;
        thread.id = id;
        thread.guild_id = guild_id;
        thread.user_id = user_id;
        thread.thread_id = thread_id;
        thread.status = "open";
        result = thread;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ---------------------------------------------------------------------------
// get_active_modmail_thread_by_user
// ---------------------------------------------------------------------------
std::optional<ModmailThread> Database::get_active_modmail_thread_by_user(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT id, thread_id, created_at FROM modmail_threads "
                       "WHERE user_id = ? AND guild_id = ? AND status = 'open' LIMIT 1";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[2];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_in[0].buffer = &user_id; bind_in[0].is_unsigned = true;
    bind_in[1].buffer_type = MYSQL_TYPE_LONGLONG; bind_in[1].buffer = &guild_id; bind_in[1].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind_in);
    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t id, thread_id;
    MYSQL_TIME created_at;
    MYSQL_BIND bind_out[3];
    memset(bind_out, 0, sizeof(bind_out));
    bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[0].buffer = &id;
    bind_out[1].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[1].buffer = &thread_id;
    bind_out[2].buffer_type = MYSQL_TYPE_TIMESTAMP; bind_out[2].buffer = &created_at;

    mysql_stmt_bind_result(stmt, bind_out);
    
    std::optional<ModmailThread> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ModmailThread thread;
        thread.id = id;
        thread.guild_id = guild_id;
        thread.user_id = user_id;
        thread.thread_id = thread_id;
        thread.status = "open";
        // Convert MYSQL_TIME to time_point if needed, but for now we skip complex conversions
        result = thread;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ---------------------------------------------------------------------------
// get_modmail_thread_by_id
// ---------------------------------------------------------------------------
std::optional<ModmailThread> Database::get_modmail_thread_by_id(uint64_t thread_id) {
    auto conn = pool_->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT id, guild_id, user_id, status FROM modmail_threads WHERE thread_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_in[0].buffer = &thread_id; bind_in[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind_in);
    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t id, guild_id, user_id;
    char status[20] = {0};
    unsigned long status_len;
    MYSQL_BIND bind_out[4];
    memset(bind_out, 0, sizeof(bind_out));
    bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[0].buffer = &id;
    bind_out[1].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[1].buffer = &guild_id;
    bind_out[2].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[2].buffer = &user_id;
    bind_out[3].buffer_type = MYSQL_TYPE_STRING;   bind_out[3].buffer = status; bind_out[3].buffer_length = sizeof(status); bind_out[3].length = &status_len;

    mysql_stmt_bind_result(stmt, bind_out);
    
    std::optional<ModmailThread> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ModmailThread thread;
        thread.id = id;
        thread.guild_id = guild_id;
        thread.user_id = user_id;
        thread.thread_id = thread_id;
        thread.status = std::string(status, status_len);
        result = thread;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ---------------------------------------------------------------------------
// create_modmail_thread
// ---------------------------------------------------------------------------
bool Database::create_modmail_thread(uint64_t guild_id, uint64_t user_id, uint64_t thread_id) {
    auto conn = pool_->acquire();
    if (!conn) return false;

    const char* query = "INSERT INTO modmail_threads (guild_id, user_id, thread_id, status) VALUES (?, ?, ?, 'open')";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = &guild_id;  bind[0].is_unsigned = true;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &user_id;   bind[1].is_unsigned = true;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG; bind[2].buffer = &thread_id; bind[2].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ---------------------------------------------------------------------------
// close_modmail_thread
// ---------------------------------------------------------------------------
bool Database::close_modmail_thread(uint64_t thread_id) {
    auto conn = pool_->acquire();
    if (!conn) return false;

    const char* query = "UPDATE modmail_threads SET status = 'closed', closed_at = CURRENT_TIMESTAMP WHERE thread_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = &thread_id; bind[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

std::vector<uint64_t> Database::get_all_modmail_enabled_guilds() {
    std::vector<uint64_t> guilds;
    auto conn = pool_->acquire();
    if (!conn) return guilds;

    const char* query = "SELECT guild_id FROM guild_modmail_config WHERE enabled = 1";
    if (mysql_query(conn->get(), query) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                guilds.push_back(std::stoull(row[0]));
            }
            mysql_free_result(res);
        }
    }
    pool_->release(conn);
    return guilds;
}

bool Database::save_modmail_transcript(const ModmailTranscript& transcript) {
    auto conn = pool_->acquire();
    if (!conn) return false;

    const char* query = "INSERT INTO modmail_transcripts (thread_id, guild_id, user_id, username, closed_by, messages_json) "
                        "VALUES (?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE "
                        "guild_id = VALUES(guild_id), user_id = VALUES(user_id), username = VALUES(username), "
                        "closed_by = VALUES(closed_by), messages_json = VALUES(messages_json)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    std::string thread_id_str = std::to_string(transcript.thread_id);
    std::string guild_id_str = std::to_string(transcript.guild_id);
    std::string user_id_str = std::to_string(transcript.user_id);
    std::string closed_by_str = std::to_string(transcript.closed_by);

    unsigned long thread_id_len = thread_id_str.length();
    unsigned long guild_id_len = guild_id_str.length();
    unsigned long user_id_len = user_id_str.length();
    unsigned long username_len = transcript.username.length();
    unsigned long closed_by_len = closed_by_str.length();
    unsigned long messages_json_len = transcript.messages_json.length();

    MYSQL_BIND bind[6];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void*)thread_id_str.c_str();
    bind[0].buffer_length = thread_id_len;
    bind[0].length = &thread_id_len;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (void*)guild_id_str.c_str();
    bind[1].buffer_length = guild_id_len;
    bind[1].length = &guild_id_len;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (void*)user_id_str.c_str();
    bind[2].buffer_length = user_id_len;
    bind[2].length = &user_id_len;

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (void*)transcript.username.c_str();
    bind[3].buffer_length = username_len;
    bind[3].length = &username_len;

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (void*)closed_by_str.c_str();
    bind[4].buffer_length = closed_by_len;
    bind[4].length = &closed_by_len;

    bind[5].buffer_type = MYSQL_TYPE_BLOB;
    bind[5].buffer = (void*)transcript.messages_json.c_str();
    bind[5].buffer_length = messages_json_len;
    bind[5].length = &messages_json_len;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    
    if (!ok) {
        std::cerr << "[DB Error] save_modmail_transcript failed: " << mysql_stmt_error(stmt) << std::endl;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ============================================================================
// Free-function wrappers
// ============================================================================

std::optional<ModmailConfig> get_modmail_config(Database* db, uint64_t guild_id) {
    return db->get_modmail_config(guild_id);
}

bool set_modmail_config(Database* db, const ModmailConfig& config) {
    return db->set_modmail_config(config);
}

std::optional<ModmailThread> get_any_active_modmail_thread(Database* db, uint64_t user_id) {
    return db->get_any_active_modmail_thread(user_id);
}

std::optional<ModmailThread> get_active_modmail_thread_by_user(Database* db, uint64_t user_id, uint64_t guild_id) {
    return db->get_active_modmail_thread_by_user(user_id, guild_id);
}

std::optional<ModmailThread> get_modmail_thread_by_id(Database* db, uint64_t thread_id) {
    return db->get_modmail_thread_by_id(thread_id);
}

bool create_modmail_thread(Database* db, uint64_t guild_id, uint64_t user_id, uint64_t thread_id) {
    return db->create_modmail_thread(guild_id, user_id, thread_id);
}

bool close_modmail_thread(Database* db, uint64_t thread_id) {
    return db->close_modmail_thread(thread_id);
}

std::vector<uint64_t> get_all_modmail_enabled_guilds(Database* db) {
    return db->get_all_modmail_enabled_guilds();
}

bool save_modmail_transcript(Database* db, const ModmailTranscript& transcript) {
    return db->save_modmail_transcript(transcript);
}

} // namespace db
} // namespace bronx

