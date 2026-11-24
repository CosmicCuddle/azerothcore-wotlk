"""Compile actual queue bodies with native friendship and protected visibility.

Extracts declarations from Item.h/Player.h; other engine members are doubles.
This is a C++ access-control regression, not a full native translation-unit build.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = root / "src/server/game/Entities/Player"
player = (native / "Player.h").read_text(encoding="utf-8")
item_header = (root / "src/server/game/Entities/Item/Item.h").read_text(encoding="utf-8")
item_source = (root / "src/server/game/Entities/Item/Item.cpp").read_text(encoding="utf-8")


def function(signature):
    start = item_source.index(signature)
    opening = item_source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (item_source[end] == "{") - (item_source[end] == "}")
        end += 1
    return item_source[start:end] + "\n"


names = ("AddToUpdateQueueOf", "AddPB59ToUpdateQueueOf", "RemoveFromUpdateQueueOf")
friends = "\n".join(re.findall(r"^    friend void Item::\w+\(Player\* player\);$", player, re.M))
declarations = {
    name: re.search(r"^    void " + name + r"\(Player\* player\);$", item_header, re.M).group()
    for name in names
}
queue_fields = "\n".join(re.findall(
    r"^    (?:std::vector<Item\*> m_itemUpdateQueue|bool m_itemUpdateQueueBlocked);$", player, re.M))
item_friend = re.search(r"^    friend class Player;.*$", item_header, re.M).group()
assert len(queue_fields.splitlines()) == 2
fixture = r'''
#include "EquipmentCaptureState.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
#define ASSERT(value) do { if (!(value)) std::abort(); } while (false)
#define LOG_DEBUG(...) do { } while (false)
struct ObjectGuid
{
    std::uint64_t value;
    bool operator==(ObjectGuid const&) const = default;
};
class Player;
class Item
{
public:
    explicit Item(std::uint64_t owner = 1) : _owner(owner) { }
    ObjectGuid GetOwnerGUID() const { return {_owner}; }
    bool IsInUpdateQueue() const { return uQueuePos != -1; }
    std::int32_t Position() const { return uQueuePos; }
    @PUBLIC@
private:
    @ITEM_FRIEND@
    @PRIVATE@
    bool PB59InventoryMutationAllowed() const { return true; }
    std::int32_t uQueuePos = -1;
    std::uint64_t _owner;
};
class Player
{
    @FRIENDS@
public:
    Player() : m_itemUpdateQueueBlocked(false) { }
    ObjectGuid GetGUID() const { return {1}; }
    auto& GetPB59EquipmentState() { return _equipment; }
    auto const& Queue() const { return m_itemUpdateQueue; }
    void Block(bool blocked) { m_itemUpdateQueueBlocked = blocked; }
    void AddPrivately(Item& item) { item.AddPB59ToUpdateQueueOf(this); }
protected:
    @FIELDS@
private:
    PB59::EquipmentCaptureState _equipment;
};
'''
fixture = (fixture.replace("@PUBLIC@", declarations[names[0]] + "\n" + declarations[names[2]])
           .replace("@PRIVATE@", declarations[names[1]]).replace("@ITEM_FRIEND@", item_friend)
           .replace("@FRIENDS@", friends).replace("@FIELDS@", queue_fields))
methods = "\n".join(function("void Item::" + name + "(") for name in names)
checks = r'''
int main()
{
    Player owner;
    Item first, second, foreign(2), blocked;
    unsigned checks = 0;
    auto check = [&](bool value)
    {
        ++checks;
        if (!value)
            std::exit(1);
    };
    first.AddToUpdateQueueOf(&owner);
    check(owner.Queue().size() == 1 && owner.Queue()[0] == &first && first.Position() == 0);
    first.AddToUpdateQueueOf(&owner);
    check(owner.Queue().size() == 1);
    owner.AddPrivately(second);
    check(owner.Queue().size() == 2 && owner.Queue()[1] == &second && second.Position() == 1);
    foreign.AddToUpdateQueueOf(&owner);
    check(owner.Queue().size() == 2 && foreign.Position() == -1);
    owner.Block(true);
    owner.AddPrivately(blocked);
    check(owner.Queue().size() == 2 && blocked.Position() == -1);
    owner.Block(false);
    first.RemoveFromUpdateQueueOf(&owner);
    check(owner.Queue().size() == 2 && !owner.Queue()[0] && first.Position() == -1);
    first.RemoveFromUpdateQueueOf(&owner);
    check(owner.Queue().size() == 2 && owner.Queue()[1] == &second);
    std::cout << checks << " native queue access/body checks PASS (engine doubles)\n";
}
'''
with tempfile.TemporaryDirectory(prefix="pb59-item-access-") as temporary:
    work = Path(temporary)
    source = work / "queue.cpp"
    binary = work / "queue.exe"
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(native)]
    source.write_text(fixture + methods + checks, encoding="utf-8")
    subprocess.run(args.compiler + flags + [str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    # The old declarations must reproduce Claude's protected-access failure.
    friendship = "    friend void Item::AddPB59ToUpdateQueueOf(Player* player);"
    assert friends.count(friendship) == 1
    source.write_text((fixture + methods + checks).replace(friendship, ""), encoding="utf-8")
    mutant = subprocess.run(args.compiler + flags + ["-fsyntax-only", str(source)],
                            capture_output=True, text=True)
    assert mutant.returncode != 0 and "m_itemUpdateQueue" in mutant.stderr and "protected" in mutant.stderr
    print("Missing-helper-friend compiler negative control PASS")
