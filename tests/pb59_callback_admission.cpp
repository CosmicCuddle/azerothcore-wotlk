#include "../src/server/game/Entities/Player/PB59TransactionAdmission.h"
#include <cassert>
using A=PB59::TransactionAdmission;
int main(){
 A a;
 { A::ExternalScope c(a,A::Work::Callback); assert(c.Admitted());
   assert(!a.TryAcquireRetirement()); assert(!a.Begin());
   { A::ExternalScope nested(a,A::Work::Callback); assert(nested.Admitted()); }
   assert(a.Request(A::Work::Destroy)==A::Decision::RetainOwner);
   assert(a.Request(A::Work::Redirect)==A::Decision::RejectBeforeMutation);
 }
 { auto r=a.TryAcquireRetirement(); assert(r);
   A::ExternalScope c(a,A::Work::Callback); assert(!c.Admitted()); }
 auto t=a.Begin(); assert(t);
 { A::ExternalScope c(a,A::Work::Callback); assert(!c.Admitted()); }
 assert(a.Finish(t,{true,true,true,true}));
 { A::ExternalScope c(a,A::Work::Callback); assert(c.Admitted());
   assert(a.Request(A::Work::Logout)==A::Decision::DeferLogout); }
 { A::ExternalScope c(a,A::Work::Callback); assert(!c.Admitted()); }
 { A::ExternalScope l(a,A::Work::Logout); assert(l.Admitted()); assert(l.SealOwner()); }
 { A::ExternalScope c(a,A::Work::Callback); assert(!c.Admitted()); }
}
