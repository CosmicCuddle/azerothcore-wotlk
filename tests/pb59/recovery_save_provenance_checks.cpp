#include "PB59RecoverySaveProvenance.h"
#include "PB59RecoveryProof.h"
#include <type_traits>

using namespace PB59::Recovery;
using Store = PB59::CharacterSaveEvidence<2>;
constexpr Operation Expected{1, 2, 3, 999};
constexpr std::array<std::uint64_t, 2> Originals{100, 101};
constexpr Store::Record NativeRecord()
{
    Store::Record record;
    record.ticket = {0, 7, {1, 2, 3, 8}}; // save sequence8 is NOT recovery ticket999
    record.state = Store::State::Terminal;
    record.evidence.items[0] = 100;
    record.evidence.items[1] = 101;
    record.evidence.itemCount = 2;
    record.result = DatabaseTransactionResult::FromExecutionCode(0);
    return record;
}
constexpr auto Base = NativeRecord();
static_assert(ObserveSave<2>(Base, Base.ticket, Expected, Originals).observation == SaveObservation::Terminal);
static_assert(ObserveSave<2>(Base, Base.ticket, Expected, Originals).executionKnown);
static_assert(ObserveSave<2>(Base, Base.ticket, Expected, Originals).allOriginalGuidsListed);
static_assert(ObserveSave<2>(Base, Base.ticket, Expected, Originals).execution.IsCommitted());
static_assert(!std::is_convertible_v<SaveProvenance, PersistenceEvidence>);
constexpr std::optional<Store::Record> Missing;
static_assert(ObserveSave<2>(Missing, Base.ticket, Expected, Originals).observation == SaveObservation::Unavailable);
static_assert(!ObserveSave<2>(Missing, Base.ticket, Expected, Originals).executionKnown);

constexpr bool RejectInvalidIdentity()
{
    for (auto member : {&PB59::SaveIdentity::guid, &PB59::SaveIdentity::player, &PB59::SaveIdentity::session})
    {
        auto record = Base;
        ++(record.ticket.identity.*member);
        if (ObserveSave<2>(record, Base.ticket, Expected, Originals).observation != SaveObservation::IdentityMismatch)
            return false;
    }
    auto invalid = Base;
    invalid.ticket.serial = 0;
    if (ObserveSave<2>(invalid, Base.ticket, Expected, Originals).observation != SaveObservation::Invalid)
        return false;
    invalid = Base;
    invalid.ticket.slot = 2;
    return ObserveSave<2>(invalid, Base.ticket, Expected, Originals).observation == SaveObservation::Invalid;
}
static_assert(RejectInvalidIdentity());
constexpr bool RejectStaleObserverTicket()
{
    auto stale = Base;
    ++stale.ticket.serial;
    if (ObserveSave<2>(stale, Base.ticket, Expected, Originals).observation != SaveObservation::TicketMismatch)
        return false;
    stale = Base;
    ++stale.ticket.identity.sequence;
    return ObserveSave<2>(stale, Base.ticket, Expected, Originals).observation == SaveObservation::TicketMismatch;
}
static_assert(RejectStaleObserverTicket());

constexpr bool KeepUncertainty()
{
    for (auto state : {Store::State::Reserved, Store::State::Pending})
    {
        auto record = Base;
        record.state = state;
        if (ObserveSave<2>(record, Base.ticket, Expected, Originals).executionKnown)
            return false;
        if (state == Store::State::Reserved &&
            (ObserveSave<2>(record, Base.ticket, Expected, Originals).captureComplete ||
                ObserveSave<2>(record, Base.ticket, Expected, Originals).allOriginalGuidsListed))
            return false;
    }
    auto incomplete = Base;
    incomplete.evidence.incomplete = true;
    if (ObserveSave<2>(incomplete, Base.ticket, Expected, Originals).captureComplete)
        return false;
    incomplete = Base;
    incomplete.evidence.itemCount = 1;
    if (ObserveSave<2>(incomplete, Base.ticket, Expected, Originals).allOriginalGuidsListed)
        return false;
    incomplete = Base;
    incomplete.evidence.itemCount = 65;
    if (ObserveSave<2>(incomplete, Base.ticket, Expected, Originals).observation != SaveObservation::Invalid)
        return false;
    incomplete = Base;
    incomplete.evidence.items[1] = incomplete.evidence.items[0];
    return ObserveSave<2>(incomplete, Base.ticket, Expected, Originals).observation == SaveObservation::Invalid;
}
static_assert(KeepUncertainty());

constexpr bool KeepExecutionOutcomes()
{
    for (int code : {DatabaseTransactionResult::EmptyCode, DatabaseTransactionResult::AmbiguousCode,
            DatabaseTransactionResult::NotStartedCode, DatabaseTransactionResult::PriorAttemptFailureCode,
            DatabaseTransactionResult::NotEnqueuedCode, DatabaseTransactionResult::CancelledBeforeExecutionCode, 5})
    {
        auto record = Base;
        record.result = DatabaseTransactionResult::FromExecutionCode(code);
        auto const observation = ObserveSave<2>(record, Base.ticket, Expected, Originals);
        if (!observation.executionKnown || observation.execution.IsCommitted() ||
            observation.execution.executionCode != code)
            return false;
    }
    auto malformed = Base;
    malformed.result = {DatabaseTransactionStatus::Committed, DatabaseTransactionResult::AmbiguousCode};
    return ObserveSave<2>(malformed, Base.ticket, Expected, Originals).observation == SaveObservation::Invalid;
}
static_assert(KeepExecutionOutcomes());
static_assert(ObserveSave<2>(Base, Base.ticket, Expected, {}).observation == SaveObservation::Invalid);
constexpr std::array<std::uint64_t, 2> Duplicate{100, 100};
static_assert(ObserveSave<2>(Base, Base.ticket, Expected, Duplicate).observation == SaveObservation::Invalid);
