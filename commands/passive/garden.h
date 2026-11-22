#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <map>

using namespace bronx::db;

namespace commands {
namespace passive {

struct CropType {
    std::string name;
    std::string emoji;
    int grow_seconds;
    int64_t seed_cost;
    int64_t harvest_value;
};

static const std::vector<CropType> crop_types = {
    {"wheat", "🌾", 120, 20, 50},
    {"carrot", "🥕", 300, 50, 150},
    {"potato", "🥔", 600, 100, 350},
    {"tomato", "🍅", 1800, 250, 1000},
    {"melon", "🍉", 3600, 500, 2500},
    {"pumpkin", "🎃", 7200, 1000, 5500},
    {"sunflower", "🌻", 14400, 2500, 15000},
    {"golden apple", "🍎", 28800, 10000, 70000}
};

inline const CropType* find_crop_type(const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    for (const auto& c : crop_types) {
        if (c.name == lower_name) return &c;
    }
    return nullptr;
}

inline int64_t get_plot_cost(int current_plots) {
    switch (current_plots) {
        case 3: return 5000;
        case 4: return 15000;
        case 5: return 50000;
        case 6: return 150000;
        case 7: return 500000;
        case 8: return 1500000;
        default: return 0;
    }
}

inline int64_t get_can_upgrade_cost(int current_level) {
    switch (current_level) {
        case 1: return 10000;
        case 2: return 50000;
        case 3: return 250000;
        case 4: return 1000000;
        default: return 0;
    }
}

inline double get_water_boost(int level) {
    return 0.15 + (level - 1) * 0.05;
}

inline std::string get_progress_bar(double progress) {
    int bars = static_cast<int>(progress * 10);
    std::string bar = "[";
    for (int i = 0; i < 10; i++) {
        if (i < bars) bar += "█";
        else bar += "░";
    }
    bar += "] " + std::to_string(static_cast<int>(progress * 100)) + "%";
    return bar;
}

// Helper to check user ID ownership in button custom IDs
inline bool verify_user(const std::string& cid, size_t prefix_len, uint64_t target_user_id) {
    if (cid.size() <= prefix_len) return false;
    std::string remainder = cid.substr(prefix_len);
    size_t next_under = remainder.find('_');
    std::string uid_str = (next_under == std::string::npos) ? remainder : remainder.substr(0, next_under);
    try {
        uint64_t parsed_uid = std::stoull(uid_str);
        return parsed_uid == target_user_id;
    } catch (...) {
        return false;
    }
}

// ── Embed Builders ─────────────────────────────────────────────────────────

inline dpp::embed build_garden_embed(Database* db, uint64_t user_id, const dpp::user& user) {
    GardenProfile profile;
    db->get_garden_profile(user_id, profile);
    
    dpp::embed embed;
    embed.set_color(bronx::COLOR_DEFAULT);
    embed.set_title(user.username + "'s garden");
    
    std::map<int, GardenCropRow> plot_map;
    for (const auto& crop : profile.crops) {
        plot_map[crop.plot_index] = crop;
    }
    
    std::string desc;
    desc += "watering can: **level " + std::to_string(profile.watering_can_level) + "** • ";
    desc += "plots: **" + std::to_string(profile.plot_count) + "/9**\n\n";
    
    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
        
    for (int i = 1; i <= profile.plot_count; i++) {
        desc += "**plot " + std::to_string(i) + "**:\n";
        auto it = plot_map.find(i);
        if (it == plot_map.end()) {
            desc += "🌱 *empty plot*\n\n";
        } else {
            const auto& crop = it->second;
            const auto* type = find_crop_type(crop.crop_name);
            if (!type) {
                desc += "❓ unknown crop: " + crop.crop_name + "\n\n";
                continue;
            }
            
            double progress = crop.growth_progress;
            int64_t elapsed = now_ts - crop.last_watered_timestamp;
            if (elapsed < 0) elapsed = 0;
            
            progress += (double)elapsed / type->grow_seconds;
            if (progress > 1.0) progress = 1.0;
            
            int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                            crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
            
            bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
            
            if (is_wilted) {
                desc += "🥀 **wilted " + crop.crop_name + "** (needs clearing)\n\n";
            } else if (progress >= 1.0) {
                desc += type->emoji + " **" + crop.crop_name + "** — **ready to harvest!** 🌟\n\n";
            } else {
                int64_t remaining = type->grow_seconds - static_cast<int64_t>(progress * type->grow_seconds);
                if (remaining < 0) remaining = 0;
                
                std::string time_str;
                if (remaining >= 60) {
                    time_str = std::to_string(remaining / 60) + "m " + std::to_string(remaining % 60) + "s";
                } else {
                    time_str = std::to_string(remaining) + "s";
                }
                
                desc += type->emoji + " **" + crop.crop_name + "** — " + get_progress_bar(progress) + " • **" + time_str + " remaining**\n\n";
            }
        }
    }
    
    embed.set_description(desc);
    
    std::string footer_text = "requested by " + user.username;
    std::transform(footer_text.begin(), footer_text.end(), footer_text.begin(), ::tolower);
    embed.set_footer(footer_text, user.get_avatar_url());
    
    return embed;
}

inline dpp::embed build_shop_embed(Database* db, uint64_t user_id, const dpp::user& user) {
    int64_t wallet = db->get_wallet(user_id);
    
    dpp::embed embed;
    embed.set_color(bronx::COLOR_DEFAULT);
    embed.set_title("seed shop");
    
    std::string desc = "your balance: **$" + economy::format_number(wallet) + "**\n\n";
    for (const auto& c : crop_types) {
        desc += c.emoji + " **" + c.name + " seed**\n";
        desc += "> cost: **$" + economy::format_number(c.seed_cost) + "**\n";
        desc += "> grow time: **" + std::to_string(c.grow_seconds / 60) + "m**\n";
        desc += "> harvest value: **$" + economy::format_number(c.harvest_value) + "**\n\n";
    }
    
    embed.set_description(desc);
    
    std::string footer_text = "requested by " + user.username;
    std::transform(footer_text.begin(), footer_text.end(), footer_text.begin(), ::tolower);
    embed.set_footer(footer_text, user.get_avatar_url());
    
    return embed;
}

inline dpp::embed build_inventory_embed(Database* db, uint64_t user_id, const dpp::user& user) {
    auto inv = db->get_garden_inventory(user_id);
    
    dpp::embed embed;
    embed.set_color(bronx::COLOR_DEFAULT);
    embed.set_title("seed inventory");
    
    std::string desc;
    if (inv.empty()) {
        desc = "*your seed bag is empty! buy seeds at the shop.*";
    } else {
        for (const auto& [item, qty] : inv) {
            std::string emoji = "🌱";
            std::string crop_name = item;
            if (crop_name.size() > 5 && crop_name.substr(crop_name.size() - 5) == " seed") {
                crop_name = crop_name.substr(0, crop_name.size() - 5);
            }
            const auto* type = find_crop_type(crop_name);
            if (type) {
                emoji = type->emoji;
            }
            desc += emoji + " **" + item + "**: " + std::to_string(qty) + "\n";
        }
    }
    
    embed.set_description(desc);
    
    std::string footer_text = "requested by " + user.username;
    std::transform(footer_text.begin(), footer_text.end(), footer_text.begin(), ::tolower);
    embed.set_footer(footer_text, user.get_avatar_url());
    
    return embed;
}

inline dpp::embed build_plot_embed(Database* db, uint64_t user_id, int plot_index, const dpp::user& user) {
    GardenProfile profile;
    db->get_garden_profile(user_id, profile);
    
    dpp::embed embed;
    embed.set_color(bronx::COLOR_DEFAULT);
    embed.set_title("plot " + std::to_string(plot_index) + " details");
    
    std::map<int, GardenCropRow> plot_map;
    for (const auto& crop : profile.crops) {
        plot_map[crop.plot_index] = crop;
    }
    
    auto it = plot_map.find(plot_index);
    if (it == plot_map.end()) {
        embed.set_description("this plot is currently empty. you can plant a seed here.");
    } else {
        const auto& crop = it->second;
        const auto* type = find_crop_type(crop.crop_name);
        if (!type) {
            embed.set_description("unknown crop: " + crop.crop_name);
        } else {
            int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
                
            double progress = crop.growth_progress;
            int64_t elapsed = now_ts - crop.last_watered_timestamp;
            if (elapsed < 0) elapsed = 0;
            
            progress += (double)elapsed / type->grow_seconds;
            if (progress > 1.0) progress = 1.0;
            
            int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                            crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
            
            bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
            
            std::string desc = type->emoji + " **crop:** " + crop.crop_name + "\n";
            if (is_wilted) {
                desc += "status: 🥀 **wilted**\n\n*this crop dried out and died. you must clear the plot.*";
            } else if (progress >= 1.0) {
                desc += "status: 🌟 **ready to harvest!**\n\n*harvest this crop to receive coins.*";
            } else {
                int64_t remaining = type->grow_seconds - static_cast<int64_t>(progress * type->grow_seconds);
                if (remaining < 0) remaining = 0;
                
                std::string time_str;
                if (remaining >= 60) {
                    time_str = std::to_string(remaining / 60) + "m " + std::to_string(remaining % 60) + "s";
                } else {
                    time_str = std::to_string(remaining) + "s";
                }
                desc += "status: 📈 **growing**\n";
                desc += "progress: " + get_progress_bar(progress) + "\n";
                desc += "time remaining: **" + time_str + "**";
            }
            embed.set_description(desc);
        }
    }
    
    std::string footer_text = "requested by " + user.username;
    std::transform(footer_text.begin(), footer_text.end(), footer_text.begin(), ::tolower);
    embed.set_footer(footer_text, user.get_avatar_url());
    
    return embed;
}

// ── Components Helpers ──────────────────────────────────────────────────────

inline dpp::message build_garden_message(Database* db, uint64_t user_id, const dpp::user& user) {
    auto embed = build_garden_embed(db, user_id, user);
    
    GardenProfile profile;
    db->get_garden_profile(user_id, profile);
    
    std::map<int, GardenCropRow> plot_map;
    for (const auto& crop : profile.crops) {
        plot_map[crop.plot_index] = crop;
    }
    
    dpp::message msg;
    msg.add_embed(embed);
    
    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
        
    dpp::component plots_row1;
    dpp::component plots_row2;
    
    for (int i = 1; i <= profile.plot_count; i++) {
        dpp::component btn = dpp::component()
            .set_type(dpp::cot_button)
            .set_label("plot " + std::to_string(i))
            .set_style(dpp::cos_secondary)
            .set_id("garden_plot_" + std::to_string(user_id) + "_" + std::to_string(i));
            
        auto it = plot_map.find(i);
        if (it != plot_map.end()) {
            const auto& crop = it->second;
            const auto* type = find_crop_type(crop.crop_name);
            if (type) {
                btn.set_emoji(type->emoji);
                
                double progress = crop.growth_progress;
                int64_t elapsed = now_ts - crop.last_watered_timestamp;
                if (elapsed < 0) elapsed = 0;
                progress += (double)elapsed / type->grow_seconds;
                if (progress > 1.0) progress = 1.0;
                
                int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                                crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
                bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
                
                if (is_wilted) {
                    btn.set_style(dpp::cos_danger);
                } else if (progress >= 1.0) {
                    btn.set_style(dpp::cos_success);
                } else {
                    btn.set_style(dpp::cos_primary);
                }
            }
        } else {
            btn.set_emoji("🌱");
        }
        
        if (i <= 5) {
            plots_row1.add_component(btn);
        } else {
            plots_row2.add_component(btn);
        }
    }
    
    if (plots_row1.components.size() > 0) msg.add_component(plots_row1);
    if (plots_row2.components.size() > 0) msg.add_component(plots_row2);
    
    dpp::component actions_row;
    actions_row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_label("seed shop")
            .set_emoji("🛒")
            .set_style(dpp::cos_secondary)
            .set_id("garden_shop_" + std::to_string(user_id))
    );
    actions_row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_label("seed inventory")
            .set_emoji("🎒")
            .set_style(dpp::cos_secondary)
            .set_id("garden_inventory_" + std::to_string(user_id))
    );
    
    if (profile.plot_count < 9) {
        actions_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("buy plot")
                .set_emoji("➕")
                .set_style(dpp::cos_secondary)
                .set_id("garden_buyplot_" + std::to_string(user_id))
        );
    }
    
    if (profile.watering_can_level < 5) {
        actions_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("upgrade can")
                .set_emoji("💧")
                .set_style(dpp::cos_secondary)
                .set_id("garden_upgradecan_" + std::to_string(user_id))
        );
    }
    
    msg.add_component(actions_row);
    return msg;
}

inline dpp::message build_plot_message(Database* db, uint64_t user_id, int plot_index, const dpp::user& user) {
    auto embed = build_plot_embed(db, user_id, plot_index, user);
    
    GardenProfile profile;
    db->get_garden_profile(user_id, profile);
    
    std::map<int, GardenCropRow> plot_map;
    for (const auto& crop : profile.crops) {
        plot_map[crop.plot_index] = crop;
    }
    
    dpp::message msg;
    msg.add_embed(embed);
    
    auto it = plot_map.find(plot_index);
    if (it == plot_map.end()) {
        auto inv = db->get_garden_inventory(user_id);
        
        bool has_seeds = false;
        dpp::component select_menu;
        select_menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("select a seed to plant")
            .set_id("garden_plant_select_" + std::to_string(user_id) + "_" + std::to_string(plot_index));
            
        for (const auto& [item, qty] : inv) {
            if (qty > 0) {
                has_seeds = true;
                std::string crop_name = item;
                if (crop_name.size() > 5 && crop_name.substr(crop_name.size() - 5) == " seed") {
                    crop_name = crop_name.substr(0, crop_name.size() - 5);
                }
                const auto* type = find_crop_type(crop_name);
                std::string emoji = type ? type->emoji : "🌱";
                
                select_menu.add_select_option(
                    dpp::select_option(item, crop_name, "quantity: " + std::to_string(qty))
                        .set_emoji(emoji)
                );
            }
        }
        
        if (has_seeds) {
            msg.add_component(dpp::component().add_component(select_menu));
        }
    } else {
        const auto& crop = it->second;
        const auto* type = find_crop_type(crop.crop_name);
        if (type) {
            int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
                
            double progress = crop.growth_progress;
            int64_t elapsed = now_ts - crop.last_watered_timestamp;
            if (elapsed < 0) elapsed = 0;
            progress += (double)elapsed / type->grow_seconds;
            if (progress > 1.0) progress = 1.0;
            
            int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                            crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
            bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
            
            dpp::component actions;
            if (is_wilted) {
                actions.add_component(
                    dpp::component()
                        .set_type(dpp::cot_button)
                        .set_label("clear plot")
                        .set_emoji("🧹")
                        .set_style(dpp::cos_danger)
                        .set_id("garden_clear_" + std::to_string(user_id) + "_" + std::to_string(plot_index))
                );
            } else if (progress >= 1.0) {
                actions.add_component(
                    dpp::component()
                        .set_type(dpp::cot_button)
                        .set_label("harvest crop")
                        .set_emoji("🧺")
                        .set_style(dpp::cos_success)
                        .set_id("garden_harvest_" + std::to_string(user_id) + "_" + std::to_string(plot_index))
                );
            } else {
                actions.add_component(
                    dpp::component()
                        .set_type(dpp::cot_button)
                        .set_label("water crop")
                        .set_emoji("💧")
                        .set_style(dpp::cos_success)
                        .set_id("garden_water_" + std::to_string(user_id) + "_" + std::to_string(plot_index))
                );
                actions.add_component(
                    dpp::component()
                        .set_type(dpp::cot_button)
                        .set_label("clear crop")
                        .set_emoji("🧹")
                        .set_style(dpp::cos_danger)
                        .set_id("garden_clear_" + std::to_string(user_id) + "_" + std::to_string(plot_index))
                );
            }
            msg.add_component(actions);
        }
    }
    
    dpp::component navigation;
    navigation.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_label("back to garden")
            .set_emoji("🏡")
            .set_style(dpp::cos_secondary)
            .set_id("garden_view_" + std::to_string(user_id))
    );
    msg.add_component(navigation);
    
    return msg;
}

// ── Main Commands ───────────────────────────────────────────────────────────

inline Command* get_garden_command(Database* db) {
    static Command* cmd = new Command(
        "garden",
        "cultivate crops and earn coins passive gameplay",
        "passive",
        {"gardening", "gdn"},
        true,
        
        // Text Handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;
            db->ensure_garden_profile(uid);
            
            std::string sub = args.empty() ? "view" : args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            if (sub == "shop") {
                event.reply(dpp::message(event.msg.channel_id, build_shop_embed(db, uid, event.msg.author)));
            } else if (sub == "inventory" || sub == "inv") {
                event.reply(dpp::message(event.msg.channel_id, build_inventory_embed(db, uid, event.msg.author)));
            } else if (sub == "buy-seed" || sub == "buyseed") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify seed name: `b.garden buy-seed <seed>`"));
                    return;
                }
                std::string seed_name = args[1];
                int qty = 1;
                if (args.size() >= 3) {
                    try { qty = std::stoi(args[2]); } catch (...) {}
                }
                if (qty <= 0) {
                    bronx::send_message(bot, event, bronx::error("quantity must be positive"));
                    return;
                }
                
                const auto* type = find_crop_type(seed_name);
                if (!type) {
                    bronx::send_message(bot, event, bronx::error("seed not found in shop"));
                    return;
                }
                
                int64_t total_cost = type->seed_cost * qty;
                int64_t wallet = db->get_wallet(uid);
                if (wallet < total_cost) {
                    bronx::send_message(bot, event, bronx::error("insufficient balance for " + std::to_string(qty) + " " + type->name + " seeds (costs $" + economy::format_number(total_cost) + ")"));
                    return;
                }
                
                db->update_wallet(uid, -total_cost);
                db->add_garden_inventory(uid, type->name + " seed", qty);
                
                bronx::send_message(bot, event, bronx::success("bought **" + std::to_string(qty) + " " + type->name + " seeds** for **$" + economy::format_number(total_cost) + "**"));
                
            } else if (sub == "plant") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("specify plot and seed: `b.garden plant <plot> <seed>`"));
                    return;
                }
                int plot_idx;
                try { plot_idx = std::stoi(args[1]); } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid plot index"));
                    return;
                }
                
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                if (plot_idx < 1 || plot_idx > profile.plot_count) {
                    bronx::send_message(bot, event, bronx::error("invalid plot (1-" + std::to_string(profile.plot_count) + ")"));
                    return;
                }
                
                std::string seed_name = args[2];
                const auto* type = find_crop_type(seed_name);
                if (!type) {
                    bronx::send_message(bot, event, bronx::error("seed not found"));
                    return;
                }
                
                // check if crop already exists on this plot
                for (const auto& crop : profile.crops) {
                    if (crop.plot_index == plot_idx) {
                        bronx::send_message(bot, event, bronx::error("plot " + std::to_string(plot_idx) + " is already occupied"));
                        return;
                    }
                }
                
                // check inventory
                auto inv = db->get_garden_inventory(uid);
                std::string inv_item = type->name + " seed";
                auto it = inv.find(inv_item);
                if (it == inv.end() || it->second <= 0) {
                    bronx::send_message(bot, event, bronx::error("you do not have any " + type->name + " seeds"));
                    return;
                }
                
                db->add_garden_inventory(uid, inv_item, -1);
                db->plant_crop(uid, plot_idx, type->name);
                
                bronx::send_message(bot, event, bronx::success("planted **" + type->name + "** in plot **" + std::to_string(plot_idx) + "**"));
                
            } else if (sub == "water") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify plot index: `b.garden water <plot>`"));
                    return;
                }
                int plot_idx;
                try { plot_idx = std::stoi(args[1]); } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid plot index"));
                    return;
                }
                
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                
                bool found = false;
                for (const auto& crop : profile.crops) {
                    if (crop.plot_index == plot_idx) {
                        found = true;
                        const auto* type = find_crop_type(crop.crop_name);
                        if (!type) break;
                        
                        int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                            
                        double progress = crop.growth_progress;
                        int64_t elapsed = now_ts - crop.last_watered_timestamp;
                        if (elapsed < 0) elapsed = 0;
                        progress += (double)elapsed / type->grow_seconds;
                        
                        int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                                        crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
                        bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
                        
                        if (is_wilted) {
                            bronx::send_message(bot, event, bronx::error("this crop has wilted and died. you must clear it."));
                            return;
                        }
                        if (progress >= 1.0) {
                            bronx::send_message(bot, event, bronx::error("this crop is already fully grown. harvest it!"));
                            return;
                        }
                        
                        double boost = get_water_boost(profile.watering_can_level);
                        db->water_crop(uid, plot_idx, boost);
                        bronx::send_message(bot, event, bronx::success("watered plot **" + std::to_string(plot_idx) + "**! growth progress boosted by **+" + std::to_string(static_cast<int>(boost * 100)) + "%**"));
                        break;
                    }
                }
                if (!found) {
                    bronx::send_message(bot, event, bronx::error("no crop planted in plot " + std::to_string(plot_idx)));
                }
                
            } else if (sub == "harvest") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify plot index: `b.garden harvest <plot>`"));
                    return;
                }
                int plot_idx;
                try { plot_idx = std::stoi(args[1]); } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid plot index"));
                    return;
                }
                
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                
                bool found = false;
                for (const auto& crop : profile.crops) {
                    if (crop.plot_index == plot_idx) {
                        found = true;
                        const auto* type = find_crop_type(crop.crop_name);
                        if (!type) break;
                        
                        int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                            
                        double progress = crop.growth_progress;
                        int64_t elapsed = now_ts - crop.last_watered_timestamp;
                        if (elapsed < 0) elapsed = 0;
                        progress += (double)elapsed / type->grow_seconds;
                        
                        int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                                        crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
                        bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
                        
                        if (is_wilted) {
                            bronx::send_message(bot, event, bronx::error("this crop wilted! you must clear it."));
                            return;
                        }
                        if (progress < 1.0) {
                            bronx::send_message(bot, event, bronx::error("this crop is not fully grown yet (" + std::to_string(static_cast<int>(progress * 100)) + "%)"));
                            return;
                        }
                        
                        db->harvest_crop(uid, plot_idx);
                        db->update_wallet(uid, type->harvest_value);
                        bronx::send_message(bot, event, bronx::success("harvested plot **" + std::to_string(plot_idx) + "** and earned **$" + economy::format_number(type->harvest_value) + "**! " + type->emoji));
                        break;
                    }
                }
                if (!found) {
                    bronx::send_message(bot, event, bronx::error("no crop planted in plot " + std::to_string(plot_idx)));
                }
                
            } else if (sub == "clear") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify plot index: `b.garden clear <plot>`"));
                    return;
                }
                int plot_idx;
                try { plot_idx = std::stoi(args[1]); } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid plot index"));
                    return;
                }
                
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                
                bool found = false;
                for (const auto& crop : profile.crops) {
                    if (crop.plot_index == plot_idx) {
                        found = true;
                        db->clear_crop(uid, plot_idx);
                        bronx::send_message(bot, event, bronx::success("cleared plot **" + std::to_string(plot_idx) + "**"));
                        break;
                    }
                }
                if (!found) {
                    bronx::send_message(bot, event, bronx::error("no crop planted in plot " + std::to_string(plot_idx)));
                }
                
            } else if (sub == "buy-plot" || sub == "buyplot") {
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                if (profile.plot_count >= 9) {
                    bronx::send_message(bot, event, bronx::error("you already have the maximum amount of plots (9)"));
                    return;
                }
                
                int64_t cost = get_plot_cost(profile.plot_count);
                int64_t wallet = db->get_wallet(uid);
                if (wallet < cost) {
                    bronx::send_message(bot, event, bronx::error("insufficient balance to buy plot (costs $" + economy::format_number(cost) + ")"));
                    return;
                }
                
                db->update_wallet(uid, -cost);
                db->buy_garden_plot(uid);
                bronx::send_message(bot, event, bronx::success("bought a new plot! your plot count is now **" + std::to_string(profile.plot_count + 1) + "**"));
                
            } else if (sub == "upgrade-can" || sub == "upgradecan") {
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                if (profile.watering_can_level >= 5) {
                    bronx::send_message(bot, event, bronx::error("watering can is already max level (5)"));
                    return;
                }
                
                int64_t cost = get_can_upgrade_cost(profile.watering_can_level);
                int64_t wallet = db->get_wallet(uid);
                if (wallet < cost) {
                    bronx::send_message(bot, event, bronx::error("insufficient balance to upgrade watering can (costs $" + economy::format_number(cost) + ")"));
                    return;
                }
                
                db->update_wallet(uid, -cost);
                db->upgrade_watering_can(uid);
                bronx::send_message(bot, event, bronx::success("upgraded watering can to **level " + std::to_string(profile.watering_can_level + 1) + "**!"));
                
            } else { // default: view
                event.reply(build_garden_message(db, uid, event.msg.author));
            }
        },
        
        // Slash Handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            db->ensure_garden_profile(uid);
            
            std::string sub = "view";
            auto ci_options = event.command.get_command_interaction().options;
            std::vector<dpp::command_data_option> sub_options;
            
            if (ci_options.size() > 0) {
                if (ci_options[0].name == "garden") {
                    if (ci_options[0].options.size() > 0) {
                        sub = ci_options[0].options[0].name;
                        sub_options = ci_options[0].options[0].options;
                    }
                } else {
                    sub = ci_options[0].name;
                    sub_options = ci_options[0].options;
                }
            }
            
            auto get_opt_val = [](const std::vector<dpp::command_data_option>& opts, const std::string& name) -> const dpp::command_value* {
                for (const auto& opt : opts) {
                    if (opt.name == name) return &opt.value;
                }
                return nullptr;
            };
            
            if (sub == "shop") {
                event.reply(dpp::message().add_embed(build_shop_embed(db, uid, event.command.get_issuing_user())));
            } else if (sub == "inventory") {
                event.reply(dpp::message().add_embed(build_inventory_embed(db, uid, event.command.get_issuing_user())));
            } else if (sub == "buy-seed") {
                auto* seed_opt = get_opt_val(sub_options, "seed");
                if (!seed_opt) {
                    event.reply(dpp::message().add_embed(bronx::error("specify seed name")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string seed_name = std::get<std::string>(*seed_opt);
                int qty = 1;
                auto* qty_opt = get_opt_val(sub_options, "quantity");
                if (qty_opt) {
                    qty = static_cast<int>(std::get<int64_t>(*qty_opt));
                }
                if (qty <= 0) {
                    event.reply(dpp::message().add_embed(bronx::error("quantity must be positive")).set_flags(dpp::m_ephemeral));
                    return;
                }
                const auto* type = find_crop_type(seed_name);
                if (!type) {
                    event.reply(dpp::message().add_embed(bronx::error("seed not found")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int64_t total_cost = type->seed_cost * qty;
                int64_t wallet = db->get_wallet(uid);
                if (wallet < total_cost) {
                    event.reply(dpp::message().add_embed(bronx::error("insufficient balance (costs $" + economy::format_number(total_cost) + ")")).set_flags(dpp::m_ephemeral));
                    return;
                }
                db->update_wallet(uid, -total_cost);
                db->add_garden_inventory(uid, type->name + " seed", qty);
                event.reply(dpp::message().add_embed(bronx::success("bought **" + std::to_string(qty) + " " + type->name + " seeds** for **$" + economy::format_number(total_cost) + "**")));
                
            } else if (sub == "plant") {
                auto* plot_opt = get_opt_val(sub_options, "plot");
                auto* seed_opt = get_opt_val(sub_options, "seed");
                if (!plot_opt || !seed_opt) {
                    event.reply(dpp::message().add_embed(bronx::error("specify plot and seed")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int plot_idx = static_cast<int>(std::get<int64_t>(*plot_opt));
                std::string seed_name = std::get<std::string>(*seed_opt);
                
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                if (plot_idx < 1 || plot_idx > profile.plot_count) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid plot (1-" + std::to_string(profile.plot_count) + ")")).set_flags(dpp::m_ephemeral));
                    return;
                }
                const auto* type = find_crop_type(seed_name);
                if (!type) {
                    event.reply(dpp::message().add_embed(bronx::error("seed not found")).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                for (const auto& crop : profile.crops) {
                    if (crop.plot_index == plot_idx) {
                        event.reply(dpp::message().add_embed(bronx::error("plot occupied")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                }
                auto inv = db->get_garden_inventory(uid);
                std::string inv_item = type->name + " seed";
                auto it = inv.find(inv_item);
                if (it == inv.end() || it->second <= 0) {
                    event.reply(dpp::message().add_embed(bronx::error("no " + type->name + " seeds in your bag")).set_flags(dpp::m_ephemeral));
                    return;
                }
                db->add_garden_inventory(uid, inv_item, -1);
                db->plant_crop(uid, plot_idx, type->name);
                event.reply(dpp::message().add_embed(bronx::success("planted **" + type->name + "** in plot **" + std::to_string(plot_idx) + "**")));
                
            } else if (sub == "water") {
                auto* plot_opt = get_opt_val(sub_options, "plot");
                if (!plot_opt) {
                    event.reply(dpp::message().add_embed(bronx::error("specify plot")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int plot_idx = static_cast<int>(std::get<int64_t>(*plot_opt));
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                
                bool found = false;
                for (const auto& crop : profile.crops) {
                    if (crop.plot_index == plot_idx) {
                        found = true;
                        const auto* type = find_crop_type(crop.crop_name);
                        if (!type) break;
                        
                        int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        double progress = crop.growth_progress;
                        int64_t elapsed = now_ts - crop.last_watered_timestamp;
                        if (elapsed < 0) elapsed = 0;
                        progress += (double)elapsed / type->grow_seconds;
                        
                        int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                                        crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
                        bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
                        
                        if (is_wilted) {
                            event.reply(dpp::message().add_embed(bronx::error("this crop has wilted")).set_flags(dpp::m_ephemeral));
                            return;
                        }
                        if (progress >= 1.0) {
                            event.reply(dpp::message().add_embed(bronx::error("this crop is ready to harvest")).set_flags(dpp::m_ephemeral));
                            return;
                        }
                        double boost = get_water_boost(profile.watering_can_level);
                        db->water_crop(uid, plot_idx, boost);
                        event.reply(dpp::message().add_embed(bronx::success("watered plot **" + std::to_string(plot_idx) + "** (boosted +" + std::to_string(static_cast<int>(boost * 100)) + "%)")));
                        break;
                    }
                }
                if (!found) {
                    event.reply(dpp::message().add_embed(bronx::error("plot is empty")).set_flags(dpp::m_ephemeral));
                }
                
            } else if (sub == "harvest") {
                auto* plot_opt = get_opt_val(sub_options, "plot");
                if (!plot_opt) {
                    event.reply(dpp::message().add_embed(bronx::error("specify plot")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int plot_idx = static_cast<int>(std::get<int64_t>(*plot_opt));
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                bool found = false;
                for (const auto& crop : profile.crops) {
                    if (crop.plot_index == plot_idx) {
                        found = true;
                        const auto* type = find_crop_type(crop.crop_name);
                        if (!type) break;
                        
                        int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        double progress = crop.growth_progress;
                        int64_t elapsed = now_ts - crop.last_watered_timestamp;
                        if (elapsed < 0) elapsed = 0;
                        progress += (double)elapsed / type->grow_seconds;
                        
                        int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                                        crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
                        bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
                        
                        if (is_wilted) {
                            event.reply(dpp::message().add_embed(bronx::error("crop has wilted")).set_flags(dpp::m_ephemeral));
                            return;
                        }
                        if (progress < 1.0) {
                            event.reply(dpp::message().add_embed(bronx::error("crop is not fully grown")).set_flags(dpp::m_ephemeral));
                            return;
                        }
                        db->harvest_crop(uid, plot_idx);
                        db->update_wallet(uid, type->harvest_value);
                        event.reply(dpp::message().add_embed(bronx::success("harvested plot **" + std::to_string(plot_idx) + "** and earned **$" + economy::format_number(type->harvest_value) + "**! " + type->emoji)));
                        break;
                    }
                }
                if (!found) {
                    event.reply(dpp::message().add_embed(bronx::error("plot is empty")).set_flags(dpp::m_ephemeral));
                }
                
            } else if (sub == "clear") {
                auto* plot_opt = get_opt_val(sub_options, "plot");
                if (!plot_opt) {
                    event.reply(dpp::message().add_embed(bronx::error("specify plot")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int plot_idx = static_cast<int>(std::get<int64_t>(*plot_opt));
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                bool found = false;
                for (const auto& crop : profile.crops) {
                    if (crop.plot_index == plot_idx) {
                        found = true;
                        db->clear_crop(uid, plot_idx);
                        event.reply(dpp::message().add_embed(bronx::success("cleared plot **" + std::to_string(plot_idx) + "**")));
                        break;
                    }
                }
                if (!found) {
                    event.reply(dpp::message().add_embed(bronx::error("plot is empty")).set_flags(dpp::m_ephemeral));
                }
                
            } else if (sub == "buy-plot") {
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                if (profile.plot_count >= 9) {
                    event.reply(dpp::message().add_embed(bronx::error("already max plots (9)")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int64_t cost = get_plot_cost(profile.plot_count);
                int64_t wallet = db->get_wallet(uid);
                if (wallet < cost) {
                    event.reply(dpp::message().add_embed(bronx::error("insufficient balance (costs $" + economy::format_number(cost) + ")")).set_flags(dpp::m_ephemeral));
                    return;
                }
                db->update_wallet(uid, -cost);
                db->buy_garden_plot(uid);
                event.reply(dpp::message().add_embed(bronx::success("bought plot! count is now **" + std::to_string(profile.plot_count + 1) + "**")));
                
            } else if (sub == "upgrade-can") {
                GardenProfile profile;
                db->get_garden_profile(uid, profile);
                if (profile.watering_can_level >= 5) {
                    event.reply(dpp::message().add_embed(bronx::error("already max watering can level (5)")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int64_t cost = get_can_upgrade_cost(profile.watering_can_level);
                int64_t wallet = db->get_wallet(uid);
                if (wallet < cost) {
                    event.reply(dpp::message().add_embed(bronx::error("insufficient balance (costs $" + economy::format_number(cost) + ")")).set_flags(dpp::m_ephemeral));
                    return;
                }
                db->update_wallet(uid, -cost);
                db->upgrade_watering_can(uid);
                event.reply(dpp::message().add_embed(bronx::success("upgraded watering can to **level " + std::to_string(profile.watering_can_level + 1) + "**!")));
                
            } else { // view
                event.reply(build_garden_message(db, uid, event.command.get_issuing_user()));
            }
        },
        
        // Options
        {
            dpp::command_option(dpp::co_sub_command, "view", "view your garden plots"),
            dpp::command_option(dpp::co_sub_command, "shop", "view seed shop"),
            dpp::command_option(dpp::co_sub_command, "inventory", "view your seeds"),
            dpp::command_option(dpp::co_sub_command, "plant", "plant a seed in a plot")
                .add_option(dpp::command_option(dpp::co_integer, "plot", "plot index (1-9)", true))
                .add_option(dpp::command_option(dpp::co_string, "seed", "seed name to plant", true)),
            dpp::command_option(dpp::co_sub_command, "water", "water a crop in a plot")
                .add_option(dpp::command_option(dpp::co_integer, "plot", "plot index (1-9)", true)),
            dpp::command_option(dpp::co_sub_command, "harvest", "harvest a grown crop")
                .add_option(dpp::command_option(dpp::co_integer, "plot", "plot index (1-9)", true)),
            dpp::command_option(dpp::co_sub_command, "clear", "clear a plot of growing or wilted crop")
                .add_option(dpp::command_option(dpp::co_integer, "plot", "plot index (1-9)", true)),
            dpp::command_option(dpp::co_sub_command, "buy-plot", "buy a new plot"),
            dpp::command_option(dpp::co_sub_command, "upgrade-can", "upgrade your watering can"),
            dpp::command_option(dpp::co_sub_command, "buy-seed", "buy seeds from the shop")
                .add_option(dpp::command_option(dpp::co_string, "seed", "seed name to buy", true))
                .add_option(dpp::command_option(dpp::co_integer, "quantity", "quantity of seeds to buy", false))
        }
    );
    return cmd;
}

// ── Interaction Handlers ────────────────────────────────────────────────────

inline void register_garden_interactions(dpp::cluster& bot, Database* db) {
    
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        const auto& cid = event.custom_id;
        uint64_t uid = event.command.usr.id;
        
        // ── 1. garden_view_ ──
        if (cid.find("garden_view_") == 0) {
            if (!verify_user(cid, 12, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            event.reply(dpp::ir_update_message, build_garden_message(db, uid, event.command.usr));
            return;
        }
        
        // ── 2. garden_plot_ ──
        if (cid.find("garden_plot_") == 0) {
            if (!verify_user(cid, 12, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            std::string remainder = cid.substr(12);
            size_t next_under = remainder.find('_');
            if (next_under == std::string::npos) return;
            
            int plot_idx = 0;
            try { plot_idx = std::stoi(remainder.substr(next_under + 1)); } catch (...) { return; }
            
            event.reply(dpp::ir_update_message, build_plot_message(db, uid, plot_idx, event.command.usr));
            return;
        }
        
        // ── 3. garden_water_ ──
        if (cid.find("garden_water_") == 0) {
            if (!verify_user(cid, 13, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            std::string remainder = cid.substr(13);
            size_t next_under = remainder.find('_');
            if (next_under == std::string::npos) return;
            
            int plot_idx = 0;
            try { plot_idx = std::stoi(remainder.substr(next_under + 1)); } catch (...) { return; }
            
            GardenProfile profile;
            db->get_garden_profile(uid, profile);
            
            bool found = false;
            for (const auto& crop : profile.crops) {
                if (crop.plot_index == plot_idx) {
                    found = true;
                    const auto* type = find_crop_type(crop.crop_name);
                    if (!type) break;
                    
                    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    double progress = crop.growth_progress;
                    int64_t elapsed = now_ts - crop.last_watered_timestamp;
                    if (elapsed < 0) elapsed = 0;
                    progress += (double)elapsed / type->grow_seconds;
                    
                    int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                                    crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
                    bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
                    
                    if (is_wilted) {
                        event.reply(dpp::message().add_embed(bronx::error("this crop has wilted and died. you must clear the plot.")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    if (progress >= 1.0) {
                        event.reply(dpp::message().add_embed(bronx::error("this crop is already ready to harvest.")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    double boost = get_water_boost(profile.watering_can_level);
                    db->water_crop(uid, plot_idx, boost);
                    break;
                }
            }
            
            event.reply(dpp::ir_update_message, build_plot_message(db, uid, plot_idx, event.command.usr));
            return;
        }
        
        // ── 4. garden_harvest_ ──
        if (cid.find("garden_harvest_") == 0) {
            if (!verify_user(cid, 15, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            std::string remainder = cid.substr(15);
            size_t next_under = remainder.find('_');
            if (next_under == std::string::npos) return;
            
            int plot_idx = 0;
            try { plot_idx = std::stoi(remainder.substr(next_under + 1)); } catch (...) { return; }
            
            GardenProfile profile;
            db->get_garden_profile(uid, profile);
            bool found = false;
            for (const auto& crop : profile.crops) {
                if (crop.plot_index == plot_idx) {
                    found = true;
                    const auto* type = find_crop_type(crop.crop_name);
                    if (!type) break;
                    
                    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    double progress = crop.growth_progress;
                    int64_t elapsed = now_ts - crop.last_watered_timestamp;
                    if (elapsed < 0) elapsed = 0;
                    progress += (double)elapsed / type->grow_seconds;
                    
                    int64_t t_100 = crop.growth_progress >= 1.0 ? crop.last_watered_timestamp : 
                                    crop.last_watered_timestamp + static_cast<int64_t>((1.0 - crop.growth_progress) * type->grow_seconds);
                    bool is_wilted = crop.is_wilted || (progress >= 1.0 && now_ts >= t_100 + static_cast<int64_t>(1.5 * type->grow_seconds));
                    
                    if (is_wilted) {
                        event.reply(dpp::message().add_embed(bronx::error("crop has wilted")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    if (progress < 1.0) {
                        event.reply(dpp::message().add_embed(bronx::error("crop is not fully grown yet")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    db->harvest_crop(uid, plot_idx);
                    db->update_wallet(uid, type->harvest_value);
                    break;
                }
            }
            
            event.reply(dpp::ir_update_message, build_garden_message(db, uid, event.command.usr));
            return;
        }
        
        // ── 5. garden_clear_ ──
        if (cid.find("garden_clear_") == 0) {
            if (!verify_user(cid, 13, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            std::string remainder = cid.substr(13);
            size_t next_under = remainder.find('_');
            if (next_under == std::string::npos) return;
            
            int plot_idx = 0;
            try { plot_idx = std::stoi(remainder.substr(next_under + 1)); } catch (...) { return; }
            
            db->clear_crop(uid, plot_idx);
            
            event.reply(dpp::ir_update_message, build_garden_message(db, uid, event.command.usr));
            return;
        }
        
        // ── 6. garden_buyplot_ ──
        if (cid.find("garden_buyplot_") == 0) {
            if (!verify_user(cid, 15, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            GardenProfile profile;
            db->get_garden_profile(uid, profile);
            if (profile.plot_count >= 9) {
                event.reply(dpp::message().add_embed(bronx::error("already max plots (9)")).set_flags(dpp::m_ephemeral));
                return;
            }
            int64_t cost = get_plot_cost(profile.plot_count);
            int64_t wallet = db->get_wallet(uid);
            if (wallet < cost) {
                event.reply(dpp::message().add_embed(bronx::error("insufficient balance to buy plot (costs $" + economy::format_number(cost) + ")")).set_flags(dpp::m_ephemeral));
                return;
            }
            db->update_wallet(uid, -cost);
            db->buy_garden_plot(uid);
            
            event.reply(dpp::ir_update_message, build_garden_message(db, uid, event.command.usr));
            return;
        }
        
        // ── 7. garden_upgradecan_ ──
        if (cid.find("garden_upgradecan_") == 0) {
            if (!verify_user(cid, 18, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            GardenProfile profile;
            db->get_garden_profile(uid, profile);
            if (profile.watering_can_level >= 5) {
                event.reply(dpp::message().add_embed(bronx::error("already max watering can level (5)")).set_flags(dpp::m_ephemeral));
                return;
            }
            int64_t cost = get_can_upgrade_cost(profile.watering_can_level);
            int64_t wallet = db->get_wallet(uid);
            if (wallet < cost) {
                event.reply(dpp::message().add_embed(bronx::error("insufficient balance to upgrade can (costs $" + economy::format_number(cost) + ")")).set_flags(dpp::m_ephemeral));
                return;
            }
            db->update_wallet(uid, -cost);
            db->upgrade_watering_can(uid);
            
            event.reply(dpp::ir_update_message, build_garden_message(db, uid, event.command.usr));
            return;
        }
        
        // ── 8. garden_shop_ ──
        if (cid.find("garden_shop_") == 0) {
            if (!verify_user(cid, 12, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            dpp::message msg;
            msg.add_embed(build_shop_embed(db, uid, event.command.usr));
            
            // Build buy select menu
            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("buy a seed from the shop")
                .set_id("garden_buy_select_" + std::to_string(uid));
                
            for (const auto& c : crop_types) {
                select_menu.add_select_option(
                    dpp::select_option(c.name + " seed", c.name, "cost: $" + economy::format_number(c.seed_cost))
                        .set_emoji(c.emoji)
                );
            }
            msg.add_component(dpp::component().add_component(select_menu));
            
            // Back button
            dpp::component back_row;
            back_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("back to garden")
                    .set_emoji("🏡")
                    .set_style(dpp::cos_secondary)
                    .set_id("garden_view_" + std::to_string(uid))
            );
            msg.add_component(back_row);
            
            event.reply(dpp::ir_update_message, msg);
            return;
        }
        
        // ── 9. garden_inventory_ ──
        if (cid.find("garden_inventory_") == 0) {
            if (!verify_user(cid, 17, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            dpp::message msg;
            msg.add_embed(build_inventory_embed(db, uid, event.command.usr));
            
            // Back button
            dpp::component back_row;
            back_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("back to garden")
                    .set_emoji("🏡")
                    .set_style(dpp::cos_secondary)
                    .set_id("garden_view_" + std::to_string(uid))
            );
            msg.add_component(back_row);
            
            event.reply(dpp::ir_update_message, msg);
            return;
        }
    });
    
    bot.on_select_click([&bot, db](const dpp::select_click_t& event) {
        const auto& cid = event.custom_id;
        uint64_t uid = event.command.usr.id;
        
        // ── 1. garden_plant_select_ ──
        if (cid.find("garden_plant_select_") == 0) {
            if (!verify_user(cid, 20, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            std::string remainder = cid.substr(20);
            size_t next_under = remainder.find('_');
            if (next_under == std::string::npos) return;
            
            int plot_idx = 0;
            try { plot_idx = std::stoi(remainder.substr(next_under + 1)); } catch (...) { return; }
            
            std::string crop_name = event.values[0];
            const auto* type = find_crop_type(crop_name);
            if (!type) return;
            
            GardenProfile profile;
            db->get_garden_profile(uid, profile);
            
            for (const auto& crop : profile.crops) {
                if (crop.plot_index == plot_idx) {
                    event.reply(dpp::message().add_embed(bronx::error("plot occupied")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }
            
            auto inv = db->get_garden_inventory(uid);
            std::string inv_item = type->name + " seed";
            auto it = inv.find(inv_item);
            if (it == inv.end() || it->second <= 0) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have that seed")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            db->add_garden_inventory(uid, inv_item, -1);
            db->plant_crop(uid, plot_idx, type->name);
            
            event.reply(dpp::ir_update_message, build_garden_message(db, uid, event.command.usr));
            return;
        }
        
        // ── 2. garden_buy_select_ ──
        if (cid.find("garden_buy_select_") == 0) {
            if (!verify_user(cid, 18, uid)) {
                event.reply(dpp::message().add_embed(bronx::error("this is not your garden!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            std::string crop_name = event.values[0];
            const auto* type = find_crop_type(crop_name);
            if (!type) return;
            
            int64_t total_cost = type->seed_cost;
            int64_t wallet = db->get_wallet(uid);
            if (wallet < total_cost) {
                event.reply(dpp::message().add_embed(bronx::error("insufficient balance (costs $" + economy::format_number(total_cost) + ")")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            db->update_wallet(uid, -total_cost);
            db->add_garden_inventory(uid, type->name + " seed", 1);
            
            // Re-render shop
            dpp::message msg;
            msg.add_embed(build_shop_embed(db, uid, event.command.usr));
            
            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("buy a seed from the shop")
                .set_id("garden_buy_select_" + std::to_string(uid));
                
            for (const auto& c : crop_types) {
                select_menu.add_select_option(
                    dpp::select_option(c.name + " seed", c.name, "cost: $" + economy::format_number(c.seed_cost))
                        .set_emoji(c.emoji)
                );
            }
            msg.add_component(dpp::component().add_component(select_menu));
            
            dpp::component back_row;
            back_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("back to garden")
                    .set_emoji("🏡")
                    .set_style(dpp::cos_secondary)
                    .set_id("garden_view_" + std::to_string(uid))
            );
            msg.add_component(back_row);
            
            event.reply(dpp::ir_update_message, msg);
            return;
        }
    });
}

} // namespace passive
} // namespace commands
