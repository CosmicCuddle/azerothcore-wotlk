#!/usr/bin/env python3
"""Non-runtime source/mutation safety gates against actual AzerothCore fork source."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SETTINGS = ROOT / "src/server/game/Entities/Player/PlayerSettings.cpp"
HEADER = ROOT / "src/server/game/Entities/Player/Player.h"


def audit(source, header):
    need = (
        'source != "ip_paid_expansion"',
        "SerializeSettingsData(parsed) == data",
        "if (!IsCanonicalPaidReceiptRow(rowSource, data))",
        "unsafePaidRow = true;",
        "if (unsafePaidRow)",
        "return; // The original untrusted row must be preserved.",
        "m_preserveMalformedPaidSettingsRow = false;",
        "if (!PlayerSettingsStore::IsCanonicalPaidReceiptRow(source, data))",
        "m_preserveMalformedPaidSettingsRow = true;",
        "return PlayerSetting(0);",
        "if (m_preserveMalformedPaidSettingsRow && source == \"ip_paid_expansion\")",
        "if (settings.empty())",
    )
    for token in need:
        if token not in source:
            raise ValueError(f"removed Core paid row guard {token!r}")
    if source.count('if (m_preserveMalformedPaidSettingsRow && source == "ip_paid_expansion")') != 3:
        raise ValueError("get, save and direct update must all block paid raw overwrite")
    if header.count("bool m_preserveMalformedPaidSettingsRow = false;") != 1:
        raise ValueError("missing fail-closed Player guard field")
    if "DELETE FROM character_settings" in source or "UPDATE character_settings" in source:
        raise ValueError("no SQL migration/write redesign allowed")
    # Unrelated settings must keep their previous permissive parser behavior.
    if source.count("return true;") < 1 or source.count('source != "ip_paid_expansion"') != 1:
        raise ValueError("unrelated settings are unexpectedly restricted")
    return True


class PaidRawGuard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.settings = SETTINGS.read_text()
        cls.header = HEADER.read_text()

    def test_source_guards_present(self):
        self.assertTrue(audit(self.settings, self.header))

    def test_refuse_if_guard_missing_for_offline_write(self):
        with self.assertRaises(ValueError):
            audit(self.settings.replace("if (unsafePaidRow)", "if (false)", 1), self.header)

    def test_refuse_if_getter_can_create_over_damaged_source(self):
        with self.assertRaises(ValueError):
            audit(self.settings.replace("return PlayerSetting(0);", "return PlayerSetting(1);", 1), self.header)

    def test_refuse_if_raw_paid_cache_flag_does_not_reset(self):
        with self.assertRaises(ValueError):
            audit(self.settings.replace("m_preserveMalformedPaidSettingsRow = false;", "", 1), self.header)

    def test_refuse_if_canonical_bytes_not_compared(self):
        with self.assertRaises(ValueError):
            audit(self.settings.replace("SerializeSettingsData(parsed) == data", "true", 1), self.header)

    def test_refuse_if_header_protection_removed(self):
        with self.assertRaises(ValueError):
            audit(self.settings, self.header.replace("bool m_preserveMalformedPaidSettingsRow = false;", "", 1))


if __name__ == "__main__":
    unittest.main()
