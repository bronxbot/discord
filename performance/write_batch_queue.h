#pragma once
// ---------------------------------------------------------------------------
// WriteBatchQueue — General-purpose mutation batch queue for remote DB writes
//
// Collects wallet deltas, bank deltas, inventory mutations, and stat deltas
// in a local queue and flushes them to the remote MariaDB on a background
// thread every 2 seconds.  This extends the XpBatchWriter pattern to cover
// ALL high-frequency write operations, not just XP.
//
// Combined with LocalDB for instant reads, this eliminates the blocking
// 40-100ms remote write latency from every economy command.
// ---------------------------------------------------------------------------

#include "../database/core/database.h"
#include "local_db.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <iostream>
#include "../utils/logger.h"

// Forward declaration — avoid circular include with async_stat_writer.h
namespace bronx { namespace perf { class RemoteStatsConnection; } }

namespace bronx {
namespace batch {

// Dmitry Vyukov's lock-free MPSC Queue
template <typename T>
class MPSCQueue {
private:
    struct Node {
        T data;
        std::atomic<Node*> next{nullptr};
        Node() = default;
        explicit Node(T&& val) : data(std::move(val)) {}
        explicit Node(const T& val) : data(val) {}
    };

    std::atomic<Node*> head_;
    Node* tail_;

public:
    MPSCQueue() {
        Node* stub = new Node();
        head_.store(stub, std::memory_order_relaxed);
        tail_ = stub;
    }

    ~MPSCQueue() {
        T dummy;
        while (dequeue(dummy)) {}
        delete tail_;
    }

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    void enqueue(T val) {
        Node* node = new Node(std::move(val));
        Node* prev = head_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }

    bool dequeue(T& result) {
        Node* tail = tail_;
        Node* next = tail->next.load(std::memory_order_acquire);
        if (next != nullptr) {
            result = std::move(next->data);
            tail_ = next;
            delete tail;
            return true;
        }
        return false;
    }
};

// Inventory mutation — add or remove items
struct ItemMutation {
    enum Type { ADD, REMOVE };
    Type type;
    uint64_t user_id;
    std::string item_id;
    std::string item_type;
    int quantity;
    std::string category;
    int level;
    std::string metadata;
};

struct WalletDelta {
    uint64_t user_id;
    int64_t delta;
};

struct BankDelta {
    uint64_t user_id;
    int64_t delta;
};

struct StatDelta {
    uint64_t user_id;
    std::string stat_name;
    int64_t amount;
};

class WriteBatchQueue {
public:
    explicit WriteBatchQueue(bronx::db::Database* db,
                             bronx::local::LocalDB* local_db,
                             std::chrono::milliseconds flush_interval = std::chrono::milliseconds(2000))
        : db_(db), local_db_(local_db), flush_interval_(flush_interval) {}

    ~WriteBatchQueue() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        flush_thread_ = std::thread([this] { flush_loop(); });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (flush_thread_.joinable()) flush_thread_.join();
        flush();  // final drain
    }

    // -----------------------------------------------------------------------
    // Enqueue methods — called from gateway threads, MUST be fast (<1us)
    // -----------------------------------------------------------------------

    // Wallet delta — the local balance is already updated in LocalDB
    void enqueue_wallet_delta(uint64_t user_id, int64_t delta) {
        wallet_queue_.enqueue(WalletDelta{user_id, delta});
    }

    // Bank delta
    void enqueue_bank_delta(uint64_t user_id, int64_t delta) {
        bank_queue_.enqueue(BankDelta{user_id, delta});
    }

    // Stat delta (generalizes the existing AsyncStatWriter pattern)
    void enqueue_stat_delta(uint64_t user_id, const std::string& stat_name, int64_t amount) {
        stat_queue_.enqueue(StatDelta{user_id, stat_name, amount});
    }

    // Inventory mutation (add or remove items)
    void enqueue_item_mutation(const ItemMutation& mutation) {
        item_queue_.enqueue(mutation);
    }

    // Convenience: enqueue an item add
    void enqueue_add_item(uint64_t user_id, const std::string& item_id,
                          const std::string& item_type, int quantity,
                          const std::string& category = "", int level = 1,
                          const std::string& metadata = "") {
        ItemMutation m{};
        m.type = ItemMutation::ADD;
        m.user_id = user_id;
        m.item_id = item_id;
        m.item_type = item_type;
        m.quantity = quantity;
        m.category = category;
        m.level = level;
        m.metadata = metadata;
        enqueue_item_mutation(m);
    }

    // Convenience: enqueue an item remove
    void enqueue_remove_item(uint64_t user_id, const std::string& item_id, int quantity) {
        ItemMutation m{};
        m.type = ItemMutation::REMOVE;
        m.user_id = user_id;
        m.item_id = item_id;
        m.quantity = quantity;
        enqueue_item_mutation(m);
    }    

    // Force immediate flush (shutdown / owner commands)
    void flush_now() { flush(); }

    // Set remote stats connection for Aiven dual-write (call after AsyncStatWriter is initialized)
    void set_remote_connection(bronx::perf::RemoteStatsConnection* remote) {
        remote_stats_ = remote;
    }

    // Get flush statistics
    struct FlushStats {
        std::atomic<uint64_t> total_flushes{0};
        std::atomic<uint64_t> wallet_writes{0};
        std::atomic<uint64_t> bank_writes{0};
        std::atomic<uint64_t> stat_writes{0};
        std::atomic<uint64_t> item_writes{0};
        std::atomic<uint64_t> errors{0};
    };
    const FlushStats& stats() const { return stats_; }

private:
    // Lock-free queues
    MPSCQueue<WalletDelta> wallet_queue_;
    MPSCQueue<BankDelta> bank_queue_;
    MPSCQueue<StatDelta> stat_queue_;
    MPSCQueue<ItemMutation> item_queue_;

    // ---- control ----
    bronx::db::Database* db_;
    bronx::local::LocalDB* local_db_;
    bronx::perf::RemoteStatsConnection* remote_stats_ = nullptr;  // Aiven dual-write (optional)
    std::chrono::milliseconds flush_interval_;
    std::atomic<bool> running_{false};
    std::thread flush_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    FlushStats stats_;

    void flush_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lk(cv_mutex_);
            cv_.wait_for(lk, flush_interval_, [this] { return !running_.load(); });
            if (!running_.load()) break;
            lk.unlock();
            flush();
        }
    }

    void flush() {
        std::unordered_map<uint64_t, int64_t> wallet_snap;
        std::unordered_map<uint64_t, int64_t> bank_snap;
        std::unordered_map<std::pair<uint64_t, std::string>, int64_t, bronx::db::Database::PairHash> stat_snap;
        std::vector<ItemMutation> item_snap;

        // 1. Drain all lock-free queues
        WalletDelta wd;
        while (wallet_queue_.dequeue(wd)) {
            wallet_snap[wd.user_id] += wd.delta;
        }

        BankDelta bd;
        while (bank_queue_.dequeue(bd)) {
            bank_snap[bd.user_id] += bd.delta;
        }

        StatDelta sd;
        while (stat_queue_.dequeue(sd)) {
            auto key = std::make_pair(sd.user_id, sd.stat_name);
            stat_snap[key] += sd.amount;
        }

        ItemMutation im;
        while (item_queue_.dequeue(im)) {
            item_snap.push_back(std::move(im));
        }

        // Also drain any deltas accumulated in LocalDB's pending tables
        if (local_db_) {
            auto local_wallet = local_db_->drain_wallet_deltas();
            for (auto& [uid, delta] : local_wallet) {
                wallet_snap[uid] += delta;
            }
            auto local_bank = local_db_->drain_bank_deltas();
            for (auto& [uid, delta] : local_bank) {
                bank_snap[uid] += delta;
            }
        }

        if (wallet_snap.empty() && bank_snap.empty() && stat_snap.empty() && item_snap.empty()) {
            return;
        }

        stats_.total_flushes++;

        // 2. Gather unique user IDs to ensure they exist first (prevent FK constraint violations)
        std::vector<uint64_t> unique_user_ids;
        std::unordered_set<uint64_t> seen_users;
        for (const auto& [uid, _] : wallet_snap) seen_users.insert(uid);
        for (const auto& [uid, _] : bank_snap) seen_users.insert(uid);
        for (const auto& [key, _] : stat_snap) seen_users.insert(key.first);
        for (const auto& m : item_snap) seen_users.insert(m.user_id);

        for (uint64_t uid : seen_users) {
            unique_user_ids.push_back(uid);
        }

        try {
            db_->ensure_users_exist_bulk(unique_user_ids);
        } catch (const std::exception& e) {
            stats_.errors++;
            bronx::logger::error("write_batch", "bulk ensure users exist failed: " + std::string(e.what()));
        }

        // 3. Coalesce wallet and bank deltas into a single map of pair deltas
        std::unordered_map<uint64_t, std::pair<int64_t, int64_t>> balance_deltas;
        for (const auto& [uid, delta] : wallet_snap) {
            balance_deltas[uid].first += delta;
        }
        for (const auto& [uid, delta] : bank_snap) {
            balance_deltas[uid].second += delta;
        }

        // Clean out deltas that are zero
        for (auto it = balance_deltas.begin(); it != balance_deltas.end();) {
            if (it->second.first == 0 && it->second.second == 0) {
                it = balance_deltas.erase(it);
            } else {
                ++it;
            }
        }

        if (!balance_deltas.empty()) {
            try {
                db_->update_balances_bulk(balance_deltas);
                for (const auto& [uid, deltas] : balance_deltas) {
                    if (deltas.first != 0) stats_.wallet_writes++;
                    if (deltas.second != 0) stats_.bank_writes++;
                }

                // Dual-write to Aiven for dashboard in bulk!
                if (remote_stats_) {
                    std::vector<uint64_t> remote_uids;
                    for (const auto& [uid, _] : balance_deltas) {
                        remote_uids.push_back(uid);
                    }
                    
                    // Bulk ensure exists on remote
                    std::string ensure_sql = "INSERT IGNORE INTO users (user_id, wallet, bank) VALUES ";
                    bool first = true;
                    for (uint64_t uid : remote_uids) {
                        if (!first) ensure_sql += ", ";
                        first = false;
                        ensure_sql += "(" + std::to_string(uid) + ", 0, 0)";
                    }
                    remote_stats_->execute(ensure_sql);

                    // Bulk update balances on remote
                    std::string update_sql = "INSERT INTO users (user_id, wallet, bank) VALUES ";
                    first = true;
                    for (const auto& [uid, deltas] : balance_deltas) {
                        if (!first) update_sql += ", ";
                        first = false;
                        update_sql += "(" + std::to_string(uid) + ", " + std::to_string(deltas.first) + ", " + std::to_string(deltas.second) + ")";
                    }
                    update_sql += " ON DUPLICATE KEY UPDATE wallet = wallet + VALUES(wallet), bank = bank + VALUES(bank)";
                    remote_stats_->execute(update_sql);
                }
            } catch (const std::exception& e) {
                stats_.errors++;
                bronx::logger::error("write_batch", "bulk balance update failed: " + std::string(e.what()));
                // Re-queue deltas back lock-free!
                for (const auto& [uid, deltas] : balance_deltas) {
                    if (deltas.first != 0) enqueue_wallet_delta(uid, deltas.first);
                    if (deltas.second != 0) enqueue_bank_delta(uid, deltas.second);
                }
            }
        }

        // 4. Bulk flush stat deltas
        decltype(stat_snap) non_zero_stats;
        for (const auto& [key, amount] : stat_snap) {
            if (amount != 0) {
                non_zero_stats[key] = amount;
            }
        }

        if (!non_zero_stats.empty()) {
            try {
                db_->increment_stats_bulk(non_zero_stats);
                stats_.stat_writes += non_zero_stats.size();
            } catch (const std::exception& e) {
                stats_.errors++;
                bronx::logger::error("write_batch", "bulk stats flush failed: " + std::string(e.what()));
                // Re-enqueue back lock-free!
                for (const auto& [key, amount] : non_zero_stats) {
                    enqueue_stat_delta(key.first, key.second, amount);
                }
            }
        }

        // 5. Flush item mutations
        for (auto& m : item_snap) {
            try {
                if (m.type == ItemMutation::ADD) {
                    db_->add_item(m.user_id, m.item_id, m.item_type, m.quantity, m.category, m.level);
                } else {
                    db_->remove_item(m.user_id, m.item_id, m.quantity);
                }
                stats_.item_writes++;
            } catch (const std::exception& e) {
                stats_.errors++;
                bronx::logger::error("write_batch", "item flush failed for " + std::to_string(m.user_id) + ":" + m.item_id + ": " + std::string(e.what()));
                // Re-enqueue item mutation back lock-free!
                enqueue_item_mutation(m);
            }
        }

        // Log periodically
        static std::atomic<uint64_t> flush_count{0};
        if (++flush_count % 30 == 1) {
            size_t total = wallet_snap.size() + bank_snap.size() + stat_snap.size() + item_snap.size();
            bronx::logger::debug("write_batch", "flushed " + std::to_string(total) + " mutations ("
                      + std::to_string(wallet_snap.size()) + "W " + std::to_string(bank_snap.size()) + "B "
                      + std::to_string(stat_snap.size()) + "S " + std::to_string(item_snap.size()) + "I)");
        }
    }
};

} // namespace batch
} // namespace bronx
