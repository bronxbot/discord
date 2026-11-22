#include "commands/utility/bugreport.h"
#include "commands/utility/suggestion.h"
#include "commands/utility/poll.h"
#include "commands/utility/prefix.h"
#include "commands/quiet_moderation/url_guard.h"
#include "commands/quiet_moderation/reaction_filter.h"
#include "commands/moderation/mod_parent.h"
#include "commands/utility/autorole.h"
#include "commands/utility/giveaways.h"
#include "commands/games/tictactoe.h"
#include "commands/games/heist.h"
#include "commands/games/react.h"
#include "commands/games/blacktea.h"
#include "commands/games/whitetea.h"
#include "commands/games/trivia.h"
#include "commands/gambling/gambling_helpers.h"
#include "commands/gambling/russian_roulette.h"
#include "commands/gambling/roulette.h"
#include "commands/gambling/minesweeper.h"
#include "commands/gambling/hilo.h"
#include "commands/gambling/crash.h"
#include "commands/gambling/gamble_parent.h"
#include "commands/utility/privacy.h"
#include "commands/utility/status.h"
#include "commands/crafting/craft.h"
#include "commands/global_boss_raid.h"
#include "commands/passive/passive_parent.h"
#include "commands/economy/bank.h"
#include "commands/economy/rob.h"
#include "commands/economy/use.h"
#include "commands/mining/mine.h"
#include "commands/mining/inventory.h"
#include "commands/social/heist.h"
#include "commands/economy/money_parent.h"
#include "commands/patch.h"
#include "commands/daily_challenges/streaks.h"
#include "commands/leveling/xp_handler.h"
#include <mutex>
#include <shared_mutex>

namespace commands {

// Global Raid State
std::mutex g_raid_mutex;
std::map<uint64_t, RaidSession> g_raid_sessions;

// Passive confirmation state
std::map<uint64_t, PendingPassiveConfirm> pending_passive_confirms;
std::mutex passive_confirm_mutex;

namespace economy {
// Active bank sessions
std::map<uint64_t, std::pair<uint64_t, uint64_t>> active_bank_sessions;
std::mutex bank_sessions_mutex;
} // namespace economy

namespace utility {
// Database pointers




// Cooldowns
std::map<uint64_t, std::chrono::system_clock::time_point> last_bugreport_time;
std::mutex last_bugreport_mutex;

std::map<uint64_t, std::chrono::system_clock::time_point> last_suggestion_time;
std::mutex last_suggestion_mutex;

// Polls
std::map<uint64_t, PollData> active_polls;
std::shared_mutex active_polls_mutex;

// Autorole
std::map<uint64_t, std::vector<uint64_t>> autorole_cache;
std::mutex autorole_mutex;

// Giveaways
GiveawayCache giveaway_cache;
std::shared_mutex giveaway_cache_mutex;

// Privacy
std::map<uint64_t, std::chrono::steady_clock::time_point> pending_optout_confirmation_;
std::mutex optout_confirm_mutex_;

// Status Page
std::unordered_map<uint64_t, StatusPageState> modules_states;
std::unordered_map<uint64_t, StatusPageState> commands_states;
std::mutex status_states_mutex;
} // namespace utility

namespace quiet_moderation {
// URL Guard
std::map<dpp::snowflake, URLGuardConfig> guild_url_guards;
std::shared_mutex guild_url_guards_mutex;
std::map<dpp::snowflake, std::map<dpp::snowflake, int>> url_guard_violations;
std::mutex url_guard_violations_mutex;

// Reaction Filter
std::map<dpp::snowflake, ReactionFilterConfig> guild_reaction_filters;
std::shared_mutex guild_reaction_filters_mutex;
std::map<dpp::snowflake, std::map<dpp::snowflake, int>> user_violations;
std::mutex user_violations_mutex;
} // namespace quiet_moderation

namespace moderation {
// Command Cache
std::unordered_map<std::string, Command*> g_mod_commands;
std::shared_mutex g_mod_commands_mutex;
} // namespace moderation

namespace games {
// TicTacToe
std::map<uint64_t, TicTacToeGame> active_tictactoe_games;
std::shared_mutex tictactoe_games_mutex;
uint64_t next_game_id = 1;

// Heist
std::mutex g_heist_mutex;
std::map<uint64_t, HeistSession> g_heist_sessions;

// React Game
std::map<uint64_t, ReactGame> active_react_games;
std::shared_mutex react_games_mutex;
uint64_t next_react_game_id = 1;

// Black Tea
std::map<uint64_t, BlackTeaGame> active_blacktea_games;
std::shared_mutex blacktea_games_mutex;

// White Tea
std::map<uint64_t, WhiteTeaGame> active_whitetea_games;
std::shared_mutex whitetea_games_mutex;

// Trivia
std::map<uint64_t, TriviaLobby> active_trivia_lobbies;
std::shared_mutex trivia_mutex;
} // namespace games

namespace gambling {
// Frogger
std::map<uint64_t, FroggerGame> active_frogger_games;
std::shared_mutex frogger_mutex;

// Russian Roulette
std::map<uint64_t, RussianRouletteGame> active_russian_roulette_games;
std::shared_mutex russian_roulette_mutex;

// Roulette
std::map<uint64_t, RouletteGame> active_roulette_games;
std::shared_mutex roulette_mutex;
std::map<uint64_t, std::pair<std::vector<PlayerResult>, int>> roulette_pagination_data;
std::shared_mutex roulette_pagination_mutex;
std::map<uint64_t, std::chrono::steady_clock::time_point> roulette_last_edit_time;
std::map<uint64_t, bool> roulette_edit_pending;

// Minesweeper
std::map<uint64_t, MinesweeperGame> active_minesweeper_games;
std::mutex minesweeper_games_mutex;

// Hilo
std::map<uint64_t, HiloGame> active_hilo_games;
std::mutex hilo_games_mutex;

// Crash
std::map<uint64_t, CrashGame> active_crash_games;
std::mutex crash_mutex;

// Command Cache
std::unordered_map<std::string, Command*> g_game_commands;
std::shared_mutex g_game_commands_mutex;
} // namespace gambling

namespace crafting {
// Crafting sessions
std::unordered_map<uint64_t, CraftSession> active_sessions;
std::mutex session_mutex;
} // namespace crafting

namespace passive {
// Passive command cache
std::unordered_map<std::string, Command*> g_passive_commands;
std::shared_mutex g_passive_commands_mutex;
} // namespace passive

namespace daily_challenges {
bool g_challenge_tables_created = false;
std::mutex g_challenge_mutex;
} // namespace daily_challenges

namespace fishing {
std::map<std::pair<uint64_t, std::string>, std::chrono::steady_clock::time_point> af_dm_throttle;
std::mutex af_dm_mutex;
dpp::cluster* af_bot_ptr = nullptr;
std::unordered_map<std::string, Command*> g_fishing_commands;
} // namespace fishing

namespace economy {
std::unordered_map<std::string, Command*> g_money_commands;
} // namespace economy

namespace mining {
std::unordered_map<uint64_t, MiningSession> mining_sessions;
std::mutex mining_mutex;
std::unordered_map<uint64_t, MineInvState> minv_states;
std::mutex minv_mutex;
} // namespace mining

namespace social {
std::mutex g_heist_mutex;
std::map<uint64_t, HeistSession> g_heist_sessions;
bool g_heist_tables_created = false;
} // namespace social

namespace daily_challenges {
std::mutex g_streak_mutex;
bool g_streak_tables_created = false;
} // namespace daily_challenges

std::map<uint64_t, PatchState> patch_states;
std::mutex patch_states_mutex;

namespace use_item {
std::map<uint64_t, std::vector<ActiveBoost>> boosts;
std::mutex mtx;
} // namespace use_item

} // namespace commands

namespace leveling {
std::unordered_map<std::string, std::chrono::system_clock::time_point> xp_cooldown_cache;
std::mutex xp_cooldown_mutex;
std::unordered_map<uint64_t, CachedLevelingConfig> leveling_config_cache;
std::mutex leveling_config_mutex;
std::unordered_map<std::string, CachedBlacklistResult> xp_blacklist_result_cache;
std::mutex xp_blacklist_result_mutex;
bronx::xp::XpBatchWriter* g_xp_batch_writer = nullptr;
} // namespace leveling

