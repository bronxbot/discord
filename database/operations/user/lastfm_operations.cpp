#include "../../core/database.h"
#include <cstring>
#include <cstdio>

namespace bronx {
namespace db {

bool Database::set_lastfm_user(uint64_t user_id, const std::string& username, bool verified, const std::string& session_key) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO lastfm_users (user_id, lastfm_username, verified, session_key) VALUES (?, ?, ?, ?) "
                        "ON DUPLICATE KEY UPDATE lastfm_username = VALUES(lastfm_username), verified = VALUES(verified), session_key = VALUES(session_key)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_lastfm_user prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)username.c_str();
    bind[1].buffer_length = username.length();
    
    int8_t is_verified = verified ? 1 : 0;
    bind[2].buffer_type = MYSQL_TYPE_TINY;
    bind[2].buffer = (char*)&is_verified;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)session_key.c_str();
    bind[3].buffer_length = session_key.length();
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_lastfm_user execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::optional<Database::LastfmUser> Database::get_lastfm_user(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "SELECT user_id, lastfm_username, verified, COALESCE(session_key, ''), created_at FROM lastfm_users WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_lastfm_user prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_lastfm_user execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    uint64_t out_user_id = 0;
    char out_username[128];
    unsigned long username_len = 0;
    int8_t out_verified = 0;
    char out_session_key[128];
    unsigned long session_key_len = 0;
    MYSQL_TIME out_created_at;
    
    MYSQL_BIND result_bind[5];
    memset(result_bind, 0, sizeof(result_bind));
    
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&out_user_id;
    result_bind[0].is_unsigned = 1;
    
    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = out_username;
    result_bind[1].buffer_length = sizeof(out_username) - 1;
    result_bind[1].length = &username_len;
    
    result_bind[2].buffer_type = MYSQL_TYPE_TINY;
    result_bind[2].buffer = (char*)&out_verified;
    
    result_bind[3].buffer_type = MYSQL_TYPE_STRING;
    result_bind[3].buffer = out_session_key;
    result_bind[3].buffer_length = sizeof(out_session_key) - 1;
    result_bind[3].length = &session_key_len;
    
    result_bind[4].buffer_type = MYSQL_TYPE_TIMESTAMP;
    result_bind[4].buffer = (char*)&out_created_at;
    
    mysql_stmt_bind_result(stmt, result_bind);
    
    std::optional<LastfmUser> result = std::nullopt;
    if (mysql_stmt_fetch(stmt) == 0) {
        out_username[username_len] = '\0';
        out_session_key[session_key_len] = '\0';
        
        LastfmUser user;
        user.user_id = out_user_id;
        user.lastfm_username = std::string(out_username, username_len);
        user.verified = (out_verified != 0);
        user.session_key = std::string(out_session_key, session_key_len);
        
        char time_buf[64];
        snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 out_created_at.year, out_created_at.month, out_created_at.day,
                 out_created_at.hour, out_created_at.minute, out_created_at.second);
        user.created_at = time_buf;
        
        result = user;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

bool Database::unlink_lastfm_user(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM lastfm_users WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("unlink_lastfm_user prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("unlink_lastfm_user execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

} // namespace db
} // namespace bronx
