#!/usr/bin/env python3
"""Fail-closed source release gate for native PB59 two-owner trading.

Review-only: this script DOES NOT make trades safe or execute the Worldserver.
Run against an exact Core checkout with:
  python3 tests/pb59/test_native_trade_fault_gates.py

Current legacy TradeHandler is intentionally RED until supported native fixes
and their own fault-injection tests establish the properties below. Merely
turning these checks green is not sufficient evidence for merging/deploying.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TRADE = ROOT / "src/server/game/Handlers/TradeHandler.cpp"
STORAGE = ROOT / "src/server/game/Entities/Player/PlayerStorage.cpp"


def block_at(source: str, match: re.Match[str]) -> str:
    """Return the full braced block following an if-expression match."""
    start = source.find("{", match.end())
    if start < 0:
        raise ValueError("missing braced failure block")
    depth = 0
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise ValueError("unclosed failure block")


def check(source: str, storage: str) -> list[str]:
    reasons: list[str] = []
    # Verify the native anchor, not an unrelated file or a truncated stub.
    if "void WorldSession::HandleAcceptTradeOpcode" not in source or "void WorldSession::moveItems" not in source:
        return ["unsupported TradeHandler shape: cannot certify trade safety"]
    if "void Player::_SaveInventory" not in storage:
        return ["unsupported PlayerStorage shape: cannot certify DB custody"]

    method = source.split("void WorldSession::HandleAcceptTradeOpcode", 1)[1]
    end = method.find("void WorldSession::HandleUnacceptTradeOpcode")
    if end < 0:
        return ["unsupported TradeHandler structure: trade acceptance boundary unknown"]
    method = method[:end]
    crossrealm = method.find("if (needsCrossrealmHandling)")
    transfer = method.find("moveItems(myItems, hisItems);")
    if crossrealm < 0 or transfer < 0 or crossrealm >= transfer:
        reasons.append("crossrealm clone/transfer sequence missing or reordered: requires independent review")
        return reasons
    clone_blocks = list(re.finditer(r"\bif\s*\(\s*!newItem\s*\)", method[crossrealm:transfer]))
    if len(clone_blocks) != 2:
        reasons.append(f"expected two crossrealm clone-failure blocks; saw {len(clone_blocks)}: review required")
    for idx, hit in enumerate(clone_blocks, 1):
        try:
            body = block_at(method[crossrealm:transfer], hit)
        except ValueError as exc:
            reasons.append(f"clone #{idx}: {exc}")
            continue
        retained = "myItems[i]" if idx == 1 else "hisItems[i]"
        restores = "MoveItemToInventory(" in body
        advances = bool(re.search(r"\bcontinue\s*;", body))
        cleared = bool(re.search(re.escape(retained) + r"\s*=\s*nullptr\s*;", body))
        if restores and advances and not cleared:
            reasons.append(f"clone #{idx}: restored/detached original remains in transfer array on continue")
        elif advances:
            # Clearing one entry can still make a partial item/gold trade.
            reasons.append(f"clone #{idx}: continue after failed clone may permit partial trade settlement")
        elif "PB59TradeReconciledAfterCloneFailure" not in body:
            # A naked return after item removal is NOT safe recovery. This
            # marker only signals that a separate native test is required.
            reasons.append(f"clone #{idx}: no separately reviewed recovery path after failed clone")

    # Existing crossrealm path commits per-owner deletions before transfer.
    first_early_commit = method.find("CharacterDatabase.CommitTransaction(trans);", crossrealm, transfer)
    if first_early_commit >= 0:
        reasons.append("crossrealm deletion DB commit occurs before final two-owner transfer/reconciliation")

    # The native method must not advertise completion without an independently
    # verifiable acknowledgement/reconciliation gate. A void CommitTransaction
    # call is not a durability result.
    status = method.find("info.Status = TRADE_STATUS_TRADE_COMPLETE")
    if status >= 0 and "CommitTransaction(trans);" in method and "verifiedTradeDurability" not in method:
        reasons.append("TRADE_COMPLETE has no recognised verified durability/reconciliation condition")

    # Legacy void commits and two separate owner saves cannot establish a
    # globally acknowledged crossrealm trade, even after clone fixes.
    final_saves = method.split("// We can't use single transaction with different databases.", 1)
    if len(final_saves) == 2:
        split = final_saves[1].split("else", 1)[0]
        if (split.count("SaveInventoryAndGoldToDB(trans);") >= 2
                and split.count("CharacterDatabase.CommitTransaction(trans);") >= 2):
            reasons.append("crossrealm final owner saves use separate unacknowledged DB commits")
    if "CharacterDatabase.CommitTransaction(trans);" in method and not (
        "AsyncCommitTransactionWithOutcome(" in method
        or "DirectCommitTransactionWithResult(" in method
    ):
        reasons.append("native trade uses void DB commits without a typed transaction outcome")

    # This known API drains dirty inventory state *before* external transaction
    # commit; checking its return value (void) cannot supply durability evidence.
    save = storage.split("void Player::_SaveInventory", 1)[1]
    end_save = save.find("\nvoid Player::")
    save = save[:end_save] if end_save >= 0 else save
    if "m_itemUpdateQueue.clear();" in save:
        reasons.append("_SaveInventory clears pending item updates during external transaction construction")

    # Never silently conclude a ledger test is connected to native gameplay.
    if "TwoOwnerTradePreflight" not in method:
        reasons.append("native HandleAcceptTradeOpcode does not use the two-owner preflight")
    return reasons


def self_test() -> bool:
    # Minimal source-shaped fixture deliberately repeats the critical legacy
    # failure semantics. This validates this parser, not real Core behaviour.
    trade = '''
void WorldSession::moveItems(Item* myItems[], Item* hisItems[]) { }
void WorldSession::HandleAcceptTradeOpcode(WorldPacket&) {
  if (needsCrossrealmHandling) {
    if (!newItem) { _player->MoveItemToInventory(dst, myItems[i], true, true); continue; }
    if (!newItem) { trader->MoveItemToInventory(dst, hisItems[i], true, true); continue; }
    CharacterDatabase.CommitTransaction(trans);
  }
  moveItems(myItems, hisItems);
  // We can't use single transaction with different databases.
  if (needsCrossrealmHandling) {
    _player->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
    trader->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
  } else {
    CharacterDatabase.CommitTransaction(trans);
  }
  info.Status = TRADE_STATUS_TRADE_COMPLETE;
}
void WorldSession::HandleUnacceptTradeOpcode(WorldPacket&) { }
'''
    storage = 'void Player::_SaveInventory(CharacterDatabaseTransaction trans) { m_itemUpdateQueue.clear(); }\nvoid Player::_SaveMail() {}'
    reasons = check(trade, storage)
    required = ["clone #1", "clone #2", "deletion DB commit", "TRADE_COMPLETE", "_SaveInventory", "two-owner preflight", "void DB commits", "separate unacknowledged DB commits"]
    for part in required:
        if not any(part in r for r in reasons):
            print(f"FAIL self-test: did not detect {part}", file=sys.stderr)
            return False
    # A missing source anchor must refuse to certify instead of returning PASS.
    if not check("nonsense", storage) or not check(trade, "nonsense"):
        print("FAIL self-test: accepted unknown source shape", file=sys.stderr)
        return False
    # Ensure braces in a failure block are not mistaken for a second block.
    body = 'if (!newItem) { if (a) { foo(); } continue; }'
    match = re.search(r"if\s*\(!newItem\)", body)
    if match is None or block_at(body, match) != '{ if (a) { foo(); } continue; }':
        print("FAIL self-test: nested block parser", file=sys.stderr)
        return False
    print("PASS self-test: 11 source-gate/parser assertions (synthetic only)")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="test parser against synthetic source without a Core checkout")
    args = parser.parse_args()
    if args.self_test:
        return 0 if self_test() else 1
    if not TRADE.is_file() or not STORAGE.is_file():
        print("BLOCKED exact Core source checkout unavailable; no release clearance", file=sys.stderr)
        return 2
    reasons = check(TRADE.read_text(encoding="utf-8"), STORAGE.read_text(encoding="utf-8"))
    if reasons:
        for i, reason in enumerate(reasons, 1):
            print(f"BLOCKED #{i}: {reason}")
        print("PB59_NATIVE_TRADE_FAULT_GATES=RED (source-only; not native execution)")
        return 1
    # A green source gate is necessary but never sufficient for native release.
    print("PB59_NATIVE_TRADE_FAULT_GATES=GREEN (source-only; native/DB/gameplay not tested)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
