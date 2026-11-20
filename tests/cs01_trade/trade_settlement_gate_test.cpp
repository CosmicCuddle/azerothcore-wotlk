#include "TradeSettlementGate.h"
#include <cassert>
#include <iostream>
using namespace CosmicTrade;
int main()
{
    Receipt a{77, 1, 1001, 9001, SideState::Prepared, true};
    Receipt b{77, 2, 2002, 9002, SideState::Prepared, true};
    int checks = 0;
    auto expect = [&](Decision d) { assert(Decide(a, b) == d); ++checks; };
    expect(Decision::Hold);
    a.state = SideState::Committed; expect(Decision::Hold);
    b.state = SideState::Committed; expect(Decision::Complete);
    b.durable = false; expect(Decision::Hold);
    b.durable = true; b.state = SideState::Refunded; expect(Decision::ManualRecovery);
    b.state = SideState::Conflict; expect(Decision::ManualRecovery);
    b.state = SideState::Committed; b.tradeId = 78; expect(Decision::ManualRecovery);
    b.tradeId = 77; b.realm = 1; expect(Decision::ManualRecovery);
    b.realm = 2; a.payloadHash = 0; expect(Decision::ManualRecovery);
    a.payloadHash = 9001; b.owner = 0; expect(Decision::ManualRecovery);
    b.owner = 2002; a.durable = false; expect(Decision::Hold);
    a.durable = true; expect(Decision::Complete);
    std::cout << "PASS " << checks << "/" << checks << "\n";
}
