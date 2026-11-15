// Exact native admission blocks, with synthetic protocol and continuation.
#include "PB59TransactionAdmission.h"
#include <cassert>
#include <cstdint>
#include <vector>
using uint32=std::uint32_t;
using A=PB59::TransactionAdmission;
constexpr unsigned MAIL_SEND=1, MAIL_DELETED=2, MAIL_RETURNED_TO_SENDER=3,
    MAIL_ITEM_TAKEN=4, MAIL_MONEY_TAKEN=5, MAIL_MADE_PERMANENT=6,
    MAIL_ERR_INTERNAL_ERROR=7, SMSG_ITEM_REFUND_RESULT=8;
struct Item { unsigned GetGUID() const { return 99; } };
struct WorldPacket
{
    std::vector<unsigned> values;
    WorldPacket(unsigned op,unsigned length){assert(op==SMSG_ITEM_REFUND_RESULT&&length==12);}
    WorldPacket& operator<<(unsigned value){values.push_back(value);return *this;}
};
struct Player
{
    A _pb59Transaction;
    unsigned continued=0, errors=0, id=0, action=0;
    void SendMailResult(unsigned i,unsigned a,unsigned err)
    {assert(err==MAIL_ERR_INTERNAL_ERROR);++errors;id=i;action=a;}
    void SendDirectMessage(WorldPacket* p)
    {assert((p->values==std::vector<unsigned>{99,10}));++errors;}
    void Continue()
    {
        ++continued;
        assert(!_pb59Transaction.Begin()&&!_pb59Transaction.TryAcquireRetirement());
        A::ExternalScope save(_pb59Transaction,A::Work::InventoryAndGold);
        assert(save.Admitted());
    }
    void RefundItem(Item* item)
    {
#include "RefundItem.inc"
        Continue();
    }
};
struct WorldSession
{
    Player* _player;
    void HandleSendMail()
    {
#include "HandleSendMail.inc"
        _player->Continue();
    }
    void HandleMailDelete(unsigned mailId)
    {
#include "HandleMailDelete.inc"
        _player->Continue();
    }
    void HandleMailReturnToSender(unsigned mailId)
    {
#include "HandleMailReturnToSender.inc"
        _player->Continue();
    }
    void HandleMailTakeItem(unsigned mailId)
    {
#include "HandleMailTakeItem.inc"
        _player->Continue();
    }
    void HandleMailTakeMoney(unsigned mailId)
    {
#include "HandleMailTakeMoney.inc"
        _player->Continue();
    }
    void HandleMailCreateTextItem(unsigned mailId)
    {
#include "HandleMailCreateTextItem.inc"
        _player->Continue();
    }
};
constexpr A::StableProof stable{true,true,true,true};
static void exercise(Player& p, bool blocked)
{
    WorldSession session{&p}; Item item;
    auto run=[&](unsigned action,auto call)
    {
        auto before=p.continued, errors=p.errors;
        call();
        if(blocked)
        {
            assert(p.continued==before&&p.errors==errors+1);
            if(action)assert(p.action==action&&p.id==(action==MAIL_SEND?0:42));
        }
        else assert(p.continued==before+1&&p.errors==errors);
    };
    run(MAIL_SEND,[&]{session.HandleSendMail();});
    run(MAIL_DELETED,[&]{session.HandleMailDelete(42);});
    run(MAIL_RETURNED_TO_SENDER,[&]{session.HandleMailReturnToSender(42);});
    run(MAIL_ITEM_TAKEN,[&]{session.HandleMailTakeItem(42);});
    run(MAIL_MONEY_TAKEN,[&]{session.HandleMailTakeMoney(42);});
    run(MAIL_MADE_PERMANENT,[&]{session.HandleMailCreateTextItem(42);});
    run(0,[&]{p.RefundItem(&item);});
}
int main()
{
    Player p; exercise(p,false); assert(p._pb59Transaction.CanBegin());
    auto ticket=p._pb59Transaction.Begin();assert(ticket);
    exercise(p,true);assert(p._pb59Transaction.RequireRecovery(ticket));
    exercise(p,true);assert(p._pb59Transaction.Finish(ticket,stable));
    exercise(p,false);
    {auto retirement=p._pb59Transaction.TryAcquireRetirement();assert(retirement);exercise(p,true);}
    exercise(p,false);
    {auto retirement=p._pb59Transaction.TryAcquireRetirement();assert(retirement.MarkMutation());}
    exercise(p,true);assert(p._pb59Transaction.RetirementRecoveryRequired());
    Player q;
    {A::ExternalScope save(q._pb59Transaction,A::Work::StandaloneSave);assert(save.Admitted());exercise(q,false);}
    assert(q._pb59Transaction.CanBegin());
    {A::ExternalScope logout(q._pb59Transaction,A::Work::Logout);assert(logout.SealOwner());}
    exercise(q,true);
}
