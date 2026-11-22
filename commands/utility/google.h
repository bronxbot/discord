#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "media.h"
#include "../owner.h"
#include <dpp/dpp.h>
#include <curl/curl.h>
#include <string>
#include <vector>
#include <thread>
#include <regex>
#include <sstream>
#include <iomanip>

namespace commands {
namespace utility {

inline std::string google_url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped << std::hex;
    for (char c : value) {
        if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::setfill('0') << (int)(unsigned char)c;
        }
    }
    return escaped.str();
}

inline std::string perform_google_search(const std::string& query) {
    CURL* curl = curl_easy_init();
    if (!curl) return "error: curl init failed.";

    std::string url = "https://www.google.com/search?q=" + google_url_encode(query);
    std::string body;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Opera/9.80 (J2ME/MIDP; Opera Mini/9.80/28.2721; U; ru) Presto/2.8.119 Version/11.10");

    CURLcode code = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) return "error: failed to connect to Google.";

    std::string out = "";
    size_t pos = 0;
    std::string block_delim = "<div class=\"Gx5Zad xpd EtOod pkphOe\">";
    
    int count = 0;
    while ((pos = body.find(block_delim, pos)) != std::string::npos && count < 5) {
        size_t next_pos = body.find(block_delim, pos + block_delim.size());
        std::string block = (next_pos == std::string::npos) ? body.substr(pos) : body.substr(pos, next_pos - pos);
        pos = (next_pos == std::string::npos) ? body.size() : next_pos;

        std::smatch m_link, m_title, m_snip;
        std::regex r_link("href=\"/url\\?q=([^&\"]+)");
        std::regex r_title("<div class=\"ilUpNd UFvD1 aSRlid\">([^<]+)</div>");
        std::regex r_snip("<div class=\"ilUpNd H66NU aSRlid\">(.*?)</div>");

        if (std::regex_search(block, m_link, r_link)) {
            std::string link = m_link[1].str();
            if (link.find("google.com") != std::string::npos || link.find("maps.google") != std::string::npos) continue;

            std::string title = std::regex_search(block, m_title, r_title) ? m_title[1].str() : "Result";
            std::string snip = "";
            if (std::regex_search(block, m_snip, r_snip)) {
                snip = std::regex_replace(m_snip[1].str(), std::regex("<[^>]+>"), "");
            }

            out += "**[" + title + "](" + link + ")**\n" + snip + "\n\n";
            count++;
        }
    }

    if (out.empty()) return "no search results found for `" + query + "`.";
    return out;
}

inline Command* get_google_command() {
    static Command google("google", "search google for web results", "utility", {"search", "g"}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("please specify a query to search."));
                return;
            }
            std::string query;
            for (const auto& arg : args) query += arg + " ";
            if (!query.empty()) query.pop_back();

            auto loading = bronx::info("searching google... 🔎");
            bot.message_create(dpp::message(event.msg.channel_id, loading), [&bot, event, query](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                auto sent_msg = std::get<dpp::message>(cb.value);
                std::thread([&bot, event, sent_msg, query]() {
                    std::string res = perform_google_search(query);
                    if (res.size() > 2048) res = res.substr(0, 2045) + "...";
                    auto embed = bronx::create_embed("").set_title("google: " + query).set_description(res);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    dpp::message edit_msg = sent_msg;
                    edit_msg.embeds.clear(); edit_msg.add_embed(embed);
                    bronx::safe_message_edit(bot, edit_msg);
                }).detach();
            });
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            std::string query = std::get<std::string>(event.get_parameter("query"));
            event.thinking(false);
            std::thread([&bot, event, query]() {
                std::string res = perform_google_search(query);
                if (res.size() > 2048) res = res.substr(0, 2045) + "...";
                auto embed = bronx::create_embed("").set_title("google: " + query).set_description(res);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.edit_original_response(dpp::message().add_embed(embed));
            }).detach();
        },
        { dpp::command_option(dpp::co_string, "query", "what to search for", true) }
    );
    return &google;
}

} // namespace utility
} // namespace commands
