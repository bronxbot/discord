#pragma once

#include <cstdint>

namespace bronx {
namespace db {

class Database;

namespace book_operations {
    bool set_book_progress(Database* db, uint64_t user_id, int book_id, int current_page);
    int get_book_progress(Database* db, uint64_t user_id, int book_id);
    int get_last_read_book_id(Database* db, uint64_t user_id);
}

} // namespace db
} // namespace bronx
