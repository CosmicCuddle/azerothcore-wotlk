#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
namespace PB59 {
enum class InventoryWriteAdmission { Closed, Tracking, Declined };
struct InventoryWriteSubmission { InventoryWriteAdmission status; };
struct Stamp {};
namespace TransactionAdmission { struct MutationScope {}; }
namespace Recovery {
struct Operation {};
namespace ReadbackWire {
struct OwnedReadbackConnection {
    std::function<void()> close;
    explicit OwnedReadbackConnection(std::function<void()> f = {}) : close(std::move(f)) {}
    OwnedReadbackConnection(OwnedReadbackConnection&& rhs) noexcept : close(std::exchange(rhs.close, {})) {}
    OwnedReadbackConnection(OwnedReadbackConnection const&) = delete;
    ~OwnedReadbackConnection() { if (close) close(); }
};
inline std::function<void()> duringRun;
inline unsigned runs = 0;
template<class R> struct BoundSnapshotReadback {
    typename R::Request req;
    static BoundSnapshotReadback Run(typename R::Request request, OwnedReadbackConnection conn, unsigned long) {
        ++runs; if (duringRun) duringRun(); (void)conn; return {std::move(request)};
    }
    template<class H> typename R::Observation DeliverTo(H& h) && { return h.FinishRetainedReadback(std::move(req)); }
};
}
}
}
using Admission = PB59::InventoryWriteAdmission;
struct SaveObserver { struct Ticket {}; };
struct PB59RecoveryWriteFence {};
inline int CharacterDatabase = 0;
struct PB59TrackedInventoryWrites {
    PB59TrackedInventoryWrites(PB59RecoveryWriteFence&, int) {}
    void CloseAdmission() {}
};
struct PB59PendingRecoverySaves {
    struct Completion {};
    struct Readback {
        enum class Outcome { Refused, ComparedValues };
        struct Identity { int id; };
        struct Request { int id; };
        struct Observation { Outcome outcome; std::optional<int> comparison; };
    };
    bool closed = false, attempted = false, hold = true, throwOnFinish = false;
    unsigned begins = 0, finishes = 0;
    std::function<void()> beforeBegin;
    explicit PB59PendingRecoverySaves(PB59RecoveryWriteFence&) {}
    std::optional<Admission> AdmissionRefusal() const { return {}; }
    template<class... T> Admission TrySubmit(T&&...) { return Admission::Tracking; }
    std::optional<Completion> PollOne() { return {}; }
    template<class... T> std::optional<Completion> CopyRetainedCompletion(T&&...) const { return {}; }
    std::optional<Readback::Request> BeginRetainedReadback(Readback::Identity const& key) {
        ++begins; if (beforeBegin) beforeBegin();
        if (closed || attempted || key.id != 1) return {};
        attempted = true; return Readback::Request{key.id};
    }
    Readback::Observation FinishRetainedReadback(Readback::Request&& req) {
        ++finishes; if (throwOnFinish) throw std::runtime_error("fixture failure");
        return {Readback::Outcome::ComparedValues, req.id};
    }
    void CloseAdmission() { closed = true; }
    bool Idle() const { return true; } // real pool's queue idle is distinct from hold ownership
};
class Player {
public:
    class PB59InventorySaveService;
    PB59::InventoryWriteSubmission TryPB59TrackedStandaloneSave(PB59TrackedInventoryWrites&, bool, bool) { return {Admission::Tracking}; }
    static int PreparePB59RecoverySavePayload(PB59RecoveryWriteFence&, Player const*, PB59::Recovery::Operation, PB59::TransactionAdmission::MutationScope const&, PB59::Stamp) { return 0; }
    static bool PollPB59TrackedStandaloneSave(PB59TrackedInventoryWrites&) { return false; }
    static bool DrainPB59TrackedStandaloneSavesForShutdown(PB59TrackedInventoryWrites&) { return true; }
    static int Checks();
};
#include "service.inc"
using O = PB59PendingRecoverySaves::Readback::Outcome;
using C = PB59::Recovery::ReadbackWire::OwnedReadbackConnection;
using PB59::Recovery::ReadbackWire::duringRun;
using PB59::Recovery::ReadbackWire::runs;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while(false)
int Player::Checks() {
    { PB59InventorySaveService s; runs = 0;
      CHECK(s.ObserveRetainedReadback({1}, C{}, 7).outcome == O::ComparedValues);
      CHECK(runs == 1 && s._recovery.finishes == 1 && s._recovery.hold);
      CHECK(s.ObserveRetainedReadback({1}, C{}, 7).outcome == O::Refused);
      CHECK(runs == 1 && s.PollShutdown().drained && s._recovery.hold); }
    { PB59InventorySaveService s; runs = 0; CHECK(s.PollShutdown().drained);
      CHECK(s.ObserveRetainedReadback({1}, C{}, 7).outcome == O::Refused);
      CHECK(runs == 0 && s._recovery.begins == 0 && s._recovery.hold); }
    { PB59InventorySaveService s; runs = 0; bool during = false, closed = false, nested = false;
      duringRun = [&]{ during = !s.PollShutdown().drained;
                        nested = s.ObserveRetainedReadback({1}, C{}, 7).outcome == O::Refused; };
      CHECK(s.ObserveRetainedReadback({1}, C{[&]{ closed = !s.PollShutdown().drained; }}, 7).outcome == O::ComparedValues);
      duringRun = {};
      CHECK(during && nested && closed && runs == 1);
      CHECK(s._recovery.finishes == 1 && s._recovery.hold && s.PollShutdown().drained); }
    { PB59InventorySaveService s; runs = 0; bool during = false, closed = false;
      s._recovery.beforeBegin = [&]{ during = !s.PollShutdown().drained; };
      CHECK(s.ObserveRetainedReadback({1}, C{[&]{ closed = !s.PollShutdown().drained; }}, 7).outcome == O::Refused);
      CHECK(during && closed && runs == 0 && s._recovery.finishes == 0);
      CHECK(s.PollShutdown().drained && s._recovery.hold); }
    { PB59InventorySaveService s; runs = 0; bool threw = false, closed = false;
      s._recovery.throwOnFinish = true;
      try { (void)s.ObserveRetainedReadback({1}, C{[&]{ closed = !s.PollShutdown().drained; }}, 7); }
      catch (std::runtime_error const&) { threw = true; }
      CHECK(threw && closed && runs == 1 && s._recovery.finishes == 1);
      CHECK(s.PollShutdown().drained && s._recovery.hold); }
    { PB59InventorySaveService s; runs = 0; bool closed = false;
      CHECK(s.ObserveRetainedReadback({0}, C{[&]{ closed = !s.PollShutdown().drained; }}, 7).outcome == O::Refused);
      CHECK(closed && runs == 0 && s.PollShutdown().drained && s._recovery.hold); }
    return 0;
}
int main() { int r = Player::Checks(); if (!r) std::cout << checks << '/' << checks << " extracted service checks passed\n"; return r; }
