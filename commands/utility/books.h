#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "media.h"
#include "../owner.h"
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace commands {
namespace utility {

template<typename T>
inline T get_safe_value(const nlohmann::json& j, const std::string& key, const T& default_val) {
    if (j.is_object() && j.contains(key) && !j[key].is_null()) {
        try {
            return j[key].get<T>();
        } catch (...) {}
    }
    return default_val;
}

// Lavender/purple premium accent color for nightIOwl reader components
static constexpr uint32_t BOOKS_EMBED_COLOR = bronx::COLOR_DEFAULT;

// Retrieve the nightIOwl API key from the env variable or .env file.
inline std::string get_nightiowl_api_key() {
    // 1. Try environment variable
    const char* env_key = std::getenv("NIGHTIOWL_API");
    if (env_key && std::strlen(env_key) > 0) {
        return std::string(env_key);
    }
    
    // 2. Try falling back to parsing .env file in the workspace
    std::vector<std::string> paths = {
        ".env",
        "site/.env",
        "/opt/bpp/.env",
        "/opt/bpp/site/.env",
        "/home/siqnole/Documents/code/bpp/.env"
    };
    for (const auto& path : paths) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("NIGHTIOWL_API=", 0) == 0) {
                    std::string val = line.substr(14);
                    // Trim quotes if any
                    if (!val.empty() && (val.front() == '"' || val.front() == '\'')) val.erase(0, 1);
                    if (!val.empty() && (val.back() == '"' || val.back() == '\'')) val.pop_back();
                    return val;
                }
            }
        }
    }
    return "";
}

// Perform a request to the nightIOwl server to list/search books.
inline nlohmann::json fetch_books_api(const std::string& search, int page, int limit) {
    std::string api_key = get_nightiowl_api_key();
    std::string url = "https://read.siqnole.dev/api/v1/books?apiKey=" + api_key;
    url += "&page=" + std::to_string(page);
    url += "&limit=" + std::to_string(limit);
    if (!search.empty()) {
        std::string encoded_search;
        for (char c : search) {
            if (c == ' ') encoded_search += "%20";
            else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded_search += c;
            else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
                encoded_search += hex;
            }
        }
        url += "&search=" + encoded_search;
    }
    
    HttpResponse res = http_get_sync(url);
    if (res.status == 200) {
        try {
            return nlohmann::json::parse(res.body);
        } catch (...) {}
    }
    return nlohmann::json();
}

// Fetch single book metadata details from the nightIOwl server.
inline nlohmann::json fetch_book_details_api(int book_id) {
    std::string api_key = get_nightiowl_api_key();
    std::string url = "https://read.siqnole.dev/api/v1/books/" + std::to_string(book_id) + "?apiKey=" + api_key;
    HttpResponse res = http_get_sync(url);
    if (res.status == 200) {
        try {
            return nlohmann::json::parse(res.body);
        } catch (...) {}
    }
    return nlohmann::json();
}

// Build a paginated browse message with book list, read dropdown, and page controls.
inline dpp::message build_browse_message(const std::string& search, int page, uint64_t user_id) {
    int limit = 5;
    nlohmann::json res = fetch_books_api(search, page, limit);
    
    if (res.is_null() || !res.is_object() || !res.contains("books") || !res["books"].is_array()) {
        dpp::embed embed = bronx::create_embed("failed to connect to nightiowl api or invalid api key.", bronx::COLOR_ERROR);
        embed.set_footer(dpp::embed_footer().set_text("powered by nightiowl"));
        return dpp::message().add_embed(embed);
    }
    
    auto books = res["books"];
    int total = 0;
    int pages = 0;
    
    if (res.contains("pagination") && res["pagination"].is_object()) {
        auto pag = res["pagination"];
        total = get_safe_value(pag, "total", 0);
        pages = get_safe_value(pag, "pages", 1);
    }
    
    if (books.empty()) {
        dpp::embed embed = bronx::create_embed("no books found matching your query.", bronx::COLOR_WARNING);
        embed.set_title("nightiowl - browse books");
        embed.set_footer(dpp::embed_footer().set_text("powered by nightiowl | page 0/0"));
        return dpp::message().add_embed(embed);
    }
    
    std::string desc = "";
    if (!search.empty()) {
        desc += "search results for: `" + search + "`\n\n";
    }
    
    for (const auto& book : books) {
        if (!book.is_object()) continue;
        int id = get_safe_value(book, "id", 0);
        std::string title = get_safe_value<std::string>(book, "title", "unknown");
        std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return std::tolower(c); });
        std::string series = get_safe_value<std::string>(book, "seriesName", "");
        std::transform(series.begin(), series.end(), series.begin(), [](unsigned char c) { return std::tolower(c); });
        int page_count = get_safe_value(book, "pageCount", 0);
        std::string file_type = get_safe_value<std::string>(book, "fileType", "cbz");
        std::transform(file_type.begin(), file_type.end(), file_type.begin(), [](unsigned char c) { return std::tolower(c); });
        int issue = get_safe_value(book, "issueNumber", 0);
        
        desc += "**[id: " + std::to_string(id) + "]** `" + title + "`\n";
        desc += "   *";
        if (!series.empty()) {
            desc += "series: " + series;
            if (issue > 0) desc += " (issue #" + std::to_string(issue) + ")";
            desc += " • ";
        }
        desc += std::to_string(page_count) + " pages • " + file_type + "*\n\n";
    }
    
    dpp::embed embed = bronx::create_embed(desc, BOOKS_EMBED_COLOR);
    embed.set_title("nightiowl - browse books");
    embed.set_footer(dpp::embed_footer().set_text("powered by nightiowl | page " + std::to_string(page) + "/" + std::to_string(pages)));
    
    dpp::message msg;
    msg.add_embed(embed);
    
    // Add dropdown selection row
    dpp::component select_menu;
    select_menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("select a book to read...")
        .set_id("books:select:" + std::to_string(user_id));
        
    for (const auto& book : books) {
        if (!book.is_object()) continue;
        int id = get_safe_value(book, "id", 0);
        std::string title = get_safe_value<std::string>(book, "title", "unknown");
        std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return std::tolower(c); });
        if (title.size() > 80) title = title.substr(0, 77) + "...";
        std::string label = "[" + std::to_string(id) + "] " + title;
        
        std::string desc_opt = "pages: " + std::to_string(get_safe_value(book, "pageCount", 0));
        std::string series = get_safe_value<std::string>(book, "seriesName", "");
        std::transform(series.begin(), series.end(), series.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!series.empty()) desc_opt += " | series: " + series;
        if (desc_opt.size() > 100) desc_opt = desc_opt.substr(0, 97) + "...";
        
        select_menu.add_select_option(dpp::select_option(label, std::to_string(id), desc_opt));
    }
    
    dpp::component select_row;
    select_row.set_type(dpp::cot_action_row).add_component(select_menu);
    msg.add_component(select_row);
    
    // Add pagination controls
    dpp::component btn_row;
    btn_row.set_type(dpp::cot_action_row);
    
    std::string short_search = search;
    if (short_search.size() > 40) short_search = short_search.substr(0, 40);
    
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_label("previous")
        .set_style(dpp::cos_secondary)
        .set_id("books:bp:" + std::to_string(user_id) + ":" + std::to_string(page - 1) + ":" + short_search);
    if (page <= 1) {
        prev_btn.set_disabled(true);
    }
    
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_label("next")
        .set_style(dpp::cos_secondary)
        .set_id("books:bn:" + std::to_string(user_id) + ":" + std::to_string(page + 1) + ":" + short_search);
    if (page >= pages) {
        next_btn.set_disabled(true);
    }
    
    btn_row.add_component(prev_btn);
    btn_row.add_component(next_btn);
    msg.add_component(btn_row);
    
    return msg;
}

// Build a dynamic read view message with custom embed pagination and progress updates.
inline dpp::message build_read_message(bronx::db::Database* db, int book_id, int page, uint64_t user_id) {
    nlohmann::json book = fetch_book_details_api(book_id);
    if (book.is_null() || !book.is_object() || !book.contains("id")) {
        dpp::embed embed = bronx::create_embed("failed to fetch book details for id **" + std::to_string(book_id) + "**.", bronx::COLOR_ERROR);
        embed.set_footer(dpp::embed_footer().set_text("powered by nightiowl"));
        return dpp::message().add_embed(embed);
    }
    
    std::string title = get_safe_value<std::string>(book, "title", "unknown book");
    std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return std::tolower(c); });
    int page_count = get_safe_value(book, "pageCount", 1);
    
    // Save/update progress in database
    db->set_book_progress(user_id, book_id, page);
    
    // Construct the page image URL with query authenticated API key
    std::string api_key = get_nightiowl_api_key();
    std::string img_url = "https://read.siqnole.dev/api/v1/books/" + std::to_string(book_id) + "/page/" + std::to_string(page) + "?apiKey=" + api_key;
    
    std::string reader_url = "https://read.siqnole.dev/reader/book" + std::to_string(book_id);
    std::string reading_text = "reading: " + title + " | page " + std::to_string(page) + "/" + std::to_string(page_count);
    std::string hyperlinked_text = "[" + reading_text + "](" + reader_url + ")";

    dpp::message msg;
    msg.set_content(hyperlinked_text);
    
    // Fetch image from URL
    HttpResponse img_res = http_get_sync(img_url);
    if (img_res.status == 200 && !img_res.body.empty()) {
        msg.add_file("page.png", img_res.body);
    } else {
        msg.set_content(hyperlinked_text + " (failed to load page image)");
    }
    
    // Action Row components
    dpp::component btn_row;
    btn_row.set_type(dpp::cot_action_row);
    
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_label("back")
        .set_style(dpp::cos_primary)
        .set_id("books:rp:" + std::to_string(user_id) + ":" + std::to_string(book_id) + ":" + std::to_string(page - 1));
    if (page <= 1) {
        prev_btn.set_disabled(true);
    }
    
    dpp::component jump_btn;
    jump_btn.set_type(dpp::cot_button)
        .set_label("jump")
        .set_style(dpp::cos_secondary)
        .set_id("books:rj:" + std::to_string(user_id) + ":" + std::to_string(book_id) + ":" + std::to_string(page));
        
    dpp::component close_btn;
    close_btn.set_type(dpp::cot_button)
        .set_label("close")
        .set_style(dpp::cos_danger)
        .set_id("books:rc:" + std::to_string(user_id) + ":" + std::to_string(book_id) + ":" + std::to_string(page));
        
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_label("next")
        .set_style(dpp::cos_primary)
        .set_id("books:rn:" + std::to_string(user_id) + ":" + std::to_string(book_id) + ":" + std::to_string(page + 1));
    if (page >= page_count) {
        next_btn.set_disabled(true);
    }
    
    btn_row.add_component(prev_btn);
    btn_row.add_component(jump_btn);
    btn_row.add_component(close_btn);
    btn_row.add_component(next_btn);
    
    msg.add_component(btn_row);
    return msg;
}

struct BookCacheEntry {
    std::vector<dpp::command_option_choice> choices;
    std::chrono::steady_clock::time_point timestamp;
};

class BookAutocompleteCache {
private:
    std::unordered_map<std::string, BookCacheEntry> cache;
    std::mutex mtx;
    static constexpr std::chrono::minutes EXPIRY_TIME{5};

public:
    bool get(const std::string& query, std::vector<dpp::command_option_choice>& out) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache.find(query);
        if (it != cache.end()) {
            if (std::chrono::steady_clock::now() - it->second.timestamp < EXPIRY_TIME) {
                out = it->second.choices;
                return true;
            }
        }
        return false;
    }

    void put(const std::string& query, const std::vector<dpp::command_option_choice>& choices) {
        std::lock_guard<std::mutex> lock(mtx);
        cache[query] = {choices, std::chrono::steady_clock::now()};
    }
};

inline BookAutocompleteCache book_autocomplete_cache;

// Initialize all commands for the media book reader.
inline std::vector<Command*> get_books_commands(bronx::db::Database* db) {
    std::vector<Command*> cmds;
    
    // 1. Browse command
    static Command browse("browse", "browse and search books in the nightiowl media library", "utility", {"books", "booklist"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            std::string search = "";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) search += " ";
                search += args[i];
            }
            
            // Query API asynchronously in thread
            std::thread([&bot, event, search, user_id]() {
                try {
                    dpp::message msg = build_browse_message(search, 1, user_id);
                    msg.set_channel_id(event.msg.channel_id);
                    bot.message_create(msg);
                } catch (const std::exception& e) {
                    bot.log(dpp::ll_error, "Exception in browse command thread: " + std::string(e.what()));
                } catch (...) {
                    bot.log(dpp::ll_error, "Unknown exception in browse command thread");
                }
            }).detach();
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            std::string search = "";
            
            auto param = event.get_parameter("search");
            if (std::holds_alternative<std::string>(param)) {
                search = std::get<std::string>(param);
            }
            
            event.thinking(false);
            std::thread([&bot, event, search, user_id]() {
                try {
                    dpp::message msg = build_browse_message(search, 1, user_id);
                    event.edit_response(msg);
                } catch (const std::exception& e) {
                    bot.log(dpp::ll_error, "Exception in browse slash command thread: " + std::string(e.what()));
                } catch (...) {
                    bot.log(dpp::ll_error, "Unknown exception in browse slash command thread");
                }
            }).detach();
        },
        {
            dpp::command_option(dpp::co_string, "search", "keyword search by title or series", false)
        });
        
    cmds.push_back(&browse);
    
    // 2. Read command
    static Command read("read", "read a book by id, saving progress as you go", "utility", {"r", "openbook"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            
            std::thread([&bot, event, db, args, user_id]() {
                try {
                    int book_id = 0;
                    if (!args.empty()) {
                        try { book_id = std::stoi(args[0]); } catch (...) {}
                    }
                    
                    if (book_id <= 0) {
                        book_id = db->get_last_read_book_id(user_id);
                    }
                    
                    if (book_id <= 0) {
                        bronx::send_message(bot, event, bronx::create_embed("please specify a book id to read. use `/browse` to search.", bronx::COLOR_ERROR));
                        return;
                    }
                    
                    int progress = db->get_book_progress(user_id, book_id);
                    if (progress < 1) progress = 1;
                    
                    dpp::message msg = build_read_message(db, book_id, progress, user_id);
                    msg.set_channel_id(event.msg.channel_id);
                    bot.message_create(msg);
                } catch (const std::exception& e) {
                    bot.log(dpp::ll_error, "Exception in read prefix command thread: " + std::string(e.what()));
                } catch (...) {
                    bot.log(dpp::ll_error, "Unknown exception in read prefix command thread");
                }
            }).detach();
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            
            std::thread([&bot, event, db, user_id]() {
                try {
                    int book_id = 0;
                    auto param = event.get_parameter("book_id");
                    if (std::holds_alternative<int64_t>(param)) {
                        book_id = static_cast<int>(std::get<int64_t>(param));
                    }
                    
                    if (book_id <= 0) {
                        book_id = db->get_last_read_book_id(user_id);
                    }
                    
                    if (book_id <= 0) {
                        event.reply(dpp::message().add_embed(bronx::create_embed("please specify a book id to read. use `/browse` to search.", bronx::COLOR_ERROR)).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    event.thinking(false);
                    int progress = db->get_book_progress(user_id, book_id);
                    if (progress < 1) progress = 1;
                    
                    dpp::message msg = build_read_message(db, book_id, progress, user_id);
                    event.edit_response(msg);
                } catch (const std::exception& e) {
                    bot.log(dpp::ll_error, "Exception in read slash command thread: " + std::string(e.what()));
                } catch (...) {
                    bot.log(dpp::ll_error, "Unknown exception in read slash command thread");
                }
            }).detach();
        },
        {
            dpp::command_option(dpp::co_integer, "book_id", "id of the book to read (leave blank for last read)", false).set_auto_complete(true)
        },
        [](dpp::cluster& bot, const dpp::autocomplete_t& event) {
            std::string query = "";
            for (const auto& opt : event.options) {
                if (opt.focused) {
                    if (std::holds_alternative<std::string>(opt.value)) {
                        query = std::get<std::string>(opt.value);
                    } else if (std::holds_alternative<int64_t>(opt.value)) {
                        query = std::to_string(std::get<int64_t>(opt.value));
                    }
                    break;
                }
            }

            // Lowercase query for caching key consistency
            std::string lquery = query;
            std::transform(lquery.begin(), lquery.end(), lquery.begin(), [](unsigned char c) { return std::tolower(c); });

            std::vector<dpp::command_option_choice> cached_choices;
            if (book_autocomplete_cache.get(lquery, cached_choices)) {
                dpp::interaction_response response(dpp::ir_autocomplete_reply);
                for (const auto& choice : cached_choices) {
                    response.add_autocomplete_choice(choice);
                }
                bot.interaction_response_create(event.command.id, event.command.token, response);
                return;
            }

            // Spawn non-blocking thread to fetch from API
            std::thread([&bot, event, query, lquery]() {
                try {
                    std::vector<dpp::command_option_choice> choices;
                    try {
                        nlohmann::json res = fetch_books_api(query, 1, 25);
                        if (res.is_object() && res.contains("books") && res["books"].is_array()) {
                            for (const auto& book : res["books"]) {
                                if (book.is_object() && book.contains("id") && book.contains("title")) {
                                    int id = book["id"].get<int>();
                                    std::string title = book["title"].get<std::string>();
                                    std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return std::tolower(c); });
                                    
                                    std::string author = "";
                                    if (book.contains("author") && book["author"].is_string()) {
                                        author = book["author"].get<std::string>();
                                        std::transform(author.begin(), author.end(), author.begin(), [](unsigned char c) { return std::tolower(c); });
                                    }
                                    
                                    std::string display = title;
                                    if (!author.empty()) {
                                        display += " by " + author;
                                    }
                                    
                                    if (display.size() > 100) {
                                        display = display.substr(0, 97) + "...";
                                    }
                                    
                                    choices.push_back(dpp::command_option_choice(display, (int64_t)id));
                                }
                            }
                        }
                    } catch (...) {}

                    if (choices.empty()) {
                        choices.push_back(dpp::command_option_choice("type to search nightiowl library...", (int64_t)0));
                    }

                    book_autocomplete_cache.put(lquery, choices);
                    dpp::interaction_response response(dpp::ir_autocomplete_reply);
                    for (const auto& choice : choices) {
                        response.add_autocomplete_choice(choice);
                    }
                    bot.interaction_response_create(event.command.id, event.command.token, response);
                } catch (const std::exception& e) {
                    bot.log(dpp::ll_error, "Exception in book autocomplete thread: " + std::string(e.what()));
                } catch (...) {
                    bot.log(dpp::ll_error, "Unknown exception in book autocomplete thread");
                }
            }).detach();
        });
        
    cmds.push_back(&read);
    
    return cmds;
}

// Register the button, select menu, and modal interaction listeners for books.
inline void register_books_interactions(dpp::cluster& bot, bronx::db::Database* db) {
    // 1. Handle pagination and readers button clicks
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("books:", 0) != 0) return;
        
        std::thread([db, &bot, event]() {
            try {
                std::vector<std::string> parts;
                std::stringstream ss(event.custom_id);
                std::string part;
                while (std::getline(ss, part, ':')) {
                    parts.push_back(part);
                }
                
                if (parts.size() < 4) return;
                
                std::string op = parts[1];
                uint64_t target_user = std::stoull(parts[2]);
                uint64_t clicker = event.command.get_issuing_user().id;
                
                if (clicker != target_user) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::create_embed("this menu is not for you", bronx::COLOR_ERROR)).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                // Handle browse prev/next
                if (op == "bp" || op == "bn") {
                    int page = std::stoi(parts[3]);
                    std::string search = "";
                    for (size_t i = 4; i < parts.size(); ++i) {
                        if (i > 4) search += ":";
                        search += parts[i];
                    }
                    
                    dpp::message msg = build_browse_message(search, page, clicker);
                    event.reply(dpp::ir_update_message, msg);
                }
                
                // Handle read prev/next
                else if (op == "rp" || op == "rn") {
                    int book_id = std::stoi(parts[3]);
                    int page = std::stoi(parts[4]);
                    
                    dpp::message msg = build_read_message(db, book_id, page, clicker);
                    event.reply(dpp::ir_update_message, msg);
                }
                
                // Handle close reader
                else if (op == "rc") {
                    int book_id = std::stoi(parts[3]);
                    int page = std::stoi(parts[4]);
                    
                    nlohmann::json book = fetch_book_details_api(book_id);
                    std::string title = get_safe_value<std::string>(book, "title", "book");
                    std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c) { return std::tolower(c); });
                    int page_count = get_safe_value(book, "pageCount", 1);
                    
                    dpp::embed embed = bronx::create_embed("you stopped reading **" + title + "** at page **" + std::to_string(page) + "/" + std::to_string(page_count) + "**.\nyour progress has been saved!", bronx::COLOR_SUCCESS);
                    embed.set_footer(dpp::embed_footer().set_text("powered by nightiowl"));
                    
                    dpp::message msg;
                    msg.add_embed(embed);
                    event.reply(dpp::ir_update_message, msg);
                }
                
                // Handle jump to page modal popup
                else if (op == "rj") {
                    int book_id = std::stoi(parts[3]);
                    
                    nlohmann::json book = fetch_book_details_api(book_id);
                    int page_count = get_safe_value(book, "pageCount", 1);
                    
                    dpp::interaction_modal_response modal("books:modal:" + std::to_string(book_id), "jump to page");
                    modal.add_component(
                        dpp::component()
                            .set_label("page number (1-" + std::to_string(page_count) + ")")
                            .set_id("jump_page_input")
                            .set_type(dpp::cot_text)
                            .set_placeholder("e.g. 42")
                            .set_min_length(1)
                            .set_max_length(5)
                            .set_required(true)
                    );
                    event.dialog(modal);
                }
            } catch (const std::exception& e) {
                bot.log(dpp::ll_error, "Exception in books button click thread: " + std::string(e.what()));
            } catch (...) {
                bot.log(dpp::ll_error, "Unknown exception in books button click thread");
            }
        }).detach();
    });
    
    // 2. Handle select menu quick reader choices
    bot.on_select_click([db, &bot](const dpp::select_click_t& event) {
        if (event.custom_id.rfind("books:select:", 0) != 0) return;
        
        std::string user_id_str = event.custom_id.substr(13);
        uint64_t target_user = std::stoull(user_id_str);
        uint64_t clicker = event.command.get_issuing_user().id;
        
        if (clicker != target_user) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::create_embed("this menu is not for you", bronx::COLOR_ERROR)).set_flags(dpp::m_ephemeral));
            return;
        }
        
        if (event.values.empty()) return;
        
        std::thread([db, &bot, event, clicker]() {
            try {
                int book_id = std::stoi(event.values[0]);
                
                // Get last saved page or start at 1
                int progress = db->get_book_progress(clicker, book_id);
                if (progress < 1) progress = 1;
                
                dpp::message msg = build_read_message(db, book_id, progress, clicker);
                event.reply(dpp::ir_update_message, msg);
            } catch (const std::exception& e) {
                bot.log(dpp::ll_error, "Exception in books select click thread: " + std::string(e.what()));
            } catch (...) {
                bot.log(dpp::ll_error, "Unknown exception in books select click thread");
            }
        }).detach();
    });
    
    // 3. Handle modal jump-to-page form submissions
    bot.on_form_submit([db, &bot](const dpp::form_submit_t& event) {
        if (event.custom_id.rfind("books:modal:", 0) != 0) return;
        
        std::thread([db, &bot, event]() {
            try {
                int book_id = std::stoi(event.custom_id.substr(12));
                uint64_t clicker = event.command.get_issuing_user().id;
                
                if (event.components.empty()) return;
                
                std::string page_str;
                try {
                    page_str = std::get<std::string>(event.components[0].value);
                } catch (...) {
                    return;
                }
                
                int page = 1;
                try {
                    page = std::stoi(page_str);
                } catch (...) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::create_embed("please enter a valid page number", bronx::COLOR_ERROR)).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                nlohmann::json book = fetch_book_details_api(book_id);
                if (book.is_null() || !book.is_object()) return;
                int page_count = get_safe_value(book, "pageCount", 1);
                
                if (page < 1) page = 1;
                if (page > page_count) page = page_count;
                
                dpp::message msg = build_read_message(db, book_id, page, clicker);
                event.reply(dpp::ir_update_message, msg);
            } catch (const std::exception& e) {
                bot.log(dpp::ll_error, "Exception in books form submit thread: " + std::string(e.what()));
            } catch (...) {
                bot.log(dpp::ll_error, "Unknown exception in books form submit thread");
            }
        }).detach();
    });
}

} // namespace utility
} // namespace commands
