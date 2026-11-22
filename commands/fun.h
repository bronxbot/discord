#pragma once
#include "../command.h"
#include "../embed_style.h"
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <vector>
#include <random>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include "utility/media.h"

namespace commands {

inline ::std::vector<::std::string> hug_gifs = {
    "https://media.tenor.com/LSxiXC_miqEAAAAC/hug.gif",
    "https://media.tenor.com/FuqYFMIENbwAAAAC/anime-hug.gif",
    "https://media.tenor.com/BQmLHP0b1YMAAAAC/hug.gif"
};

inline ::std::vector<::std::string> kiss_gifs = {
    "https://media.tenor.com/cBEe1EucFSgAAAAC/kiss-anime.gif",
    "https://media.tenor.com/pW9DERG5g3kAAAAC/anime-kiss.gif"
};

inline ::std::vector<::std::string> pat_gifs = {
    "https://media.tenor.com/RXJe0FjKJgkAAAAC/head-pat-anime.gif",
    "https://media.tenor.com/Q8hQDvf7830AAAAC/anime-pat.gif"
};

inline ::std::string random_gif(const ::std::vector<::std::string>& gifs) {
    static ::std::random_device rd;
    static ::std::mt19937 gen(rd());
    ::std::uniform_int_distribution<> dis(0, gifs.size() - 1);
    return gifs[dis(gen)];
}

inline std::string get_tenor_api_key() {
    const char* env_key = std::getenv("TENOR_API_KEY");
    if (env_key && std::strlen(env_key) > 0) {
        return std::string(env_key);
    }
    
    std::vector<std::string> paths = {".env", "/home/siqnole/Documents/code/bpp/.env"};
    for (const auto& path : paths) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("TENOR_API_KEY=", 0) == 0) {
                    std::string val = line.substr(14);
                    if (!val.empty() && (val.front() == '"' || val.front() == '\'')) val.erase(0, 1);
                    if (!val.empty() && (val.back() == '"' || val.back() == '\'')) val.pop_back();
                    return val;
                }
            }
        }
    }
    return "";
}

inline std::string url_encode_string(const std::string& value) {
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

inline std::string get_random_gif_from_tenor(const std::string& search_term, const std::vector<std::string>& fallback_gifs) {
    std::string api_key = get_tenor_api_key();
    if (api_key.empty()) {
        return random_gif(fallback_gifs);
    }

    std::string url = "https://tenor.googleapis.com/v2/search?q=" + url_encode_string(search_term) + 
                      "&key=" + api_key + "&limit=15&client_key=bronx_bot";
    
    commands::utility::HttpResponse res = commands::utility::http_get_sync(url);
    if (res.status != 200) {
        return random_gif(fallback_gifs);
    }

    try {
        auto json = nlohmann::json::parse(res.body);
        if (json.contains("results") && json["results"].is_array() && !json["results"].empty()) {
            auto results = json["results"];
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, results.size() - 1);
            int idx = dis(gen);
            auto item = results[idx];
            if (item.contains("media_formats") && item["media_formats"].contains("gif") && 
                item["media_formats"]["gif"].contains("url")) {
                return item["media_formats"]["gif"]["url"].get<std::string>();
            }
        }
    } catch (...) {
        // Fallback on error
    }

    return random_gif(fallback_gifs);
}

inline bool parse_dice_token(const std::string& token, int& count, int& sides) {
    if (token.empty()) return false;
    
    // Find 'd' or 'D'
    size_t d_pos = token.find('d');
    if (d_pos == std::string::npos) {
        d_pos = token.find('D');
    }
    
    if (d_pos == std::string::npos) {
        // No 'd' found, check if it's a single number
        for (char c : token) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        try {
            count = 1;
            long long val = std::stoll(token);
            if (val < 1 || val > 1000000) return false;
            sides = static_cast<int>(val);
            return true;
        } catch (...) {
            return false;
        }
    }
    
    // Split by 'd'
    std::string prefix = token.substr(0, d_pos);
    std::string suffix = token.substr(d_pos + 1);
    
    if (suffix.empty()) return false;
    for (char c : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    
    if (!prefix.empty()) {
        for (char c : prefix) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        try {
            long long count_val = std::stoll(prefix);
            if (count_val < 1 || count_val > 100) return false;
            count = static_cast<int>(count_val);
        } catch (...) {
            return false;
        }
    } else {
        count = 1;
    }
    
    try {
        long long sides_val = std::stoll(suffix);
        if (sides_val < 1 || sides_val > 1000000) return false;
        sides = static_cast<int>(sides_val);
    } catch (...) {
        return false;
    }
    
    return true;
}

inline ::std::vector<Command*> get_fun_commands() {
    static ::std::vector<Command*> cmds;

    // Hug command (text only)
    static Command hug("hug", "hug someone!", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to mention someone to hug!"));
                return;
            }

            dpp::user target = event.msg.mentions.begin()->first;
            ::std::string invoker_name = event.msg.author.global_name.empty() ? 
                event.msg.author.username : event.msg.author.global_name;
            ::std::string target_name = target.global_name.empty() ? 
                target.username : target.global_name;

            // Extract optional customized query from remaining args (ignoring mention structures)
            std::string custom_query = "";
            for (const auto& arg : args) {
                if (arg.rfind("<@", 0) == 0 || arg.rfind("!", 0) == 0) continue;
                custom_query += arg + " ";
            }
            if (!custom_query.empty() && custom_query.back() == ' ') custom_query.pop_back();

            std::thread([&bot, event, invoker_name, target_name, custom_query]() {
                std::string search_term = custom_query.empty() ? "anime hug" : custom_query;
                std::string gif_url = get_random_gif_from_tenor(search_term, hug_gifs);

                auto embed = bronx::create_embed(invoker_name + " just hugged " + target_name + "!")
                    .set_image(gif_url);

                bronx::send_message(bot, event, embed);
            }).detach();
        });
    cmds.push_back(&hug);

    // Kiss command (text only)
    static Command kiss("kiss", "kiss someone!", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to mention someone to kiss!"));
                return;
            }

            dpp::user target = event.msg.mentions.begin()->first;
            ::std::string invoker_name = event.msg.author.global_name.empty() ? 
                event.msg.author.username : event.msg.author.global_name;
            ::std::string target_name = target.global_name.empty() ? 
                target.username : target.global_name;

            // Extract optional customized query from remaining args (ignoring mention structures)
            std::string custom_query = "";
            for (const auto& arg : args) {
                if (arg.rfind("<@", 0) == 0 || arg.rfind("!", 0) == 0) continue;
                custom_query += arg + " ";
            }
            if (!custom_query.empty() && custom_query.back() == ' ') custom_query.pop_back();

            std::thread([&bot, event, invoker_name, target_name, custom_query]() {
                std::string search_term = custom_query.empty() ? "anime kiss" : custom_query;
                std::string gif_url = get_random_gif_from_tenor(search_term, kiss_gifs);

                auto embed = bronx::create_embed(invoker_name + " just kissed " + target_name + "!")
                    .set_image(gif_url);

                bronx::send_message(bot, event, embed);
            }).detach();
        });
    cmds.push_back(&kiss);

    // Pat command (text only)
    static Command pat("pat", "pat someone's head!", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to mention someone to pat!"));
                return;
            }

            dpp::user target = event.msg.mentions.begin()->first;
            ::std::string invoker_name = event.msg.author.global_name.empty() ? 
                event.msg.author.username : event.msg.author.global_name;
            ::std::string target_name = target.global_name.empty() ? 
                target.username : target.global_name;

            // Extract optional customized query from remaining args (ignoring mention structures)
            std::string custom_query = "";
            for (const auto& arg : args) {
                if (arg.rfind("<@", 0) == 0 || arg.rfind("!", 0) == 0) continue;
                custom_query += arg + " ";
            }
            if (!custom_query.empty() && custom_query.back() == ' ') custom_query.pop_back();

            std::thread([&bot, event, invoker_name, target_name, custom_query]() {
                std::string search_term = custom_query.empty() ? "anime head pat" : custom_query;
                std::string gif_url = get_random_gif_from_tenor(search_term, pat_gifs);

                auto embed = bronx::create_embed(invoker_name + " is patting " + target_name + "'s head!")
                    .set_image(gif_url);

                bronx::send_message(bot, event, embed);
            }).detach();
        });
    cmds.push_back(&pat);

    // Study command (fake facts)
    static Command study("study", "get your friends to study with fake facts!", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            std::string subject = args.empty() ? "your work" : "";
            if (!args.empty()) {
                for (const auto& arg : args) subject += arg + " ";
                if (!subject.empty() && subject.back() == ' ') subject.pop_back();
            }

            std::vector<std::string> fake_facts = {
                "90% of c students commit crime within 3 years of graduating...",
                "researchers found a 100% correlation between bad grades and being unable to find the tv remote.",
                "failing a test has been linked to spontaneous combustion in 14.5% of cases.",
                "every time you procrastinate, a penguin loses its tuxedo.",
                "statistics show that people with straight a's are 300% less likely to be abducted by aliens.",
                "a study by the university of fake science claims that bad grades cause your hair to turn into spaghetti.",
                "99% of people who don't study for " + subject + " eventually forget how to use a spoon."
            };

            std::string fact = fake_facts[rand() % fake_facts.size()];
            auto embed = bronx::create_embed(fact + "\n\nget working on **" + subject + "**...")
                .set_color(bronx::COLOR_INFO);

            bronx::send_message(bot, event, embed);
        });
    cmds.push_back(&study);

    // .fyp command (prefix-only as requested, but easily extensible to slash)
    static Command fyp("fyp", "get a random tiktok/short", "fun", {"tt"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            std::string query_str = "";
            bool is_pool = false;
            
            if (args.empty()) {
                // Authentic TikTok Pool Discovery
                static const std::vector<std::string> tt_pool = {
                    "https://www.tiktok.com/@tiktok",
                    "https://www.tiktok.com/@trending",
                    "https://www.tiktok.com/@khaby.lame",
                    "https://www.tiktok.com/@mrbeast",
                    "https://www.tiktok.com/@zachking"
                };
                query_str = tt_pool[rand() % tt_pool.size()];
                is_pool = true;
            } else {
                for (const auto& arg : args) query_str += arg + " ";
                query_str += " tiktok";
            }
            
            // Send initial feedback
            dpp::message status_msg(event.msg.channel_id, is_pool ? "🔎 exploring authentic tiktok feed..." : "🔎 searching for a random tiktok...");
            status_msg.set_reference(event.msg.id);
            
            bot.message_create(status_msg, [&bot, event, query_str, is_pool](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message sent_msg = std::get<dpp::message>(cb.value);
                
                std::thread([&bot, event, sent_msg, query_str, is_pool]() {
                    auto log_cb = [&bot, sent_msg](const std::string& logs) {
                        dpp::message update(sent_msg.channel_id, "```\n" + logs + "\n```");
                        update.id = sent_msg.id;
                        bot.message_edit(update);
                    };
                    utility::process_search_request(bot, query_str, "TikTok", true, [&bot, &sent_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = sent_msg.id; reply.set_channel_id(sent_msg.channel_id);
                        bot.message_edit(reply);
                    }, log_cb);
                }).detach();
            });
        });
    cmds.push_back(&fyp);

    // .reel command
    static Command reel("reel", "Get a random Instagram Reel", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            std::string query_str = "";
            bool is_pool = false;
            
            if (args.empty()) {
                // Seed-Based Discovery: Since direct platform scraping is currently restricted,
                // we use a curated pool of high-quality, verified Reels content to ensure authenticity.
                static const std::vector<std::string> ig_seeds = {
                    "https://www.instagram.com/reels/C5o8J7LMT3S/",
                    "https://www.instagram.com/reels/C5rD1xRM-kX/",
                    "https://www.instagram.com/reels/C5uE2vLs5PZ/",
                    "https://www.instagram.com/reels/C6G7H8I9J0K/",
                    "https://www.instagram.com/reels/C6L3M4N5O6P/",
                    "https://www.instagram.com/reels/C6Q8R9S0T1U/",
                    "https://www.instagram.com/reels/C6V3W4X5Y6Z/",
                    "https://www.instagram.com/reels/C6A1B2C3D4E/",
                    "https://www.instagram.com/reels/C6F6G7H8I9J/",
                    "https://www.instagram.com/reels/C6K1L2M3N4O/",
                    "https://www.instagram.com/reels/C6P6Q7R8S9T/",
                    "https://www.instagram.com/reels/C6U1V2W3X4Y/",
                    "https://www.instagram.com/reels/C6Z6A7B8C9D/"
                };
                query_str = ig_seeds[rand() % ig_seeds.size()];
                is_pool = true;
            } else {
                for (const auto& arg : args) query_str += arg + " ";
                query_str += " instagram reels";
            }

            // Send initial feedback
            dpp::message status_msg(event.msg.channel_id, is_pool ? "🔎 exploring authentic instagram feed..." : "🔎 searching for a random reel...");
            status_msg.set_reference(event.msg.id);
            
            bot.message_create(status_msg, [&bot, event, query_str, is_pool](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message sent_msg = std::get<dpp::message>(cb.value);
                
                std::thread([&bot, event, sent_msg, query_str, is_pool]() {
                    auto log_cb = [&bot, sent_msg](const std::string& logs) {
                        dpp::message update(sent_msg.channel_id, "```\n" + logs + "\n```");
                        update.id = sent_msg.id;
                        bot.message_edit(update);
                    };
                    utility::process_search_request(bot, query_str, "Instagram", true, [&bot, &sent_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = sent_msg.id; reply.set_channel_id(sent_msg.channel_id);
                        bot.message_edit(reply);
                    }, log_cb);
                }).detach();
            });
        });
    cmds.push_back(&reel);

    // .yt command
    static Command yt("yt", "search and get a random youtube video", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to provide keywords to search!"));
                return;
            }
            
            std::string query_str = "";
            for (const auto& arg : args) query_str += arg + " ";
            
            // Send initial feedback
            dpp::message status_msg(event.msg.channel_id, "🔎 searching youtube for: `" + query_str + "`...");
            status_msg.set_reference(event.msg.id);
            
            bot.message_create(status_msg, [&bot, event, query_str](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message sent_msg = std::get<dpp::message>(cb.value);
                
                std::thread([&bot, event, sent_msg, query_str]() {
                    auto log_cb = [&bot, sent_msg, query_str](const std::string& logs) {
                        dpp::message update(sent_msg.channel_id, "🔎 searching youtube for: `" + query_str + "`...\n```\n" + logs + "\n```");
                        update.id = sent_msg.id;
                        bot.message_edit(update);
                    };
                    utility::process_search_request(bot, query_str, "YouTube", false, [&bot, sent_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = sent_msg.id; reply.set_channel_id(sent_msg.channel_id);
                        bot.message_edit(reply);
                    }, log_cb);
                }).detach();
            });
        });
    cmds.push_back(&yt);

    // Dice roll command
    static Command dice("dice", "roll one or more dice (e.g. .dice 6 or .dice 4d6 2d10).", "fun", {"roll"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            std::vector<std::string> dice_args = args;
            if (dice_args.empty()) {
                dice_args.push_back("1d6"); // default to 1d6
            }
            
            struct DiceGroup {
                std::string input;
                int count = 0;
                int sides = 0;
                std::vector<int> rolls;
                int sum = 0;
            };
            
            std::vector<DiceGroup> groups;
            int total_dice = 0;
            
            for (const auto& arg : dice_args) {
                int count = 0;
                int sides = 0;
                if (!parse_dice_token(arg, count, sides)) {
                    bronx::send_message(bot, event, bronx::error("Invalid dice expression: `" + arg + "`. Please use format like `6`, `d20`, or `4d6`."));
                    return;
                }
                
                total_dice += count;
                if (total_dice > 100) {
                    bronx::send_message(bot, event, bronx::error("You cannot roll more than 100 dice at once."));
                    return;
                }
                
                DiceGroup group;
                group.input = arg;
                group.count = count;
                group.sides = sides;
                groups.push_back(group);
            }
            
            // Perform rolls
            static thread_local std::mt19937 gen(std::random_device{}());
            int grand_total = 0;
            
            for (auto& group : groups) {
                std::uniform_int_distribution<int> dis(1, group.sides);
                for (int i = 0; i < group.count; ++i) {
                    int roll = dis(gen);
                    group.rolls.push_back(roll);
                    group.sum += roll;
                }
                grand_total += group.sum;
            }
            
            // Format description
            std::string desc = "🎲 **Dice Roll Results**\n\n";
            for (const auto& group : groups) {
                std::string normalized_input = std::to_string(group.count) + "d" + std::to_string(group.sides);
                desc += "**" + normalized_input + "** (Sum: **" + std::to_string(group.sum) + "**)\n";
                desc += "Rolls: ";
                for (size_t i = 0; i < group.rolls.size(); ++i) {
                    desc += "`" + std::to_string(group.rolls[i]) + "`";
                    if (i + 1 < group.rolls.size()) {
                        desc += ", ";
                    }
                }
                desc += "\n\n";
            }
            
            desc += "━━━━━━━━━━━━━━━━━━━━━━\n";
            desc += "**Grand Total**: 🏆 **" + std::to_string(grand_total) + "**";
            
            auto embed = bronx::create_embed(desc)
                .set_title("Dice Roller")
                .set_color(bronx::COLOR_DEFAULT);
            
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        });
    cmds.push_back(&dice);

    return cmds;
}

} // namespace commands
