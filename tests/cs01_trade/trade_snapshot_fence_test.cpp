#include "TradeSnapshotFence.h"
#include <cstdlib>
#include <iostream>
using namespace CosmicTrade;
int main() {
 int checks=0; auto ck=[&](bool ok){++checks;if(!ok){std::cerr<<"FAIL "<<checks<<'\n';std::abort();}};
 OwnerSnapshot a{1,42,1,100,200,2,888,999,7};
 OwnerSnapshot b{1,42,2,200,100,1,999,888,8};
 DurableSnapshotReadback ra{a,true,true,1,100,1},rb{b,true,true,2,200,1};
 auto verify=[&](){return VerifyPreparedPair(a,b,ra,rb);};
 ck(verify()==SnapshotCheck::PreparedVerified);
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
 a.version=2;ck(verify()==SnapshotCheck::Reject);a.version=1;
 a.payloadHash=0;ck(verify()==SnapshotCheck::Reject);a.payloadHash=888;
 a.revision=0;ck(verify()==SnapshotCheck::Reject);a.revision=7;
 // F66-4: one party's peer offer must be bound into the opposite snapshot.
 a.peerPayloadHash++;ck(verify()==SnapshotCheck::Reject);a.peerPayloadHash=999;
 b.peerPayloadHash++;ck(verify()==SnapshotCheck::Reject);b.peerPayloadHash=888;
 // F66-2: a caller must supply realm/owner/sequence provenance.
 ra.queriedRealm=2;ck(verify()==SnapshotCheck::RecoveryRequired);ra.queriedRealm=1;
 rb.queriedOwner=201;ck(verify()==SnapshotCheck::RecoveryRequired);rb.queriedOwner=200;
 ra.journalSequence=0;ck(verify()==SnapshotCheck::RecoveryRequired);ra.journalSequence=1;
 rb.journalSequence=0;ck(verify()==SnapshotCheck::RecoveryRequired);rb.journalSequence=1;
 // F66-3: low GUIDs may coincide across DIFFERENT realms.
 b.owner=100; a.peerOwner=100; ra.snapshot=a; rb.snapshot=b; rb.queriedOwner=100;
 ck(verify()==SnapshotCheck::PreparedVerified);
 b.owner=200; a.peerOwner=200; ra.snapshot=a; rb.snapshot=b; rb.queriedOwner=200;
 // F66-1: missing readback and fully verified PREPARED are distinct.
 ck(verify()==SnapshotCheck::PreparedVerified);
 rb.persisted=false;ck(verify()==SnapshotCheck::HoldForReadback);rb.persisted=true;
 ck(verify()==SnapshotCheck::PreparedVerified);
 std::cout<<checks<<" snapshot peer-binding assertions passed\n";
}
