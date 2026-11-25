#include "PB59InventoryWriteFence.h"
#include <barrier>
#include <iostream>
#include <thread>
#include <type_traits>

using Fence = PB59::InventoryWriteFence<2>;
using Result = DatabaseTransactionResult;
static_assert(std::is_move_constructible_v<Fence::Writer> && !std::is_copy_constructible_v<Fence::Writer>);
static_assert(std::is_move_constructible_v<Fence::Capture> && !std::is_copy_constructible_v<Fence::Capture>);
static_assert(!std::is_default_constructible_v<Fence::Writer> && !std::is_default_constructible_v<Fence::Capture>);
static_assert(!std::is_copy_constructible_v<Fence> && !std::is_move_constructible_v<Fence>);

static unsigned Checks = 0;
#define FENCE_CHECK(value) do { \
    ++Checks; \
    if (!(value)) \
    { \
        std::cerr << "inventory write fence line " << __LINE__ << '\n'; \
        return 1; \
    } \
} while (false)

int main()
{
    constexpr std::uint64_t character = 11;
    constexpr std::uint64_t other = 22;
    constexpr std::uint64_t third = 33;
    auto const committed = Result::FromExecutionCode(0);
    {
        Fence fence;
        FENCE_CHECK(!fence.TryTrackWrite(0) && !fence.TryReserveCapture(0));
        auto capture = fence.TryReserveCapture(character);
        FENCE_CHECK(capture && !fence.TryReserveCapture(character) && !fence.TryTrackWrite(character));
        auto movedCapture = std::move(*capture);
        capture.reset();
        FENCE_CHECK(!fence.TryTrackWrite(character));
        auto unrelated = fence.TryTrackWrite(other);
        FENCE_CHECK(unrelated && !fence.TryTrackWrite(third) && !fence.TryReserveCapture(third));
        unrelated.reset();
        FENCE_CHECK(fence.TryReserveCapture(third));
    }
    {
        Fence fence;
        auto preSubmit = fence.TryTrackWrite(character);
        FENCE_CHECK(preSubmit && !preSubmit->Complete(committed));
        FENCE_CHECK(!fence.TryReserveCapture(character));
        preSubmit.reset();
        FENCE_CHECK(fence.TryReserveCapture(character));
    }
    {
        Fence fence;
        auto older = fence.TryTrackWrite(character);
        auto newer = fence.TryTrackWrite(character);
        FENCE_CHECK(older && newer && older->MarkPossiblySubmitted() && newer->MarkPossiblySubmitted());
        FENCE_CHECK(!older->MarkPossiblySubmitted());
        // Simulate a save whose lease survives its original Player, followed by
        // a save for the same GUID from the replacement session.
        FENCE_CHECK(newer->Complete(committed) && !newer->Complete(committed));
        FENCE_CHECK(!fence.TryReserveCapture(character) && fence.TryReserveCapture(other));
        bool handedOff = false;
        std::thread completion([writer = std::move(*older), &handedOff, committed]() mutable
        {
            handedOff = writer.Complete(committed);
        });
        completion.join();
        FENCE_CHECK(!older->Complete(committed));
        FENCE_CHECK(handedOff && fence.TryReserveCapture(character));
    }
    for (int code : {Result::NotEnqueuedCode, Result::CancelledBeforeExecutionCode, Result::NotStartedCode,
        Result::EmptyCode, 0})
    {
        Fence fence;
        auto writer = fence.TryTrackWrite(character);
        FENCE_CHECK(writer && writer->MarkPossiblySubmitted() && !fence.TryReserveCapture(character));
        FENCE_CHECK(writer->Complete(Result::FromExecutionCode(code)));
        FENCE_CHECK(fence.TryReserveCapture(character));
    }
    for (auto result : {Result::FromExecutionCode(Result::AmbiguousCode),
        Result::FromExecutionCode(Result::PriorAttemptFailureCode), Result::FromExecutionCode(1213),
        Result{DatabaseTransactionStatus::Committed, Result::AmbiguousCode}})
    {
        Fence fence;
        auto writer = fence.TryTrackWrite(character);
        FENCE_CHECK(writer && writer->MarkPossiblySubmitted() && !writer->Complete(result));
        FENCE_CHECK(!writer->Complete(committed) && !fence.TryReserveCapture(character));
        auto later = fence.TryTrackWrite(character);
        FENCE_CHECK(later && later->MarkPossiblySubmitted() && later->Complete(committed));
        FENCE_CHECK(!fence.TryReserveCapture(character) && fence.TryReserveCapture(other));
    }
    {
        Fence fence;
        auto writer = fence.TryTrackWrite(character);
        FENCE_CHECK(writer && writer->MarkPossiblySubmitted());
        writer.reset(); // callback/result delivery lost after possible submission
        FENCE_CHECK(!fence.TryReserveCapture(character) && fence.TryReserveCapture(other));
    }
    {
        auto fence = std::make_unique<Fence>();
        auto writer = fence->TryTrackWrite(character);
        FENCE_CHECK(writer && writer->MarkPossiblySubmitted());
        fence.reset(); // tests safe release of shared storage, not recovery across restart
        FENCE_CHECK(writer->Complete(committed));
    }
    for (int round = 0; round < 32; ++round)
    {
        Fence fence;
        std::barrier rendezvous(2);
        bool writerAdmitted = false;
        std::thread writer([&]
        {
            rendezvous.arrive_and_wait();
            auto lease = fence.TryTrackWrite(character);
            writerAdmitted = lease.has_value();
            rendezvous.arrive_and_wait();
        });
        rendezvous.arrive_and_wait();
        auto capture = fence.TryReserveCapture(character);
        rendezvous.arrive_and_wait();
        writer.join();
        FENCE_CHECK(capture.has_value() != writerAdmitted);
        capture.reset();
        FENCE_CHECK(fence.TryReserveCapture(character));
    }
    std::cout << Checks << " PB59 inventory write fence checks PASS\n";
}
