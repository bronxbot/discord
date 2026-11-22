#include <dpp/dpp.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <functional>

#include "performance/optimized_command_handler.h"
#include "commands/utility.h"
#include "commands/utility/autopurge.h"
#include "commands/fun.h"
#include "commands/games.h"
#include "commands/endgame.h"
#include "commands/help_new.h"
#include "commands/help_data.h"
#include "commands/guide.h"
#include "commands/economy.h"
#include "commands/leaderboard.h"
#include "commands/leveling.h"
#include "commands/leveling/xpblacklist.h"
#include "commands/patch.h"
#include "commands/owner.h"
#include "commands/owner/log_beta.h"
#include "commands/owner/feature_command.h"
#include "commands/gambling.h"
#include "commands/moderation.h"
#include "commands/moderation_commands.h"
#include "commands/moderation/logconfig.h"
#include "commands/mining.h"
#include "commands/global_boss.h"
#include "commands/global_boss_raid.h"
#include "commands/passive.h"
#include "commands/social.h"
#include "commands/world_events.h"
#include "commands/server_economy.h"
#include "commands/stats_cmd.h"

#include <atomic>

// Define global atomic states that are declared in event_handlers / other files
std::atomic<bool> g_initial_load_complete{false};
std::atomic<bool> g_running{true};

// Define global stats
namespace commands {
    BotStats global_stats;
}

int main() {
    std::cout << "Starting offline slash command validator..." << std::endl;

    OptimizedCommandHandler cmd_handler("b.", nullptr);

    // Register all commands (just like in main.cpp, passing nullptr for db/cache)
    for (auto* cmd : commands::get_utility_commands(&cmd_handler, nullptr, nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_fun_commands()) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_games_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_endgame_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_moderation_commands()) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_manual_moderation_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_automod_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_economy_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_leaderboard_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_leveling_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_xpblacklist_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_patch_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_owner_commands(&cmd_handler, nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_mining_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::global_boss::get_global_boss_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_boss_raid_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_passive_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_social_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::server_economy::get_server_economy_commands(nullptr)) cmd_handler.register_command(cmd);
    for (auto* cmd : commands::get_gambling_commands(nullptr)) cmd_handler.register_command(cmd);
    
    cmd_handler.register_command(commands::world_events::get_event_command(nullptr));
    cmd_handler.register_command(commands::create_help_command(&cmd_handler));
    cmd_handler.register_command(commands::create_guide_command(nullptr));
    cmd_handler.register_command(commands::gambling::get_russian_roulette_command(nullptr));
    cmd_handler.register_command(commands::moderation::get_log_command(nullptr));
    cmd_handler.register_command(commands::owner::get_log_beta_command(nullptr));
    cmd_handler.register_command(commands::get_feature_command(nullptr));

    for (auto* cmd : commands::get_stats_commands(nullptr)) cmd_handler.register_command(cmd);
    commands::populate_extended_help(&cmd_handler);

    auto slash_commands = cmd_handler.get_slash_commands();
    std::cout << "Retrieved " << slash_commands.size() << " slash commands for validation." << std::endl;

    std::vector<dpp::slashcommand> commands_to_register;
    for (auto* cmd : slash_commands) {
        dpp::slashcommand slash_cmd(cmd->name, cmd->description, 0);
        for (const auto& opt : cmd->options) {
            slash_cmd.add_option(opt);
        }
        commands_to_register.push_back(slash_cmd);
    }

    int total_errors = 0;
    std::map<std::string, size_t> name_index;
    for (size_t ci = 0; ci < commands_to_register.size(); ci++) {
        const auto& sc = commands_to_register[ci];
        if (sc.description.size() > 100) {
            std::cout << "[WARN] description too long for '" << sc.name << "' (" << sc.description.size() << " chars, max 100)" << std::endl;
        }
        auto it = name_index.find(sc.name);
        if (it != name_index.end()) {
            std::cout << "[WARN] duplicate name '" << sc.name << "' at index " << it->second << " and " << ci << std::endl;
        }
        name_index[sc.name] = ci;

        bool has_subcommand = false;
        bool has_normal_option = false;
        for (const auto& opt : sc.options) {
            if (opt.type == dpp::co_sub_command || opt.type == dpp::co_sub_command_group) {
                has_subcommand = true;
            } else {
                has_normal_option = true;
            }
        }

        if (has_subcommand && has_normal_option) {
            std::cout << "[ERROR] command '" << sc.name << "' has both subcommands and regular options at top level! Discord will reject this." << std::endl;
            total_errors++;
        }

        if (sc.options.size() > 25) {
            std::cout << "[ERROR] command '" << sc.name << "' has " << sc.options.size() << " options at top-level (Discord max is 25)!" << std::endl;
            total_errors++;
        }

        std::function<void(const std::string&, const dpp::command_option&, int)> validate_option;
        validate_option = [&validate_option, &sc, &total_errors](const std::string& path, const dpp::command_option& opt, int depth) {
            if (opt.options.size() > 25) {
                std::cout << "[ERROR] command '" << sc.name << "' option '" << path << "' has " << opt.options.size() << " options (Discord max is 25)!" << std::endl;
                total_errors++;
            }
            if (opt.type < 1 || opt.type > 11) {
                std::cout << "[ERROR] command '" << sc.name << "' option '" << path << "' has invalid type: " << (int)opt.type << std::endl;
                total_errors++;
            }

            for (const auto& child : opt.options) {
                if (opt.type == dpp::co_sub_command) {
                    if (child.type < 3 || child.type > 11) {
                        std::cout << "[ERROR] command '" << sc.name << "' subcommand option '" << path << "." << child.name << "' has invalid type: " << (int)child.type << " (must be 3-11)" << std::endl;
                        total_errors++;
                    }
                }
                if (opt.type == dpp::co_sub_command_group) {
                    if (child.type != dpp::co_sub_command) {
                        std::cout << "[ERROR] command '" << sc.name << "' subcommand group option '" << path << "." << child.name << "' has invalid type: " << (int)child.type << " (must be 1)" << std::endl;
                        total_errors++;
                    }
                }
                if (opt.type >= 3 && opt.type <= 11) {
                    std::cout << "[ERROR] command '" << sc.name << "' normal option '" << path << "' has nested child option '" << child.name << "', which is illegal!" << std::endl;
                    total_errors++;
                }
                validate_option(path + "." + child.name, child, depth + 1);
            }
        };

        for (const auto& opt : sc.options) {
            validate_option(opt.name, opt, 0);
        }
    }

    std::cout << "Validation completed. Total structural/nesting errors found: " << total_errors << std::endl;
    return total_errors == 0 ? 0 : 1;
}
