#pragma once
#include <dpp/dpp.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include "../database/core/database.h"

namespace bronx::bridge {

class BridgeClient {
public:
    static BridgeClient& get() {
        static BridgeClient instance;
        return instance;
    }

    void init(dpp::cluster* bot, bronx::db::Database* db) {
        bot_ = bot;
        db_ = db;
        running_ = true;
        loop_thread_ = std::thread([this]() { run_loop(); });
    }

    ~BridgeClient() {
        running_ = false;
        close_socket();
        if (loop_thread_.joinable()) {
            loop_thread_.join();
        }
    }

    bool is_bridged(uint64_t channel_id) {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        bool res = bridged_channels_.count(channel_id) > 0;
        std::cout << "[debug-bridge] is_bridged check channel=" << channel_id << " result=" << (res ? "true" : "false") << std::endl;
        return res;
    }

    void send_json(const nlohmann::json& j) {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (socket_fd_ != -1) {
            std::string data = j.dump() + "\n";
            std::cout << "[debug-bridge] sending json: " << j.dump() << std::endl;
            ssize_t n = send(socket_fd_, data.c_str(), data.size(), 0);
            if (n < 0) {
                std::cout << "[debug-bridge] send error: " << strerror(errno) << std::endl;
            } else {
                std::cout << "[debug-bridge] sent " << n << " bytes" << std::endl;
            }
        } else {
            std::cout << "[debug-bridge] cannot send json, socket_fd_ is -1" << std::endl;
        }
    }

    void register_bridge_add_gen(uint64_t user_id, std::function<void(const std::string&, uint64_t)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_add_gens_[user_id] = cb;
    }

    void register_bridge_add_redeem(uint64_t user_id, std::function<void(const std::string&, uint64_t, const std::string&)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_add_redeems_[user_id] = cb;
    }

    void register_bridge_remove(uint64_t user_id, std::function<void(const std::string&)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_removes_[user_id] = cb;
    }

    void register_bridge_check(uint64_t user_id, std::function<void(bool, uint64_t, uint64_t, uint64_t)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_checks_[user_id] = cb;
    }

    void register_bridge_info(uint64_t user_id, std::function<void(bool, const std::string&, uint64_t, const std::string&, uint64_t, const std::string&, uint64_t, const std::string&)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_infos_[user_id] = cb;
    }

    void register_bridge_emojis(uint64_t user_id, std::function<void(bool, const nlohmann::json&)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_emojis_[user_id] = cb;
    }

    void register_link_generate(uint64_t user_id, std::function<void(const std::string&)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_link_gens_[user_id] = cb;
    }

    void register_link_redeem(uint64_t user_id, std::function<void(const std::string&)> cb) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        pending_link_redeems_[user_id] = cb;
    }

private:
    BridgeClient() : bot_(nullptr), db_(nullptr), running_(false), socket_fd_(-1) {}  // NOLINT

    void close_socket() {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (socket_fd_ != -1) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }

    void run_loop() {
        while (running_) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(socket_mutex_);
                socket_fd_ = sock;
            }

            struct sockaddr_in serv_addr;
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(9000);
            inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

            if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                close_socket();
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            nlohmann::json reg = {{"type", "register"}, {"platform", "discord"}};
            send_json(reg);

            std::string buffer;
            char temp[1024];
            while (running_) {
                ssize_t n = recv(sock, temp, sizeof(temp) - 1, 0);
                if (n <= 0) {
                    break;
                }
                temp[n] = '\0';
                buffer += temp;

                size_t pos;
                while ((pos = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, pos);
                    buffer.erase(0, pos + 1);
                    if (!line.empty()) {
                        try {
                            auto j = nlohmann::json::parse(line);
                            handle_event(j);
                        } catch (...) {}
                    }
                }
            }

            close_socket();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    void handle_event(const nlohmann::json& j) {
        if (!bot_) return;
        std::cout << "[debug-bridge] received event from daemon: " << j.dump() << std::endl;
        std::string type = j.contains("type") ? j["type"].get<std::string>() : "";

        if (type == "init") {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            bridged_channels_.clear();
            std::cout << "[debug-bridge] init event received: " << j.dump() << std::endl;
            if (j.contains("bridged_channels") && j["bridged_channels"].is_array()) {
                for (const auto& c : j["bridged_channels"]) {
                    try {
                        uint64_t cid = 0;
                        if (c.is_string()) {
                            cid = std::stoull(c.get<std::string>());
                        } else if (c.is_number()) {
                            cid = c.get<uint64_t>();
                        }
                        bridged_channels_.insert(cid);
                        std::cout << "[debug-bridge] parsed and loaded bridged channel: " << cid << std::endl;
                    } catch (const std::exception& e) {
                        std::cout << "[debug-bridge] failed to parse channel entry: " << e.what() << std::endl;
                    } catch (...) {
                        std::cout << "[debug-bridge] unknown exception parsing channel entry" << std::endl;
                    }
                }
            }
            send_emoji_registry();
        }
        else if (type == "relay") {
            uint64_t channel_id = 0;
            try { channel_id = std::stoull(j["channel_id"].get<std::string>()); } catch (...) { return; }
            std::string author_name = j["author_name"].get<std::string>();
            std::string author_avatar = j["author_avatar"].get<std::string>();
            std::string content = j["content"].get<std::string>();

            std::string src_platform = j.contains("src_platform") ? j["src_platform"].get<std::string>() : "";
            std::string src_channel_id = j.contains("src_channel_id") ? j["src_channel_id"].get<std::string>() : "";
            std::string src_channel_name = j.contains("src_channel_name") ? j["src_channel_name"].get<std::string>() : "";
            std::string src_server_id = j.contains("src_server_id") ? j["src_server_id"].get<std::string>() : "";
            std::string src_server_name = j.contains("src_server_name") ? j["src_server_name"].get<std::string>() : "";
            std::string src_user_id = j.contains("src_user_id") ? j["src_user_id"].get<std::string>() : "";
            std::string src_username = j.contains("src_username") ? j["src_username"].get<std::string>() : "";
            std::string src_message_id = j.contains("src_message_id") ? j["src_message_id"].get<std::string>() : "";

            relay_message(channel_id, author_name, author_avatar, content, [this, src_platform, src_channel_id, src_channel_name, src_server_id, src_server_name, src_user_id, src_username, src_message_id](uint64_t dest_message_id, bool success) {
                if (success) {
                    nlohmann::json track = {
                        {"type", "track_relayed_message"},
                        {"dest_message_id", std::to_string(dest_message_id)},
                        {"src_message_id", src_message_id},
                        {"src_platform", src_platform},
                        {"src_channel_id", src_channel_id},
                        {"src_channel_name", src_channel_name},
                        {"src_server_id", src_server_id},
                        {"src_server_name", src_server_name},
                        {"src_user_id", src_user_id},
                        {"src_username", src_username}
                    };
                    send_json(track);
                }
            });
        }
        else if (type == "reaction_add") {
            try {
                uint64_t channel_id = std::stoull(j["channel_id"].get<std::string>());
                uint64_t message_id = std::stoull(j["message_id"].get<std::string>());
                std::string emoji = j["emoji"].get<std::string>();
                bot_->message_add_reaction(message_id, channel_id, emoji);
            } catch (...) {}
        }
        else if (type == "reaction_remove") {
            try {
                uint64_t channel_id = std::stoull(j["channel_id"].get<std::string>());
                uint64_t message_id = std::stoull(j["message_id"].get<std::string>());
                std::string emoji = j["emoji"].get<std::string>();
                bot_->message_delete_reaction(message_id, channel_id, bot_->me.id, emoji);
            } catch (...) {}
        }
        else if (type == "bridge_add_generated") {
            uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
            std::string code = j["code"].get<std::string>();
            uint64_t channel_id = std::stoull(j["channel_id"].get<std::string>());
            
            std::function<void(const std::string&, uint64_t)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = pending_add_gens_.find(user_id);
                if (it != pending_add_gens_.end()) {
                    cb = it->second;
                    pending_add_gens_.erase(it);
                }
            }
            if (cb) cb(code, channel_id);
        }
        else if (type == "bridge_add_success") {
            uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
            uint64_t other_channel_id = 0;
            try { other_channel_id = std::stoull(j["other_channel_id"].get<std::string>()); } catch (...) {}
            std::string other_platform = j["other_platform"].get<std::string>();
            
            std::function<void(const std::string&, uint64_t, const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = pending_add_redeems_.find(user_id);
                if (it != pending_add_redeems_.end()) {
                    cb = it->second;
                    pending_add_redeems_.erase(it);
                }
            }
            if (cb) cb("", other_channel_id, other_platform);
        }
        else if (type == "bridge_add_error") {
            uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
            std::string message = j["message"].get<std::string>();
            
            std::function<void(const std::string&, uint64_t, const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = pending_add_redeems_.find(user_id);
                if (it != pending_add_redeems_.end()) {
                    cb = it->second;
                    pending_add_redeems_.erase(it);
                }
            }
            if (cb) cb(message, 0, "");
        }
        else if (type == "bridge_remove_success") {
            uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = pending_removes_.find(user_id);
                if (it != pending_removes_.end()) {
                    cb = it->second;
                    pending_removes_.erase(it);
                }
            }
            if (cb) cb("");
        }
        else if (type == "bridge_remove_error") {
            uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
            std::string message = j["message"].get<std::string>();
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = pending_removes_.find(user_id);
                if (it != pending_removes_.end()) {
                    cb = it->second;
                    pending_removes_.erase(it);
                }
            }
            if (cb) cb(message);
        }
        else if (type == "bridge_check_response") {
            uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
            bool found = j["found"].get<bool>();
            uint64_t stoat_ch = 0, fluxer_ch = 0, discord_ch = 0;
            if (found) {
                try {
                    if (j.contains("stoat_channel") && !j["stoat_channel"].is_null()) stoat_ch = std::stoull(j["stoat_channel"].get<std::string>());
                    if (j.contains("fluxer_channel") && !j["fluxer_channel"].is_null()) fluxer_ch = std::stoull(j["fluxer_channel"].get<std::string>());
                    if (j.contains("discord_channel") && !j["discord_channel"].is_null()) discord_ch = std::stoull(j["discord_channel"].get<std::string>());
                } catch (...) {}
            }
            std::function<void(bool, uint64_t, uint64_t, uint64_t)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = pending_checks_.find(user_id);
                if (it != pending_checks_.end()) {
                    cb = it->second;
                    pending_checks_.erase(it);
                }
            }
            if (cb) cb(found, stoat_ch, fluxer_ch, discord_ch);
        }
        else if (type == "bridge_info_response") {
            uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
            bool found = j["found"].get<bool>();
            std::string src_platform = found ? j["src_platform"].get<std::string>() : "";
            uint64_t src_channel_id = 0;
            std::string src_channel_name = found ? j["src_channel_name"].get<std::string>() : "";
            uint64_t src_server_id = 0;
            std::string src_server_name = found ? j["src_server_name"].get<std::string>() : "";
            uint64_t src_user_id = 0;
            std::string src_username = found ? j["src_username"].get<std::string>() : "";
            try {
                if (found) {
                    src_channel_id = std::stoull(j["src_channel_id"].get<std::string>());
                    if (j.contains("src_server_id") && !j["src_server_id"].is_null() && !j["src_server_id"].get<std::string>().empty()) {
                        src_server_id = std::stoull(j["src_server_id"].get<std::string>());
                    }
                    src_user_id = std::stoull(j["src_user_id"].get<std::string>());
                }
            } catch (...) {}

            std::function<void(bool, const std::string&, uint64_t, const std::string&, uint64_t, const std::string&, uint64_t, const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = pending_infos_.find(user_id);
                if (it != pending_infos_.end()) {
                    cb = it->second;
                    pending_infos_.erase(it);
                }
            }
            if (cb) cb(found, src_platform, src_channel_id, src_channel_name, src_server_id, src_server_name, src_user_id, src_username);
        }
        else if (type == "bridge_emojis_response") {
            uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
            bool found = j["found"].get<bool>();
            nlohmann::json emojis = (found && j.contains("emojis")) ? j["emojis"] : nlohmann::json::array();
            
            std::function<void(bool, const nlohmann::json&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                auto it = pending_emojis_.find(user_id);
                if (it != pending_emojis_.end()) {
                    cb = it->second;
                    pending_emojis_.erase(it);
                }
            }
            if (cb) cb(found, emojis);
        }
        else if (type == "bridge_established_notification") {
            uint64_t channel_id = std::stoull(j["channel_id"].get<std::string>());
            std::string other_channel_id = j["other_channel_id"].get<std::string>();
            std::string other_platform = j["other_platform"].get<std::string>();
            dpp::message m(channel_id, "🔗 bridge successfully established with channel `" + other_channel_id + "` on **" + other_platform + "**!");
            bot_->message_create(m);
        }
        else if (type == "bridge_removed_notification") {
            uint64_t channel_id = std::stoull(j["channel_id"].get<std::string>());
            dpp::message m(channel_id, "⚠️ this channel has been unbridged.");
            bot_->message_create(m);
        }
        else if (type == "link_generated") {
            try {
                uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
                std::string code = j["code"].get<std::string>();
                std::function<void(const std::string&)> cb = nullptr;
                {
                    std::lock_guard<std::mutex> lock(callbacks_mutex_);
                    auto it = pending_link_gens_.find(user_id);
                    if (it != pending_link_gens_.end()) {
                        cb = it->second;
                        pending_link_gens_.erase(it);
                    }
                }
                if (cb) cb(code);
            } catch (...) {}
        }
        else if (type == "link_created") {
            try {
                std::string stoat_id = j.contains("stoat_id") && !j["stoat_id"].is_null() ? j["stoat_id"].get<std::string>() : "";
                std::string fluxer_id = j.contains("fluxer_id") && !j["fluxer_id"].is_null() ? j["fluxer_id"].get<std::string>() : "";
                std::string discord_id = j.contains("discord_id") && !j["discord_id"].is_null() ? j["discord_id"].get<std::string>() : "";

                if (db_) {
                    db_->save_account_link(stoat_id, fluxer_id, discord_id);
                }

                if (!discord_id.empty()) {
                    uint64_t disc_id = std::stoull(discord_id);
                    std::function<void(const std::string&)> cb = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(callbacks_mutex_);
                        auto it = pending_link_redeems_.find(disc_id);
                        if (it != pending_link_redeems_.end()) {
                            cb = it->second;
                            pending_link_redeems_.erase(it);
                        }
                    }
                    if (cb) cb("");
                }
            } catch (...) {}
        }
        else if (type == "link_error") {
            try {
                uint64_t user_id = std::stoull(j["user_id"].get<std::string>());
                std::string message = j["message"].get<std::string>();
                std::function<void(const std::string&)> cb = nullptr;
                {
                    std::lock_guard<std::mutex> lock(callbacks_mutex_);
                    auto it = pending_link_redeems_.find(user_id);
                    if (it != pending_link_redeems_.end()) {
                        cb = it->second;
                        pending_link_redeems_.erase(it);
                    }
                }
                if (cb) cb(message);
            } catch (...) {}
        }
        else if (type == "merge_balances_request") {
            try {
                std::string stoat_id = j.contains("stoat_id") && !j["stoat_id"].is_null() ? j["stoat_id"].get<std::string>() : "";
                std::string fluxer_id = j.contains("fluxer_id") && !j["fluxer_id"].is_null() ? j["fluxer_id"].get<std::string>() : "";
                std::string discord_id = j.contains("discord_id") && !j["discord_id"].is_null() ? j["discord_id"].get<std::string>() : "";

                if (!discord_id.empty() && db_) {
                    uint64_t disc_id = std::stoull(discord_id);
                    db_->ensure_user_exists(disc_id);
                    int64_t wallet = db_->get_wallet(disc_id);
                    int64_t bank = db_->get_bank(disc_id);
                    nlohmann::json rep = {
                        {"type", "report_balance"},
                        {"stoat_id", stoat_id},
                        {"fluxer_id", fluxer_id},
                        {"discord_id", discord_id},
                        {"wallet", wallet},
                        {"bank", bank}
                    };
                    send_json(rep);
                }
            } catch (...) {}
        }
        else if (type == "balance_sync") {
            try {
                std::string discord_id = j.contains("discord_id") && !j["discord_id"].is_null() ? j["discord_id"].get<std::string>() : "";
                int64_t target_wallet = j["wallet"].get<int64_t>();
                int64_t target_bank = j["bank"].get<int64_t>();

                if (!discord_id.empty() && db_) {
                    uint64_t disc_id = std::stoull(discord_id);
                    db_->ensure_user_exists(disc_id);
                    int64_t current_wallet = db_->get_wallet(disc_id);
                    int64_t current_bank = db_->get_bank(disc_id);
                    int64_t w_delta = target_wallet - current_wallet;
                    int64_t b_delta = target_bank - current_bank;
                    if (w_delta != 0) db_->update_wallet(disc_id, w_delta);
                    if (b_delta != 0) db_->update_bank(disc_id, b_delta);
                }
            } catch (...) {}
        }
        else if (type == "balance_sync_delta") {
            try {
                std::string discord_id = j.contains("discord_id") && !j["discord_id"].is_null() ? j["discord_id"].get<std::string>() : "";
                int64_t w_delta = j["wallet_delta"].get<int64_t>();
                int64_t b_delta = j["bank_delta"].get<int64_t>();

                if (!discord_id.empty() && db_ && (w_delta != 0 || b_delta != 0)) {
                    uint64_t disc_id = std::stoull(discord_id);
                    db_->ensure_user_exists(disc_id);
                    if (w_delta != 0) db_->update_wallet(disc_id, w_delta);
                    if (b_delta != 0) db_->update_bank(disc_id, b_delta);
                }
            } catch (...) {}
        }
    }

    void relay_message(uint64_t channel_id, const std::string& author_name, const std::string& author_avatar, const std::string& content, std::function<void(uint64_t msg_id, bool success)> callback) {
        if (!bot_) return;
        bot_->get_channel_webhooks(channel_id, [this, channel_id, author_name, author_avatar, content, callback](const dpp::confirmation_callback_t& cb) {
            auto do_fallback = [this, channel_id, author_name, content, callback]() {
                dpp::message m(channel_id, "**" + author_name + "**: " + content);
                bot_->message_create(m, [callback](const dpp::confirmation_callback_t& mc_cb) {
                    if (mc_cb.is_error()) {
                        if (callback) callback(0, false);
                    } else {
                        auto sent = std::get<dpp::message>(mc_cb.value);
                        if (callback) callback(sent.id, true);
                    }
                });
            };

            if (cb.is_error()) {
                do_fallback();
                return;
            }

            auto whmap = cb.get<dpp::webhook_map>();
            dpp::webhook chosen_wh;
            bool found = false;
            for (auto &p : whmap) {
                const dpp::webhook &w = p.second;
                if (w.name.rfind("bpp-bridge-", 0) == 0) {
                    chosen_wh = w;
                    found = true;
                    break;
                }
            }

            dpp::message m;
            m.content = content;

            if (found) {
                chosen_wh.name = author_name;
                chosen_wh.avatar_url = author_avatar;
                bot_->execute_webhook(chosen_wh, m, true, 0, "", [do_fallback, callback](const dpp::confirmation_callback_t& exec_cb) {
                    if (exec_cb.is_error()) {
                        do_fallback();
                    } else {
                        auto sent = std::get<dpp::message>(exec_cb.value);
                        if (callback) callback(sent.id, true);
                    }
                });
                return;
            }

            dpp::webhook newwh;
            newwh.channel_id = channel_id;
            newwh.name = "bpp-bridge-" + std::to_string(bot_->me.id);

            bot_->create_webhook(newwh, [this, m, author_name, author_avatar, do_fallback, callback](const dpp::confirmation_callback_t& create_cb) {
                if (create_cb.is_error()) {
                    do_fallback();
                    return;
                }
                dpp::webhook created = create_cb.get<dpp::webhook>();
                created.name = author_name;
                created.avatar_url = author_avatar;
                bot_->execute_webhook(created, m, true, 0, "", [do_fallback, callback](const dpp::confirmation_callback_t& exec_cb) {
                    if (exec_cb.is_error()) {
                        do_fallback();
                    } else {
                        auto sent = std::get<dpp::message>(exec_cb.value);
                        if (callback) callback(sent.id, true);
                    }
                });
            });
        });
     }

     void send_emoji_registry() {
         if (!bot_) return;
         nlohmann::json reg = {{"type", "emoji_registry"}};
         nlohmann::json emojis_array = nlohmann::json::array();

         std::unordered_set<uint64_t> processed_guilds;
         std::unordered_set<uint64_t> channels_copy;
         {
             std::lock_guard<std::mutex> lock(channels_mutex_);
             channels_copy = bridged_channels_;
         }

         for (uint64_t channel_id : channels_copy) {
             auto* ch = dpp::find_channel(channel_id);
             if (ch && ch->guild_id != 0) {
                 if (processed_guilds.count(ch->guild_id) == 0) {
                     processed_guilds.insert(ch->guild_id);
                     auto* guild = dpp::find_guild(ch->guild_id);
                     if (guild) {
                         for (const auto& emoji_id : guild->emojis) {
                             auto* emoji = dpp::find_emoji(emoji_id);
                             if (emoji) {
                                 emojis_array.push_back(nlohmann::json{
                                     {"name", emoji->name},
                                     {"id", std::to_string(emoji->id)},
                                     {"channel_id", std::to_string(channel_id)}
                                 });
                             }
                         }
                     }
                 }
             }
         }
         reg["emojis"] = emojis_array;
         send_json(reg);
     }

    dpp::cluster* bot_;
    bronx::db::Database* db_;
    std::atomic<bool> running_;
    int socket_fd_;
    std::mutex socket_mutex_;
    std::thread loop_thread_;

    std::mutex channels_mutex_;
    std::unordered_set<uint64_t> bridged_channels_;

    std::mutex callbacks_mutex_;
    std::unordered_map<uint64_t, std::function<void(const std::string&, uint64_t)>> pending_add_gens_;
    std::unordered_map<uint64_t, std::function<void(const std::string&, uint64_t, const std::string&)>> pending_add_redeems_;
    std::unordered_map<uint64_t, std::function<void(const std::string&)>> pending_removes_;
    std::unordered_map<uint64_t, std::function<void(bool, uint64_t, uint64_t, uint64_t)>> pending_checks_;
    std::unordered_map<uint64_t, std::function<void(bool, const std::string&, uint64_t, const std::string&, uint64_t, const std::string&, uint64_t, const std::string&)>> pending_infos_;
    std::unordered_map<uint64_t, std::function<void(const std::string&)>> pending_link_gens_;
    std::unordered_map<uint64_t, std::function<void(const std::string&)>> pending_link_redeems_;
    std::unordered_map<uint64_t, std::function<void(bool, const nlohmann::json&)>> pending_emojis_;
};

}
