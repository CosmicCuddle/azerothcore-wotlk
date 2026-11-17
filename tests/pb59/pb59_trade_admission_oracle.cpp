#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

struct Ledger { enum Phase { Idle, Active, Retirement, Recovery } phase = Idle; bool sealed = false; int depth = 0; bool pendingLogout = false; bool alive = true; };
struct Scope {
 Ledger* l = nullptr; bool ok = false;
 explicit Scope(Ledger& x, bool failAlloc=false) noexcept { if (!x.sealed && x.alive && x.phase==Ledger::Idle && x.depth<100 && !failAlloc) { l=&x; ++l->depth; ok=true; } }
 Scope(Scope const&) = delete; Scope& operator=(Scope const&) = delete;
 ~Scope(){ if(l) --l->depth; }
};
struct Actor { Ledger ledger; int money=100; int items=1; bool accepted=false; };
struct Tx { int appends=0; int commits=0; void append(){++appends;} void commit(){++commits;} };
struct Trace { int closes=0; int back=0; int moves=0; int logoutDeferred=0; };
// Synthetic *same-realm* admission oracle. Not native TradeHandler integration.
bool Accept(Actor& a, Actor& b, Tx& tx, Trace& tr, bool crossrealm=false, bool failA=false, bool failB=false, bool requestLogout=false) {
  // Deliberately refuse crossrealm pending durable atomicity proof.
  if (crossrealm) { ++tr.closes; return false; }
  Scope sa(a.ledger,failA); Scope sb(b.ledger,failB);
  if (!sa.ok || !sb.ok) { ++tr.closes; ++tr.back; return false; }
  a.accepted=true;
  if (!b.accepted) return true; // first acceptance does not execute
  if (requestLogout) { a.ledger.pendingLogout=true; ++tr.logoutDeferred; }
  --a.items; ++b.items; a.money+=10; b.money-=10; tr.moves+=2;
  tx.append(); tx.append(); tx.commit();
  a.accepted=false; b.accepted=false;
  return true;
}
int main(){int n=0;auto check=[&](bool p){++n;if(!p)throw std::runtime_error("failed assertion "+std::to_string(n));};
 {Actor a,b; b.accepted=true; Tx tx;Trace tr; check(Accept(a,b,tx,tr));check(tx.appends==2&&tx.commits==1);check(a.ledger.depth==0&&b.ledger.depth==0);check(a.items==0&&b.items==2);}
 for(int mode=0;mode<8;++mode){Actor a,b; b.accepted=true;Tx tx;Trace tr; if(mode==0)a.ledger.phase=Ledger::Active; if(mode==1)b.ledger.phase=Ledger::Active; if(mode==2)a.ledger.phase=Ledger::Recovery; if(mode==3)b.ledger.phase=Ledger::Retirement; if(mode==4)a.ledger.sealed=true; if(mode==5)b.ledger.alive=false;bool failA=mode==6,failB=mode==7; check(!Accept(a,b,tx,tr,false,failA,failB));check(tx.appends==0&&tx.commits==0);check(a.items==1&&b.items==1&&a.money==100&&b.money==100);check(!a.accepted&&b.accepted);check(a.ledger.depth==0&&b.ledger.depth==0);check(tr.closes==1&&tr.back==1);}
 {Actor a,b;Tx tx;Trace tr;check(Accept(a,b,tx,tr));check(a.accepted&&tx.appends==0);b.ledger.phase=Ledger::Active;check(!Accept(b,a,tx,tr));check(tx.appends==0&&a.items==1&&b.items==1);check(a.ledger.depth==0&&b.ledger.depth==0);}
 {Actor a,b;Tx tx;Trace tr;check(!Accept(a,b,tx,tr,true));check(tx.appends==0&&tx.commits==0&&tr.closes==1);}
 {Actor a,b;b.accepted=true;Tx tx;Trace tr;check(Accept(a,b,tx,tr,false,false,false,true));check(a.ledger.pendingLogout&&tr.logoutDeferred==1);check(a.ledger.depth==0&&b.ledger.depth==0);}
 std::cout<<"checks="<<n<<" status=PASS\n";}
