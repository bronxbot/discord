#include "../../core/database.h"
#include <cstring>
#include <vector>

namespace bronx {
namespace db {

static std::string escape(MYSQL* mysql, const std::string& s) {
    std::vector<char> buffer(s.size() * 2 + 1);
    unsigned long len = mysql_real_escape_string(mysql, buffer.data(), s.c_str(), s.size());
    return std::string(buffer.data(), len);
}

bool Database::save_account_link(const std::string& stoat_id, const std::string& fluxer_id, const std::string& discord_id) {
    auto conn = pool_->acquire();
    std::string val_stoat = stoat_id.empty() ? "NULL" : "'" + escape(conn->get(), stoat_id) + "'";
    std::string val_fluxer = fluxer_id.empty() ? "NULL" : "'" + escape(conn->get(), fluxer_id) + "'";
    std::string val_discord = discord_id.empty() ? "NULL" : "'" + escape(conn->get(), discord_id) + "'";

    std::string query = "REPLACE INTO account_links (stoat_id, fluxer_id, discord_id) VALUES (" +
                        val_stoat + ", " + val_fluxer + ", " + val_discord + ")";
    bool success = mysql_query(conn->get(), query.c_str()) == 0;
    if (!success) {
        last_error_ = mysql_error(conn->get());
        log_error("save_account_link");
    }
    pool_->release(conn);
    return success;
}

std::optional<Database::AccountLink> Database::get_account_link(const std::string& platform, const std::string& user_id) {
    auto conn = pool_->acquire();
    std::string query = "SELECT stoat_id, fluxer_id, discord_id FROM account_links WHERE " +
                        escape(conn->get(), platform) + "_id = '" + escape(conn->get(), user_id) + "'";
    std::optional<AccountLink> result = std::nullopt;
    if (mysql_query(conn->get(), query.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                AccountLink link;
                link.stoat_id = row[0] ? row[0] : "";
                link.fluxer_id = row[1] ? row[1] : "";
                link.discord_id = row[2] ? row[2] : "";
                result = link;
            }
            mysql_free_result(res);
        }
    } else {
        last_error_ = mysql_error(conn->get());
        log_error("get_account_link");
    }
    pool_->release(conn);
    return result;
}

} // namespace db
} // namespace bronx
