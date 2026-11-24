#include <iostream>
#include <stdexcept>
#include <type_traits>

using Ledger = PB59::TransactionAdmission;
using Observer = PB59::CharacterSaveObserver<1>;
using Store = Observer::Store;
using namespace PB59::Recovery;
static_assert(!std::is_convertible_v<SaveProvenance, PersistenceEvidence>);
static unsigned SaveChecks = 0;
#define SAVE_CHECK(value) do { \
    ++SaveChecks; \
    if (!(value)) \
    { \
        std::cerr << "save contract line " << __LINE__ << '\n'; \
        return 1; \
    } \
} while (false)

static std::array<PB59::InventoryBatchMove, 1> SavePlan(Player& actor)
{
    SaveBoundary = {}; Bridge = {};
    actor.Add(65280, 10);
    return {{{*actor.CapturePB59InventorySlot(65280), *actor.CapturePB59InventorySlot(65303)}}};
}
static PB59::InventoryBatchResult ExecuteSavePlan(Player& actor, std::span<PB59::InventoryBatchMove const> plan)
{
    return actor.ExecutePB59InventoryBatchPlan(1, actor._pb59Transaction.OwnerIncarnation(), 7,
        *actor.CapturePB59InventoryStamp(), plan);
}

int main()
{
    {
        Player actor; (void)SavePlan(actor);
        actor.SaveToDB(false, false);
        actor.SaveToDBWithEvidence(0, false, false, nullptr);
        SAVE_CHECK(SaveBoundary.transactionBoundary == 1 && SaveBoundary.appendBoundary == 1);
        SAVE_CHECK(!actor._pb59Transaction.PendingSave());
    }
    {
        Player actor; (void)SavePlan(actor);
        Ledger::MutationScope scope(actor._pb59Transaction); // unsettled without native inventory quarantine
        actor.SaveToDB(false, false);
        SAVE_CHECK(SaveBoundary.transactionBoundary == 0 && SaveBoundary.scheduled == 1);
        SAVE_CHECK(actor.m_nextSave == 1 && actor._pb59Transaction.PendingSave());
        SAVE_CHECK(actor._pb59Transaction.Request(Ledger::Work::CallerOwnedSave) ==
            Ledger::Decision::RejectBeforeMutation);
    }
    for (unsigned mode = 0; mode < 5; ++mode)
    {
        Player actor; auto plan = SavePlan(actor);
        unsigned attempts = 0, cancellations = 0;
        actor.modsHook = [&]
        {
            ++attempts;
            try
            {
                if (mode == 4) actor.SaveToDBWithEvidence(0, false, false, nullptr);
                else actor.SaveToDB(mode & 1, mode & 2);
            }
            catch (PB59::ReservedInventoryMutation const&) { ++cancellations; }
        };
        SAVE_CHECK(ExecuteSavePlan(actor, plan).outcome == PB59::InventoryBatchOutcome::RecoveryRequired);
        SAVE_CHECK(attempts && attempts == cancellations);
        SAVE_CHECK(!SaveBoundary.transactionBoundary && !SaveBoundary.appendBoundary && !SaveBoundary.scheduled);
        SAVE_CHECK(actor.m_nextSave == 42 && Bridge.values && Bridge.values->summary.intentsObserved);
        SAVE_CHECK(actor._pb59Transaction.PendingSave() == (mode == 0));
        SAVE_CHECK(!actor._pb59Transaction.PendingLogout());
        auto const fields = actor.fields;
        actor.SaveToDB(false, false); // retained quarantine after native scope unwind: no throw, no transaction
        actor.SaveToDB(true, true);
        actor.SaveToDBWithEvidence(0, false, false, nullptr);
        SAVE_CHECK(!SaveBoundary.transactionBoundary && !SaveBoundary.appendBoundary && actor.fields == fields);
        SAVE_CHECK(actor._pb59Transaction.PendingSave() && actor._pb59Equipment.InventoryQuarantined());
        SAVE_CHECK(actor._pb59Transaction.Request(Ledger::Work::CallerOwnedSave) ==
            Ledger::Decision::RejectBeforeMutation);
    }
    {
        Player actor; auto plan = SavePlan(actor); actor.throwMods = 1;
        SAVE_CHECK(ExecuteSavePlan(actor, plan).outcome == PB59::InventoryBatchOutcome::RecoveryRequired);
        SAVE_CHECK(Bridge.values && Bridge.collectStatus == CollectionStatus::Collected);
        auto const operation = Bridge.values->evidence.operation;
        std::array<std::uint64_t, 1> originals{10};
        for (int code : {0, DatabaseTransactionResult::EmptyCode, DatabaseTransactionResult::AmbiguousCode,
            DatabaseTransactionResult::NotStartedCode, DatabaseTransactionResult::PriorAttemptFailureCode,
            DatabaseTransactionResult::NotEnqueuedCode, DatabaseTransactionResult::CancelledBeforeExecutionCode, 5})
        {
            // Synthetic typed delivery into the real observer, NOT a save issued
            // by this quarantined actor or proof that SQL/commit ever occurred.
            Observer observer;
            auto reservation = observer.Reserve(operation.owner, operation.ownerIncarnation,
                operation.sessionIncarnation);
            SAVE_CHECK(reservation.status == Store::Admission::Accepted);
            PB59::SaveEvidence dirty; dirty.AddItem(10);
            SAVE_CHECK(observer.Seal(reservation.ticket, dirty));
            std::promise<DatabaseTransactionResult> delivery;
            SAVE_CHECK(observer.Attach(reservation.ticket, delivery.get_future()));
            std::optional<Store::Record> report;
            unsigned reports = 0;
            auto sink = [&](auto const& value) { report = value; ++reports; };
            observer.Drain(sink); SAVE_CHECK(reports == 0);
            delivery.set_value(DatabaseTransactionResult::FromExecutionCode(code));
            observer.Drain(sink); SAVE_CHECK(reports == 1 && report);
            auto observed = ObserveSave<1>(*report, reservation.ticket, operation, originals);
            SAVE_CHECK(observed.executionKnown && observed.execution.executionCode == code);
            SAVE_CHECK(observed.execution.IsCommitted() == (code == 0));
            SAVE_CHECK(observed.captureComplete && observed.allOriginalGuidsListed);
            auto differentRecovery = operation;
            ++differentRecovery.ticket; // hypothetical expected ID, no admission
            SAVE_CHECK(ObserveSave<1>(*report, reservation.ticket, differentRecovery, originals).executionKnown);
            auto stale = reservation.ticket; ++stale.serial;
            SAVE_CHECK(ObserveSave<1>(*report, stale, operation, originals).observation ==
                SaveObservation::TicketMismatch);
            std::optional<Store::Record> missing;
            SAVE_CHECK(ObserveSave<1>(missing, reservation.ticket, operation, originals).observation ==
                SaveObservation::Unavailable);
            observer.Drain(sink); SAVE_CHECK(reports == 1); // acknowledgement, no duplicate completion
            SAVE_CHECK(actor._pb59Transaction.State() == Ledger::Phase::RecoveryRequired);
            SAVE_CHECK(!SaveBoundary.transactionBoundary && !SaveBoundary.appendBoundary);
        }
        for (unsigned deliveryFailure = 0; deliveryFailure < 3; ++deliveryFailure)
        {
            Observer observer;
            auto reservation = observer.Reserve(operation.owner, operation.ownerIncarnation,
                operation.sessionIncarnation);
            PB59::SaveEvidence dirty; dirty.AddItem(10);
            SAVE_CHECK(observer.Seal(reservation.ticket, dirty));
            {
                std::promise<DatabaseTransactionResult> delivery;
                SAVE_CHECK(observer.Attach(reservation.ticket, delivery.get_future()));
                if (deliveryFailure == 0)
                    delivery.set_exception(std::make_exception_ptr(
                        std::runtime_error("synthetic typed delivery failure")));
                if (deliveryFailure == 2)
                    delivery.set_exception(std::make_exception_ptr(17)); // non-std exceptional future
            }
            std::optional<Store::Record> report;
            bool escaped = false;
            try { observer.Drain([&](auto const& value) { report = value; }); }
            catch (...) { escaped = true; }
            SAVE_CHECK(!escaped && report);
            SAVE_CHECK(report->result.status == DatabaseTransactionStatus::Ambiguous);
            SAVE_CHECK(!ObserveSave<1>(*report, reservation.ticket, operation, originals).execution.IsCommitted());
            SAVE_CHECK(actor._pb59Equipment.InventoryQuarantined());
        }
    }
    {
        Observer observer;
        auto reservation = observer.Reserve(1, 2, 3);
        PB59::SaveEvidence dirty; dirty.AddItem(10);
        SAVE_CHECK(observer.Seal(reservation.ticket, dirty));
        std::promise<DatabaseTransactionResult> delivery;
        SAVE_CHECK(observer.Attach(reservation.ticket, delivery.get_future()));
        delivery.set_value(DatabaseTransactionResult::FromExecutionCode(0));
        bool reportThrew = false;
        try { observer.Drain([](auto const&) { throw std::runtime_error("report sink fault"); }); }
        catch (std::runtime_error const&) { reportThrew = true; }
        SAVE_CHECK(reportThrew); // only future delivery errors are contained, not the report callback
        unsigned reports = 0;
        observer.Drain([&](auto const& value) { if (value.result.IsCommitted()) ++reports; });
        SAVE_CHECK(reports == 1); // failed reporting did not acknowledge/drop the terminal record
    }
    std::cout << SaveChecks << " recovery-save blocking contract checks PASS (no database/IO)\n";
}
