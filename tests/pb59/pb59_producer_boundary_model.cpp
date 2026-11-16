// PB59 producer boundary synthetic regression model. This DOES NOT bind native Player/Map/DB.
// Source authority: Naxxramas-Server-Coordination #13 comment 6931509335; Core 403c74e.
#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <string_view>
#include <utility>

namespace
{
int checks = 0;
bool Check(bool okay, std::string_view why)
{
    ++checks;
    if (!okay)
        std::cerr << "FAIL check " << checks << ": " << why << '\n';
    return okay;
}

struct Item
{
    int guid = 1;
    bool inWorld = true;
    bool updatePending = true;
};
struct Map
{
    int id;
    std::set<int> updateObjects;
};
struct Owner
{
    Map* map = nullptr;
    Item* equipped = nullptr; // native owner link is NOT moved without complete proof
    bool nearTeleport = false;
    bool farTeleport = false;
    bool holdActive = false;
    bool quarantined = false;
    bool alive = true;
    int money = 100;
    int gear = 10;
};

bool NativeReleaseProven(Item const& item, Map const& a, Map const& b)
{
    // Stronger intended proof than Core403's m_objectUpdated-only check.
    return !item.inWorld && !item.updatePending &&
        !a.updateObjects.contains(item.guid) && !b.updateObjects.contains(item.guid);
}

enum class HoldResult { Denied, Held, RecoveryRequired };
HoldResult Hold(Owner& owner, Item& item, Map& mapA, Map& mapB, int faultStep = 0)
{
    if (!owner.alive || owner.holdActive || owner.nearTeleport || owner.farTeleport ||
        owner.equipped != &item || !item.inWorld || !item.updatePending || !owner.map)
        return HoldResult::Denied;
    // New intended map-registry identity check: the bit alone is inadequate.
    if (owner.map->id != mapA.id && owner.map->id != mapB.id)
        return HoldResult::Denied;
    if (!owner.map->updateObjects.contains(item.guid))
        return HoldResult::Denied;
    if ((owner.map == &mapA && mapB.updateObjects.contains(item.guid)) ||
        (owner.map == &mapB && mapA.updateObjects.contains(item.guid)))
        return HoldResult::Denied;
    item.inWorld = false;
    if (faultStep == 1)
    {
        owner.quarantined = true;
        return HoldResult::RecoveryRequired;
    }
    item.updatePending = false;
    if (faultStep == 2)
    {
        owner.quarantined = true;
        return HoldResult::RecoveryRequired;
    }
    owner.map->updateObjects.erase(item.guid);
    if (faultStep == 3)
    {
        owner.quarantined = true;
        return HoldResult::RecoveryRequired;
    }
    if (!NativeReleaseProven(item, mapA, mapB))
    {
        owner.quarantined = true;
        return HoldResult::RecoveryRequired;
    }
    owner.holdActive = true;
    owner.equipped = nullptr; // controlled mock pointer handoff, no actual delete
    return HoldResult::Held;
}

struct Transaction
{
    int appends = 0;
    void Append() { ++appends; }
};
struct ActionReport
{
    bool admitted = false;
    bool returnedError = false;
    bool flagChanged = false;
};
ActionReport Trade(Owner& a, Owner& b, Transaction& trans)
{
    // Both participants checked before trade accept state or the first write.
    if (!a.alive || !b.alive || a.holdActive || a.quarantined ||
        b.holdActive || b.quarantined)
        return {false, true, false};
    a.gear -= 1;
    b.gear += 1;
    a.money += 5;
    b.money -= 5;
    trans.Append();
    trans.Append();
    return {true, false, true};
}
ActionReport SendMail(Owner& sender, Owner& receiver, Transaction& trans)
{
    if (!sender.alive || !receiver.alive || sender.holdActive || sender.quarantined ||
        receiver.holdActive || receiver.quarantined)
        return {false, true, false};
    sender.money -= 3;
    trans.Append();
    return {true, false, true};
}

struct Logout
{
    bool logoutFlag = false;
    bool callbackCalled = false;
    bool saved = false;
    bool disposed = false;
};
// Explicitly models a *pre* side-effect gate. It is not wired to WorldSession.
bool TryOrdinaryLogout(Owner const& owner, Logout& trace)
{
    if (!owner.alive || owner.holdActive || owner.quarantined)
        return false;
    trace.logoutFlag = true;
    trace.callbackCalled = true;
    trace.saved = true;
    trace.disposed = true;
    return true;
}
bool AtSynchronousScopeExit(Owner const& owner)
{
    // Would have to be enforced by the real owner-thread executor *before*
    // an involuntary destructor; no destructor can wait for a later retry.
    return !owner.holdActive && !owner.quarantined;
}
}

int main()
{
    bool ok = true;
    auto check = [&](bool v, std::string_view s) { ok = Check(v, s) && ok; };

    // Wrong-map: player is on map B, Item still registered on A, no teleport flag.
    {
        Map a{1, {1}}, b{2, {}};
        Item item;
        Owner owner{&b, &item};
        check(Hold(owner, item, a, b) == HoldResult::Denied, "wrong-map orphan denied");
        check(owner.equipped == &item && !owner.holdActive, "wrong-map pointer retained");
        check(a.updateObjects.contains(1), "wrong-map registration not silently cleared");
        check(!NativeReleaseProven(item, a, b), "wrong-map item must not be freed");
        check(!owner.nearTeleport && !owner.farTeleport, "failure holds without teleport flag");
    }
    // Partial RemoveFromWorld: update bit can be clear while A still registers item.
    for (int fault = 1; fault <= 3; ++fault)
    {
        Map a{1, {1}}, b{2, {}};
        Item item;
        Owner owner{&a, &item};
        check(Hold(owner, item, a, b, fault) == HoldResult::RecoveryRequired, "injected partial removal quarantined");
        check(owner.equipped == &item && !owner.holdActive, "partial removal retains owner pointer");
        check(owner.quarantined, "partial removal recovery latched");
        check(!AtSynchronousScopeExit(owner), "quarantine blocks ordinary scope completion");
        check(fault == 3 || !NativeReleaseProven(item, a, b), "registered/in-world item not releasable");
    }
    {
        Map a{1, {1}}, b{2, {}};
        Item item;
        Owner owner{&a, &item};
        owner.nearTeleport = true;
        check(Hold(owner, item, a, b) == HoldResult::Denied, "near teleport denies");
        owner.nearTeleport = false;
        owner.farTeleport = true;
        check(Hold(owner, item, a, b) == HoldResult::Denied, "far teleport denies");
        owner.farTeleport = false;
        check(Hold(owner, item, a, b) == HoldResult::Held, "complete handoff accepted");
        check(owner.holdActive && owner.equipped == nullptr, "completed handoff holds lifetime");
        check(NativeReleaseProven(item, a, b), "all maps clean after complete handoff");
        check(!AtSynchronousScopeExit(owner), "active hold cannot escape owner scope");
        owner.holdActive = false; // represents independently verified synchronous restore
        owner.equipped = &item;
        check(AtSynchronousScopeExit(owner), "restored scope can end");
    }

    // Test both ends of multi-party admission before any side effect/append.
    for (int caseId = 0; caseId < 4; ++caseId)
    {
        Owner a, b;
        a.holdActive = (caseId & 1) != 0;
        b.quarantined = (caseId & 2) != 0;
        Transaction tx;
        auto result = Trade(a, b, tx);
        bool const allowed = caseId == 0;
        check(result.admitted == allowed && result.returnedError == !allowed, "trade participant admission");
        check(result.flagChanged == allowed, "trade accepted flag changes only on admit");
        check(tx.appends == (allowed ? 2 : 0), "trade transaction cannot partially append");
        check(a.gear == (allowed ? 9 : 10) && b.gear == (allowed ? 11 : 10), "trade gear unchanged on refusal");
        check(a.money == (allowed ? 105 : 100) && b.money == (allowed ? 95 : 100), "trade money unchanged on refusal");
    }
    for (int caseId = 0; caseId < 4; ++caseId)
    {
        Owner sender, receiver;
        sender.quarantined = (caseId & 1) != 0;
        receiver.holdActive = (caseId & 2) != 0;
        Transaction tx;
        auto result = SendMail(sender, receiver, tx);
        bool const allowed = caseId == 0;
        check(result.admitted == allowed && result.returnedError == !allowed, "mail participant admission");
        check(tx.appends == (allowed ? 1 : 0), "mail transaction cannot partially append");
        check(sender.money == (allowed ? 97 : 100), "mail cash unchanged on refusal");
        check(sender.gear == 10 && receiver.gear == 10, "mail equipment unchanged on refusal");
    }
    for (int caseId = 0; caseId < 4; ++caseId)
    {
        Owner owner;
        owner.holdActive = (caseId & 1) != 0;
        owner.quarantined = (caseId & 2) != 0;
        Logout trace;
        bool allowed = caseId == 0;
        check(TryOrdinaryLogout(owner, trace) == allowed, "ordinary logout entry gated");
        check(trace.logoutFlag == allowed && trace.callbackCalled == allowed, "logout callback not reached on refusal");
        check(trace.saved == allowed && trace.disposed == allowed, "save/delete not reached on refusal");
        check(AtSynchronousScopeExit(owner) == allowed, "forced destruction requires prior scope completion");
    }

    std::cout << "checks=" << checks << " status=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
