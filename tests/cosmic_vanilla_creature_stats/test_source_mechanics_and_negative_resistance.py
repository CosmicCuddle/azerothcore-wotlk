#!/usr/bin/env python3
"""CosmicCuddle 3.3.5a core: READ-ONLY Vanilla immunity/resistance contract.

Protects SOURCE SEMANTICS from silent upstream changes, not installed DB,
spell-damage formula, launch test or historical Vanilla weakness intent.
No DB, filesystem writes, SQL execution, modules, network, or live actors.
"""
from pathlib import Path
import re
import unittest

CORE = Path(__file__).resolve().parents[2]
SOURCE = {
    "defines": "src/server/shared/SharedDefines.h",
    "immunitytype": "src/server/game/Spells/SpellMgr.h",
    "spellmgr": "src/server/game/Spells/SpellMgr.cpp",
    "creature": "src/server/game/Entities/Creature/Creature.cpp",
    "objectmgr": "src/server/game/Globals/ObjectMgr.cpp",
    "stats": "src/server/game/Entities/Unit/StatSystem.cpp",
    "unit": "src/server/game/Entities/Unit/Unit.cpp",
    "unith": "src/server/game/Entities/Unit/Unit.h",
    "object": "src/server/game/Entities/Object/Object.cpp",
}


def body(text: str, signature: str) -> str:
    # Locate one precise compiled function, then match brace depth. Purely
    # textual: this is NOT a C++ parser and cannot prove dynamic behaviour.
    if text.count(signature) != 1:
        raise AssertionError(f"Expected one exact source function: {signature}")
    begin = text.index(signature)
    first = text.index("{", begin)
    depth = 0
    for i in range(first, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[begin:i + 1]
    raise AssertionError(f"Malformed source body for {signature}")


class VanillaCreatureMechanics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.s = {
            k: (CORE / path).read_text(encoding="utf-8")
            for k, path in SOURCE.items()
        }

    def test_enum_positions_are_not_wiki_shifted(self):
        src = self.s["defines"]
        for key, value in (
            ("MECHANIC_NONE", 0),
            ("MECHANIC_KNOCKOUT", 14),
            ("MECHANIC_BLEED", 15),
            ("MECHANIC_BANDAGE", 16),
            ("MECHANIC_INFECTED", 22),
            ("MAX_MECHANIC", 37),
        ):
            match = re.search(r"(?m)^\s*" + key +
                              r"\s*=\s*(\d+)\b", src)
            self.assertIsNotNone(match, key)
            self.assertEqual(int(match.group(1)), value, key)
        self.assertEqual(1 << 15, 0x8000)
        self.assertEqual(1 << 16, 0x10000)
        self.assertEqual(1 << 22, 0x400000)
        self.assertEqual(1 << 14, 0x4000)  # KNOCKOUT, not BLEED.

    def test_sql_creature_immunity_mask_bitset_has_no_extra_conversion(self):
        self.assertIn("std::bitset<MAX_MECHANIC> Mechanic;", self.s["immunitytype"])
        fn = body(self.s["spellmgr"], "void SpellMgr::LoadCreatureImmunities()")
        self.assertIn("SELECT ID, SchoolMask, DispelTypeMask, MechanicsMask,", fn)
        self.assertIn("uint64 mechanics = fields[3].Get<uint64>();", fn)
        self.assertIn("immunities.Mechanic = mechanics;", fn)
        self.assertNotIn("mechanics <<", fn)
        self.assertNotIn("mechanics >>", fn)
        self.assertNotIn("mechanics - 1", fn)
        self.assertLess(fn.index("uint64 mechanics ="),
                        fn.index("immunities.Mechanic = mechanics;"))

    def test_creature_applies_exact_mechanic_bit_index(self):
        load = body(self.s["creature"],
                    "void Creature::LoadTemplateImmunities(int32 creatureImmunitiesId)")
        self.assertIn(
            "for (std::size_t i = 0; i < immunities->Mechanic.size(); ++i)", load)
        self.assertIn("if (immunities->Mechanic[i])", load)
        self.assertIn("ApplySpellImmune(placeholderSpellId, IMMUNITY_MECHANIC, i, apply);",
                      load)
        has = body(self.s["creature"],
                   "bool Creature::HasMechanicTemplateImmunity(uint64 mask) const")
        self.assertIn("(immunities->Mechanic.to_ullong() & mask) != 0", has)
        self.assertIn("if (GetOwnerGUID().IsPlayer())", has)

    def test_spell_and_effect_mechanics_keep_exception_semantics(self):
        spell = body(self.s["creature"],
                     "bool Creature::IsImmunedToSpell(SpellInfo const* spellInfo, Spell const* spell)")
        effect = body(self.s["creature"],
                      "bool Creature::IsImmunedToSpellEffect(SpellInfo const* spellInfo, uint32 index, WorldObject const* caster /*= nullptr*/) const")
        self.assertIn("SPELL_ATTR0_CU_BYPASS_MECHANIC_IMMUNITY", spell)
        self.assertIn("HasMechanicTemplateImmunity(1ULL << spellInfo->Mechanic)", spell)
        self.assertIn("SPELL_ATTR0_CU_BYPASS_MECHANIC_IMMUNITY", effect)
        self.assertIn(
            "HasMechanicTemplateImmunity(1ULL << spellInfo->Effects[index].Mechanic)",
            effect)

    def test_negative_sql_school_resistance_remains_signed_on_load(self):
        obj = self.s["objectmgr"]
        self.assertIn("SELECT CreatureID, School, Resistance FROM creature_template_resistance",
                      obj)
        self.assertIn(
            "creatureTemplate.resistance[school] = fields[2].Get<int16>();", obj)
        creature = self.s["creature"]
        for school in ("HOLY", "FIRE", "NATURE", "FROST", "SHADOW", "ARCANE"):
            self.assertIn(
                f"BASE_VALUE, float(cInfo->resistance[SPELL_SCHOOL_{school}])",
                creature)
        self.assertIn("float value = GetTotalAuraModValue(", self.s["stats"])
        self.assertIn("SetResistance(SpellSchools(school), int32(value));",
                      self.s["stats"])

    def test_generic_negative_setter_still_clamps_to_zero(self):
        self.assertIn(
            "void SetResistance(SpellSchools school, int32 val) { SetStatInt32Value(",
            self.s["unith"])
        src = body(self.s["object"],
                   "void Object::SetStatInt32Value(uint16 index, int32 value)")
        self.assertIn("if (value < 0)\n        value = 0;", src)
        self.assertIn("SetUInt32Value(index, uint32(value));", src)
        self.assertLess(src.index("if (value < 0)"),
                        src.index("SetUInt32Value("))

    def test_effective_spell_resistance_reclamps_after_penetration(self):
        fn = body(
            self.s["unit"],
            "float Unit::GetEffectiveResistChance(Unit const* owner, SpellSchoolMask schoolMask, Unit const* victim, SpellInfo const* spellInfo /*= nullptr*/, uint8 casterLevel /*= 0*/)")
        self.assertIn("victim->GetResistance(schoolMask)", fn)
        self.assertIn("victimResistance -= static_cast<float>(player->GetSpellPenetrationItemMod());", fn)
        self.assertIn("victimResistance = std::max(victimResistance, 0.0f);", fn)
        self.assertIn("return std::min(victimResistance / (victimResistance + resistanceConstant), 0.75f);",
                      fn)

    def test_no_negative_spell_damage_bonus_implied_by_this_source(self):
        # This is a *negative* inference boundary, not a judgment on what
        # Blizzard Vanilla or other auras and scripts SHOULD do.
        fn = body(
            self.s["unit"],
            "float Unit::GetEffectiveResistChance(Unit const* owner, SpellSchoolMask schoolMask, Unit const* victim, SpellInfo const* spellInfo /*= nullptr*/, uint8 casterLevel /*= 0*/)")
        self.assertNotIn("creature_template_resistance", fn)
        self.assertNotIn("ModifySpellDamage", fn)
        self.assertIn("std::max(victimResistance, 0.0f)", fn)

    def test_historical_double_mask_example_is_arithmetic_not_sql_migration(self):
        # The archived IP SQL comments include legacy 0x6206 and *already*
        # converted 0xC40C. This checks arithmetic only; no real SQL loaded.
        self.assertEqual(0x6206 << 1, 0xC40C)
        self.assertEqual(0xC40C, 50188)
        self.assertTrue(0xC40C & 0x8000)
        self.assertFalse(0x6206 & 0x8000)


if __name__ == "__main__":
    unittest.main()
