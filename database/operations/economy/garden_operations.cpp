#include "../../core/database.h"
#include <mariadb/mysql.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <map>

namespace bronx {
namespace db {

// 1. ensure_garden_profile
bool Database::ensure_garden_profile(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("ensure_garden_profile acquire failed");
        return false;
    }
    const char* sql = "INSERT IGNORE INTO user_gardens (user_id, plot_count, watering_can_level) VALUES (?, 3, 1)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        log_error("ensure_garden_profile prepare failed");
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
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// 2. get_garden_profile
bool Database::get_garden_profile(uint64_t user_id, GardenProfile& profile) {
    ensure_garden_profile(user_id);
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_garden_profile acquire failed");
        return false;
    }
    profile.user_id = user_id;
    profile.plot_count = 3;
    profile.watering_can_level = 1;
    profile.crops.clear();

    // get garden metadata
    const char* sql1 = "SELECT plot_count, watering_can_level FROM user_gardens WHERE user_id = ?";
    MYSQL_STMT* stmt1 = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt1, sql1, strlen(sql1)) == 0) {
        MYSQL_BIND bind1[1];
        memset(bind1, 0, sizeof(bind1));
        bind1[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind1[0].buffer = (char*)&user_id;
        bind1[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt1, bind1);
        if (mysql_stmt_execute(stmt1) == 0) {
            MYSQL_BIND resbind[2];
            memset(resbind, 0, sizeof(resbind));
            int plot_count = 3;
            int can_level = 1;
            resbind[0].buffer_type = MYSQL_TYPE_LONG;
            resbind[0].buffer = &plot_count;
            resbind[1].buffer_type = MYSQL_TYPE_LONG;
            resbind[1].buffer = &can_level;
            mysql_stmt_bind_result(stmt1, resbind);
            if (mysql_stmt_fetch(stmt1) == 0) {
                profile.plot_count = plot_count;
                profile.watering_can_level = can_level;
            }
        }
    }
    mysql_stmt_close(stmt1);

    // get crops
    const char* sql2 = "SELECT id, plot_index, crop_name, UNIX_TIMESTAMP(planted_at), UNIX_TIMESTAMP(last_watered_at), growth_progress, is_wilted "
                       "FROM user_crops WHERE user_id = ? ORDER BY plot_index ASC";
    MYSQL_STMT* stmt2 = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt2, sql2, strlen(sql2)) == 0) {
        MYSQL_BIND bind2[1];
        memset(bind2, 0, sizeof(bind2));
        bind2[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind2[0].buffer = (char*)&user_id;
        bind2[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt2, bind2);
        if (mysql_stmt_execute(stmt2) == 0) {
            MYSQL_BIND resbind2[7];
            memset(resbind2, 0, sizeof(resbind2));

            uint64_t crop_id;
            int plot_idx;
            char crop_name[65]; unsigned long name_len;
            int64_t planted_time;
            int64_t watered_time;
            double progress;
            my_bool wilted;

            resbind2[0].buffer_type = MYSQL_TYPE_LONGLONG; resbind2[0].buffer = &crop_id;
            resbind2[1].buffer_type = MYSQL_TYPE_LONG; resbind2[1].buffer = &plot_idx;
            resbind2[2].buffer_type = MYSQL_TYPE_STRING; resbind2[2].buffer = crop_name; resbind2[2].buffer_length = sizeof(crop_name); resbind2[2].length = &name_len;
            resbind2[3].buffer_type = MYSQL_TYPE_LONGLONG; resbind2[3].buffer = &planted_time;
            resbind2[4].buffer_type = MYSQL_TYPE_LONGLONG; resbind2[4].buffer = &watered_time;
            resbind2[5].buffer_type = MYSQL_TYPE_DOUBLE; resbind2[5].buffer = &progress;
            resbind2[6].buffer_type = MYSQL_TYPE_TINY; resbind2[6].buffer = &wilted;

            mysql_stmt_bind_result(stmt2, resbind2);
            mysql_stmt_store_result(stmt2);

            while (mysql_stmt_fetch(stmt2) == 0) {
                GardenCropRow crop;
                crop.id = crop_id;
                crop.user_id = user_id;
                crop.plot_index = plot_idx;
                crop.crop_name = std::string(crop_name, name_len);
                crop.planted_timestamp = planted_time;
                crop.last_watered_timestamp = watered_time;
                crop.growth_progress = progress;
                crop.is_wilted = (wilted != 0);
                profile.crops.push_back(crop);
            }
        }
    }
    mysql_stmt_close(stmt2);
    pool_->release(conn);
    return true;
}

// 3. upgrade_watering_can
bool Database::upgrade_watering_can(uint64_t user_id) {
    ensure_garden_profile(user_id);
    auto conn = pool_->acquire();
    if (!conn) return false;
    const char* sql = "UPDATE user_gardens SET watering_can_level = watering_can_level + 1 WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
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
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// 4. buy_garden_plot
bool Database::buy_garden_plot(uint64_t user_id) {
    ensure_garden_profile(user_id);
    auto conn = pool_->acquire();
    if (!conn) return false;
    const char* sql = "UPDATE user_gardens SET plot_count = plot_count + 1 WHERE user_id = ? AND plot_count < 9";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
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
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// 5. plant_crop
bool Database::plant_crop(uint64_t user_id, int plot_index, const std::string& crop_name) {
    ensure_garden_profile(user_id);
    auto conn = pool_->acquire();
    if (!conn) return false;
    const char* sql = "INSERT INTO user_crops (user_id, plot_index, crop_name, planted_at, last_watered_at, growth_progress, is_wilted) "
                      "VALUES (?, ?, ?, NOW(), NOW(), 0.0, 0) "
                      "ON DUPLICATE KEY UPDATE crop_name = VALUES(crop_name), planted_at = NOW(), last_watered_at = NOW(), growth_progress = 0.0, is_wilted = 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&user_id; bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONG;     bind[1].buffer = (char*)&plot_index;
    bind[2].buffer_type = MYSQL_TYPE_STRING;   bind[2].buffer = (char*)crop_name.c_str(); bind[2].buffer_length = crop_name.length();

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// 6. water_crop
bool Database::water_crop(uint64_t user_id, int plot_index, double growth_boost) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    const char* sql = "UPDATE user_crops SET last_watered_at = NOW(), growth_progress = LEAST(1.0, growth_progress + ?) "
                      "WHERE user_id = ? AND plot_index = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_DOUBLE;   bind[0].buffer = (char*)&growth_boost;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = (char*)&user_id; bind[1].is_unsigned = 1;
    bind[2].buffer_type = MYSQL_TYPE_LONG;     bind[2].buffer = (char*)&plot_index;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// 7. harvest_crop
bool Database::harvest_crop(uint64_t user_id, int plot_index) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    const char* sql = "DELETE FROM user_crops WHERE user_id = ? AND plot_index = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&user_id; bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONG;     bind[1].buffer = (char*)&plot_index;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

bool Database::clear_crop(uint64_t user_id, int plot_index) {
    return harvest_crop(user_id, plot_index);
}

// 8. add_garden_inventory
bool Database::add_garden_inventory(uint64_t user_id, const std::string& item_name, int quantity) {
    ensure_garden_profile(user_id);
    auto conn = pool_->acquire();
    if (!conn) return false;
    const char* sql = "INSERT INTO user_garden_inventory (user_id, item_name, quantity) VALUES (?, ?, ?) "
                      "ON DUPLICATE KEY UPDATE quantity = GREATEST(0, quantity + VALUES(quantity))";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&user_id; bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;   bind[1].buffer = (char*)item_name.c_str(); bind[1].buffer_length = item_name.length();
    bind[2].buffer_type = MYSQL_TYPE_LONG;     bind[2].buffer = (char*)&quantity;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// 9. get_garden_inventory
std::map<std::string, int> Database::get_garden_inventory(uint64_t user_id) {
    std::map<std::string, int> inventory;
    auto conn = pool_->acquire();
    if (!conn) return inventory;

    const char* sql = "SELECT item_name, quantity FROM user_garden_inventory WHERE user_id = ? AND quantity > 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return inventory;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&user_id; bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);

    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND resbind[2];
        memset(resbind, 0, sizeof(resbind));
        char item_buf[65]; unsigned long name_len;
        int qty;

        resbind[0].buffer_type = MYSQL_TYPE_STRING; resbind[0].buffer = item_buf; resbind[0].buffer_length = sizeof(item_buf); resbind[0].length = &name_len;
        resbind[1].buffer_type = MYSQL_TYPE_LONG; resbind[1].buffer = &qty;

        mysql_stmt_bind_result(stmt, resbind);
        mysql_stmt_store_result(stmt);

        while (mysql_stmt_fetch(stmt) == 0) {
            inventory[std::string(item_buf, name_len)] = qty;
        }
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return inventory;
}

} // namespace db
} // namespace bronx
