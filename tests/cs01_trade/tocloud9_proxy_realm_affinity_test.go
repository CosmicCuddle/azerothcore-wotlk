// Review-only test for walkline/ToCloud9@7fa8f2c7.
// Copy into apps/mysqlreverseproxy/proxy/proxy_realm_affinity_test.go
// alongside the existing proxy_test.go; it intentionally FAILS on that
// upstream source. This Core draft PR does not alter ToCloud9 or worldserver.
// No real MariaDB or owner proxy revision was tested.
package proxy

import (
    "testing"

    "github.com/go-mysql-org/go-mysql/mysql"
    "github.com/walkline/ToCloud9/apps/mysqlreverseproxy/sqlparser"
)

// TestTwoRealmContextStartsAndCommitsOnSameConnection checks the specific
// BEGIN/COMMIT affinity invariant required before any CS01 trade custody.
// A source realm 0 (proxy default) and realm 2 must remain independent.
func TestTwoRealmContextStartsAndCommitsOnSameConnection(t *testing.T) {
    realm0 := new(MockDBConnection)
    realm2 := new(MockDBConnection)
    // Permit both paths so that the test reports a clean assertion failure,
    // rather than panicking on an unexpected testify mock call.
    realm0.On("Execute", StartTransaction, []interface{}(nil)).Return(&mysql.Result{}, nil).Maybe()
    realm2.On("Execute", StartTransaction, []interface{}(nil)).Return(&mysql.Result{}, nil).Maybe()
    realm2.On("Execute", CommitTransaction, []interface{}(nil)).Return(&mysql.Result{}, nil).Once()

    p := NewTC9Proxy(42, map[uint32]DBConnection{0: realm0, 2: realm2}, nil)
    if _, err := p.HandleQuery(StartTransaction); err != nil {
        t.Fatal(err)
    }
    finder := sqlparser.NewCharGUIDFinder()
    ctx := &StmtWithParsedDataContext{
        GuidFinder: &finder,
        Stmts: map[uint32]DBStatement{},
        IsDummy: true,
    }
    if _, err := p.HandleStmtExecute(ctx, DummyPreparedStmt, []interface{}{uint32(2)}); err != nil {
        t.Fatal(err)
    }
    if _, err := p.HandleQuery(CommitTransaction); err != nil {
        t.Fatal(err)
    }
    // Existing ToCloud9 code calls ConnByRealm(0).Execute(START TRANSACTION),
    // while COMMIT goes to ConnByRealm(transactionRealmID)==realm2.
    // Realm2 writes are therefore not protected by the realm0 BEGIN.
    if n := realm0.Calls; len(n) != 0 {
        t.Fatalf("P0: BEGIN incorrectly executed on default realm 0 (%d calls)", len(n))
    }
    realm2.AssertNumberOfCalls(t, "Execute", 2) // BEGIN and COMMIT, same backend
}
