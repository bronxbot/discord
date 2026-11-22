#pragma once

#include "../../database/operations/utility/rss_operations.h"
#include "../../utils/logger.h"
#include "../command.h"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <netdb.h>
#include <mutex>
#include <optional>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#include <sys/socket.h>
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

namespace commands::utility::rss_detail {

struct Item {
    std::string guid;
    std::string title;
    std::string link;
    std::string description;
};

struct FetchResult {
    std::vector<Item> items;
    int interval_seconds = 1200;
    std::string error;
};

inline std::mutex checks_mutex;
inline std::unordered_set<uint64_t> active_checks;
inline bool parse_snowflake(std::string value, uint64_t& id);

inline std::string replace_all(std::string value, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

inline std::string decode_html(std::string value) {
    value = replace_all(std::move(value), "&amp;", "&");
    value = replace_all(std::move(value), "&lt;", "<");
    value = replace_all(std::move(value), "&gt;", ">");
    value = replace_all(std::move(value), "&quot;", "\"");
    value = replace_all(std::move(value), "&#39;", "'");
    value = replace_all(std::move(value), "&apos;", "'");
    return value;
}

inline std::string strip_html(std::string value) {
    std::string plain;
    plain.reserve(value.size());
    bool in_tag = false;
    for (char ch : value) {
        if (ch == '<') in_tag = true;
        else if (ch == '>') in_tag = false;
        else if (!in_tag) plain.push_back(ch);
    }
    plain = decode_html(std::move(plain));
    const auto first = plain.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = plain.find_last_not_of(" \t\r\n");
    plain = plain.substr(first, last - first + 1);
    if (plain.size() > 3500) plain.resize(3497), plain += "...";
    return plain;
}

inline std::string xml_tag(const std::string& xml, const std::string& tag) {
    const std::string open = "<" + tag;
    auto start = xml.find(open);
    if (start == std::string::npos) return {};
    auto end_open = xml.find('>', start);
    if (end_open == std::string::npos) return {};
    const std::string close = "</" + tag + ">";
    auto end = xml.find(close, end_open + 1);
    if (end == std::string::npos) return {};
    std::string value = xml.substr(end_open + 1, end - end_open - 1);
    if (value.rfind("<![CDATA[", 0) == 0) {
        value.erase(0, 9);
        auto cdata_end = value.find("]]>");
        if (cdata_end != std::string::npos) value.resize(cdata_end);
    }
    return decode_html(std::move(value));
}

inline std::string xml_attribute(const std::string& tag, const std::string& name) {
    for (const char quote : {'"', '\''}) {
        const std::string key = name + "=" + quote;
        auto start = tag.find(key);
        if (start == std::string::npos) continue;
        start += key.size();
        auto end = tag.find(quote, start);
        if (end != std::string::npos) return decode_html(tag.substr(start, end - start));
    }
    return {};
}

inline std::string xml_link(const std::string& xml) {
    auto start = xml.find("<link");
    if (start == std::string::npos) return {};
    auto end = xml.find('>', start);
    if (end == std::string::npos) return {};
    const auto tag = xml.substr(start, end - start + 1);
    auto href = xml_attribute(tag, "href");
    if (!href.empty()) return href;
    return xml_tag(xml, "link");
}

inline std::string json_string(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object[key].is_string()) return {};
    return object[key].get<std::string>();
}

inline void add_item(std::vector<Item>& items, Item item) {
    if (item.title.empty() && item.link.empty() && item.description.empty()) return;
    if (item.guid.empty()) item.guid = item.link.empty() ? item.title : item.link;
    if (item.link.rfind("http://", 0) != 0 && item.link.rfind("https://", 0) != 0) item.link.clear();
    if (item.title.size() > 250) item.title.resize(247), item.title += "...";
    if (item.description.size() > 3500) item.description.resize(3497), item.description += "...";
    items.push_back(std::move(item));
}

inline int parse_interval(const std::string& body) {
    int seconds = 1200;
    auto ttl = xml_tag(body, "ttl");
    if (!ttl.empty()) {
        try {
            const int minutes = std::stoi(ttl);
            if (minutes > 0) seconds = minutes * 60;
        } catch (...) {}
    }
    auto json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_discarded()) {
        for (const char* key : {"ttl", "_ttl"}) {
            if (json.contains(key) && json[key].is_number_integer()) {
                const auto minutes = json[key].get<int>();
                if (minutes > 0) seconds = minutes * 60;
            }
        }
    }
    return std::clamp(seconds, 60, 86400);
}

inline std::vector<Item> parse_items(const std::string& body) {
    std::vector<Item> items;
    auto json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_discarded() && json.contains("items") && json["items"].is_array()) {
        for (const auto& entry : json["items"]) {
            Item item;
            item.guid = json_string(entry, "id");
            item.link = json_string(entry, "url");
            item.title = json_string(entry, "title");
            item.description = json_string(entry, "content_text");
            if (item.description.empty()) item.description = json_string(entry, "summary");
            if (item.description.empty()) item.description = json_string(entry, "content_html");
            item.description = strip_html(std::move(item.description));
            add_item(items, std::move(item));
            if (items.size() >= 100) break;
        }
        return items;
    }

    const bool atom = body.find("<feed") != std::string::npos;
    const std::string open = atom ? "<entry" : "<item";
    const std::string close = atom ? "</entry>" : "</item>";
    size_t pos = 0;
    while (items.size() < 100) {
        auto start = body.find(open, pos);
        if (start == std::string::npos) break;
        auto end = body.find(close, start);
        if (end == std::string::npos) break;
        const auto block = body.substr(start, end + close.size() - start);
        Item item;
        item.title = xml_tag(block, "title");
        item.link = xml_link(block);
        item.guid = xml_tag(block, atom ? "id" : "guid");
        item.description = xml_tag(block, "description");
        if (item.description.empty()) item.description = xml_tag(block, "content:encoded");
        if (item.description.empty()) item.description = xml_tag(block, "summary");
        if (item.description.empty()) item.description = xml_tag(block, "content");
        item.description = strip_html(std::move(item.description));
        add_item(items, std::move(item));
        pos = end + close.size();
    }
    return items;
}

#ifdef HAVE_LIBCURL
inline bool is_public_ipv4(uint32_t address) {
    const uint32_t first = address >> 24;
    const uint32_t second = (address >> 16) & 0xff;
    const uint32_t third = (address >> 8) & 0xff;
    if (first == 0 || first == 10 || first == 127 || first >= 224) return false;
    if (first == 169 && second == 254) return false;
    if (first == 172 && second >= 16 && second <= 31) return false;
    if (first == 192 && (second == 168 || (second == 0 && (third == 0 || third == 2)))) return false;
    if (first == 100 && second >= 64 && second <= 127) return false;
    if (first == 198 && (second == 18 || second == 19 || (second == 51 && third == 100))) return false;
    if (first == 203 && second == 0 && third == 113) return false;
    if (first == 240) return false;
    return true;
}

inline bool public_address(const addrinfo* address, std::string& text) {
    char buffer[INET6_ADDRSTRLEN]{};
    if (address->ai_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
        const uint32_t value = ntohl(ipv4->sin_addr.s_addr);
        if (!is_public_ipv4(value) || !inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer))) return false;
        text = buffer;
        return true;
    }
    if (address->ai_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address->ai_addr);
        const auto* bytes = ipv6->sin6_addr.s6_addr;
        if (IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr) ||
            IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr) ||
            IN6_IS_ADDR_LINKLOCAL(&ipv6->sin6_addr) ||
            IN6_IS_ADDR_MULTICAST(&ipv6->sin6_addr) ||
            (bytes[0] & 0xfe) == 0xfc ||
            (bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x0d && bytes[3] == 0xb8)) return false;
        if (IN6_IS_ADDR_V4MAPPED(&ipv6->sin6_addr)) {
            uint32_t v4 = 0;
            std::memcpy(&v4, bytes + 12, sizeof(v4));
            if (!is_public_ipv4(ntohl(v4))) return false;
        }
        if (!inet_ntop(AF_INET6, &ipv6->sin6_addr, buffer, sizeof(buffer))) return false;
        text = buffer;
        return true;
    }
    return false;
}

struct Endpoint {
    std::string host;
    std::string port;
    std::string pinned_ip;
};

inline bool resolve_public_endpoint(const std::string& url, Endpoint& endpoint, std::string& error) {
    const bool https = url.rfind("https://", 0) == 0;
    const size_t scheme_length = https ? 8 : (url.rfind("http://", 0) == 0 ? 7 : 0);
    if (scheme_length == 0) {
        error = "feed URLs must use http or https";
        return false;
    }
    const auto authority_start = scheme_length;
    auto authority_end = url.find_first_of("/?#", authority_start);
    if (authority_end == std::string::npos) authority_end = url.size();
    std::string authority = url.substr(authority_start, authority_end - authority_start);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        error = "feed URL host is invalid";
        return false;
    }

    endpoint.port = https ? "443" : "80";
    if (authority.front() == '[') {
        auto bracket = authority.find(']');
        if (bracket == std::string::npos) {
            error = "feed URL host is invalid";
            return false;
        }
        endpoint.host = authority.substr(1, bracket - 1);
        if (bracket + 1 < authority.size()) {
            if (authority[bracket + 1] != ':') {
                error = "feed URL port is invalid";
                return false;
            }
            endpoint.port = authority.substr(bracket + 2);
        }
    } else {
        auto colon = authority.rfind(':');
        if (colon != std::string::npos && authority.find(':') == colon) {
            endpoint.host = authority.substr(0, colon);
            endpoint.port = authority.substr(colon + 1);
        } else {
            endpoint.host = authority;
        }
    }
    if (endpoint.host.empty() || endpoint.port.empty()) {
        error = "feed URL host or port is invalid";
        return false;
    }
    if ((https && endpoint.port != "443") || (!https && endpoint.port != "80")) {
        error = "feed URLs may use only the standard http and https ports";
        return false;
    }
    for (char& ch : endpoint.host) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    if (getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &results) != 0 || !results) {
        error = "feed host could not be resolved";
        return false;
    }
    bool ok = false;
    bool unsafe = false;
    for (auto* result = results; result; result = result->ai_next) {
        std::string ip;
        if (!public_address(result, ip)) {
            unsafe = true;
            break;
        }
        if (!ok) {
            endpoint.pinned_ip = std::move(ip);
            ok = true;
        }
    }
    freeaddrinfo(results);
    if (!ok || unsafe) {
        error = "feed host must resolve only to public IP addresses";
        return false;
    }
    return true;
}

struct Response {
    std::string body;
    bool too_large = false;
};

inline size_t append_body(char* data, size_t size, size_t count, void* context) {
    auto* response = static_cast<Response*>(context);
    const size_t bytes = size * count;
    constexpr size_t max_body = 2 * 1024 * 1024;
    if (bytes > max_body - response->body.size()) {
        response->too_large = true;
        return 0;
    }
    response->body.append(data, bytes);
    return bytes;
}

inline FetchResult fetch_feed(const std::string& url) {
    FetchResult result;
    static std::once_flag curl_once;
    static CURLcode curl_init_result = CURLE_FAILED_INIT;
    std::call_once(curl_once, [] { curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (curl_init_result != CURLE_OK) {
        result.error = "http client initialization failed";
        return result;
    }

    Endpoint endpoint;
    if (!resolve_public_endpoint(url, endpoint, result.error)) return result;

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "could not initialize feed request";
        return result;
    }
    Response response;
    const std::string resolve = endpoint.host + ":" + endpoint.port + ":" +
        (endpoint.pinned_ip.find(':') == std::string::npos ? endpoint.pinned_ip : "[" + endpoint.pinned_ip + "]");
    curl_slist* resolve_list = curl_slist_append(nullptr, resolve.c_str());
    if (!resolve_list) {
        curl_easy_cleanup(curl);
        result.error = "could not prepare a safe feed connection";
        return result;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "bronx-bot-rss/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    const CURLcode status = curl_easy_perform(curl);
    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_slist_free_all(resolve_list);
    curl_easy_cleanup(curl);
    if (status != CURLE_OK) {
        result.error = response.too_large ? "feed response exceeds 2 mb" : curl_easy_strerror(status);
        return result;
    }
    if (http_status < 200 || http_status >= 300) {
        result.error = "feed returned http status " + std::to_string(http_status);
        return result;
    }

    result.interval_seconds = parse_interval(response.body);
    result.items = parse_items(response.body);
    if (result.items.empty()) result.error = "response did not contain feed items";
    return result;
}
#else
inline FetchResult fetch_feed(const std::string&) {
    return {{}, 1200, "RSS requires libcurl; rebuild with libcurl available"};
}
#endif

inline bool begin_check(uint64_t feed_id) {
    std::lock_guard<std::mutex> lock(checks_mutex);
    return active_checks.insert(feed_id).second;
}

inline void end_check(uint64_t feed_id) {
    std::lock_guard<std::mutex> lock(checks_mutex);
    active_checks.erase(feed_id);
}

inline void post_item(dpp::cluster& bot, uint64_t channel_id, const Item& item) {
    dpp::embed embed;
    std::string title = item.title.empty() ? "new feed item" : item.title;
    if (title.size() > 250) title.resize(247), title += "...";
    embed.set_title(title).set_color(0xf26522);
    if (!item.description.empty()) embed.set_description(item.description);
    if (!item.link.empty()) embed.set_url(item.link);
    dpp::message message(channel_id, "");
    message.add_embed(embed);
    bot.message_create(message, [](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) bronx::logger::error("rss", "failed to post feed item");
    });
}

inline std::vector<uint64_t> target_channels(const std::string& encoded) {
    std::vector<uint64_t> channels;
    std::stringstream stream(encoded);
    std::string part;
    while (std::getline(stream, part, ',')) {
        uint64_t id = 0;
        if (parse_snowflake(part, id)) channels.push_back(id);
    }
    return channels;
}

inline void check_feed(dpp::cluster& bot, bronx::db::Database* db,
                       bronx::db::rss_operations::Feed feed, bool post_latest = false) {
    if (!begin_check(feed.id)) return;
    std::thread([&bot, db, feed = std::move(feed), post_latest]() {
        const auto result = fetch_feed(feed.url);
        const auto checked_at = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        if (result.error.empty() && !result.items.empty()) {
            if (result.interval_seconds != feed.interval_seconds &&
                !bronx::db::rss_operations::update_interval(
                    db, feed.guild_id, feed.id, result.interval_seconds)) {
                bronx::logger::error("rss", "failed to save feed check interval");
            }
            std::vector<Item> new_items;
            if (post_latest) {
                new_items.push_back(result.items.front());
            } else {
                for (const auto& item : result.items) {
                    if (!feed.last_guid.empty() && item.guid == feed.last_guid) break;
                    new_items.push_back(item);
                    if (new_items.size() >= 10) break;
                }
                std::reverse(new_items.begin(), new_items.end());
            }
            for (const auto channel_id : target_channels(feed.channels)) {
                for (const auto& item : new_items) post_item(bot, channel_id, item);
            }
            if (!post_latest && !bronx::db::rss_operations::update_check(
                    db, feed.guild_id, feed.id, checked_at, result.items.front().guid)) {
                bronx::logger::error("rss", "failed to save feed check state");
            }
        } else if (!result.error.empty()) {
            bronx::logger::warn("rss", "feed check failed for feed " + std::to_string(feed.id) +
                                        ": " + result.error);
            if (!post_latest && !bronx::db::rss_operations::update_check(
                    db, feed.guild_id, feed.id, checked_at, feed.last_guid)) {
                bronx::logger::error("rss", "failed to save failed feed check state");
            }
        }
        end_check(feed.id);
    }).detach();
}

inline bool parse_snowflake(std::string value, uint64_t& id) {
    if (value.size() >= 3 && value.rfind("<#", 0) == 0 && value.back() == '>') {
        value = value.substr(2, value.size() - 3);
    }
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return false;
    try {
        size_t used = 0;
        id = std::stoull(value, &used);
        return used == value.size() && id != 0;
    } catch (...) {
        return false;
    }
}

inline bool can_manage_guild(const dpp::message_create_t& event) {
    auto* guild = dpp::find_guild(event.msg.guild_id);
    if (!guild) return false;
    const auto permissions = guild->base_permissions(event.msg.member);
    return permissions.can(dpp::p_manage_guild) || permissions.can(dpp::p_administrator);
}

inline void send_text(dpp::cluster& bot, uint64_t channel_id, const std::string& text) {
    bot.message_create(dpp::message(channel_id, text));
}

}

namespace commands::utility {

inline Command* get_rss_command(bronx::db::Database* db) {
    static Command command("rss", "manage rss feed watchers", "utility", {"feed"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            using namespace rss_detail;
            const uint64_t guild_id = event.msg.guild_id;
            const uint64_t channel_id = event.msg.channel_id;
            if (guild_id == 0) {
                send_text(bot, channel_id, "rss commands only work in a server.");
                return;
            }
            if (!db) {
                send_text(bot, channel_id, "the rss database is unavailable.");
                return;
            }
            if (args.empty()) {
                send_text(bot, channel_id,
                    "usage: `b.rss add <url>`, `list`, `remove <id>`, `target <id> <channel>`, "
                    "`check [id]`, `read <id|url>`, or `test <id|url>`");
                return;
            }
            std::string action = args[0];
            std::transform(action.begin(), action.end(), action.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            const bool admin_action = action == "add" || action == "remove" || action == "target" ||
                                      action == "check" || action == "test";
            if (admin_action && !can_manage_guild(event)) {
                send_text(bot, channel_id, "you need manage server permission to change rss feeds.");
                return;
            }

            if (action == "list") {
                const auto feeds = bronx::db::rss_operations::get_guild_feeds(db, guild_id);
                if (feeds.empty()) {
                    send_text(bot, channel_id, "no rss feeds are configured. add one with `b.rss add <url>`.");
                    return;
                }
                std::string output = "**rss feeds**\n";
                for (const auto& feed : feeds) {
                    output += "`" + std::to_string(feed.id) + "` " + feed.url + " → ";
                    const auto targets = target_channels(feed.channels);
                    for (size_t i = 0; i < targets.size(); ++i) {
                        if (i) output += ", ";
                        output += "<#" + std::to_string(targets[i]) + ">";
                    }
                    output += " (every " + std::to_string(feed.interval_seconds / 60) + " min)\n";
                }
                send_text(bot, channel_id, output);
                return;
            }

            if (action == "add") {
                if (args.size() < 2) {
                    send_text(bot, channel_id, "usage: `b.rss add <url>`");
                    return;
                }
                const std::string url = args[1];
                for (const auto& feed : bronx::db::rss_operations::get_guild_feeds(db, guild_id)) {
                    if (feed.url == url) {
                        send_text(bot, channel_id, "that feed is already configured as id `" +
                            std::to_string(feed.id) + "`.");
                        return;
                    }
                }
                bot.message_create(dpp::message(channel_id, "validating feed..."),
                    [&bot, db, guild_id, channel_id, url](const dpp::confirmation_callback_t& callback) {
                        if (callback.is_error()) return;
                        std::thread([&bot, db, guild_id, channel_id, url]() {
                            const auto result = fetch_feed(url);
                            if (!result.error.empty() || result.items.empty()) {
                                send_text(bot, channel_id, "could not add feed: " +
                                    (result.error.empty() ? "no feed items found" : result.error));
                                return;
                            }
                            bronx::db::rss_operations::Feed feed;
                            feed.guild_id = guild_id;
                            feed.channels = std::to_string(channel_id);
                            feed.url = url;
                            feed.interval_seconds = result.interval_seconds;
                            feed.last_checked = static_cast<uint64_t>(
                                std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count());
                            feed.last_guid = result.items.front().guid;
                            const auto id = bronx::db::rss_operations::add_feed(db, feed);
                            if (!id) {
                                send_text(bot, channel_id, "feed validation passed, but saving it failed.");
                                return;
                            }
                            send_text(bot, channel_id, "rss feed added as id `" + std::to_string(*id) +
                                "`. new items will post in <#" + std::to_string(channel_id) + ">.");
                            post_item(bot, channel_id, result.items.front());
                        }).detach();
                    });
                return;
            }

            if (action == "remove" || action == "target") {
                if ((action == "remove" && args.size() < 2) ||
                    (action == "target" && args.size() < 3)) {
                    send_text(bot, channel_id, action == "remove"
                        ? "usage: `b.rss remove <id>`"
                        : "usage: `b.rss target <id> <channel> [channels...]`");
                    return;
                }
                uint64_t feed_id = 0;
                if (!parse_snowflake(args[1], feed_id)) {
                    send_text(bot, channel_id, "feed id must be a number.");
                    return;
                }
                auto feed = bronx::db::rss_operations::get_feed(db, guild_id, feed_id);
                if (!feed) {
                    send_text(bot, channel_id, "no matching feed was found.");
                    return;
                }
                if (action == "remove") {
                    send_text(bot, channel_id,
                        bronx::db::rss_operations::remove_feed(db, guild_id, feed_id)
                            ? "rss feed removed."
                            : "failed to remove rss feed.");
                    return;
                }
                std::string targets;
                for (size_t i = 2; i < args.size(); ++i) {
                    uint64_t target_channel = 0;
                    if (!parse_snowflake(args[i], target_channel)) {
                        send_text(bot, channel_id, "each target must be a channel mention or id.");
                        return;
                    }
                    const auto* target = dpp::find_channel(target_channel);
                    if (!target || target->guild_id != guild_id) {
                        send_text(bot, channel_id, "target channels must be in this server and available in cache.");
                        return;
                    }
                    if (!targets.empty()) targets += ",";
                    targets += std::to_string(target_channel);
                }
                send_text(bot, channel_id,
                    bronx::db::rss_operations::set_target(db, guild_id, feed_id, targets)
                        ? "rss targets updated."
                        : "failed to update rss target.");
                return;
            }

            if (action == "check") {
                std::vector<bronx::db::rss_operations::Feed> feeds;
                if (args.size() >= 2) {
                    uint64_t feed_id = 0;
                    if (!parse_snowflake(args[1], feed_id)) {
                        send_text(bot, channel_id, "feed id must be a number.");
                        return;
                    }
                    auto feed = bronx::db::rss_operations::get_feed(db, guild_id, feed_id);
                    if (feed) feeds.push_back(*feed);
                } else {
                    feeds = bronx::db::rss_operations::get_guild_feeds(db, guild_id);
                }
                if (feeds.empty()) {
                    send_text(bot, channel_id, "no matching feeds were found.");
                    return;
                }
                for (const auto& feed : feeds) check_feed(bot, db, feed);
                send_text(bot, channel_id, "feed check started.");
                return;
            }

            if (action == "test" || action == "read") {
                if (args.size() < 2) {
                    send_text(bot, channel_id, "usage: `b.rss " + action + " <id|url>`");
                    return;
                }
                std::string url;
                uint64_t feed_id = 0;
                if (parse_snowflake(args[1], feed_id)) {
                    auto feed = bronx::db::rss_operations::get_feed(db, guild_id, feed_id);
                    if (!feed) {
                        send_text(bot, channel_id, "no matching feed was found.");
                        return;
                    }
                    url = feed->url;
                } else {
                    url = args[1];
                }
                std::thread([&bot, channel_id, url, action]() {
                    const auto result = fetch_feed(url);
                    if (!result.error.empty()) {
                        send_text(bot, channel_id, "feed request failed: " + result.error);
                        return;
                    }
                    const size_t count = action == "test" ? 1 : std::min<size_t>(5, result.items.size());
                    for (size_t i = 0; i < count; ++i) post_item(bot, channel_id, result.items[i]);
                }).detach();
                return;
            }

            send_text(bot, channel_id, "unknown rss action. use `add`, `remove`, `target`, `list`, `check`, `read`, or `test`.");
        });
    command.subcommands = {
        {"add <url>", "watch a feed in the current channel"},
        {"target <id> <channel> [channels...]", "change a feed's target channels"},
        {"remove <id>", "remove a feed"},
        {"list", "list this server's feeds"},
        {"check [id]", "check one or all feeds now"},
        {"read <id|url>", "show the five newest items"},
        {"test <id|url>", "post the newest item"}
    };
    return &command;
}

inline void register_rss_timer(dpp::cluster& bot, bronx::db::Database* db) {
    static std::atomic<bool> registered{false};
    if (!db || registered.exchange(true)) return;
    bot.start_timer([&bot, db](dpp::timer) {
        const auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        for (const auto& feed : bronx::db::rss_operations::get_all_feeds(db)) {
            if (feed.last_checked == 0 ||
                now >= feed.last_checked + static_cast<uint64_t>(feed.interval_seconds)) {
                rss_detail::check_feed(bot, db, feed);
            }
        }
    }, 60);
}

}
