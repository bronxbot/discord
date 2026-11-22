#include "../../core/database.h"
#include <cstring>
#include <iostream>
#include <vector>

namespace bronx {
namespace db {

std::vector<TriviaQuestion> Database::get_random_trivia_questions(int count, const std::string& category) {
    std::vector<TriviaQuestion> results;
    auto conn = pool_->acquire();

    std::string query;
    bool has_category = !category.empty();
    if (has_category) {
        query = "SELECT id, category, question_text, option_a, option_b, option_c, option_d, correct_option, difficulty "
                "FROM trivia_questions WHERE category = ? ORDER BY RAND() LIMIT ?";
    } else {
        query = "SELECT id, category, question_text, option_a, option_b, option_c, option_d, correct_option, difficulty "
                "FROM trivia_questions ORDER BY RAND() LIMIT ?";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query.c_str(), query.size()) != 0) {
        log_error("get_random_trivia_questions prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    // Bind parameters
    MYSQL_BIND bind_param[2];
    memset(bind_param, 0, sizeof(bind_param));

    if (has_category) {
        bind_param[0].buffer_type = MYSQL_TYPE_STRING;
        bind_param[0].buffer = (char*)category.c_str();
        bind_param[0].buffer_length = category.size();

        bind_param[1].buffer_type = MYSQL_TYPE_LONG;
        bind_param[1].buffer = (char*)&count;
    } else {
        bind_param[0].buffer_type = MYSQL_TYPE_LONG;
        bind_param[0].buffer = (char*)&count;
    }

    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        log_error("get_random_trivia_questions bind param");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_random_trivia_questions execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    // Bind results
    TriviaQuestion q;
    char cat_buf[128];
    char text_buf[512];
    char opt_a_buf[256];
    char opt_b_buf[256];
    char opt_c_buf[256];
    char opt_d_buf[256];
    char correct_buf[4];
    char diff_buf[32];

    unsigned long cat_len = 0, text_len = 0;
    unsigned long opt_a_len = 0, opt_b_len = 0, opt_c_len = 0, opt_d_len = 0;
    unsigned long correct_len = 0, diff_len = 0;

    MYSQL_BIND bind_result[9];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &q.id;
    bind_result[0].is_unsigned = 1;

    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = cat_buf;
    bind_result[1].buffer_length = sizeof(cat_buf);
    bind_result[1].length = &cat_len;

    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = text_buf;
    bind_result[2].buffer_length = sizeof(text_buf);
    bind_result[2].length = &text_len;

    bind_result[3].buffer_type = MYSQL_TYPE_STRING;
    bind_result[3].buffer = opt_a_buf;
    bind_result[3].buffer_length = sizeof(opt_a_buf);
    bind_result[3].length = &opt_a_len;

    bind_result[4].buffer_type = MYSQL_TYPE_STRING;
    bind_result[4].buffer = opt_b_buf;
    bind_result[4].buffer_length = sizeof(opt_b_buf);
    bind_result[4].length = &opt_b_len;

    bind_result[5].buffer_type = MYSQL_TYPE_STRING;
    bind_result[5].buffer = opt_c_buf;
    bind_result[5].buffer_length = sizeof(opt_c_buf);
    bind_result[5].length = &opt_c_len;

    bind_result[6].buffer_type = MYSQL_TYPE_STRING;
    bind_result[6].buffer = opt_d_buf;
    bind_result[6].buffer_length = sizeof(opt_d_buf);
    bind_result[6].length = &opt_d_len;

    bind_result[7].buffer_type = MYSQL_TYPE_STRING;
    bind_result[7].buffer = correct_buf;
    bind_result[7].buffer_length = sizeof(correct_buf);
    bind_result[7].length = &correct_len;

    bind_result[8].buffer_type = MYSQL_TYPE_STRING;
    bind_result[8].buffer = diff_buf;
    bind_result[8].buffer_length = sizeof(diff_buf);
    bind_result[8].length = &diff_len;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("get_random_trivia_questions bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        log_error("get_random_trivia_questions store result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        q.category = std::string(cat_buf, cat_len);
        q.question_text = std::string(text_buf, text_len);
        q.option_a = std::string(opt_a_buf, opt_a_len);
        q.option_b = std::string(opt_b_buf, opt_b_len);
        q.option_c = std::string(opt_c_buf, opt_c_len);
        q.option_d = std::string(opt_d_buf, opt_d_len);
        q.correct_option = (correct_len > 0) ? correct_buf[0] : 'a';
        q.difficulty = std::string(diff_buf, diff_len);
        results.push_back(q);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return results;
}

std::vector<std::string> Database::get_trivia_categories() {
    std::vector<std::string> results;
    auto conn = pool_->acquire();

    const char* query = "SELECT DISTINCT category FROM trivia_questions ORDER BY category ASC";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_trivia_categories prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_trivia_categories execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    char cat_buf[128];
    unsigned long cat_len = 0;
    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = cat_buf;
    bind_result[0].buffer_length = sizeof(cat_buf);
    bind_result[0].length = &cat_len;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("get_trivia_categories bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        log_error("get_trivia_categories store result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        results.push_back(std::string(cat_buf, cat_len));
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return results;
}

bool Database::get_trivia_stats(uint64_t user_id, TriviaUserStats& stats) {
    stats.user_id = user_id;
    auto conn = pool_->acquire();

    const char* query = "SELECT games_played, games_won, correct_answers, total_answers, total_winnings "
                        "FROM trivia_user_stats WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_trivia_stats prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        log_error("get_trivia_stats bind param");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_trivia_stats execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind_result[5];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &stats.games_played;

    bind_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[1].buffer = &stats.games_won;

    bind_result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[2].buffer = &stats.correct_answers;

    bind_result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[3].buffer = &stats.total_answers;

    bind_result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[4].buffer = &stats.total_winnings;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("get_trivia_stats bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    mysql_stmt_store_result(stmt);
    bool found = mysql_stmt_fetch(stmt) == 0;

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return found;
}

bool Database::update_trivia_stats(uint64_t user_id, bool won, int correct, int total, int64_t winnings) {
    auto conn = pool_->acquire();

    int64_t win_val = won ? 1 : 0;
    int64_t play_val = 1;
    int64_t correct_val = correct;
    int64_t total_val = total;

    const char* query = "INSERT INTO trivia_user_stats (user_id, games_played, games_won, correct_answers, total_answers, total_winnings) "
                        "VALUES (?, ?, ?, ?, ?, ?) "
                        "ON DUPLICATE KEY UPDATE games_played = games_played + ?, games_won = games_won + ?, "
                        "correct_answers = correct_answers + ?, total_answers = total_answers + ?, total_winnings = total_winnings + ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("update_trivia_stats prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[11];
    memset(bind, 0, sizeof(bind));

    // INSERT values
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&play_val;

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&win_val;

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&correct_val;

    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&total_val;

    bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[5].buffer = (char*)&winnings;

    // UPDATE updates
    bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[6].buffer = (char*)&play_val;

    bind[7].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[7].buffer = (char*)&win_val;

    bind[8].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[8].buffer = (char*)&correct_val;

    bind[9].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[9].buffer = (char*)&total_val;

    bind[10].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[10].buffer = (char*)&winnings;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("update_trivia_stats bind param");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::seed_default_trivia_questions() {
    auto conn = pool_->acquire();

    // Check if table is empty
    const char* count_query = "SELECT COUNT(*) FROM trivia_questions";
    MYSQL_STMT* stmt_count = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt_count, count_query, strlen(count_query)) == 0) {
        if (mysql_stmt_execute(stmt_count) == 0) {
            long long count = 0;
            MYSQL_BIND bind_result[1];
            memset(bind_result, 0, sizeof(bind_result));
            bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind_result[0].buffer = &count;
            mysql_stmt_bind_result(stmt_count, bind_result);
            mysql_stmt_store_result(stmt_count);
            mysql_stmt_fetch(stmt_count);
            mysql_stmt_close(stmt_count);

            if (count > 0) {
                pool_->release(conn);
                return true; // Already seeded
            }
        } else {
            mysql_stmt_close(stmt_count);
        }
    } else {
        mysql_stmt_close(stmt_count);
    }

    // Seed questions
    struct RawQuestion {
        std::string category;
        std::string text;
        std::string a;
        std::string b;
        std::string c;
        std::string d;
        std::string correct;
        std::string diff;
    };

    std::vector<RawQuestion> default_questions = {
        {"general", "what is the capital of australia?", "sydney", "melbourne", "canberra", "brisbane", "c", "medium"},
        {"general", "which of these colors is not part of a primary rainbow?", "red", "violet", "pink", "yellow", "c", "easy"},
        {"science", "what is the chemical symbol for gold?", "gd", "au", "ag", "go", "b", "easy"},
        {"science", "what is the closest planet to the sun?", "venus", "mars", "mercury", "jupiter", "c", "easy"},
        {"gaming", "which video game franchise features the character master chief?", "halo", "doom", "gears of war", "mass effect", "a", "easy"},
        {"gaming", "in minecraft, which tool is best for mining obsidian?", "iron pickaxe", "diamond pickaxe", "golden pickaxe", "stone shovel", "b", "easy"},
        {"history", "who was the first president of the united states?", "thomas jefferson", "john adams", "george washington", "benjamin franklin", "c", "easy"},
        {"history", "in which year did world war ii end?", "1918", "1939", "1945", "1950", "c", "medium"},
        {"technology", "what does cpu stand for?", "central processing unit", "computer power utility", "control path user", "core process unit", "a", "easy"},
        {"technology", "which programming language was developed by bjarne stroustrup?", "python", "c++", "java", "rust", "b", "medium"},
        {"general", "how many bones are there in an adult human body?", "106", "206", "306", "406", "b", "medium"},
        {"science", "what gas do plants absorb during photosynthesis?", "oxygen", "carbon dioxide", "nitrogen", "helium", "b", "easy"},
        {"gaming", "which game holds the record for the best-selling video game of all time?", "tetris", "grand theft auto v", "minecraft", "wii sports", "c", "easy"},
        {"history", "which ancient empire built the colosseum in rome?", "greek empire", "roman empire", "persian empire", "egyptian empire", "b", "easy"},
        {"general", "which ocean is the largest on earth?", "atlantic ocean", "indian ocean", "arctic ocean", "pacific ocean", "d", "easy"}
    };

    const char* insert_query = "INSERT INTO trivia_questions (category, question_text, option_a, option_b, option_c, option_d, correct_option, difficulty) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    
    for (const auto& q : default_questions) {
        MYSQL_STMT* stmt_ins = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt_ins, insert_query, strlen(insert_query)) != 0) {
            mysql_stmt_close(stmt_ins);
            continue;
        }

        MYSQL_BIND bind[8];
        memset(bind, 0, sizeof(bind));

        bind[0].buffer_type = MYSQL_TYPE_STRING;
        bind[0].buffer = (char*)q.category.c_str();
        bind[0].buffer_length = q.category.size();

        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)q.text.c_str();
        bind[1].buffer_length = q.text.size();

        bind[2].buffer_type = MYSQL_TYPE_STRING;
        bind[2].buffer = (char*)q.a.c_str();
        bind[2].buffer_length = q.a.size();

        bind[3].buffer_type = MYSQL_TYPE_STRING;
        bind[3].buffer = (char*)q.b.c_str();
        bind[3].buffer_length = q.b.size();

        bind[4].buffer_type = MYSQL_TYPE_STRING;
        bind[4].buffer = (char*)q.c.c_str();
        bind[4].buffer_length = q.c.size();

        bind[5].buffer_type = MYSQL_TYPE_STRING;
        bind[5].buffer = (char*)q.d.c_str();
        bind[5].buffer_length = q.d.size();

        bind[6].buffer_type = MYSQL_TYPE_STRING;
        bind[6].buffer = (char*)q.correct.c_str();
        bind[6].buffer_length = q.correct.size();

        bind[7].buffer_type = MYSQL_TYPE_STRING;
        bind[7].buffer = (char*)q.diff.c_str();
        bind[7].buffer_length = q.diff.size();

        mysql_stmt_bind_param(stmt_ins, bind);
        mysql_stmt_execute(stmt_ins);
        mysql_stmt_close(stmt_ins);
    }

    pool_->release(conn);
    return true;
}

} // namespace db
} // namespace bronx
