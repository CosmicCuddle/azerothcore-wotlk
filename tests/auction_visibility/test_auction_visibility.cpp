#include "AuctionVisibility.h"
#include <array>
#include <cassert>
#include <vector>
#include <algorithm>
using namespace AuctionVisibility;
constexpr Policy p{7, 1, 18, 3};
constexpr Tag unrestricted{TagState::Unrestricted, 7, 1, 0, 0};
constexpr Tag controlled{TagState::Controlled, 7, 1, 4, 3};
constexpr Viewer v{true, 7, 1, 4, 3};
constexpr auto en = Mode::Enforced;
constexpr auto obs = Mode::Observe;
static_assert(Evaluate(en,p,v,controlled).visible);
static_assert(Evaluate(en,p,{},unrestricted).visible);
static_assert(!Evaluate(en,p,{},controlled).visible);
static_assert(!Evaluate(en,p,v,{}).visible);
static_assert(!Evaluate(en,p,v,{TagState::Unrestricted,7,1,1,0}).visible);
static_assert(!Evaluate(en,p,v,{TagState::Controlled,7,1,19,0}).visible);
static_assert(!Evaluate(en,p,v,{TagState::Controlled,7,1,0,4}).visible);
static_assert(!Evaluate(en,p,v,{static_cast<TagState>(255),7,1,0,0}).visible);
static_assert(!Evaluate(en,p,v,{TagState::Unrestricted,6,1,0,0}).visible);
static_assert(!Evaluate(en,p,v,{TagState::Controlled,7,2,0,0}).visible);
static_assert(!Evaluate(en,p,{true,7,1,3,3},controlled).visible);
static_assert(Evaluate(en,p,{true,7,1,5,3},controlled).visible);
static_assert(!Evaluate(en,p,{true,7,1,4,1},controlled).visible);
static_assert(!Evaluate(en,p,{true,6,1,4,3},controlled).visible);
static_assert(!Evaluate(en,p,{true,7,2,4,3},controlled).visible);
static_assert(!Evaluate(en,p,{true,7,1,19,3},controlled).visible);
static_assert(!Evaluate(en,p,{true,7,1,4,7},controlled).visible);
static_assert(!Evaluate(en,{8,1,18,3},v,controlled).visible);
static_assert(Evaluate(obs,p,{},controlled).visible && !Evaluate(obs,p,{},controlled).wouldPermit);
static_assert(Evaluate(obs,p,v,{}).reason == Reason::InvalidTag);
static_assert(Evaluate(obs,p,v,controlled).wouldPermit);
static_assert(Evaluate(Mode::Disabled,{}, {}, {}).visible);
static_assert(!Evaluate(static_cast<Mode>(255),p,v,controlled).visible);
static_assert(!Evaluate(en,{},v,unrestricted).visible);
constexpr std::array<Allocation,2> allocations{{{1,1},{2,2}}};
constexpr std::array<Contribution,2> grants{{{1,true,4,1},{2,true,0,2}}};
static_assert(BuildViewer(p,1,allocations,grants).valid);
static_assert(BuildViewer(p,1,allocations,grants).granted == 3);
static_assert(BuildViewer(p,1,allocations,grants).ordered == 4);
constexpr std::array<Contribution,2> invalidGrant{{{1,true,4,1},{2,false,0,2}}};
static_assert(BuildViewer(p,1,allocations,invalidGrant).granted == 1);
constexpr std::array<Contribution,2> invalidAuthority{{{1,false,4,1},{2,true,0,2}}};
static_assert(!BuildViewer(p,1,allocations,invalidAuthority).valid);
static_assert(BuildViewer(p,1,allocations,invalidAuthority).granted == 0);
constexpr std::array<Contribution,2> foreignBits{{{1,true,4,3},{2,true,0,2}}};
static_assert(!BuildViewer(p,1,allocations,foreignBits).valid);
constexpr std::array<Contribution,2> foreignOrder{{{1,true,4,1},{2,true,18,2}}};
static_assert(!BuildViewer(p,1,allocations,foreignOrder).valid);
constexpr std::array<Allocation,2> collision{{{1,1},{2,1}}};
static_assert(!BuildViewer(p,1,collision,grants).valid);
constexpr std::array<Contribution,2> duplicate{{{1,true,4,1},{1,true,4,1}}};
static_assert(!BuildViewer(p,1,allocations,duplicate).valid);
static_assert(!BuildViewer(p,3,allocations,grants).valid);
static_assert(!BuildViewer(p,1,allocations,{}).valid);
static_assert(Combine(unrestricted,controlled,p).state == TagState::Controlled);
static_assert(Combine(controlled,{TagState::Controlled,7,1,8,1},p).minimum == 8);
static_assert(Combine({TagState::Controlled,7,1,4,1},{TagState::Controlled,7,1,2,2},p).required == 3);
static_assert(Combine(controlled,{},p).state == TagState::Unknown);
static_assert(Combine(unrestricted,unrestricted,p).state == TagState::Unrestricted);
int main()
{
    // Synthetic browse oracle, not the server packet/search implementation.
    std::vector<unsigned> visible, legacy, observe;
    unsigned denied = 0;
    for (unsigned id = 120; id > 0; --id)
    {
        Tag t = id % 3 ? controlled : Tag{TagState::Controlled,7,1,5,0};
        legacy.push_back(id);
        if (Evaluate(en,p,v,t).visible) visible.push_back(id);
        auto d = Evaluate(obs,p,v,t);
        if (d.visible) observe.push_back(id);
        if (!d.wouldPermit) ++denied;
        assert(Evaluate(Mode::Disabled,p,v,t).visible);
    }
    assert(observe == legacy && denied == 40);
    std::sort(visible.begin(),visible.end());
    assert(visible.size() == 80);
    assert(visible[49] == 74 && visible[50] == 76 && visible.back() == 119);
    assert(std::min<std::size_t>(50,visible.size()-50) == 30);
}
