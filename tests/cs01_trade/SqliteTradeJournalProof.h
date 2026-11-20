#pragma once
// DISPOSABLE SQLite proof only. No worldserver/MariaDB wiring, no character item or gold mutation.
// A Commit receipt in this harness shares a transaction with an idempotent effect MARKER,
// NOT with the real source/debit/destination-grant mutations. Do not use to authorize TRADE_COMPLETE.
#include "TradeJournalReplay.h"
#include <sqlite3.h>
#include <cstdint>
#include <limits>
#include <string>

namespace CosmicTrade {
enum class StoreResult : std::uint8_t { Persisted, Duplicate, Rejected, ManualRecovery, IOError };
enum class InjectFailure : std::uint8_t { None, AfterEffectBeforeReceipt, AfterReceiptBeforeCommit };

class SqliteTradeJournalProof final {
public:
    SqliteTradeJournalProof(std::string const& filename, std::uint32_t realm) : realm_(realm) {
        if (!realm_ || sqlite3_open_v2(filename.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) return;
        ready_ = Execute("PRAGMA journal_mode=WAL;") && Execute("PRAGMA synchronous=FULL;") &&
            Execute("CREATE TABLE IF NOT EXISTS journal ("
                    "trade_id INTEGER NOT NULL, realm INTEGER NOT NULL, owner INTEGER NOT NULL, "
                    "payload_hash INTEGER NOT NULL, seq INTEGER NOT NULL, kind INTEGER NOT NULL, "
                    "PRIMARY KEY (trade_id,realm,seq));") &&
            Execute("CREATE TABLE IF NOT EXISTS effect_marker ("
                    "trade_id INTEGER NOT NULL, realm INTEGER NOT NULL, owner INTEGER NOT NULL, "
                    "payload_hash INTEGER NOT NULL, effect_token INTEGER NOT NULL, "
                    "PRIMARY KEY (trade_id,realm));");
    }
    SqliteTradeJournalProof(SqliteTradeJournalProof const&) = delete;
    SqliteTradeJournalProof& operator=(SqliteTradeJournalProof const&) = delete;
    ~SqliteTradeJournalProof() { if (db_) sqlite3_close(db_); }
    bool Ready() const noexcept { return ready_; }
    // effectToken is only a synthetic token that proves SQL atomicity and exactly-once
    // receipt/marker insertion; it is NOT a grant of inventory items or gold.
    StoreResult Persist(JournalEvent const& event, std::uint64_t effectToken = 0,
        InjectFailure injection = InjectFailure::None) {
        if (!ready_) return StoreResult::IOError;
        if (event.identity.realm != realm_ || !InRange(event.identity.tradeId) ||
            !InRange(event.identity.owner) || !InRange(event.identity.payloadHash) ||
            !InRange(event.sequence) || event.identity.durable || event.identity.state != SideState::None ||
            (event.kind == EventKind::Commit ? !InRange(effectToken) : effectToken != 0))
            return StoreResult::Rejected;
        if (!Execute("BEGIN IMMEDIATE;")) return StoreResult::IOError;
        bool committed = false;
        auto finish = [&](StoreResult status) {
            if (!committed) Execute("ROLLBACK;");
            return status;
        };
        // One writer under BEGIN IMMEDIATE; replay must not assume a call to CommitTransaction
        // or an in-memory event constitutes a durable receipt.
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT seq,kind,owner,payload_hash FROM journal WHERE trade_id=? AND realm=? ORDER BY seq DESC LIMIT 1", -1, &raw, nullptr) != SQLITE_OK)
            return finish(StoreResult::IOError);
        Statement last(raw);
        sqlite3_bind_int64(last.ptr, 1, static_cast<sqlite3_int64>(event.identity.tradeId));
        sqlite3_bind_int64(last.ptr, 2, static_cast<sqlite3_int64>(realm_));
        int rc = sqlite3_step(last.ptr);
        std::uint64_t seq = 0;
        EventKind prior = EventKind::Prepare;
        if (rc == SQLITE_ROW) {
            seq = static_cast<std::uint64_t>(sqlite3_column_int64(last.ptr, 0));
            int kind = sqlite3_column_int(last.ptr, 1);
            if (kind < 0 || kind > 3) return finish(StoreResult::ManualRecovery);
            prior = static_cast<EventKind>(kind);
            if (sqlite3_column_int64(last.ptr, 2) != static_cast<sqlite3_int64>(event.identity.owner) ||
                sqlite3_column_int64(last.ptr, 3) != static_cast<sqlite3_int64>(event.identity.payloadHash))
                return finish(StoreResult::ManualRecovery);
        } else if (rc != SQLITE_DONE) return finish(StoreResult::IOError);
        if (seq) {
            if (event.sequence < seq) return finish(StoreResult::ManualRecovery);
            if (event.sequence == seq) {
                if (prior != event.kind) return finish(StoreResult::ManualRecovery);
                if (prior == EventKind::Commit && !SameEffect(event, effectToken))
                    return finish(StoreResult::ManualRecovery);
                return finish(StoreResult::Duplicate);
            }
            if (seq == static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) || event.sequence != seq + 1)
                return finish(StoreResult::Rejected);
            if (prior != EventKind::Prepare || event.kind == EventKind::Prepare)
                return finish(StoreResult::ManualRecovery);
        } else if (event.sequence != 1 || event.kind != EventKind::Prepare) return finish(StoreResult::Rejected);
        last.Reset();
        if (event.kind == EventKind::Commit) {
            if (!InsertEffect(event, effectToken)) return finish(StoreResult::IOError);
            if (injection == InjectFailure::AfterEffectBeforeReceipt) return finish(StoreResult::IOError);
        }
        if (!InsertJournal(event)) return finish(StoreResult::IOError);
        if (injection == InjectFailure::AfterReceiptBeforeCommit) return finish(StoreResult::IOError);
        if (!Execute("COMMIT;")) return finish(StoreResult::IOError);
        committed = true;
        return StoreResult::Persisted;
    }
    // Rebuild the PR58 replay gate from actual SQLite rows after a close/reopen.
    bool Replay(std::uint64_t tradeId, JournalReplay& into) const {
        if (!ready_ || !InRange(tradeId)) return false;
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT realm,owner,payload_hash,seq,kind FROM journal WHERE trade_id=? AND realm=? ORDER BY seq", -1, &raw, nullptr) != SQLITE_OK)
            return false;
        Statement rows(raw);
        sqlite3_bind_int64(rows.ptr, 1, static_cast<sqlite3_int64>(tradeId));
        sqlite3_bind_int64(rows.ptr, 2, static_cast<sqlite3_int64>(realm_));
        for (int rc = sqlite3_step(rows.ptr);; rc = sqlite3_step(rows.ptr)) {
            if (rc == SQLITE_DONE) return true;
            if (rc != SQLITE_ROW) return false;
            int kind = sqlite3_column_int(rows.ptr, 4);
            if (kind < 0 || kind > 3) return false;
            JournalEvent e;
            e.identity.tradeId = tradeId;
            e.identity.realm = static_cast<std::uint32_t>(sqlite3_column_int64(rows.ptr, 0));
            e.identity.owner = static_cast<std::uint64_t>(sqlite3_column_int64(rows.ptr, 1));
            e.identity.payloadHash = static_cast<std::uint64_t>(sqlite3_column_int64(rows.ptr, 2));
            e.sequence = static_cast<std::uint64_t>(sqlite3_column_int64(rows.ptr, 3));
            e.kind = static_cast<EventKind>(kind);
            // Verify the persisted effect marker BEFORE admitting the receipt into the replay gate.
            // Otherwise a corrupt/missing marker can leave a falsely Complete in-memory state.
            if (e.kind == EventKind::Commit && !EffectExists(e)) return false;
            if (e.identity.realm != realm_ || into.Apply(e) != ApplyResult::Applied) return false;
        }
    }
    int EventCount(std::uint64_t tradeId) const { return CountRows("journal", tradeId); }
    int EffectCount(std::uint64_t tradeId) const { return CountRows("effect_marker", tradeId); }
private:
    struct Statement {
        sqlite3_stmt* ptr;
        explicit Statement(sqlite3_stmt* p) : ptr(p) {}
        ~Statement() { Reset(); }
        Statement(Statement const&) = delete;
        Statement& operator=(Statement const&) = delete;
        void Reset() { if (ptr) { sqlite3_finalize(ptr); ptr = nullptr; } }
    };
    static bool InRange(std::uint64_t x) { return x > 0 && x <= static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()); }
    bool Execute(char const* sql) const { return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
    bool InsertEffect(JournalEvent const& e, std::uint64_t token) {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db_, "INSERT INTO effect_marker (trade_id,realm,owner,payload_hash,effect_token) VALUES (?,?,?,?,?)", -1, &raw, nullptr) != SQLITE_OK) return false;
        Statement stmt(raw);
        sqlite3_bind_int64(stmt.ptr,1,static_cast<sqlite3_int64>(e.identity.tradeId));
        sqlite3_bind_int64(stmt.ptr,2,static_cast<sqlite3_int64>(realm_));
        sqlite3_bind_int64(stmt.ptr,3,static_cast<sqlite3_int64>(e.identity.owner));
        sqlite3_bind_int64(stmt.ptr,4,static_cast<sqlite3_int64>(e.identity.payloadHash));
        sqlite3_bind_int64(stmt.ptr,5,static_cast<sqlite3_int64>(token));
        return sqlite3_step(stmt.ptr) == SQLITE_DONE;
    }
    bool InsertJournal(JournalEvent const& e) {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db_, "INSERT INTO journal (trade_id,realm,owner,payload_hash,seq,kind) VALUES (?,?,?,?,?,?)", -1, &raw, nullptr) != SQLITE_OK) return false;
        Statement stmt(raw);
        sqlite3_bind_int64(stmt.ptr,1,static_cast<sqlite3_int64>(e.identity.tradeId));
        sqlite3_bind_int64(stmt.ptr,2,static_cast<sqlite3_int64>(realm_));
        sqlite3_bind_int64(stmt.ptr,3,static_cast<sqlite3_int64>(e.identity.owner));
        sqlite3_bind_int64(stmt.ptr,4,static_cast<sqlite3_int64>(e.identity.payloadHash));
        sqlite3_bind_int64(stmt.ptr,5,static_cast<sqlite3_int64>(e.sequence));
        sqlite3_bind_int(stmt.ptr,6,static_cast<int>(e.kind));
        return sqlite3_step(stmt.ptr) == SQLITE_DONE;
    }
    bool EffectExists(JournalEvent const& e) const {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT 1 FROM effect_marker WHERE trade_id=? AND realm=? AND owner=? AND payload_hash=?", -1, &raw, nullptr) != SQLITE_OK) return false;
        Statement stmt(raw);
        sqlite3_bind_int64(stmt.ptr,1,static_cast<sqlite3_int64>(e.identity.tradeId));
        sqlite3_bind_int64(stmt.ptr,2,static_cast<sqlite3_int64>(realm_));
        sqlite3_bind_int64(stmt.ptr,3,static_cast<sqlite3_int64>(e.identity.owner));
        sqlite3_bind_int64(stmt.ptr,4,static_cast<sqlite3_int64>(e.identity.payloadHash));
        return sqlite3_step(stmt.ptr) == SQLITE_ROW;
    }
    bool SameEffect(JournalEvent const& e, std::uint64_t token) const {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT effect_token FROM effect_marker WHERE trade_id=? AND realm=? AND owner=? AND payload_hash=?", -1, &raw, nullptr) != SQLITE_OK) return false;
        Statement stmt(raw);
        sqlite3_bind_int64(stmt.ptr,1,static_cast<sqlite3_int64>(e.identity.tradeId));
        sqlite3_bind_int64(stmt.ptr,2,static_cast<sqlite3_int64>(realm_));
        sqlite3_bind_int64(stmt.ptr,3,static_cast<sqlite3_int64>(e.identity.owner));
        sqlite3_bind_int64(stmt.ptr,4,static_cast<sqlite3_int64>(e.identity.payloadHash));
        return sqlite3_step(stmt.ptr) == SQLITE_ROW && sqlite3_column_int64(stmt.ptr,0) == static_cast<sqlite3_int64>(token);
    }
    int CountRows(char const* table, std::uint64_t tradeId) const {
        if (!ready_ || !InRange(tradeId)) return -1;
        std::string sql = std::string("SELECT count(*) FROM ") + table + " WHERE trade_id=? AND realm=?";
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db_,sql.c_str(),-1,&raw,nullptr) != SQLITE_OK) return -1;
        Statement stmt(raw);
        sqlite3_bind_int64(stmt.ptr,1,static_cast<sqlite3_int64>(tradeId));
        sqlite3_bind_int64(stmt.ptr,2,static_cast<sqlite3_int64>(realm_));
        return sqlite3_step(stmt.ptr) == SQLITE_ROW ? sqlite3_column_int(stmt.ptr,0) : -1;
    }
    std::uint32_t realm_;
    sqlite3* db_ = nullptr;
    bool ready_ = false;
};
} // namespace CosmicTrade
