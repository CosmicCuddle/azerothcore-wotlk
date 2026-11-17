#include <cassert>
#include <iostream>
struct Actor { bool serverAccepted=false; bool clientAccepted=false; bool held=false; int money=100; int items=1; };
struct Trace { int backRequester=0, backTrader=0, closesRequester=0, appends=0, commits=0, moves=0; };
// Synthetic policy matrix, not a native TradeHandler implementation.
bool Accept(Actor& requester, Actor& trader, Trace& trace, bool resetCounterparty) {
    requester.clientAccepted=true; // native client anticipates button acceptance
    if (requester.held || trader.held) {
        requester.serverAccepted=false;
        requester.clientAccepted=false;
        ++trace.backRequester;
        if (resetCounterparty && trader.serverAccepted) {
            trader.serverAccepted=false;
            trader.clientAccepted=false;
            ++trace.backTrader;
        }
        return false;
    }
    requester.serverAccepted=true;
    if (!trader.serverAccepted) return true;
    --requester.items; ++trader.items;
    requester.money+=10; trader.money-=10;
    trace.moves+=2; trace.appends+=2; ++trace.commits;
    requester.serverAccepted=trader.serverAccepted=false;
    requester.clientAccepted=trader.clientAccepted=false;
    return true;
}
int main() {
    int n=0;
    auto check=[&](bool p) { ++n; if(!p) { std::cerr<<"FAIL "<<n<<'\n'; std::abort(); } };
    for(bool reset : {false,true}) {
        for(bool previous : {false,true}) {
            Actor a,b; b.serverAccepted=b.clientAccepted=previous; a.held=true; Trace t;
            check(!Accept(a,b,t,reset));
            check(!a.serverAccepted&&!a.clientAccepted&&t.backRequester==1);
            check(b.serverAccepted==(previous&&!reset));
            check(b.clientAccepted==b.serverAccepted);
            check(t.backTrader==(previous&&reset ? 1:0));
            check(t.appends==0&&t.commits==0&&t.moves==0);
            check(a.money==100&&b.money==100&&a.items==1&&b.items==1);
        }
        { Actor a,b; b.serverAccepted=b.clientAccepted=true; a.held=true; Trace t;
          for(int i=0;i<5;++i) check(!Accept(a,b,t,reset));
          check(t.backRequester==5&&t.appends==0&&t.commits==0);
          check(a.money==100&&b.money==100&&a.items==1&&b.items==1);
          a.held=false;
          if(reset) { check(Accept(b,a,t,reset)); check(!Accept(a,b,t,reset)==false); }
          else check(Accept(a,b,t,reset));
          check(t.appends==2&&t.commits==1&&t.moves==2);
          check(a.items==0&&b.items==2);
        }
        { Actor a,b; Trace t; check(Accept(a,b,t,reset)); check(a.serverAccepted&&a.clientAccepted);
          b.held=true; check(!Accept(b,a,t,reset));
          check(!b.serverAccepted&&!b.clientAccepted);
          check(a.serverAccepted==!reset&&a.clientAccepted==!reset);
          check(t.appends==0&&t.commits==0);
          b.held=false;
          if(reset) { check(Accept(a,b,t,reset)); }
          check(Accept(b,a,t,reset));
          check(t.appends==2&&t.commits==1&&t.moves==2);
        }
    }
    std::cout<<"checks="<<n<<" status=PASS\n";
}
