// Additional review-only NEGATIVE regression cases for walkline/ToCloud9@7fa8f2c7.
// Transplant into apps/mysqlreverseproxy/proxy beside proxy_test.go.
// Expected RED on upstream: it falls back to an unrelated backend for unknown realms,
// and does not fence a transaction against a mid-transaction realm switch.
// Do not run on the owner's installed proxy without separate permission.
package proxy

import (
    "testing"
    "github.com/go-mysql-org/go-mysql/mysql"
    "github.com/walkline/ToCloud9/apps/mysqlreverseproxy/sqlparser"
)

func TestTradeCustodyUnknownRealmMustNotFallback(t *testing.T) {
    realm0 := new(MockDBConnection)
    realm0.On("Execute", StartTransaction, []interface{}(nil)).Return(&mysql.Result{}, nil).Maybe()
    p := NewTC9Proxy(51, map[uint32]DBConnection{0: realm0}, nil)
    if _, err := p.HandleQuery(StartTransaction); err != nil { t.Fatal(err) }
    finder := sqlparser.NewCharGUIDFinder()
    ctx := &StmtWithParsedDataContext{GuidFinder: &finder, Stmts: map[uint32]DBStatement{}, IsDummy: true}
    // The requested realm 9 does not exist. It MUST fail before any BEGIN.
    _, err := p.HandleStmtExecute(ctx, DummyPreparedStmt, []interface{}{uint32(9)})
    if err == nil { t.Fatal("P0: unknown realm 9 was accepted; custody transaction could be routed to fallback backend") }
    realm0.AssertNumberOfCalls(t, "Execute", 0)
}

func TestTradeCustodyRealmSwitchMustReject(t *testing.T) {
    realm0, realm2 := new(MockDBConnection), new(MockDBConnection)
    realm0.On("Execute", StartTransaction, []interface{}(nil)).Return(&mysql.Result{}, nil).Maybe()
    realm2.On("Execute", StartTransaction, []interface{}(nil)).Return(&mysql.Result{}, nil).Maybe()
    p := NewTC9Proxy(52, map[uint32]DBConnection{0: realm0, 2: realm2}, nil)
    if _, err := p.HandleQuery(StartTransaction); err != nil { t.Fatal(err) }
    finder := sqlparser.NewCharGUIDFinder()
    ctx := &StmtWithParsedDataContext{GuidFinder: &finder, Stmts: map[uint32]DBStatement{}, IsDummy: true}
    if _, err := p.HandleStmtExecute(ctx, DummyPreparedStmt, []interface{}{uint32(2)}); err != nil { t.Fatal(err) }
    // The second dummy realm context attempts to switch from 2 to 0 while
    // TransactionInProgress. The current source silently accepts it.
    if _, err := p.HandleStmtExecute(ctx, DummyPreparedStmt, []interface{}{uint32(0)}); err == nil {
        t.Fatal("P0: realm switch accepted inside active transaction")
    }
}
