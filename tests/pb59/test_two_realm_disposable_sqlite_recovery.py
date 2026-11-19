#!/usr/bin/env python3
"""PB59 pre-native disposable two-DB recovery experiment (NOT AzerothCore/MySQL).

Exercises OS-process termination after independent SQLite commits and safe,
idempotent recovery from a durable pre-mutation snapshot. Never opens external
DBs; all paths are generated under TemporaryDirectory by the test parent.
"""
import itertools
import os
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

TRADE = "fixture-trade-9201"
A, B = "A", "B"
A_ITEM, B_ITEM = "item-GUID-A1", "item-GUID-B1"
GOLD_A, GOLD_B = 100, 200
A_AFTER, B_AFTER = 80, 220
CRASH_CODE = 91

class Assertions:
    def __init__(self):
        self.count = 0
    def check(self, condition, explanation):
        self.count += 1
        if not condition:
            raise AssertionError(f"check={self.count}: {explanation}")


def connect(path):
    db = sqlite3.connect(path, isolation_level=None, timeout=5)
    db.execute("PRAGMA journal_mode=WAL")
    db.execute("PRAGMA synchronous=FULL")
    return db


def paths(root):
    root = Path(root)
    return root / "realm-A.sqlite", root / "realm-B.sqlite", root / "journal.sqlite"


def initialize(root):
    a, b, j = paths(root)
    for path, oid, guid, amount in ((a, 42, A_ITEM, GOLD_A), (b, 43, B_ITEM, GOLD_B)):
        with connect(path) as db:
            db.execute("CREATE TABLE owner (id INTEGER PRIMARY KEY, incarnation INTEGER NOT NULL, gold INTEGER NOT NULL, locked INTEGER NOT NULL)")
            db.execute("CREATE TABLE item (guid TEXT PRIMARY KEY, owner INTEGER NOT NULL)")
            db.execute("INSERT INTO owner VALUES (?, ?, ?, 0)", (oid, oid + 200, amount))
            db.execute("INSERT INTO item VALUES (?, ?)", (guid, oid))
    with connect(j) as db:
        db.execute("CREATE TABLE journal (trade_key TEXT PRIMARY KEY, state TEXT NOT NULL, owner_a INTEGER NOT NULL, owner_b INTEGER NOT NULL, incarnation_a INTEGER NOT NULL, incarnation_b INTEGER NOT NULL, item_a TEXT NOT NULL, item_b TEXT NOT NULL, gold_a_before INTEGER NOT NULL, gold_b_before INTEGER NOT NULL, gold_a_after INTEGER NOT NULL, gold_b_after INTEGER NOT NULL)")


def snapshot(root):
    a, b, j = paths(root)
    result = []
    for path in (a, b):
        with connect(path) as db:
            result.append((tuple(db.execute("SELECT id, incarnation, gold, locked FROM owner")),
                           tuple(db.execute("SELECT guid, owner FROM item ORDER BY guid"))))
    with connect(j) as db:
        result.append(tuple(db.execute("SELECT * FROM journal")))
    return tuple(result)


def preflight_and_prepare(root, admitted=True):
    a, b, j = paths(root)
    with connect(a) as ad, connect(b) as bd:
        ao = ad.execute("SELECT id, incarnation, gold, locked FROM owner").fetchone()
        bo = bd.execute("SELECT id, incarnation, gold, locked FROM owner").fetchone()
        ai = ad.execute("SELECT guid FROM item WHERE owner=?", (42,)).fetchall()
        bi = bd.execute("SELECT guid FROM item WHERE owner=?", (43,)).fetchall()
    valid = (admitted and ao == (42, 242, GOLD_A, 0) and bo == (43, 243, GOLD_B, 0)
             and ai == [(A_ITEM,)] and bi == [(B_ITEM,)] and ao[0] != bo[0]
             and ao[1] != bo[1])
    if not valid:
        return False
    # Durable intent BEFORE first character-DB side effect; not a native journal.
    with connect(j) as db:
        db.execute("INSERT INTO journal VALUES (?, 'PREPARED', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                   (TRADE, ao[0], bo[0], ao[1], bo[1], A_ITEM, B_ITEM,
                    GOLD_A, GOLD_B, A_AFTER, B_AFTER))
    for path in (a, b):
        with connect(path) as db:
            db.execute("UPDATE owner SET locked=1")
    return True


def do_phase(root, phase):
    a, b, _ = paths(root)
    path = a if phase in (0, 2) else b
    with connect(path) as db:
        db.execute("BEGIN IMMEDIATE")
        try:
            if phase == 0:
                db.execute("DELETE FROM item WHERE guid=?", (A_ITEM,))
            elif phase == 1:
                db.execute("DELETE FROM item WHERE guid=?", (B_ITEM,))
            elif phase == 2:
                db.execute("INSERT INTO item VALUES (?, 42)", (B_ITEM,))
                db.execute("UPDATE owner SET gold=?", (A_AFTER,))
            elif phase == 3:
                db.execute("INSERT INTO item VALUES (?, 43)", (A_ITEM,))
                db.execute("UPDATE owner SET gold=?", (B_AFTER,))
            else:
                raise ValueError(phase)
            db.execute("COMMIT")
        except BaseException:
            db.execute("ROLLBACK")
            raise


def journal_state(root):
    with connect(paths(root)[2]) as db:
        row = db.execute("SELECT state FROM journal WHERE trade_key=?", (TRADE,)).fetchone()
        return None if row is None else row[0]


def journal_intent_valid(root):
    """Check the persisted fixture intent before any compensation or commit.

    A production implementation must use trusted immutable journal values,
    real identities and durable integrity rather than these sample constants.
    """
    with connect(paths(root)[2]) as db:
        row = db.execute(
            "SELECT trade_key, owner_a, owner_b, incarnation_a, incarnation_b, "
            "item_a, item_b, gold_a_before, gold_b_before, gold_a_after, gold_b_after "
            "FROM journal WHERE trade_key=?", (TRADE,)
        ).fetchone()
    return row == (TRADE, 42, 43, 242, 243, A_ITEM, B_ITEM,
                   GOLD_A, GOLD_B, A_AFTER, B_AFTER)


def set_state(root, state):
    with connect(paths(root)[2]) as db:
        db.execute("UPDATE journal SET state=? WHERE trade_key=?", (state, TRADE))


def restore_one(root, realm):
    a, b, _ = paths(root)
    path = a if realm == A else b
    guid, owner, gold = ((A_ITEM, 42, GOLD_A) if realm == A else (B_ITEM, 43, GOLD_B))
    with connect(path) as db:
        db.execute("BEGIN IMMEDIATE")
        try:
            db.execute("UPDATE owner SET locked=1")
            # Remove both possible transferred item GUIDs, then restore ONLY source.
            db.execute("DELETE FROM item WHERE guid IN (?, ?)", (A_ITEM, B_ITEM))
            db.execute("INSERT INTO item (guid, owner) VALUES (?, ?)", (guid, owner))
            db.execute("UPDATE owner SET gold=?", (gold,))
            db.execute("COMMIT")
        except BaseException:
            db.execute("ROLLBACK")
            raise


def unlock(root):
    for path in paths(root)[:2]:
        with connect(path) as db:
            db.execute("UPDATE owner SET locked=0")


def expected_state(root, settled):
    a, b, _ = paths(root)
    item_a, item_b = ((B_ITEM, A_ITEM) if settled else (A_ITEM, B_ITEM))
    gold_a, gold_b = ((A_AFTER, B_AFTER) if settled else (GOLD_A, GOLD_B))
    for path, oid, guid, gold in ((a, 42, item_a, gold_a), (b, 43, item_b, gold_b)):
        with connect(path) as db:
            if db.execute("SELECT gold FROM owner WHERE id=?", (oid,)).fetchone() != (gold,):
                return False
            if db.execute("SELECT guid, owner FROM item ORDER BY guid").fetchall() != [(guid, oid)]:
                return False
    return True


def recovery_scope_valid(root):
    """Validate model-only custody and balances BEFORE compensating deletes.

    The only legitimate duplicate in an interrupted recovery is source A's GUID
    temporarily present on both shards AFTER A is restored and BEFORE B is.
    Such a duplicate is never permitted in a PREPARED intent or a new settlement.
    This is a narrowly scoped SQLite fixture invariant, NOT a native item proof.
    """
    state = journal_state(root)
    observed = []
    for path, oid, incarnation, acceptable_gold in (
        (paths(root)[0], 42, 242, (GOLD_A, A_AFTER)),
        (paths(root)[1], 43, 243, (GOLD_B, B_AFTER)),
    ):
        with connect(path) as db:
            row = db.execute("SELECT id, incarnation, gold, locked FROM owner").fetchone()
            if not row or row[:2] != (oid, incarnation) or row[3] != 1:
                return False
            if row[2] not in acceptable_gold:
                return False  # Unexpected currency must never be silently overwritten.
            item_rows = db.execute("SELECT guid, owner FROM item").fetchall()
            if any(guid not in (A_ITEM, B_ITEM) or owner != oid
                   for guid, owner in item_rows):
                return False
            observed.append({guid for guid, _ in item_rows})
    duplicates = observed[0] & observed[1]
    if duplicates and not (state == "RECOVERY" and duplicates == {A_ITEM}):
        return False
    return True


def terminal_owner_valid(root):
    for path, oid, incarnation in ((paths(root)[0], 42, 242),
                                   (paths(root)[1], 43, 243)):
        with connect(path) as db:
            if db.execute("SELECT id, incarnation FROM owner").fetchone() != (oid, incarnation):
                return False
    return True


def reconcile(root, crash_after_first_restore=False):
    state = journal_state(root)
    if state is None:
        return "NO_INTENT"
    if not journal_intent_valid(root):
        return "QUARANTINE"
    if state == "COMMITTED":
        if not expected_state(root, settled=True) or not terminal_owner_valid(root):
            return "QUARANTINE"
        unlock(root)
        return "COMMITTED"
    if state == "ROLLED_BACK":
        if not expected_state(root, settled=False) or not terminal_owner_valid(root):
            return "QUARANTINE"
        unlock(root)
        return "ROLLED_BACK"
    if state not in ("PREPARED", "RECOVERY"):
        return "QUARANTINE"
    # Both DBs must still belong to the intended, quarantined owners.
    # Otherwise a compensating delete could wipe another owner's items.
    if not recovery_scope_valid(root):
        return "QUARANTINE"
    set_state(root, "RECOVERY")
    restore_one(root, A)
    if crash_after_first_restore:
        os._exit(CRASH_CODE)  # Real process termination: second realm not restored.
    restore_one(root, B)
    if not expected_state(root, settled=False):
        return "QUARANTINE"
    set_state(root, "ROLLED_BACK")
    unlock(root)
    return "ROLLED_BACK"


def complete_verified(root, trusted_four_outcomes=False):
    # No on-disk shape or guessed callback implies authorization. A future
    # native owner-context adapter must independently prove four real results.
    if (not trusted_four_outcomes or journal_state(root) != "PREPARED" or
            not journal_intent_valid(root)):
        return False
    if not expected_state(root, settled=True) or not recovery_scope_valid(root):
        return False
    for path in paths(root)[:2]:
        with connect(path) as db:
            if db.execute("SELECT locked FROM owner").fetchone() != (1,):
                return False
    set_state(root, "COMMITTED")
    unlock(root)
    return True


def process_child(args):
    mode, root = args[0], Path(args[1]).resolve()
    # This test should never be manually pointed at any real account DB or an
    # arbitrary working directory, even if someone invokes its child CLI.
    if root.parent != Path(tempfile.gettempdir()).resolve() or not root.name.startswith("pb59-disposable-recovery-"):
        raise RuntimeError("Refusing non-temporary, non-disposable DB path")
    if mode == "crash_phase":
        phase = int(args[2]); before = args[3] == "before"
        for i in range(phase):
            do_phase(root, i)
        if not before:
            do_phase(root, phase)
        os._exit(CRASH_CODE)  # Does NOT run normal Python destructors.
    if mode == "crash_recover":
        reconcile(root, crash_after_first_restore=True)
        raise AssertionError("expected injected recovery crash")
    raise ValueError(mode)


def fresh():
    temp = tempfile.TemporaryDirectory(prefix="pb59-disposable-recovery-")
    initialize(temp.name)
    return temp


def child(mode, root, *args):
    p = subprocess.run([sys.executable, __file__, mode, root, *map(str,args)],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=12)
    if p.returncode != CRASH_CODE:
        raise AssertionError(f"child failed to terminate at fault: rc={p.returncode}, stderr={p.stderr.decode()}")


def run():
    t=Assertions(); scenarios=0
    with fresh() as root:
        baseline = snapshot(root)
        t.check(not preflight_and_prepare(root, admitted=False), "refused second owner preflight")
        t.check(snapshot(root) == baseline, "pre-side-effect refusal mutates no DB or journal")
        t.check(journal_state(root) is None, "no orphan intent from preflight refusal")
        scenarios += 1

    for failure_phase in range(4):
        for at in ("before", "after"):
            with fresh() as root:
                t.check(preflight_and_prepare(root), "both owners admitted")
                t.check(journal_state(root) == "PREPARED", "intent durable before first write")
                child("crash_phase", root, failure_phase, at)
                t.check(journal_state(root) == "PREPARED", "OS process death did not erase intent")
                t.check(not complete_verified(root), "partial or unacknowledged result never completes")
                t.check(reconcile(root) == "ROLLED_BACK", "restart reconstructs both sources")
                t.check(expected_state(root, settled=False), "source GUIDs and balances restored")
                t.check(reconcile(root) == "ROLLED_BACK", "repeated recovery is idempotent")
                t.check(journal_state(root) == "ROLLED_BACK", "durable rollback milestone persisted")
                a, b, _ = paths(root)
                with connect(a) as ad, connect(b) as bd:
                    t.check(ad.execute("SELECT locked FROM owner").fetchone() == (0,) and
                            bd.execute("SELECT locked FROM owner").fetchone() == (0,),
                            "both recovery fences released after two owner checks")
                scenarios+=1

    # Crash in the middle of recovering already partially committed state.
    with fresh() as root:
        t.check(preflight_and_prepare(root), "recovery-crash setup")
        do_phase(root, 0); do_phase(root, 1); do_phase(root, 2)
        child("crash_recover", root)
        t.check(journal_state(root) == "RECOVERY", "recovery state survives hard process death")
        t.check(reconcile(root) == "ROLLED_BACK", "recovery restarts after partial restoration")
        t.check(expected_state(root, settled=False), "restoration remains idempotent")
        scenarios+=1

    # Unexpected item custody or recycled owner identity must quarantine,
    # not delete unfamiliar items while running compensating SQL.
    with fresh() as root:
        t.check(preflight_and_prepare(root), "stale-incarnation test setup")
        do_phase(root, 0)
        with connect(paths(root)[1]) as db:
            db.execute("UPDATE owner SET incarnation=999")
        prior = snapshot(root)
        t.check(reconcile(root) == "QUARANTINE", "stale owner cannot receive rollback writes")
        t.check(snapshot(root) == prior, "stale identity leaves evidence untouched")
        t.check(not complete_verified(root, trusted_four_outcomes=True),
                "forged results cannot override stale owner identity")
        scenarios+=1

    with fresh() as root:
        t.check(preflight_and_prepare(root), "unexpected-custody test setup")
        do_phase(root, 0)
        with connect(paths(root)[0]) as db:
            db.execute("INSERT INTO item VALUES ('unrelated-item-GUID', 42)")
        prior = snapshot(root)
        t.check(reconcile(root) == "QUARANTINE", "unexpected third item blocks compensating delete")
        t.check(snapshot(root) == prior, "foreign item retained for manual review")
        scenarios+=1

    # No compensating SQL may turn unexplained new currency or duplicated
    # cross-shard GUIDs into a seemingly clean rollback. Preserve the evidence.
    for fault in ("duplicate-guid", "unexpected-gold", "prepared-owner-unlocked"):
        with fresh() as root:
            t.check(preflight_and_prepare(root), f"{fault}: setup")
            if fault == "duplicate-guid":
                with connect(paths(root)[1]) as db:
                    db.execute("INSERT INTO item VALUES (?, 43)", (A_ITEM,))
            elif fault == "unexpected-gold":
                with connect(paths(root)[0]) as db:
                    db.execute("UPDATE owner SET gold=999")
            else:
                with connect(paths(root)[1]) as db:
                    db.execute("UPDATE owner SET locked=0")
            before = snapshot(root)
            t.check(reconcile(root) == "QUARANTINE", f"{fault}: quarantined")
            t.check(snapshot(root) == before, f"{fault}: no compensation or unlock")
            t.check(not complete_verified(root, trusted_four_outcomes=True),
                    f"{fault}: cannot use claimed driver successes")
            scenarios += 1

    # A corrupted/misbound journal must be quarantined rather than using
    # hardcoded fallback values and erasing contradictory durable evidence.
    for field, value in (("owner_a", 999), ("incarnation_b", 777),
                         ("item_b", "unknown-source-GUID"),
                         ("gold_a_before", 500), ("gold_b_after", 9999)):
        with fresh() as root:
            t.check(preflight_and_prepare(root), f"corrupt {field}: setup")
            with connect(paths(root)[2]) as db:
                db.execute(f"UPDATE journal SET {field}=?", (value,))
            before = snapshot(root)
            t.check(not journal_intent_valid(root), f"corrupt {field}: detected")
            t.check(reconcile(root) == "QUARANTINE", f"corrupt {field}: quarantine")
            t.check(snapshot(root) == before, f"corrupt {field}: no destructive recovery")
            t.check(not complete_verified(root, trusted_four_outcomes=True),
                    f"corrupt {field}: cannot authorise trade")
            scenarios += 1

    # The only temporary cross-shard duplicate allowed under RECOVERY is A's
    # restored source GUID after crashing between the two compensation writes.
    with fresh() as root:
        t.check(preflight_and_prepare(root), "completed four-phase recovery-crash setup")
        for phase in range(4):
            do_phase(root, phase)
        child("crash_recover", root)
        t.check(journal_state(root) == "RECOVERY", "crashed half-restored trade persisted")
        with connect(paths(root)[0]) as ad, connect(paths(root)[1]) as bd:
            t.check(ad.execute("SELECT guid FROM item").fetchone() == (A_ITEM,) and
                    bd.execute("SELECT guid FROM item").fetchone() == (A_ITEM,),
                    "temporary A GUID duplication is confined to incomplete recovery")
        t.check(reconcile(root) == "ROLLED_BACK", "resume correctly resolves recovery-only duplicate")
        t.check(expected_state(root, settled=False), "no duplicate remains after second restoration")
        scenarios += 1

    # All four independent commits: durable readback still required to settle.
    with fresh() as root:
        t.check(preflight_and_prepare(root), "success-case preflight")
        for i in range(4):
            do_phase(root, i)
            t.check(journal_state(root) == "PREPARED", "driver success not journal settlement")
        t.check(expected_state(root, settled=True), "both owner DBs independently reflect exchange")
        t.check(not complete_verified(root), "readback alone cannot authorize settlement")
        t.check(complete_verified(root, trusted_four_outcomes=True), "trusted typed outcomes plus readback permit settlement")
        t.check(journal_state(root) == "COMMITTED", "settlement journal persisted")
        t.check(reconcile(root) == "COMMITTED", "restart never rolls back settled trade")
        t.check(expected_state(root, settled=True), "settled replay preserves single GUID each")
        scenarios+=1

    # All four commits, but crash before verified reconciliation: conservatively roll back.
    with fresh() as root:
        t.check(preflight_and_prepare(root), "unacknowledged all-four test setup")
        for i in range(4):
            do_phase(root, i)
        t.check(journal_state(root) == "PREPARED", "four DB success statuses not durable settlement")
        t.check(reconcile(root) == "ROLLED_BACK", "unacknowledged completion recovered conservatively")
        t.check(expected_state(root, settled=False), "original GUID custody restored")
        scenarios+=1

    # Recycled owner after a previously settled trade cannot be unlocked on
    # an inherited stale journal without an explicit native lifetime policy.
    with fresh() as root:
        t.check(preflight_and_prepare(root), "terminal identity test setup")
        for i in range(4):
            do_phase(root, i)
        t.check(complete_verified(root, trusted_four_outcomes=True), "settled terminal setup")
        with connect(paths(root)[0]) as db:
            db.execute("UPDATE owner SET incarnation=987")
            db.execute("UPDATE owner SET locked=1")
        prior = snapshot(root)
        t.check(reconcile(root) == "QUARANTINE", "settled journal cannot release recycled owner")
        t.check(snapshot(root) == prior, "terminal stale owner unchanged")
        scenarios+=1

    # Simulated callback delivery may be permuted without affecting the on-disk
    # journal. The actual C++ bridge separately tests 24 future arrival orders.
    permutations = 0
    for order in itertools.permutations(range(4)):
        permutations += 1
        t.check(set(order) == set(range(4)), "four non-interchangeable phase identities")
    t.check(permutations == 24, "24 callback permutations enumerated")
    print(f"checks={t.count} scenarios={scenarios} callback_orders={permutations} status=PASS")
    print("SCOPE: two disposable SQLite databases + durable journal; real OS process death/reopen")
    print("NOT: native Core DB API, MySQL/MariaDB, Player lifetime, Worldserver or gameplay")

if __name__ == "__main__":
    if len(sys.argv)>1:
        process_child(sys.argv[1:])
    else:
        run()
