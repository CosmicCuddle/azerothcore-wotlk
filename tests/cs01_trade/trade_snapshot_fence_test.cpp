#include "TradeSnapshotFence.h"
#include <cstdlib>
#include <iostream>
using namespace CosmicTrade;
int main() {
 int checks=0; auto ck=[&](bool ok){++checks;if(!ok){std::cerr<<"FAIL "<<checks<<'\n';std::abort();}};
 OwnerSnapshot a{1,42,1,100,200,2,888,7}, b{1,42,2,200,100,1,999,8};
 DurableSnapshotReadback ra{a,true,true}, rb{b,true,true};
 auto verify=[&](){return VerifyPreparedPair(a,b,ra,rb);};
 ck(verify()==SnapshotCheck::HoldForReadback);
 ra.persisted=false;ck(verify()==SnapshotCheck::HoldForReadback);ra.persisted=true;
 rb.persisted=false;ck(verify()==SnapshotCheck::HoldForReadback);rb.persisted=true;
 ra.prepared=false;ck(verify()==SnapshotCheck::RecoveryRequired);ra.prepared=true;
 rb.prepared=false;ck(verify()==SnapshotCheck::RecoveryRequired);rb.prepared=true;
 ra.snapshot.revision++;ck(verify()==SnapshotCheck::RecoveryRequired);ra.snapshot=a;
 rb.snapshot.payloadHash++;ck(verify()==SnapshotCheck::RecoveryRequired);rb.snapshot=b;
 rb.snapshot.owner++;ck(verify()==SnapshotCheck::RecoveryRequired);rb.snapshot=b;
 ra.snapshot.tradeId++;ck(verify()==SnapshotCheck::RecoveryRequired);ra.snapshot=a;
 a.peerOwner++;ck(verify()==SnapshotCheck::Reject);a.peerOwner=b.owner;
 b.peerRealm++;ck(verify()==SnapshotCheck::Reject);b.peerRealm=a.realm;
 b.tradeId++;ck(verify()==SnapshotCheck::Reject);b.tradeId=a.tradeId;
 b.realm=a.realm;ck(verify()==SnapshotCheck::Reject);b.realm=2;
 b.owner=a.owner;ck(verify()==SnapshotCheck::Reject);b.owner=200;
 a.version=2;ck(verify()==SnapshotCheck::Reject);a.version=1;
 a.payloadHash=0;ck(verify()==SnapshotCheck::Reject);a.payloadHash=888;
 a.revision=0;ck(verify()==SnapshotCheck::Reject);a.revision=7;
 ck(verify()==SnapshotCheck::HoldForReadback);
 std::cout<<checks<<" snapshot fence assertions passed\n";
}
