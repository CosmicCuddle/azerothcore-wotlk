// Pure native C++ 128-case truth table. No actual player, packet or DB.
#include "F14PlayerClientVisibilityQuery.h"
#include <cstdio>
#include <cstdlib>
using CosmicF14PlayerClientVisibilityQuery::Candidate;
using CosmicF14PlayerClientVisibilityQuery::MayAskScript;
static void Check(bool ok, char const* name)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
int main()
{
    int positive = 0;
    for (unsigned n = 0; n < 128; ++n)
    {
        Candidate c {bool(n&1),bool(n&2),bool(n&4),bool(n&8),
                     bool(n&16),bool(n&32),bool(n&64)};
        Check(MayAskScript(c) == (n == 49), "128-case independent negative gate");
        if (MayAskScript(c)) ++positive;
    }
    Check(positive==1,"exactly one pre-query candidate, not permission to hide");
    std::puts("PASS 128-case F14 player query model (source-only, OFF)");
    return 0;
}
