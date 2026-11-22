#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "media.h"
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <map>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <thread>

namespace commands {
namespace utility {

// Retrieve the Last.fm API key from the env variable or .env file.
inline std::string get_lastfm_api_key() {
    const char* env_key = std::getenv("LASTFM_API_KEY");
    if (env_key && std::strlen(env_key) > 0) {
        return std::string(env_key);
    }
    
    std::vector<std::string> paths = {".env", "/home/siqnole/Documents/code/bpp/.env"};
    for (const auto& path : paths) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("LASTFM_API_KEY=", 0) == 0) {
                    std::string val = line.substr(15);
                    if (!val.empty() && (val.front() == '"' || val.front() == '\'')) val.erase(0, 1);
                    if (!val.empty() && (val.back() == '"' || val.back() == '\'')) val.pop_back();
                    return val;
                }
            }
        }
    }
    return "";
}

// URL encodes a string.
inline std::string url_encode(const std::string& value) {
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

// Fetch Last.fm API endpoint.
inline nlohmann::json fetch_lastfm_api(const std::string& method, const std::map<std::string, std::string>& params) {
    std::string api_key = get_lastfm_api_key();
    if (api_key.empty()) {
        return nlohmann::json();
    }
    
    std::string url = "http://ws.audioscrobbler.com/2.0/?format=json&api_key=" + api_key + "&method=" + method;
    for (const auto& [k, v] : params) {
        url += "&" + k + "=" + url_encode(v);
    }
    
    HttpResponse res = http_get_sync(url);
    if (res.status != 200) {
        // Even if status is not 200, Last.fm might return error in JSON format
        try {
            return nlohmann::json::parse(res.body);
        } catch (...) {
            return nlohmann::json();
        }
    }
    try {
        return nlohmann::json::parse(res.body);
    } catch (...) {
        return nlohmann::json();
    }
}

// Map user input timeframe to Last.fm periods.
inline std::string map_period(const std::string& input, std::string& display_name) {
    std::string p = input;
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c){ return std::tolower(c); });
    
    if (p == "weekly" || p == "7day" || p == "7days" || p == "w") {
        display_name = "weekly";
        return "7day";
    }
    if (p == "monthly" || p == "1month" || p == "30day" || p == "30days" || p == "m") {
        display_name = "monthly";
        return "1month";
    }
    if (p == "3month" || p == "3months" || p == "quarterly" || p == "3m" || p == "q") {
        display_name = "3 months";
        return "3month";
    }
    if (p == "6month" || p == "6months" || p == "halfyear" || p == "6m") {
        display_name = "6 months";
        return "6month";
    }
    if (p == "yearly" || p == "12month" || p == "12months" || p == "1year" || p == "y") {
        display_name = "yearly";
        return "12month";
    }
    if (p == "alltime" || p == "overall" || p == "all" || p == "a") {
        display_name = "all time";
        return "overall";
    }
    if (p == "24h" || p == "1day" || p == "24hours" || p == "daily" || p == "d") {
        display_name = "24h";
        return "24h";
    }
    
    display_name = "weekly";
    return "7day";
}

// Format relative time or playing status in lowercase.
inline std::string format_play_status(const nlohmann::json& track) {
    if (track.contains("@attr")) {
        auto attr = track["@attr"];
        if (attr.contains("nowplaying") && attr["nowplaying"] == "true") {
            return "listening now";
        }
    }
    if (track.contains("date")) {
        auto date = track["date"];
        if (date.contains("uts")) {
            try {
                std::string uts_str = date["uts"].get<std::string>();
                long long epoch = std::stoll(uts_str);
                return "last played <t:" + std::to_string(epoch) + ":R>";
            } catch (...) {}
        }
    }
    return "last played sometime";
}

// Assemble the Last.fm command suite.
inline std::vector<Command*> get_lastfm_commands(bronx::db::Database* db) {
    static std::vector<Command*> cmds;
    if (!cmds.empty()) return cmds;

    // --- Helper to verify key presence ---
    auto check_api_key = [](dpp::cluster& bot, const dpp::message_create_t& event) -> bool {
        if (get_lastfm_api_key().empty()) {
            bot.message_create(dpp::message(event.msg.channel_id, 
                bronx::error("last.fm api key not configured. please set `LASTFM_API_KEY` in your `.env` file.")));
            return false;
        }
        return true;
    };

    static Command fm_cmd("fm", "shows what you are currently playing on last.fm", "utility", {"np", "lastfm"}, false,
        [db, check_api_key](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            std::thread([&bot, event, db, args, check_api_key]() {
                if (!check_api_key(bot, event)) return;
                uint64_t target_user_id = event.msg.author.id;
                
                // Check subcommands: link, unlink, verify
                if (!args.empty()) {
                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), [](unsigned char c){ return std::tolower(c); });
                    
                    if (sub == "link") {
                        if (args.size() < 2) {
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::error("specify a last.fm username to link")));
                            return;
                        }
                        std::string username = args[1];
                        // Fetch profile to verify it exists
                        auto profile_res = fetch_lastfm_api("user.getinfo", {{"user", username}});
                        if (profile_res.empty() || profile_res.contains("error")) {
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::error("last.fm user **" + username + "** not found")));
                            return;
                        }
                        
                        std::string real_username = profile_res["user"]["name"].get<std::string>();
                        bool ok = db->set_lastfm_user(target_user_id, real_username, false, "");
                        if (ok) {
                            bot.message_create(dpp::message(event.msg.channel_id, 
                                bronx::success("linked to last.fm username **" + real_username + "**!\n"
                                               "to verify ownership, set your last.fm real name to **bronx** (or `" + std::to_string(target_user_id) + "`) and run `.fm verify`.")));
                        } else {
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to link last.fm user")));
                        }
                        return;
                    }
                    
                    if (sub == "unlink") {
                        auto user_opt = db->get_lastfm_user(target_user_id);
                        if (!user_opt.has_value()) {
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::error("you do not have a last.fm account linked")));
                            return;
                        }
                        bool ok = db->unlink_lastfm_user(target_user_id);
                        if (ok) {
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::success("unlinked your last.fm account")));
                        } else {
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to unlink account")));
                        }
                        return;
                    }
                    
                    if (sub == "verify") {
                        auto user_opt = db->get_lastfm_user(target_user_id);
                        if (!user_opt.has_value()) {
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::error("link your last.fm account first using `.fm link <username>`")));
                            return;
                        }
                        auto user = user_opt.value();
                        auto profile_res = fetch_lastfm_api("user.getinfo", {{"user", user.lastfm_username}});
                        if (profile_res.empty() || profile_res.contains("error")) {
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to fetch last.fm profile details")));
                            return;
                        }
                        
                        std::string realname = "";
                        if (profile_res["user"].contains("realname")) {
                            realname = profile_res["user"]["realname"].get<std::string>();
                            std::transform(realname.begin(), realname.end(), realname.begin(), [](unsigned char c){ return std::tolower(c); });
                        }
                        
                        std::string discord_id_str = std::to_string(target_user_id);
                        if (realname == "bronx" || realname == discord_id_str) {
                            db->set_lastfm_user(target_user_id, user.lastfm_username, true, "");
                            bot.message_create(dpp::message(event.msg.channel_id, bronx::success("successfully verified your last.fm account!")));
                        } else {
                            bot.message_create(dpp::message(event.msg.channel_id, 
                                bronx::error("verification failed. please set your last.fm profile's **real name** to **bronx** or `" + discord_id_str + "` and try again.")));
                        }
                        return;
                    }
                }

                // Normal Now Playing behavior
                auto user_opt = db->get_lastfm_user(target_user_id);
                if (!user_opt.has_value()) {
                    bot.message_create(dpp::message(event.msg.channel_id, 
                        bronx::info("you don't have a last.fm account linked. use `.fm link <username>` to connect.")));
                    return;
                }
                
                auto user = user_opt.value();
                auto res = fetch_lastfm_api("user.getrecenttracks", {{"user", user.lastfm_username}, {"limit", "1"}});
                if (res.empty() || res.contains("error")) {
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to fetch now playing data")));
                    return;
                }
                
                if (!res.contains("recenttracks") || !res["recenttracks"].contains("track")) {
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::error("no tracks found for last.fm user **" + user.lastfm_username + "**")));
                    return;
                }
                
                auto tracks = res["recenttracks"]["track"];
                if (tracks.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::error("no tracks found for last.fm user **" + user.lastfm_username + "**")));
                    return;
                }
                
                auto track = tracks[0];
                std::string artist = track["artist"]["#text"].get<std::string>();
                std::string name = track["name"].get<std::string>();
                std::string album = "";
                if (track.contains("album") && track["album"].contains("#text")) {
                    album = track["album"]["#text"].get<std::string>();
                }
                
                // Format lowercase text elements
                std::string artist_lower = artist;
                std::transform(artist_lower.begin(), artist_lower.end(), artist_lower.begin(), [](unsigned char c){ return std::tolower(c); });
                std::string name_lower = name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), [](unsigned char c){ return std::tolower(c); });
                std::string album_lower = album;
                std::transform(album_lower.begin(), album_lower.end(), album_lower.begin(), [](unsigned char c){ return std::tolower(c); });
                
                std::string play_status = format_play_status(track);
                
                // Get total user playcount
                std::string user_playcount = "";
                if (res["recenttracks"].contains("@attr") && res["recenttracks"]["@attr"].contains("total")) {
                    user_playcount = res["recenttracks"]["@attr"]["total"].get<std::string>();
                }
                
                // Try to extract large/extralarge image URL
                std::string image_url = "";
                if (track.contains("image")) {
                    auto images = track["image"];
                    // image array has size indicators: 0: small, 1: medium, 2: large, 3: extralarge
                    if (images.size() > 2) {
                        image_url = images[2]["#text"].get<std::string>();
                    } else if (!images.empty()) {
                        image_url = images[images.size() - 1]["#text"].get<std::string>();
                    }
                }
                
                std::string desc = "**now playing:** " + name_lower + " — " + artist_lower + "\n";
                if (!album_lower.empty()) {
                    desc += "**album:** " + album_lower + "\n";
                }
                if (!user_playcount.empty()) {
                    desc += "**plays:** " + user_playcount + "\n";
                }
                desc += "**status:** " + play_status;
                
                auto embed = bronx::create_embed(desc);
                embed.set_color(bronx::COLOR_DEFAULT); // BronxDefault pastel
                
                std::string title = "🎵 last.fm";
                if (user.verified) {
                    title += " (verified)";
                }
                title += " - " + user.lastfm_username;
                std::transform(title.begin(), title.end(), title.begin(), [](unsigned char c){ return std::tolower(c); });
                // Put the emoji back at the start since tolower changes it but keep the anchor intact
                embed.set_title("🎵 " + title.substr(title.find("last.fm")));
                
                if (!image_url.empty()) {
                    embed.set_thumbnail(image_url);
                }
                
                bronx::add_invoker_footer(embed, event.msg.author);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
            }).detach();
        });
    cmds.push_back(&fm_cmd);

    static Command ta_cmd("ta", "shows your top last.fm artists", "utility", {"topartists"}, false,
        [db, check_api_key](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            std::thread([&bot, event, db, args, check_api_key]() {
                if (!check_api_key(bot, event)) return;
                uint64_t target_user_id = event.msg.author.id;
                auto user_opt = db->get_lastfm_user(target_user_id);
                if (!user_opt.has_value()) {
                    bot.message_create(dpp::message(event.msg.channel_id, 
                        bronx::info("you don't have a last.fm account linked. use `.fm link <username>` to connect.")));
                    return;
                }
                
                auto user = user_opt.value();
                std::string raw_tf = args.empty() ? "weekly" : args[0];
                std::string display_tf;
                std::string period = map_period(raw_tf, display_tf);
                
                std::string desc = "";
                if (period == "24h") {
                    // Fetch recent tracks to calculate 24h stats
                    auto res = fetch_lastfm_api("user.getrecenttracks", {{"user", user.lastfm_username}, {"limit", "200"}});
                    if (res.empty() || res.contains("error")) {
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to fetch 24h stats")));
                        return;
                    }
                    
                    long long cutoff = std::time(nullptr) - 86400;
                    std::map<std::string, int> artist_counts;
                    
                    if (res.contains("recenttracks") && res["recenttracks"].contains("track")) {
                        for (const auto& track : res["recenttracks"]["track"]) {
                            if (track.contains("date") && track["date"].contains("uts")) {
                                long long uts = std::stoll(track["date"]["uts"].get<std::string>());
                                if (uts >= cutoff) {
                                    std::string artist = track["artist"]["#text"].get<std::string>();
                                    std::transform(artist.begin(), artist.end(), artist.begin(), [](unsigned char c){ return std::tolower(c); });
                                    artist_counts[artist]++;
                                }
                            }
                        }
                    }
                    
                    std::vector<std::pair<std::string, int>> sorted_artists(artist_counts.begin(), artist_counts.end());
                    std::sort(sorted_artists.begin(), sorted_artists.end(), [](const auto& a, const auto& b){
                        return a.second > b.second;
                    });
                    
                    if (sorted_artists.empty()) {
                        desc = "no tracks scrobbled in the last 24 hours.";
                    } else {
                        for (size_t i = 0; i < sorted_artists.size() && i < 10; ++i) {
                            desc += std::to_string(i + 1) + ". " + sorted_artists[i].first + " (" + std::to_string(sorted_artists[i].second) + " plays)\n";
                        }
                    }
                } else {
                    auto res = fetch_lastfm_api("user.gettopartists", {{"user", user.lastfm_username}, {"period", period}, {"limit", "10"}});
                    if (res.empty() || res.contains("error")) {
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to fetch top artists")));
                        return;
                    }
                    
                    if (!res.contains("topartists") || !res["topartists"].contains("artist")) {
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::error("no data found for timeframe **" + display_tf + "**")));
                        return;
                    }
                    
                    auto artists = res["topartists"]["artist"];
                    if (artists.empty()) {
                        desc = "no scrobbled artists found.";
                    } else {
                        int idx = 1;
                        for (const auto& artist : artists) {
                            std::string name = artist["name"].get<std::string>();
                            std::string plays = artist["playcount"].get<std::string>();
                            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });
                            desc += std::to_string(idx++) + ". " + name + " (" + plays + " plays)\n";
                        }
                    }
                }
                
                auto embed = bronx::create_embed(desc);
                embed.set_color(bronx::COLOR_DEFAULT);
                embed.set_title("📊 top artists - " + user.lastfm_username);
                
                embed.description += "\n**timeframe:** " + display_tf;
                
                bronx::add_invoker_footer(embed, event.msg.author);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
            }).detach();
        });
    cmds.push_back(&ta_cmd);

    static Command tt_cmd("tt", "shows your top last.fm tracks", "utility", {"toptracks"}, false,
        [db, check_api_key](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            std::thread([&bot, event, db, args, check_api_key]() {
                if (!check_api_key(bot, event)) return;
                uint64_t target_user_id = event.msg.author.id;
                auto user_opt = db->get_lastfm_user(target_user_id);
                if (!user_opt.has_value()) {
                    bot.message_create(dpp::message(event.msg.channel_id, 
                        bronx::info("you don't have a last.fm account linked. use `.fm link <username>` to connect.")));
                    return;
                }
                
                auto user = user_opt.value();
                std::string raw_tf = args.empty() ? "weekly" : args[0];
                std::string display_tf;
                std::string period = map_period(raw_tf, display_tf);
                
                std::string desc = "";
                if (period == "24h") {
                    // Fetch recent tracks to calculate 24h stats
                    auto res = fetch_lastfm_api("user.getrecenttracks", {{"user", user.lastfm_username}, {"limit", "200"}});
                    if (res.empty() || res.contains("error")) {
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to fetch 24h stats")));
                        return;
                    }
                    
                    long long cutoff = std::time(nullptr) - 86400;
                    std::map<std::string, int> track_counts;
                    
                    if (res.contains("recenttracks") && res["recenttracks"].contains("track")) {
                        for (const auto& track : res["recenttracks"]["track"]) {
                            if (track.contains("date") && track["date"].contains("uts")) {
                                long long uts = std::stoll(track["date"]["uts"].get<std::string>());
                                if (uts >= cutoff) {
                                    std::string artist = track["artist"]["#text"].get<std::string>();
                                    std::string track_name = track["name"].get<std::string>();
                                    std::string full_track = artist + " - " + track_name;
                                    std::transform(full_track.begin(), full_track.end(), full_track.begin(), [](unsigned char c){ return std::tolower(c); });
                                    track_counts[full_track]++;
                                }
                            }
                        }
                    }
                    
                    std::vector<std::pair<std::string, int>> sorted_tracks(track_counts.begin(), track_counts.end());
                    std::sort(sorted_tracks.begin(), sorted_tracks.end(), [](const auto& a, const auto& b){
                        return a.second > b.second;
                    });
                    
                    if (sorted_tracks.empty()) {
                        desc = "no tracks scrobbled in the last 24 hours.";
                    } else {
                        for (size_t i = 0; i < sorted_tracks.size() && i < 10; ++i) {
                            desc += std::to_string(i + 1) + ". " + sorted_tracks[i].first + " (" + std::to_string(sorted_tracks[i].second) + " plays)\n";
                        }
                    }
                } else {
                    auto res = fetch_lastfm_api("user.gettoptracks", {{"user", user.lastfm_username}, {"period", period}, {"limit", "10"}});
                    if (res.empty() || res.contains("error")) {
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to fetch top tracks")));
                        return;
                    }
                    
                    if (!res.contains("toptracks") || !res["toptracks"].contains("track")) {
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::error("no data found for timeframe **" + display_tf + "**")));
                        return;
                    }
                    
                    auto tracks = res["toptracks"]["track"];
                    if (tracks.empty()) {
                        desc = "no scrobbled tracks found.";
                    } else {
                        int idx = 1;
                        for (const auto& track : tracks) {
                            std::string name = track["name"].get<std::string>();
                            std::string artist = track["artist"]["name"].get<std::string>();
                            std::string plays = track["playcount"].get<std::string>();
                            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });
                            std::transform(artist.begin(), artist.end(), artist.begin(), [](unsigned char c){ return std::tolower(c); });
                            desc += std::to_string(idx++) + ". " + name + " — " + artist + " (" + plays + " plays)\n";
                        }
                    }
                }
                
                auto embed = bronx::create_embed(desc);
                embed.set_color(bronx::COLOR_DEFAULT);
                embed.set_title("📊 top tracks - " + user.lastfm_username);
                
                embed.description += "\n**timeframe:** " + display_tf;
                
                bronx::add_invoker_footer(embed, event.msg.author);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
            }).detach();
        });
    cmds.push_back(&tt_cmd);

    return cmds;
}

} // namespace utility
} // namespace commands
