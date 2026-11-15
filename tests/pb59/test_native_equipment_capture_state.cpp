#include "EquipmentCaptureState.h"
#include <cassert>
#include <type_traits>

int main()
{
    using namespace PB59;
    EquipmentCaptureState state;
    assert(!state.Capture());
    {
        EquipmentCaptureState::Mutation load(state);
        state.Bind();
        assert(!state.Capture());
    }
    auto first = state.Capture();
    assert(first && state.Current(*first));
    {
        EquipmentCaptureState::Mutation swap(state);
        assert(!state.Capture() && !state.Current(*first));
        {
            EquipmentCaptureState::Mutation nested(state);
            assert(!state.Capture());
        }
        assert(!state.Capture());
    }
    auto second = state.Capture();
    assert(second && second->session == first->session && second->revision == first->revision + 1);
    assert(!state.Current(*first));
    assert(state.Current(*second));
    {
        EquipmentCaptureState::Mutation logout(state);
        state.Detach();
        assert(!state.Capture());
        state.Bind();
        assert(!state.Capture());
    }
    auto third = state.Capture();
    assert(third && third->session > second->session && !state.Current(*second));
    EquipmentCaptureState another;
    another.Bind();
    assert(another.Capture()->session > third->session);
    state.Detach();
    assert(!state.Capture() && !state.Current(*third));
    { EquipmentCaptureState::Mutation noPlayer(nullptr); }
    static_assert(!std::is_copy_constructible_v<EquipmentCaptureState::Mutation>);
    EquipmentFacts facts;
    assert(facts.slots.size() == 19 && facts.slots[0].itemGuid == 0);
}
