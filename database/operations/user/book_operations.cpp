#include "book_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {

bool Database::set_book_progress(uint64_t user_id, int book_id, int current_page) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO user_book_progress (user_id, book_id, current_page) VALUES (?, ?, ?) "
                        "ON DUPLICATE KEY UPDATE current_page = VALUES(current_page)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_book_progress prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    // user_id
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    // book_id
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&book_id;
    bind[1].is_unsigned = 0;
    
    // current_page
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = (char*)&current_page;
    bind[2].is_unsigned = 0;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_book_progress execute");
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

int Database::get_book_progress(uint64_t user_id, int book_id) {
    auto conn = pool_->acquire();
    const char* query = "SELECT current_page FROM user_book_progress WHERE user_id = ? AND book_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_book_progress prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    // user_id
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    // book_id
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&book_id;
    bind[1].is_unsigned = 0;
    
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_book_progress execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    int current_page = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
    result_bind[0].buffer = (char*)&current_page;
    result_bind[0].is_unsigned = 0;
    
    mysql_stmt_bind_result(stmt, result_bind);
    if (mysql_stmt_fetch(stmt) == 0) {
        // found
    } else {
        current_page = 0; // default to first page
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return current_page;
}

int Database::get_last_read_book_id(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "SELECT book_id FROM user_book_progress WHERE user_id = ? ORDER BY last_read_at DESC LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_last_read_book_id prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    // user_id
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_last_read_book_id execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    int book_id = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
    result_bind[0].buffer = (char*)&book_id;
    result_bind[0].is_unsigned = 0;
    
    mysql_stmt_bind_result(stmt, result_bind);
    if (mysql_stmt_fetch(stmt) == 0) {
        // found
    } else {
        book_id = 0;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return book_id;
}

} // namespace db
} // namespace bronx

namespace bronx {
namespace db {
namespace book_operations {
    bool set_book_progress(Database* db, uint64_t user_id, int book_id, int current_page) {
        return db->set_book_progress(user_id, book_id, current_page);
    }
    int get_book_progress(Database* db, uint64_t user_id, int book_id) {
        return db->get_book_progress(user_id, book_id);
    }
    int get_last_read_book_id(Database* db, uint64_t user_id) {
        return db->get_last_read_book_id(user_id);
    }
}
}
}
