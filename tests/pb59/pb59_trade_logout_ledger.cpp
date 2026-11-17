#include <cstdlib>
#include <iostream>
#include <utility>
struct Ledger {
    int depth=0, accepted=0, rejected=0, mutations=0, appends=0, commits=0;
    bool pendingLogout=false, replayed=false, alive=true;
    bool admit(bool held, bool ownerPresent, bool sessionPresent) {
        if (held || !ownerPresent || !sessionPresent || !alive) { ++rejected; return false; }
        ++depth; ++accepted; return true;
    }
    void requestLogout() { pendingLogout=true; }
    void leave() { if (depth<=0) std::abort(); --depth; }
    bool replayLogout() {
        if (depth || !pendingLogout || replayed) return false;
        replayed=true; alive=false; return true;
    }
};
int main() {
 int checks=0, scenarios=0;
 auto ck=[&](bool b){ ++checks; if(!b){std::cerr<<"FAIL "<<checks<<'\n';std::abort();}};
 for(int held=0;held<2;++held) for(int owner=0;owner<2;++owner)
 for(int session=0;session<2;++session) for(int logoutMoment=0;logoutMoment<3;++logoutMoment) {
   Ledger x; ++scenarios;
   if(logoutMoment==0) x.requestLogout();
   bool ok=x.admit(held,owner,session);
   ck(ok==(!held&&owner&&session));
   ck(x.depth==(ok?1:0));
   if(logoutMoment==1) x.requestLogout();
   if(ok) {
      // No durable mutation is performed by admission itself.
      ck(x.mutations==0&&x.appends==0&&x.commits==0);
      ck(!x.replayLogout()); // scope remains active even if logout pending
      x.leave();
   }
   if(logoutMoment==2) x.requestLogout();
   ck(x.depth==0);
   ck(x.pendingLogout==(logoutMoment>=0));
   ck(!x.replayed); // scope exit does not automatically replay pending logout
   ck(x.replayLogout()); ck(!x.replayLogout());
   ck(!x.alive&&x.replayed);
   ck(x.mutations==0&&x.appends==0&&x.commits==0);
 }
 std::cout<<"scenarios="<<scenarios<<" checks="<<checks<<" PASS (synthetic ledger only)\n";
}
