// F14 Core9 PURE MODEL ONLY: not wired to Worldserver or IP E1, all flags OFF.
// No real epoch authentication, F14 provenance, client ACK or debt clearing.
#include <array>
#include <cstdint>
#include <cstdio>
enum class Status { Admitted, Replayed, Excluded, Nested, Full, Nomination, Late, NoPass, Sealed, SendCall, Unsealed, Ended, Frozen, RepeatedSend };
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
        if(!token || token<=highwater) return Status::Replayed;
        highwater=token; // consume token even if viewer unknown/GM; no E1 tombstone
        if(!v || !epoch || epoch!=verifiedEpoch || !eligible) return Status::Excluded;
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
        return p->nominated ? Status::SendCall : Status::NoPass;
    }
    Status end(uint64_t v,uint64_t e,uint64_t t) {
        Pass* p=find(v,e,t); if(!p) return Status::NoPass;
        *p={}; return Status::Ended;
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
    w.unload();
    CHECK(w.begin(101,8,8,17,true)==Status::Frozen);
    CHECK(w.observePostSend(101,8,12)==Status::NoPass);
    if(bad) std::printf("F14 MODEL FAIL %u/%u\n",ok,ok+bad);
    else std::printf("F14 MODEL PASS %u/%u; NO CLIENT ACK OR RUNTIME\n",ok,ok+bad);
    return bad?1:0;
}
