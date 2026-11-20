#include "TradeDbOutcomeFence.h"
#include <cassert>
#include <iostream>
#include <future>
using namespace CosmicTrade;
int main() {
    int n=0;
    auto check=[&](bool b){++n;if(!b)std::abort();};
    auto committed=DatabaseTransactionResult::FromExecutionCode(0);
    check(committed.IsCommitted());
    check(EvaluateOutcomes({1,true,committed},{2,true,committed})==OutcomeDecision::EligibleForReadback);
    check(EvaluateOutcomes({1,true,committed},{1,true,committed})==OutcomeDecision::NeedsReconciliation);
    check(EvaluateOutcomes({0,true,committed},{2,true,committed})==OutcomeDecision::NeedsReconciliation);
    check(EvaluateOutcomes({1,false,committed},{2,true,committed})==OutcomeDecision::Hold);
    check(EvaluateOutcomes({1,true,committed},{2,false,committed})==OutcomeDecision::Hold);
    for(int code:{-1,-2,-3,-4,-5,-6,1213,2006}) {
        auto failure=DatabaseTransactionResult::FromExecutionCode(code);
        check(!failure.IsCommitted());
        check(EvaluateOutcomes({1,true,committed},{2,true,failure})==OutcomeDecision::NeedsReconciliation);
        check(EvaluateOutcomes({1,true,failure},{2,true,committed})==OutcomeDecision::NeedsReconciliation);
    }
    std::promise<DatabaseTransactionResult> promise;
    NonBlockingOutcome left(1,promise.get_future());
    check(!left.Poll().outcomeReceived);
    promise.set_value(committed);
    check(left.Poll().outcomeReceived);
    check(left.Poll().outcome.IsCommitted());
    std::promise<DatabaseTransactionResult> broken;
    NonBlockingOutcome right(2,broken.get_future());
    check(!right.Poll().outcomeReceived);
    broken = std::promise<DatabaseTransactionResult>{};
    auto failed=right.Poll();
    check(failed.outcomeReceived && !failed.outcome.IsCommitted());
    check(EvaluateOutcomes(left.Poll(),failed)==OutcomeDecision::NeedsReconciliation);
    std::future<DatabaseTransactionResult> invalid;
    NonBlockingOutcome missing(3,std::move(invalid));
    check(missing.Poll().outcomeReceived && !missing.Poll().outcome.IsCommitted());
    std::cout<<n<<" native outcome checks passed\n";
}
