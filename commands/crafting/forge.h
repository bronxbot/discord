#pragma once
#include "crafting_helpers.h"
#include "../economy/helpers.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <nlohmann/json.hpp>

using namespace bronx::db;

namespace commands {
namespace crafting {

struct ForgeRecipe {
    std::string id;
    std::string name;
    std::string emoji;
    std::string description;
    std::string utility_boost;
    
    // Ingredients
    std::string raw_gem_name;
    std::string ore_name;
    int ore_qty;
    std::string fish_name;
    int fish_qty;
    int64_t coin_cost;
};

inline const std::vector<ForgeRecipe>& get_forge_recipes() {
    static const std::vector<ForgeRecipe> recipes = {
        {
            "ruby", "forged ruby", "❤️",
            "forged with a ruby, iron ore, and shrimp.",
            "pickaxe: +5% critical multiplier\nfishing rod: +5% critical multiplier",
            "ruby", "iron ore", 10, "shrimp", 3, 2000
        },
        {
            "sapphire", "forged sapphire", "💙",
            "forged with a sapphire, cobalt ore, and guppy.",
            "pickaxe: -10% cooldown reduction\nfishing rod: -10% cooldown reduction",
            "sapphire", "cobalt ore", 10, "guppy", 3, 2000
        },
        {
            "emerald", "forged emerald", "💚",
            "forged with an emerald, copper ore, and clownfish.",
            "pickaxe: +8% sell value multiplier\nfishing rod: +8% sell value multiplier",
            "emerald", "copper ore", 10, "clownfish", 3, 2000
        },
        {
            "diamond", "forged diamond", "💎",
            "forged with a diamond, quartz, and goldfish.",
            "pickaxe: +6% double loot chance\nfishing rod: +6% double catch chance",
            "diamond", "quartz", 10, "goldfish", 3, 5000
        },
        {
            "topaz", "forged topaz", "🟡",
            "forged with a topaz, sandstone, and trout.",
            "pickaxe: +10% luck bonus\nfishing rod: +10% luck bonus",
            "topaz", "sandstone", 15, "trout", 3, 1500
        },
        {
            "amethyst", "forged amethyst", "🟣",
            "forged with an amethyst, mica, and tadpole.",
            "pickaxe: -5% cooldown / +5% luck\nfishing rod: -5% cooldown / +5% luck",
            "amethyst", "mica", 15, "tadpole", 3, 1500
        }
    };
    return recipes;
}

inline std::vector<std::string> find_collectible_by_name(const std::vector<InventoryItem>& inventory, const std::string& target_name, int required_qty) {
    std::vector<std::string> item_ids;
    int found_qty = 0;
    for (const auto& item : inventory) {
        if (item.item_type == "collectible" && !item.metadata.empty()) {
            if (matches_ingredient_by_name(item.metadata, target_name)) {
                if (item.metadata.find("\"locked\":true") != std::string::npos) continue;
                item_ids.push_back(item.item_id);
                found_qty += item.quantity;
                if (found_qty >= required_qty) break;
            }
        }
    }
    if (found_qty >= required_qty) {
        return item_ids;
    }
    return {};
}

inline void consume_collectible(Database* db, uint64_t user_id, const std::vector<std::string>& matching_ids, int required_qty) {
    int remaining = required_qty;
    for (const auto& item_id : matching_ids) {
        if (remaining <= 0) break;
        int qty = db->get_item_quantity(user_id, item_id);
        int to_remove = std::min(qty, remaining);
        db->remove_item(user_id, item_id, to_remove);
        remaining -= to_remove;
    }
}

inline dpp::embed build_forge_recipes_embed(Database* db, uint64_t user_id) {
    auto embed = bronx::create_embed("", bronx::COLOR_INFO);
    embed.set_title("forging recipes");
    embed.set_description(
        "forge raw gems with mined ores and caught fish to create socketable gems.\n"
        "sockets provide passive utility boosts to your pickaxes and fishing rods.\n\n"
        "**how to forge:**\n"
        "use `forge gem <gem_name>` to craft a forged gem.\n"
        "use `forge slot <pickaxe|rod>` to add a socket slot (max 3 slots).\n"
        "use `forge socket <pickaxe|rod> <gem_name>` to insert a gem.\n"
        "use `forge unsocket <pickaxe|rod> <gem_name>` to remove a gem.\n"
    );
    
    for (const auto& recipe : get_forge_recipes()) {
        std::string name = recipe.emoji + " " + recipe.name;
        std::string value = 
            "**ingredients:** 1x " + recipe.raw_gem_name + ", " + 
            std::to_string(recipe.ore_qty) + "x " + recipe.ore_name + ", " + 
            std::to_string(recipe.fish_qty) + "x " + recipe.fish_name + ", $" + 
            std::to_string(recipe.coin_cost) + "\n" +
            "**utility boost:**\n" + recipe.utility_boost + "\n" +
            "`forge gem " + recipe.id + "`";
        embed.add_field(name, value, false);
    }
    
    embed.add_field(
        "⚙️ tool socket slots",
        "add socket slots to your pickaxe or fishing rod (max 3 slots):\n"
        "• **1st slot:** $10,000 + 5x obsidian shard\n"
        "• **2nd slot:** $25,000 + 5x obsidian shard + 2x platinum ore\n"
        "• **3rd slot:** $50,000 + 5x obsidian shard + 5x platinum ore + 2x meteorite shard\n"
        "`forge slot pickaxe` or `forge slot rod`",
        false
    );
    
    return embed;
}

inline dpp::message execute_forge_gem(Database* db, uint64_t user_id, const std::string& gem_name) {
    std::string lower_gem = gem_name;
    std::transform(lower_gem.begin(), lower_gem.end(), lower_gem.begin(), ::tolower);
    
    const ForgeRecipe* found_recipe = nullptr;
    for (const auto& r : get_forge_recipes()) {
        if (r.id == lower_gem) {
            found_recipe = &r;
            break;
        }
    }
    
    if (!found_recipe) {
        return dpp::message().add_embed(bronx::error("recipe not found. try `forge recipes` to view valid options."));
    }
    
    int64_t wallet = db->get_wallet(user_id);
    if (wallet < found_recipe->coin_cost) {
        return dpp::message().add_embed(bronx::error("you don't have enough coins. you need $" + std::to_string(found_recipe->coin_cost) + "."));
    }
    
    auto inventory = db->get_inventory(user_id);
    auto raw_gem_ids = find_collectible_by_name(inventory, found_recipe->raw_gem_name, 1);
    if (raw_gem_ids.empty()) {
        return dpp::message().add_embed(bronx::error("missing raw gem: " + found_recipe->raw_gem_name));
    }
    
    auto ore_ids = find_collectible_by_name(inventory, found_recipe->ore_name, found_recipe->ore_qty);
    if (ore_ids.empty()) {
        return dpp::message().add_embed(bronx::error("missing ore: " + std::to_string(found_recipe->ore_qty) + "x " + found_recipe->ore_name));
    }
    
    auto fish_ids = find_collectible_by_name(inventory, found_recipe->fish_name, found_recipe->fish_qty);
    if (fish_ids.empty()) {
        return dpp::message().add_embed(bronx::error("missing fish: " + std::to_string(found_recipe->fish_qty) + "x " + found_recipe->fish_name));
    }
    
    // Deduct coins & consume items
    db->update_wallet(user_id, -found_recipe->coin_cost);
    consume_collectible(db, user_id, raw_gem_ids, 1);
    consume_collectible(db, user_id, ore_ids, found_recipe->ore_qty);
    consume_collectible(db, user_id, fish_ids, found_recipe->fish_qty);
    
    // Add forged gem to inventory
    db->add_item(user_id, "forged_" + found_recipe->id, "crafted", 1, "{\"name\":\"forged " + found_recipe->id + "\"}");
    
    return dpp::message().add_embed(bronx::success("forged 1x " + found_recipe->emoji + " " + found_recipe->name + "!"));
}

inline dpp::message execute_forge_slot(Database* db, uint64_t user_id, const std::string& tool_type) {
    std::string tool = tool_type;
    std::transform(tool.begin(), tool.end(), tool.begin(), ::tolower);
    
    if (tool != "pickaxe" && tool != "rod" && tool != "fishing rod") {
        return dpp::message().add_embed(bronx::error("invalid tool type. choose `pickaxe` or `rod`."));
    }
    
    auto inventory = db->get_inventory(user_id);
    InventoryItem target_item;
    bool found = false;
    
    if (tool == "pickaxe") {
        for (const auto& item : inventory) {
            if (item.item_id.rfind("pickaxe_", 0) == 0 && item.level > target_item.level) {
                target_item = item;
                found = true;
            }
        }
    } else {
        auto gear = db->get_active_fishing_gear(user_id);
        if (!gear.first.empty()) {
            for (const auto& item : inventory) {
                if (item.item_id == gear.first) {
                    target_item = item;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            for (const auto& item : inventory) {
                if (item.item_id.rfind("rod_", 0) == 0 && item.level > target_item.level) {
                    target_item = item;
                    found = true;
                }
            }
        }
    }
    
    if (!found) {
        return dpp::message().add_embed(bronx::error("no " + tool + " found in your inventory. buy one first!"));
    }
    
    using json = nlohmann::json;
    json meta;
    if (!target_item.metadata.empty()) {
        try {
            meta = json::parse(target_item.metadata);
        } catch (...) {}
    }
    
    int current_slots = meta.value("socket_slots", 0);
    if (current_slots >= 3) {
        return dpp::message().add_embed(bronx::error("your tool already has the maximum of 3 socket slots."));
    }
    
    int64_t coin_cost = 0;
    std::vector<std::pair<std::string, int>> required_collectibles;
    if (current_slots == 0) {
        coin_cost = 10000;
        required_collectibles.push_back({"obsidian shard", 5});
    } else if (current_slots == 1) {
        coin_cost = 25000;
        required_collectibles.push_back({"obsidian shard", 5});
        required_collectibles.push_back({"platinum ore", 2});
    } else if (current_slots == 2) {
        coin_cost = 50000;
        required_collectibles.push_back({"obsidian shard", 5});
        required_collectibles.push_back({"platinum ore", 5});
        required_collectibles.push_back({"meteorite shard", 2});
    }
    
    int64_t wallet = db->get_wallet(user_id);
    if (wallet < coin_cost) {
        return dpp::message().add_embed(bronx::error("you need $" + std::to_string(coin_cost) + " to add this slot."));
    }
    
    std::vector<std::pair<std::vector<std::string>, int>> matching_collectibles;
    for (const auto& req : required_collectibles) {
        auto ids = find_collectible_by_name(inventory, req.first, req.second);
        if (ids.empty()) {
            return dpp::message().add_embed(bronx::error("missing " + std::to_string(req.second) + "x " + req.first + " to add a slot."));
        }
        matching_collectibles.push_back({ids, req.second});
    }
    
    // Deduct coins & consume items
    db->update_wallet(user_id, -coin_cost);
    for (const auto& match : matching_collectibles) {
        consume_collectible(db, user_id, match.first, match.second);
    }
    
    meta["socket_slots"] = current_slots + 1;
    db->update_item_metadata(user_id, target_item.item_id, meta.dump());
    
    return dpp::message().add_embed(bronx::success("added socket slot " + std::to_string(current_slots + 1) + " to your " + target_item.item_id + "!"));
}

inline dpp::message execute_forge_socket(Database* db, uint64_t user_id, const std::string& tool_type, const std::string& gem_name) {
    std::string tool = tool_type;
    std::transform(tool.begin(), tool.end(), tool.begin(), ::tolower);
    std::string gem = gem_name;
    std::transform(gem.begin(), gem.end(), gem.begin(), ::tolower);
    
    if (tool != "pickaxe" && tool != "rod" && tool != "fishing rod") {
        return dpp::message().add_embed(bronx::error("invalid tool type. choose `pickaxe` or `rod`."));
    }
    
    bool valid_gem = false;
    for (const auto& r : get_forge_recipes()) {
        if (r.id == gem) {
            valid_gem = true;
            break;
        }
    }
    if (!valid_gem) {
        return dpp::message().add_embed(bronx::error("invalid gem type. choose ruby, sapphire, emerald, diamond, topaz, or amethyst."));
    }
    
    int forged_qty = db->get_item_quantity(user_id, "forged_" + gem);
    if (forged_qty <= 0) {
        return dpp::message().add_embed(bronx::error("you do not have a forged " + gem + " in your inventory. forge one first!"));
    }
    
    auto inventory = db->get_inventory(user_id);
    InventoryItem target_item;
    bool found = false;
    
    if (tool == "pickaxe") {
        for (const auto& item : inventory) {
            if (item.item_id.rfind("pickaxe_", 0) == 0 && item.level > target_item.level) {
                target_item = item;
                found = true;
            }
        }
    } else {
        auto gear = db->get_active_fishing_gear(user_id);
        if (!gear.first.empty()) {
            for (const auto& item : inventory) {
                if (item.item_id == gear.first) {
                    target_item = item;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            for (const auto& item : inventory) {
                if (item.item_id.rfind("rod_", 0) == 0 && item.level > target_item.level) {
                    target_item = item;
                    found = true;
                }
            }
        }
    }
    
    if (!found) {
        return dpp::message().add_embed(bronx::error("no " + tool + " found in your inventory."));
    }
    
    using json = nlohmann::json;
    json meta;
    if (!target_item.metadata.empty()) {
        try {
            meta = json::parse(target_item.metadata);
        } catch (...) {}
    }
    
    int slots = meta.value("socket_slots", 0);
    std::vector<std::string> sockets;
    if (meta.contains("sockets") && meta["sockets"].is_array()) {
        for (const auto& el : meta["sockets"]) {
            if (el.is_string()) {
                sockets.push_back(el.get<std::string>());
            }
        }
    }
    
    if ((int)sockets.size() >= slots) {
        return dpp::message().add_embed(bronx::error("no empty socket slots available. add a slot with `forge slot` or unsocket a gem."));
    }
    
    // Add socket
    sockets.push_back(gem);
    meta["sockets"] = sockets;
    
    db->remove_item(user_id, "forged_" + gem, 1);
    db->update_item_metadata(user_id, target_item.item_id, meta.dump());
    
    return dpp::message().add_embed(bronx::success("successfully socketed forged " + gem + " into your " + target_item.item_id + "!"));
}

inline dpp::message execute_forge_unsocket(Database* db, uint64_t user_id, const std::string& tool_type, const std::string& gem_name) {
    std::string tool = tool_type;
    std::transform(tool.begin(), tool.end(), tool.begin(), ::tolower);
    std::string gem = gem_name;
    std::transform(gem.begin(), gem.end(), gem.begin(), ::tolower);
    
    if (tool != "pickaxe" && tool != "rod" && tool != "fishing rod") {
        return dpp::message().add_embed(bronx::error("invalid tool type. choose `pickaxe` or `rod`."));
    }
    
    auto inventory = db->get_inventory(user_id);
    InventoryItem target_item;
    bool found = false;
    
    if (tool == "pickaxe") {
        for (const auto& item : inventory) {
            if (item.item_id.rfind("pickaxe_", 0) == 0 && item.level > target_item.level) {
                target_item = item;
                found = true;
            }
        }
    } else {
        auto gear = db->get_active_fishing_gear(user_id);
        if (!gear.first.empty()) {
            for (const auto& item : inventory) {
                if (item.item_id == gear.first) {
                    target_item = item;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            for (const auto& item : inventory) {
                if (item.item_id.rfind("rod_", 0) == 0 && item.level > target_item.level) {
                    target_item = item;
                    found = true;
                }
            }
        }
    }
    
    if (!found) {
        return dpp::message().add_embed(bronx::error("no " + tool + " found in your inventory."));
    }
    
    using json = nlohmann::json;
    json meta;
    if (!target_item.metadata.empty()) {
        try {
            meta = json::parse(target_item.metadata);
        } catch (...) {}
    }
    
    std::vector<std::string> sockets;
    if (meta.contains("sockets") && meta["sockets"].is_array()) {
        for (const auto& el : meta["sockets"]) {
            if (el.is_string()) {
                sockets.push_back(el.get<std::string>());
            }
        }
    }
    
    auto it = std::find(sockets.begin(), sockets.end(), gem);
    if (it == sockets.end()) {
        return dpp::message().add_embed(bronx::error("there is no " + gem + " socketed in your " + tool + "."));
    }
    
    sockets.erase(it);
    meta["sockets"] = sockets;
    
    db->add_item(user_id, "forged_" + gem, "crafted", 1, "{\"name\":\"forged " + gem + "\"}");
    db->update_item_metadata(user_id, target_item.item_id, meta.dump());
    
    return dpp::message().add_embed(bronx::success("successfully unsocketed forged " + gem + " from your " + target_item.item_id + "!"));
}

inline Command* create_forge_command(Database* db) {
    Command* cmd = new Command(
        "forge", "forge items, slots, and sockets on tools", "economy",
        {"forging", "socket", "sockets", "socketing"}, true,
        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);
            
            if (args.empty()) {
                auto embed = build_forge_recipes_embed(db, user_id);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            std::string subcommand = args[0];
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);
            
            if (subcommand == "recipes") {
                auto embed = build_forge_recipes_embed(db, user_id);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (subcommand == "gem") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `forge gem <gem_name>`"));
                    return;
                }
                auto msg = execute_forge_gem(db, user_id, args[1]);
                bronx::send_message(bot, event, msg);
                return;
            }
            
            if (subcommand == "slot") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `forge slot <pickaxe|rod>`"));
                    return;
                }
                auto msg = execute_forge_slot(db, user_id, args[1]);
                bronx::send_message(bot, event, msg);
                return;
            }
            
            if (subcommand == "socket") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: `forge socket <pickaxe|rod> <gem_name>`"));
                    return;
                }
                auto msg = execute_forge_socket(db, user_id, args[1], args[2]);
                bronx::send_message(bot, event, msg);
                return;
            }
            
            if (subcommand == "unsocket") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: `forge unsocket <pickaxe|rod> <gem_name>`"));
                    return;
                }
                auto msg = execute_forge_unsocket(db, user_id, args[1], args[2]);
                bronx::send_message(bot, event, msg);
                return;
            }
            
            bronx::send_message(bot, event, bronx::error("unknown subcommand. use `forge recipes` to view options."));
        },
        // SLASH HANDLER
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.usr.id;
            db->ensure_user_exists(user_id);
            
            std::string action = "recipes";
            try {
                action = std::get<std::string>(event.get_parameter("action"));
            } catch (...) {}
            
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            
            if (action == "recipes") {
                auto embed = build_forge_recipes_embed(db, user_id);
                bronx::add_invoker_footer(embed, event.command.usr);
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            if (action == "gem") {
                std::string target;
                try {
                    target = std::get<std::string>(event.get_parameter("target"));
                } catch (...) {}
                
                if (target.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("you must specify a gem name (e.g. ruby, sapphire).")));
                    return;
                }
                
                auto msg = execute_forge_gem(db, user_id, target);
                event.reply(msg);
                return;
            }
            
            if (action == "slot") {
                std::string target;
                try {
                    target = std::get<std::string>(event.get_parameter("target"));
                } catch (...) {}
                
                if (target.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("you must specify a tool type (pickaxe/rod).")));
                    return;
                }
                
                auto msg = execute_forge_slot(db, user_id, target);
                event.reply(msg);
                return;
            }
            
            if (action == "socket") {
                std::string target;
                std::string gem;
                try {
                    target = std::get<std::string>(event.get_parameter("target"));
                } catch (...) {}
                try {
                    gem = std::get<std::string>(event.get_parameter("gem"));
                } catch (...) {}
                
                if (target.empty() || gem.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("you must specify both a target (pickaxe/rod) and a gem name.")));
                    return;
                }
                
                auto msg = execute_forge_socket(db, user_id, target, gem);
                event.reply(msg);
                return;
            }
            
            if (action == "unsocket") {
                std::string target;
                std::string gem;
                try {
                    target = std::get<std::string>(event.get_parameter("target"));
                } catch (...) {}
                try {
                    gem = std::get<std::string>(event.get_parameter("gem"));
                } catch (...) {}
                
                if (target.empty() || gem.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("you must specify both a target (pickaxe/rod) and a gem name.")));
                    return;
                }
                
                auto msg = execute_forge_unsocket(db, user_id, target, gem);
                event.reply(msg);
                return;
            }
            
            event.reply(dpp::message().add_embed(bronx::error("unknown action.")));
        },
        // OPTIONS
        {
            dpp::command_option(dpp::co_string, "action", "recipes, gem, slot, socket, or unsocket", true)
                .add_choice(dpp::command_option_choice("recipes", std::string("recipes")))
                .add_choice(dpp::command_option_choice("gem", std::string("gem")))
                .add_choice(dpp::command_option_choice("slot", std::string("slot")))
                .add_choice(dpp::command_option_choice("socket", std::string("socket")))
                .add_choice(dpp::command_option_choice("unsocket", std::string("unsocket"))),
            dpp::command_option(dpp::co_string, "target", "gem name or tool type (pickaxe/rod)", false),
            dpp::command_option(dpp::co_string, "gem", "gem to socket/unsocket (ruby, sapphire, etc)", false)
        }
    );
    
    cmd->extended_description = "forge powerful custom gems and socket them into your mining and fishing gear for passive upgrades.";
    cmd->detailed_usage = "forge [recipes | gem <gem_name> | slot <pickaxe|rod> | socket <pickaxe|rod> <gem_name> | unsocket <pickaxe|rod> <gem_name>]";
    cmd->subcommands = {
        {"forge recipes", "view available forging recipes and guides"},
        {"forge gem <gem_name>", "forge a raw gem with ores and caught fish into a socketable gem"},
        {"forge slot <pickaxe|rod>", "adds a socket slot to your pickaxe or fishing rod (max 3)"},
        {"forge socket <pickaxe|rod> <gem_name>", "insert a forged gem into a tool's slot"},
        {"forge unsocket <pickaxe|rod> <gem_name>", "removes a gem from a tool's slot, returning it to inventory"}
    };
    cmd->examples = {"forge recipes", "forge gem ruby", "forge slot pickaxe", "forge socket pickaxe ruby"};
    
    return cmd;
}

} // namespace crafting
} // namespace commands
