#include "PB59EffectBoundary.h"
#include <cstdio>
#include <stdexcept>

#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %u\n", checks); return 1; } } while (false)
int main()
{
    unsigned checks = 0;
    int owner = 0, other = 0;
    struct Context { bool valid = true; unsigned calls = 0; } context;
    auto verify = [](void* pointer)
    {
        auto& value = *static_cast<Context*>(pointer);
        ++value.calls;
        return value.valid;
    };
    CHECK(PB59::EffectBoundary::Check(&owner));
    {
        PB59::EffectBoundary boundary(&owner, &context, verify);
        CHECK(PB59::EffectBoundary::Check(&owner));
        CHECK(context.calls == 1);
        CHECK(PB59::EffectBoundary::Check(&other));
        CHECK(context.calls == 1);
        context.valid = false;
        CHECK(!PB59::EffectBoundary::Check(&owner));
        CHECK(boundary.Failed());
        context.valid = true;
        CHECK(!PB59::EffectBoundary::Check(&owner));
        CHECK(context.calls == 2);
    }
    CHECK(PB59::EffectBoundary::Check(&owner));
    {
        PB59::EffectBoundary outer(&owner, &context, verify);
        {
            PB59::EffectBoundary nested(&owner, &context, verify);
            CHECK(outer.Failed() && nested.Failed());
            CHECK(!PB59::EffectBoundary::Check(&owner));
        }
        CHECK(!PB59::EffectBoundary::Check(&owner));
    }
    {
        PB59::EffectBoundary outer(&owner, &context, verify);
        { PB59::EffectBoundary nested(&other, &context, verify);
          CHECK(PB59::EffectBoundary::Check(&owner)); CHECK(PB59::EffectBoundary::Check(&other)); }
        CHECK(PB59::EffectBoundary::Check(&owner));
        CHECK(!outer.Failed());
    }
    {
        PB59::EffectBoundary invalid(&owner, nullptr, nullptr);
        CHECK(!PB59::EffectBoundary::Check(&owner));
    }
    {
        PB59::EffectBoundary throwing(&owner, nullptr,
            [](void*) -> bool { throw std::runtime_error("fixture"); });
        CHECK(!PB59::EffectBoundary::Check(&owner));
        CHECK(throwing.Failed());
    }
    {
        PB59::EffectBoundary recursive(&owner, &owner,
            [](void* pointer) { return PB59::EffectBoundary::Check(pointer); });
        CHECK(!PB59::EffectBoundary::Check(&owner));
        CHECK(recursive.Failed());
    }
    CHECK(PB59::EffectBoundary::Check(&owner));
    std::printf("%u actual effect-boundary header checks PASS\n", checks);
}
