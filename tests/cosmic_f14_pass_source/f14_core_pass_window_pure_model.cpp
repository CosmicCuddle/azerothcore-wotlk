// F14 Core9 PURE MODEL ONLY: not wired to Worldserver or IP E1, all flags OFF.
// No real epoch authentication, F14 provenance, client ACK or debt clearing.
// CAUTION: this model cannot validate that a Core seal hook is located BEFORE
// the actual SendDirectMessage. A wholly fabricated nominate/seal/send sequence
// constructed AFTER the real send would still pass this synthetic test.
// Only a separately reviewed, real Core-owned pre-send hook can enforce order.
// All supplied facts, including token provenance/epoch and map ownership, are
// UNTRUSTED model arguments; no visible/hidden decision is executed here.
// The one highwater is valid ONLY for strictly ordered genuine Core tokens on
// one map owner; cross-map transfer/debt handoff and client receipt are unsolved.
#include <array>
#include <cstdint>
#include <cstdio>
enum class Status { Admitted, Replayed, Excluded, Nested, Full, Nomination, Late, NoPass, Sealed, SendCall, Unsealed, Ended, Frozen, RepeatedSend, NoCandidate, EndedWithoutObservedSend };
struct Pass { uint64_t viewer=0, epoch=0, token=0; bool nominated=false, sealed=false, reported=false; };
template<unsigned N=2> struct Window {
    std::array<Pass,N> active{};
    uint64_t highwater=0;
    bool frozen=false;
    Pass* find(uint64_t v,uint64_t e,uint64_t t) {
        for(auto& p:active) if(p.token && p.viewer==v && p.epoch==e && p.token==t) return &p;
        return nullptr;
    }
    Status begin(uint64_t v,uint64_t epoch,uint64_t verifiedEpoch,uint64_t token,bool eligible) {
        if(frozen) return Status::Frozen;
        // Never let an unauthenticated/GM/unknown token poison the highwater:
        // genuine Core-issued tokens must be established by a future producer.
        if(!v || !epoch || epoch!=verifiedEpoch || !eligible) return Status::Excluded;
        if(!token || token==~uint64_t{0} || token<=highwater) return Status::Replayed;
        highwater=token; // synthetic accepted tokens only; real order unproven
        for(auto const& p:active) if(p.token && p.viewer==v) return Status::Nested;
        for(auto& p:active) if(!p.token) { p={v,epoch,token,false,false,false}; return Status::Admitted; }
        return Status::Full; // no eviction of a real existing pass
    }
    Status nominate(uint64_t v,uint64_t e,uint64_t t) {
        Pass* p=find(v,e,t); if(!p) return Status::NoPass;
        if(p->sealed) return Status::Late;
        p->nominated=true; return Status::Nomination;
    }
    Status sealBeforeSend(uint64_t v,uint64_t e,uint64_t t) {
        Pass* p=find(v,e,t); if(!p) return Status::NoPass;
        if(p->sealed) return Status::Late;
        p->sealed=true; return Status::Sealed;
    }
    Status observePostSend(uint64_t v,uint64_t e,uint64_t t) {
        Pass* p=find(v,e,t); if(!p) return Status::NoPass;
        if(!p->sealed) return Status::Unsealed;
        if(p->reported) return Status::RepeatedSend;
        p->reported=true;
        return p->nominated ? Status::SendCall : Status::NoCandidate;
    }
    Status end(uint64_t v,uint64_t e,uint64_t t) {
        Pass* p=find(v,e,t); if(!p) return Status::NoPass;
        bool const noObservedCall=p->nominated && !p->reported;
        *p={}; return noObservedCall ? Status::EndedWithoutObservedSend : Status::Ended;
    }
    void forget(uint64_t v) { for(auto& p:active) if(p.viewer==v) p={}; }
    void unload() { active={}; frozen=true; } // cannot reset without debt handoff
};
static unsigned ok=0, bad=0;
#define CHECK(x) do { if (x) ++ok; else { ++bad; std::printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
int main() {
    Window<> w;
    CHECK(w.begin(101,7,7,10,true)==Status::Admitted);
    CHECK(w.nominate(101,7,10)==Status::Nomination);
    CHECK(w.observePostSend(101,7,10)==Status::Unsealed);
    CHECK(w.sealBeforeSend(101,7,10)==Status::Sealed);
    CHECK(w.observePostSend(101,7,10)==Status::SendCall);
    CHECK(w.observePostSend(101,7,10)==Status::RepeatedSend);
    CHECK(w.sealBeforeSend(101,7,10)==Status::Late);
    CHECK(w.nominate(101,7,10)==Status::Late);
    CHECK(w.end(101,7,10)==Status::Ended);
    w.forget(101);
    CHECK(w.begin(101,7,7,10,true)==Status::Replayed); // same epoch after forget
    CHECK(w.begin(101,7,8,11,true)==Status::Excluded); // old epoch after relog
    CHECK(w.begin(101,8,8,12,true)==Status::Admitted);
    CHECK(w.begin(101,8,8,13,true)==Status::Nested);
    CHECK(w.end(101,8,13)==Status::NoPass);
    CHECK(w.begin(202,9,9,14,false)==Status::Excluded); // unknown / GM not E1 slot
    CHECK(w.begin(303,9,9,15,true)==Status::Admitted);
    CHECK(w.begin(404,9,9,16,true)==Status::Full);
    CHECK(w.end(303,9,15)==Status::Ended);
    // Review M1: forgetting a LIVE sealed+pending pass MUST revoke its send
    // evidence (previous tests only forgot an already-ended pass).
    Window<> activeForget;
    CHECK(activeForget.begin(501,20,20,21,true)==Status::Admitted);
    CHECK(activeForget.nominate(501,20,21)==Status::Nomination);
    CHECK(activeForget.sealBeforeSend(501,20,21)==Status::Sealed);
    activeForget.forget(501);
    CHECK(activeForget.observePostSend(501,20,21)==Status::NoPass);
    CHECK(activeForget.nominate(501,20,21)==Status::NoPass);
    CHECK(activeForget.end(501,20,21)==Status::NoPass);
    CHECK(activeForget.begin(501,20,20,21,true)==Status::Replayed);
    CHECK(activeForget.begin(501,21,21,22,true)==Status::Admitted);

    // Review M1/L2: a sealed-but-un-nominated generic Core pass must NEVER
    // advertise F14 send evidence. Diagnostics distinguish it from no pass.
    Window<> empty;
    CHECK(empty.begin(601,30,30,31,true)==Status::Admitted);
    CHECK(empty.sealBeforeSend(601,30,31)==Status::Sealed);
    CHECK(empty.observePostSend(601,30,31)==Status::NoCandidate);
    CHECK(empty.observePostSend(601,30,31)==Status::RepeatedSend);
    CHECK(empty.end(601,30,31)==Status::Ended);

    // L3: a nominated sealed pass with no observed send must report this
    // unresolved state. It is NOT client evidence and MUST NOT discharge debt.
    Window<> unsent;
    CHECK(unsent.begin(701,40,40,41,true)==Status::Admitted);
    CHECK(unsent.nominate(701,40,41)==Status::Nomination);
    CHECK(unsent.sealBeforeSend(701,40,41)==Status::Sealed);
    CHECK(unsent.end(701,40,41)==Status::EndedWithoutObservedSend);
    CHECK(unsent.observePostSend(701,40,41)==Status::NoPass);

    // M3: unauthenticated epoch=0 + maximum token cannot poison the map.
    // This does NOT authenticate the source of a future seemingly valid token.
    Window<> maxToken;
    CHECK(maxToken.begin(801,0,0,~uint64_t{0},false)==Status::Excluded);
    CHECK(maxToken.begin(801,50,50,51,true)==Status::Admitted);
    CHECK(maxToken.end(801,50,51)==Status::Ended);
    CHECK(maxToken.begin(801,50,50,~uint64_t{0},true)==Status::Replayed);
    CHECK(maxToken.begin(802,51,51,52,true)==Status::Admitted);

    // L1: out-of-order token across viewers is conservatively refused;
    // genuine Core must establish ordering on this map worker at Begin.
    Window<> outOfOrder;
    CHECK(outOfOrder.begin(901,60,60,80,true)==Status::Admitted);
    CHECK(outOfOrder.begin(902,60,60,79,true)==Status::Replayed);

    w.unload();
    CHECK(w.begin(101,8,8,17,true)==Status::Frozen);
    CHECK(w.observePostSend(101,8,12)==Status::NoPass);
    if(bad) std::printf("F14 MODEL FAIL %u/%u\n",ok,ok+bad);
    else std::printf("F14 MODEL PASS %u/%u; NO CLIENT ACK OR RUNTIME\n",ok,ok+bad);
    return bad?1:0;
}
