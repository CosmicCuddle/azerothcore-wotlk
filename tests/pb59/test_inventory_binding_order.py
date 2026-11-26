"""Static regression for the PB59 inventory write parameter order.
Checks current SQL and C++ producers, not execution or database persistence.
Run: python3 tests/pb59/test_inventory_binding_order.py
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SQL = {
    'CHAR_REP_INVENTORY_ITEM': 'REPLACE INTO character_inventory (guid, bag, slot, item) VALUES (?, ?, ?, ?)',
    'CHAR_REP_ITEM_INSTANCE': 'REPLACE INTO item_instance (itemEntry, owner_guid, creatorGuid, giftCreatorGuid, count, duration, charges, flags, enchantments, randomPropertyId, durability, playedTime, text, guid) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
    'CHAR_UPD_ITEM_INSTANCE': 'UPDATE item_instance SET itemEntry = ?, owner_guid = ?, creatorGuid = ?, giftCreatorGuid = ?, count = ?, duration = ?, charges = ?, flags = ?, enchantments = ?, randomPropertyId = ?, durability = ?, playedTime = ?, text = ? WHERE guid = ?',
    'CHAR_UPD_GIFT_OWNER': 'UPDATE character_gifts SET guid = ? WHERE item_guid = ?',
}
INSTANCE = [
    'index, GetEntry()', '++index, owner',
    '++index, GetGuidValue(ITEM_FIELD_CREATOR).GetCounter()',
    '++index, GetGuidValue(ITEM_FIELD_GIFTCREATOR).GetCounter()',
    '++index, GetCount()', '++index, GetUInt32Value(ITEM_FIELD_DURATION)',
    '++index, ssSpells.str()', '++index, GetUInt32Value(ITEM_FIELD_FLAGS)',
    '++index, ssEnchants.str()', '++index, GetItemRandomPropertyId()',
    '++index, GetUInt32Value(ITEM_FIELD_DURABILITY)',
    '++index, GetUInt32Value(ITEM_FIELD_CREATE_PLAYED_TIME)',
    '++index, m_text', '++index, guid',
]
GIFT = ['0, owner', '1, guid']
LINK = ['0, GetGUID().GetRawValue()', '1, links->containerGuid.GetCounter()',
        '2, uint8(pos & 255)', '3, links->item->GetGUID().GetCounter()']


def compact(text):
    return re.sub(r'\s+', '', text)


def body(source, start, end):
    a = source.index(start)
    return source[a:source.index(end, a + len(start))]


def bindings(source, variable):
    return re.findall(r'\b' + variable + r'\s*->\s*SetData\s*\(([^;]*?)\)\s*;', source)


class BindingOrder(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.database = (ROOT / 'src/server/database/Database/Implementation/CharacterDatabase.cpp').read_text()
        cls.item = body((ROOT / 'src/server/game/Entities/Item/Item.cpp').read_text(),
                        'Item::AppendItemInstanceSave(', 'void Item::SaveToDB(')
        cls.player = body((ROOT / 'src/server/game/Entities/Player/PlayerStorage.cpp').read_text(),
                          'Player::AppendPB59InventoryItemSave(', 'bool Player::AppendPB59InventorySaveBatch(')

    def test_sql_order(self):
        for name, expected in SQL.items():
            with self.subTest(statement=name):
                found = re.findall(r'\bPrepareStatement\s*\(\s*' + name
                                   + r'\s*,\s*"([^"\n]*)"\s*,\s*CONNECTION_ASYNC\s*\)', self.database)
                self.assertEqual(len(found), 1)
                self.assertEqual(compact(found[0]), compact(expected))

    def test_producer_order(self):
        for source, variable, expected in ((self.item, 'instance', INSTANCE),
                                           (self.item, 'giftOwner', GIFT),
                                           (self.player, 'link', LINK)):
            with self.subTest(producer=variable):
                self.assertEqual(list(map(compact, bindings(source, variable))), list(map(compact, expected)))


if __name__ == '__main__':
    unittest.main(verbosity=2)
