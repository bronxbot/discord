#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "utility_helpers.h"
#include "../owner.h"
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <curl/curl.h>
#include <string>
#include <vector>
#include <future>
#include <thread>

namespace commands {
namespace utility {

// Helper to write curl response body
static size_t _ai_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

inline std::string call_ai_api(const std::string& prompt) {
    CURL* curl = curl_easy_init();
    if (!curl) return "error: failed to initialize curl.";

    std::string response_body;
    
    // Codex Proxy runs on the desktop host.
    // Since BPP uses host networking (network_mode: host), "localhost:8080" is perfect.
    std::string url = "http://localhost:8080/v1/chat/completions";
    
    // Build the request body using nlohmann::json
    nlohmann::json body;
    body["model"] = "gpt-5.4-mini"; // Use the working free model we verified!
    body["stream"] = false;
    
    nlohmann::json msg;
    msg["role"] = "user";
    msg["content"] = prompt;
    body["messages"] = nlohmann::json::array({msg});

    std::string post_data = body.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Authorization: Bearer pwd");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _ai_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); // Up to 30 second timeout

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        return "error: failed to connect to local ai proxy on port 8080.";
    }

    try {
        auto parsed = nlohmann::json::parse(response_body);
        if (parsed.contains("choices") && parsed["choices"].is_array() && !parsed["choices"].empty()) {
            auto choice = parsed["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                return choice["message"]["content"].get<std::string>();
            }
        }
        if (parsed.contains("error") && parsed["error"].contains("message")) {
            return "proxy error: " + parsed["error"]["message"].get<std::string>();
        }
    } catch (...) {
        return "error: failed to parse response from proxy.";
    }

    return "error: unknown proxy response format.";
}

inline Command* get_ai_command() {
    static Command ai("ai", "ask the ai assistant a question", "utility", {"ask", "gpt", "chat"}, true,
        // TEXT COMMAND HANDLER
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("please specify a prompt to ask the ai."));
                return;
            }

            std::string prompt;
            for (const auto& arg : args) prompt += arg + " ";
            if (!prompt.empty()) prompt.pop_back();

            // Send thinking state
            auto loading = bronx::info("thinking... 🧠");
            bot.message_create(dpp::message(event.msg.channel_id, loading), [&bot, event, prompt](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                auto sent_msg = std::get<dpp::message>(cb.value);

                // Run curl in a background thread to avoid blocking DPP event loop
                std::thread([&bot, event, sent_msg, prompt]() {
                    std::string answer = call_ai_api(prompt);
                    
                    // Discord Embed description limit check
                    if (answer.size() > 2048) {
                        answer = answer.substr(0, 2045) + "...";
                    }

                    auto embed = bronx::create_embed("");
                    embed.set_title("ai assistant");
                    embed.set_description(answer);
                    bronx::add_invoker_footer(embed, event.msg.author);

                    dpp::message edit_msg = sent_msg;
                    edit_msg.embeds.clear();
                    edit_msg.add_embed(embed);
                    bronx::safe_message_edit(bot, edit_msg);
                }).detach();
            });
        },
        // SLASH COMMAND HANDLER
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            std::string prompt = std::get<std::string>(event.get_parameter("prompt"));

            event.thinking(false);
            std::thread([&bot, event, prompt]() {
                std::string answer = call_ai_api(prompt);

                if (answer.size() > 2048) {
                    answer = answer.substr(0, 2045) + "...";
                }

                auto embed = bronx::create_embed("");
                embed.set_title("ai assistant");
                embed.set_description(answer);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());

                event.edit_original_response(dpp::message().add_embed(embed));
            }).detach();
        },
        {
            dpp::command_option(dpp::co_string, "prompt", "what to ask the ai", true)
        }
    );
    return &ai;
}

} // namespace utility
} // namespace commands
