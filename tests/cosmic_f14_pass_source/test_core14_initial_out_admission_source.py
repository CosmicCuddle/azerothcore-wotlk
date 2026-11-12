"""Core14 child: require actual one-shot notifier admission BEFORE a new creature OUT.
Source tests only; no registered positive listener, client ACK or gameplay.
"""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = "6dd1589b9067b02f88bb4e04de33818e36bc87c3"
PLAYER = "src/server/game/Entities/Player/PlayerUpdates.cpp"
NOTIFIER = "src/server/game/Grids/Notifiers/GridNotifiers.cpp"
DATA = "src/server/game/Entities/Object/Updates/UpdateData.h"
DATA_CPP = "src/server/game/Entities/Object/Updates/UpdateData.cpp"
GATE = "data.F14MayAskInitialOutQuery() &&\n            "
def read(path):
    return (ROOT / path).read_text()
def old(path):
    return subprocess.check_output(["git", "show", BASE + ":" + path], cwd=ROOT, text=True)
def has_prequery_gate(src):
    marker = "bool const positiveF14CreatureQuery ="
    expr = src.split(marker, 1)[1].split(";", 1)[0]
    return expr.strip() == (
        "data.F14MayAskInitialOutQuery() &&\n            "
        "ordinaryVisible && IsF14CreatureSuppressedFromClientView(this, target)")
class InitialOutAdmission(unittest.TestCase):
    def test_actual_positive_query_short_circuits_before_listener(self):
        src = read(PLAYER)
        self.assertTrue(has_prequery_gate(src))
        self.assertFalse(has_prequery_gate(old(PLAYER)))
        self.assertEqual(src.count(GATE), 1)
        self.assertEqual(src.replace(GATE, "", 1), old(PLAYER))
        self.assertFalse(has_prequery_gate(src.replace(GATE, "", 1)))
        # Restoring the old query order after the listener is also unsafe.
        self.assertFalse(has_prequery_gate(src.replace(
            GATE + "ordinaryVisible && IsF14CreatureSuppressedFromClientView(this, target)",
            "ordinaryVisible && IsF14CreatureSuppressedFromClientView(this, target) && data.F14MayAskInitialOutQuery()", 1)))
    def test_only_actual_notifier_opens_after_valid_token_and_begin(self):
        src = read(NOTIFIER)
        begin = src.split("void VisibleNotifier::BeginF14NotifierPass()", 1)[1].split(
            "VisibleNotifier::~VisibleNotifier()", 1)[0]
        self.assertEqual(src.count("i_data.F14OpenInitialOutQuery(token);"), 1)
        self.assertLess(begin.index("if (!token)"), begin.index("i_data.F14OpenInitialOutQuery(token);"))
        self.assertLess(begin.index("OnF14ViewerVisibilityPassBegin(&i_player, token)"),
                        begin.index("i_data.F14OpenInitialOutQuery(token);"))
        self.assertIn("friend struct Acore::VisibleNotifier;", read(DATA))
        private = read(DATA).split("friend struct Acore::VisibleNotifier;", 1)[0]
        self.assertTrue(private.rstrip().endswith("private:"))
    def test_close_before_any_return_packet_build_or_end_callback(self):
        src = read(NOTIFIER)
        send = src.split("void VisibleNotifier::SendToSelf()", 1)[1].split(
            "void VisibleChangesNotifier::Visit(", 1)[0]
        close = send.index("i_data.F14CloseInitialOutQuery();")
        self.assertLess(send.index("ZoneWideVisibleWorldObjectsSet"), close)
        for marker in ("if (!i_data.HasData())", "i_data.BuildPacket(packet);",
                       "OnF14ViewerCreaturePositiveQueryOutPreSend(", "i_player.SendDirectMessage(&packet);"):
            self.assertLess(close, send.index(marker), marker)
        destructor = src.split("VisibleNotifier::~VisibleNotifier()", 1)[1].split(
            "void VisibleNotifier::Visit(", 1)[0]
        self.assertLess(destructor.index("i_data.F14CloseInitialOutQuery();"),
                        destructor.index("OnF14ViewerVisibilityPassEnd("))
    def test_merge_and_clear_only_seal_never_open(self):
        cpp = read(DATA_CPP)
        self.assertEqual(cpp.count("F14CloseInitialOutQuery();"), 2)
        self.assertNotIn("F14OpenInitialOutQuery(", cpp)
        self.assertIn("F14CloseInitialOutQuery();", cpp.split(
            "void UpdateData::AddUpdateBlock(UpdateData const& block)", 1)[1].split("bool UpdateData::BuildPacket", 1)[0])
        self.assertIn("F14CloseInitialOutQuery();", cpp.split("void UpdateData::Clear()", 1)[1])
        # Neither packet serialization nor the original normal Core path changes.
        self.assertEqual(cpp.split("bool UpdateData::BuildPacket", 1)[1].split("void UpdateData::Clear", 1)[0],
                         old(DATA_CPP).split("bool UpdateData::BuildPacket", 1)[1].split("void UpdateData::Clear", 1)[0])
if __name__ == "__main__":
    unittest.main()
