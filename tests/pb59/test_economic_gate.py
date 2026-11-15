#!/usr/bin/env python3
"""Compile exact actor-side mail/refund admission blocks with synthetic dependencies."""
import argparse
from pathlib import Path
import subprocess
import tempfile

MAIL = {
    "HandleSendMail": ("0", "MAIL_SEND"),
    "HandleMailDelete": ("mailId", "MAIL_DELETED"),
    "HandleMailReturnToSender": ("mailId", "MAIL_RETURNED_TO_SENDER"),
    "HandleMailTakeItem": ("mailId", "MAIL_ITEM_TAKEN"),
    "HandleMailTakeMoney": ("mailId", "MAIL_MONEY_TAKEN"),
    "HandleMailCreateTextItem": ("mailId", "MAIL_MADE_PERMANENT"),
}


def unique(source, marker):
    if source.count(marker) != 1:
        raise RuntimeError(f"expected unique source marker: {marker}")
    return source.index(marker)


def extract(source, method, marker, end):
    start = unique(source, method)
    pos = source.index(marker, start)
    finish = source.index(end, pos)
    next_method = source.find("\nvoid ", start + len(method))
    if next_method != -1 and finish >= next_method:
        raise RuntimeError(f"gate outside method: {method}")
    block = source[pos:finish]
    if block.count("Work::InventoryMutation") != 1 or "if (!admission.Admitted())" not in block:
        raise RuntimeError(f"incorrect admission block: {method}")
    return block


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--sanitizers", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    native = root / "src/server/game/Entities/Player"
    mail = (root / "src/server/game/Handlers/MailHandler.cpp").read_text(encoding="utf-8")
    player = (native / "Player.cpp").read_text(encoding="utf-8")
    blocks = {}
    for name, (mail_id, action) in MAIL.items():
        method = f"void WorldSession::{name}(WorldPacket& recvData)"
        block = extract(mail, method,
                        "    // Admit before mailbox callbacks, item/money changes or SQL creation.\n",
                        "    if (!CanOpenMailBox(mailbox))")
        assert f"_player->SendMailResult({mail_id}, {action}, MAIL_ERR_INTERNAL_ERROR);" in block
        prefix = mail[mail.index(method):mail.index(block, mail.index(method))]
        # Packet parsing and early error replies only, before the mailbox access.
        for effect in ("CanOpenMailBox(", "ModifyMoney(", "BeginTransaction(",
                       "OnPlayerCanSendMail(", "GetMail(", "MoveItemFromInventory("):
            assert effect not in prefix, (name, effect)
        blocks[name] = block
    method = "void Player::RefundItem(Item* item)\n{\n"
    refund = extract(player, method,
                     "    // Refund expiration and recipient checks can also mutate the item.\n",
                     "    if (!item->IsRefundable())")
    assert player.index(method) + len(method) == player.index(refund, player.index(method))
    assert "SMSG_ITEM_REFUND_RESULT" in refund and "uint32(10)" in refund
    blocks["RefundItem"] = refund
    fixture = Path(__file__).with_suffix(".cpp")
    with tempfile.TemporaryDirectory(prefix="pb59-economic-") as tmp:
        work = Path(tmp)
        for name, block in blocks.items():
            (work / (name + ".inc")).write_text(block, encoding="utf-8")
        binary = work / "economic"
        command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I", str(native), "-I", str(work), str(fixture), "-o", str(binary)]
        if args.sanitizers:
            command.append("-fsanitize=address,undefined")
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: seven exact native admission blocks; synthetic continuation and source contracts")


if __name__ == "__main__":
    main()
