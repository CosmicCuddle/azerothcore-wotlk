"""Run native item statement capture and save bodies with local Item and pool doubles."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
parser.add_argument("--mutants", action="store_true")
parser.add_argument("--batch-mutants", action="store_true")
parser.add_argument("--receipt-mutants", action="store_true")
parser.add_argument("--hold-mutants", action="store_true")
parser.add_argument("--pending-recovery-mutants", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
database = root / "src/server/database/Database"
item = root / "src/server/game/Entities/Item"
player = root / "src/server/game/Entities/Player"
source = Path(__file__).with_name("item_instance_save_checks.cpp")


def section(path, begin, end):
    text = path.read_text(encoding="utf-8")
    start = text.index(begin)
    return text[start:text.index(end, start + len(begin))]


definitions = "\n".join([
    section(database / "PreparedStatement.cpp", "PreparedStatementBase::PreparedStatementBase(", "//- Execution"),
    section(database / "Transaction.cpp", "void TransactionBase::AppendPreparedStatement(",
            "bool TransactionTask::Execute()"),
    section(database / "Transaction.cpp", "TransactionWithOutcomeTask::~TransactionWithOutcomeTask()",
            "bool TransactionCallback::"),
    section(database / "DatabaseWorkerPool.cpp",
            "template <class T>\nvoid DatabaseWorkerPool<T>::AsyncCommitPreparedTransactionWithTransferredOutcome(",
            "template <class T>\nvoid DatabaseWorkerPool<T>::DirectCommitTransaction("),
    section(item / "Item.cpp", "bool Item::AppendItemInstanceSave(", "bool Item::LoadFromDB("),
    section(item / "Item.cpp", "uint8 Item::GetBagSlot()", "bool Item::IsEquipped()"),
    section(item / "Container/Bag.cpp", "Item* Bag::GetItemByPos(", "std::string Bag::GetDebugInfo()"),
    section(player / "PlayerStorage.cpp", "Item* Player::GetItemByPos(uint16", "std::optional<PB59::Stamp>"),
    section(player / "PlayerStorage.cpp", "Bag* Player::GetBagByPos(", "uint32 Player::GetFreeInventorySpace()"),
    section(player / "PlayerStorage.cpp", "std::optional<PB59::InventorySlotFacts> Player::CapturePB59InventorySlot(",
            "bool Player::SuspendPB59OriginalQueue("),
    section(player / "PlayerStorage.cpp", "namespace\n{\n    using SaveObserver",
            "void Player::DrainPB59CharacterSaveEvidence("),
])
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.ndebug:
    flags += ["-DNDEBUG", "-O2"]

with tempfile.TemporaryDirectory(prefix="pb59-item-instance-save-") as temporary:
    work = Path(temporary)
    binary = work / "item-save.exe"
    aliases = "\n".join(f"using {sign}{width} = std::{sign}{width}_t;"
                        for sign in ("int", "uint") for width in (8, 16, 32, 64))
    (work / "Define.h").write_text(
        f"#pragma once\n#include <cstdint>\n#define AC_DATABASE_API\n{aliases}\n", encoding="utf-8")
    (work / "StringFormat.h").write_text(
        "#pragma once\n#include <string>\n#include <string_view>\n"
        "namespace Acore { template<class... Args> std::string StringFormat(std::string_view, Args&&...); }\n",
        encoding="utf-8")
    extracted = work / "ItemInstanceSaveDefinitions.inc"
    extracted.write_text(definitions, encoding="utf-8")
    include_paths = (work, database, player, root / "src/common/Utilities", root / "src/common/Threading")
    includes = [argument for path in include_paths for argument in ("-I", str(path))]
    compile_fixture = args.compiler + flags + includes + [str(source), "-o", str(binary)]
    subprocess.run(compile_fixture, check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
    if args.mutants or args.batch_mutants or args.receipt_mutants or args.hold_mutants or args.pending_recovery_mutants:
        variants = []
        if args.mutants:
            variants += [
                ("auto const owner = GetOwnerGUID().GetRawValue();",
                 "auto const owner = GetOwnerGUID().GetCounter();"),
                ("if (state == ITEM_CHANGED && IsWrapped())", "if (IsWrapped())"),
                ("i < MAX_ENCHANTMENT_SLOT; ++i)\n    {\n        ssEnchants",
                 "i < MAX_ENCHANTMENT_SLOT - 1; ++i)\n    {\n        ssEnchants"),
                ("instance->SetData(++index, GetItemRandomPropertyId());",
                 "instance->SetData(++index, static_cast<uint32>(GetItemRandomPropertyId()));"),
                ("            (void)AppendItemInstanceSave(*trans);\n", ""),
                ("if (!links || links->item != expected)", "if (!links || !links->item)"),
                ("item->GetContainer() != container || item->GetPos() != pos ||", "item->GetPos() != pos ||"),
                (" || GetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + bag * 2) != containerGuid", ""),
                ("std::ranges::any_of(m_enchantDuration, [&](auto const& timer) { return timer.item == &item; })",
                 "false"),
                ("link->SetData(1, links->containerGuid.GetCounter());", "link->SetData(1, uint32{0});"),
            ]
        if args.batch_mutants:
            variants += [
                ("!positions[index] || !CheckPB59InventoryItemSave(positions[index], queue[index])",
                 "!positions[index]"),
                ("return id && ids.insert(id).second;", "return id && (void(ids.insert(id)), true);"),
                ("if (!eligible() || m_itemUpdateQueue != queue)", "if (m_itemUpdateQueue != queue)"),
                ("if (!eligible() || m_itemUpdateQueue != queue)", "if (!eligible())"),
                ("if (!hasWork || captured.incomplete || !eligible() || m_itemUpdateQueue != queue)",
                 "if ((void(hasWork), captured.incomplete) || !eligible() || m_itemUpdateQueue != queue)"),
            ]
        if args.receipt_mutants:
            variants += [
                (" || scope.GetTicket() != operation.ticket", ""),
                ("stamp, *transaction, &prepared->membership)",
                 "stamp, *CharacterDatabase.BeginTransaction(), &prepared->membership)"),
                ("&prepared->membership) || !current())", "&prepared->membership))"),
                (" || captured.incomplete", ""),
                ("        else if (observer.AbandonProtected(*receipt))\n"
                 "            (void)observer.ConsumeProtected(*receipt);", ""),
            ]
        if args.hold_mutants:
            variants += [
                ("auto capture = fence.TryReserveCapture(operation.owner);\n"
                 "        if (!capture)\n            return {};\n"
                 "        auto hold = fence.TryPromoteCapture(*capture, operation.owner);",
                 "auto capture = fence.TryReserveCapture(operation.owner + 1);\n"
                 "        if (!capture)\n            return {};\n"
                 "        auto hold = fence.TryPromoteCapture(*capture, operation.owner + 1);"),
                ("!task->GetTransaction() ||\n            !Current(owner, operation, scope, stamp)",
                 "!task->GetTransaction() ||\n            (void(Current(owner, operation, scope, stamp)), false)"),
                ("hold.Settle(record->result)", "PB59RecoveryWriteFence::RecoverySettlement::NoWrite"),
                ("if (!record)\n            return std::nullopt;",
                 "if (!record) { (void)hold.Settle(DatabaseTransactionResult::FromExecutionCode("
                 "DatabaseTransactionResult::NotEnqueuedCode)); return std::nullopt; }"),
            ]
        if args.pending_recovery_mutants:
            variants += [
                ("if (!prepared._task || prepared._result.valid())", "if (!prepared._task)"),
                ("_closed || !prepared || &prepared->fence != &_fence", "_closed || !prepared"),
                ("_closed || !prepared || &prepared->fence != &_fence", "!prepared || &prepared->fence != &_fence"),
                ("if (result->settlement == PB59RecoveryWriteFence::RecoverySettlement::NoWrite)", "if (true)"),
                ("return save && save->receipt;", "return bool(save);"),
            ]
        for before, after in variants:
            if definitions.count(before) != 1:
                raise RuntimeError(f"Expected one mutation site for {before}")
            extracted.write_text(definitions.replace(before, after), encoding="utf-8")
            subprocess.run(compile_fixture, check=True, timeout=90)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=False, timeout=30)
            if result.returncode != 1 or not result.stderr.startswith("item instance save line "):
                raise RuntimeError(f"Expected a fixture assertion after changing {before}. Got {result!r}")
        print(f"{len(variants)}/{len(variants)} save variants rejected by runtime assertions")
