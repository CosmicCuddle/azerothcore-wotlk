#include "PB59OriginalCombatEffects.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>

constexpr std::size_t EQUIPMENT_SLOT_END = 19;
struct Item {};
namespace PB59
{
struct TransactionAdmission
{
    enum class Phase { Idle, Active, RecoveryRequired };
    enum class Work { Logout };
    Phase phase = Phase::Idle;
    unsigned requests = 0;
    bool pendingLogout = false;
    Phase State() const noexcept { return phase; }
    bool Request(Work) noexcept { ++requests; pendingLogout = true; return true; }
};
}
struct Player
{
    struct OriginalQueue { bool held = false; };
    struct QueueHold { OriginalQueue queue; std::uint64_t ticket = 0; };
    struct OriginalHold
    {
        std::unique_ptr<Item> item;
        std::optional<int> original;
        QueueHold queue;
        bool originallyInWorld = false;
    };
    PB59::TransactionAdmission _pb59Transaction;
    std::array<OriginalHold, EQUIPMENT_SLOT_END> _pb59OriginalItems{};
    std::array<PB59::OriginalCombatEffects, EQUIPMENT_SLOT_END> _pb59OriginalCombatEffects{};
#include "native_lifecycle.inc"
};
int main()
{
    using Phase = PB59::TransactionAdmission::Phase;
    using Journal = PB59::OriginalCombatEffects;
    using Result = Journal::Result;
    unsigned checks = 0;
    auto check = [&](bool value) { assert(value); ++checks; };
    {
        Player p;
        check(!p.HasPB59UnsettledOriginals());
        check(!p.RetainPB59OwnerForUnsettledLogout());
        check(p._pb59Transaction.requests == 0);
        for (Phase state : {Phase::Active, Phase::RecoveryRequired})
        {
            p._pb59Transaction.phase = state;
            check(p.HasPB59UnsettledOriginals());
            check(p.RetainPB59OwnerForUnsettledLogout());
            check(p._pb59Transaction.pendingLogout);
            p._pb59Transaction.pendingLogout = false;
        }
        p._pb59Transaction.phase = Phase::Idle;
        check(!p.HasPB59UnsettledOriginals());
    }
    {
        Player p;
        for (std::size_t slot : {std::size_t{0}, std::size_t{10}, std::size_t{17}})
        {
            auto& hold = p._pb59OriginalItems[slot];
            hold.item = std::make_unique<Item>();
            check(p.HasPB59UnsettledOriginals());
            check(p.RetainPB59OwnerForUnsettledLogout());
            hold.item.reset();
            hold.original = 42;
            check(p.HasPB59UnsettledOriginals());
            hold.original.reset();
            hold.queue.queue.held = true;
            check(p.HasPB59UnsettledOriginals());
            hold.queue.queue.held = false;
            hold.queue.ticket = 55;
            check(p.HasPB59UnsettledOriginals());
            hold.queue.ticket = 0;
            hold.originallyInWorld = true;
            check(p.HasPB59UnsettledOriginals());
            hold.originallyInWorld = false;
            check(!p.HasPB59UnsettledOriginals());
        }
    }
    {
        Player p;
        auto& journal = p._pb59OriginalCombatEffects[4];
        auto yes = [] { return true; };
        auto unchanged = [](bool) {};
        check(journal.Run(1, 42, false, yes, unchanged, unchanged) == Result::Complete);
        check(p.HasPB59UnsettledOriginals());
        check(journal.State() == Journal::Phase::Removed);
        check(journal.Run(1, 42, true, yes, unchanged, unchanged) == Result::Complete);
        check(p.HasPB59UnsettledOriginals()); // Restored still needs native proof
        check(journal.RearmAfterVerifiedRestore(1, 42, yes));
        check(!p.HasPB59UnsettledOriginals());
        auto fails = [](bool) { throw 1; };
        check(journal.Run(2, 43, false, yes, fails, unchanged) == Result::RecoveryRequired);
        check(journal.State() == Journal::Phase::Uncertain);
        check(p.HasPB59UnsettledOriginals());
        check(p.RetainPB59OwnerForUnsettledLogout());
        check(p._pb59Transaction.requests == 1);
    }
    std::cout << checks << " native lifecycle custody admission checks passed\n";
}
