"""Exact Core8->Core9 F14 creature batch GUID source contract.

A candidate is GUID+direction tied to a real UpdateData block and emitted
only after its real notifier SendDirectMessage CALL. No positive hiding, client
ACK, login epoch, creature incarnation or restoration authority is installed.
"""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
CORE8 = "2e2a21ae2224a9738cd92abf00bab957dbfb7604"
CORE9 = "865724cee8d0fb94d7496854c0f426f60bac2232"
PRE = "COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT"
MACRO = "COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT"
PARENT = "COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT"


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def old(path):
    return subprocess.check_output(["git", "show", CORE8 + ":" + path],
                                   cwd=ROOT, text=True)


def core9(path):
    return subprocess.check_output(["git", "show", CORE9 + ":" + path],
                                   cwd=ROOT, text=True)


class Core9RealCreatureBatchEvidence(unittest.TestCase):
    def test_old_core8_missing_tags_new_core9_guarded_disabled(self):
        paths = (
            "src/server/game/Entities/Object/Updates/UpdateData.h",
            "src/server/game/Entities/Object/Updates/UpdateData.cpp",
            "src/server/game/Entities/Player/PlayerUpdates.cpp",
            "src/server/game/Grids/Notifiers/GridNotifiers.cpp",
            "src/server/game/Scripting/ScriptMgr.h",
            "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h",
            "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp",
        )
        for p in paths:
            self.assertNotIn(MACRO, old(p), p)
            self.assertIn(MACRO, read(p), p)
            self.assertNotIn("#define " + MACRO, read(p), p)
        self.assertIn('#error "F14 per-creature batch evidence needs a genuine',
                      read("src/server/game/Entities/Player/PlayerUpdates.cpp"))
        self.assertIn('#error "F14 per-creature batch candidate send proof requires',
                      read("src/server/game/Grids/Notifiers/GridNotifiers.cpp"))

    def test_actual_update_data_sidecar_only_and_merge_is_poisoned(self):
        h = read("src/server/game/Entities/Object/Updates/UpdateData.h")
        cpp = read("src/server/game/Entities/Object/Updates/UpdateData.cpp")
        self.assertIn("F14CreatureBatchGuidCandidateLedger.h", h)
        self.assertIn("CreatureBatchGuidCandidateLedger<ObjectGuid> _f14CreatureCandidates", h)
        self.assertIn("F14RecordCreatureBatchCandidate(", h)
        self.assertIn("_f14CreatureCandidates.Record(creatureGuid, direction)", h)
        self.assertIn("F14CreatureCandidatesPoisoned()", h)
        self.assertIn("CompatibleWithRealOutGuids(m_outOfRangeGUIDs)", h)
        self.assertIn("F14CreatureBatchCandidates()", h)
        real_ledger = read(
            "src/server/game/Entities/Object/Updates/F14CreatureBatchGuidCandidateLedger.h")
        self.assertIn("CompatibleWithRealOutGuids(std::vector<Guid> const& outGuids)", real_ledger)
        self.assertIn("candidate.direction == CreatureBatchDirection::Create && count != 0", real_ledger)
        self.assertIn("candidate.direction == CreatureBatchDirection::OutOfRange && count != 1", real_ledger)
        self.assertIn("_f14CreatureCandidates.Poison();", cpp)
        self.assertIn("_f14CreatureCandidates.Clear();", cpp)
        start=cpp.index("bool UpdateData::BuildPacket(WorldPacket& packet)")
        stop=cpp.index("void UpdateData::Clear()", start)
        oldcpp=old("src/server/game/Entities/Object/Updates/UpdateData.cpp")
        oldstart=oldcpp.index("bool UpdateData::BuildPacket(WorldPacket& packet)")
        oldstop=oldcpp.index("void UpdateData::Clear()", oldstart)
        self.assertEqual(cpp[start:stop], oldcpp[oldstart:oldstop])
        self.assertNotIn("F14RecordCreatureBatchCandidate", cpp[start:stop])

    def test_exact_player_batch_out_create_build_call_and_noncontrolled_only(self):
        path="src/server/game/Entities/Player/PlayerUpdates.cpp"
        cpp=read(path)
        orig=old(path)
        self.assertIn("!creature->GetCharmerOrOwnerGUID()", cpp)
        for excluded in ("IsPet()", "IsGuardian()", "IsSummon()", "IsTotem()"):
            self.assertIn("!creature->" + excluded, cpp)
        self.assertIn("F14EligibleUncontrolledCreatureBatchCandidate(target)", cpp)
        self.assertEqual(cpp.count("F14RecordCreatureBatchCandidate("), 2)
        outbuild="target->BuildOutOfRangeUpdateBlock(&data);"
        createbuild="target->BuildCreateUpdateBlockForPlayer(&data, this);"
        self.assertLess(cpp.index(outbuild),cpp.index("CreatureBatchDirection::OutOfRange"))
        self.assertLess(cpp.index(createbuild),cpp.index("CreatureBatchDirection::Create"))
        begin=cpp.index("template <class T>\nvoid Player::UpdateVisibilityOf(")
        end=cpp.index("void Player::GetInitialVisiblePackets(",begin)
        templ=cpp[begin:end]
        self.assertIn("GetObjectVisibilityContainer().UnlinkWorldObjectVisibility(target);", templ)
        self.assertIn("UpdateVisibilityOf_helper(this, target, visibleNow);", templ)
        direct=cpp[cpp.index("void Player::UpdateVisibilityOf(WorldObject* target)"):
                   cpp.index("void Player::UpdateTriggerVisibility()")]
        olddirect=orig[orig.index("void Player::UpdateVisibilityOf(WorldObject* target)"):
                       orig.index("void Player::UpdateTriggerVisibility()")]
        self.assertEqual(direct, olddirect)
        self.assertNotIn(MACRO,direct)

        # First reconstruct pinned reviewed Core9 EXACTLY, allowing only
        # the separately guarded new positive-query OUT conditional and
        # its same-UpdateData source-cause sidecar. Never weaken the old
        # Core8 reconstruction, and never remove any ordinary visibility code.
        new=cpp
        start=new.index("#if defined("+PRE+")",new.index("template <class T>\\n".replace("\\n", "\n")))
        middle=new.index("#else\\n".replace("\\n", "\n"),start)
        stop=new.index("#endif\\n".replace("\\n", "\n"),middle)+len("#endif\\n".replace("\\n", "\n"))
        new=new[:start]+new[middle+len("#else\\n".replace("\\n", "\n")):stop-len("#endif\\n".replace("\\n", "\n"))]+new[stop:]
        start=new.index("#if defined("+PRE+")",new.index("target->BuildOutOfRangeUpdateBlock(&data);"))
        stop=new.index("#endif\\n".replace("\\n", "\n"),start)+len("#endif\\n".replace("\\n", "\n"))
        new=new[:start]+new[stop:]
        self.assertEqual(new,core9(path))
        # Then preserve the original Core9 -> Core8 exact source proof.
        # These original three Core9 sections must be the ONLY remaining delta.
        new=core9(path)
        start=new.index("#if defined("+MACRO+")")
        stop=new.index("#endif\n\ntemplate <class T>",start)+len("#endif\n\n")
        new=new[:start]+new[stop:]
        for direction in ("OutOfRange", "Create"):
            marker="CosmicF14::CreatureBatchDirection::"+direction
            pos=new.index(marker)
            start=new.rfind("#if defined("+MACRO+")",0,pos)
            stop=new.index("#endif\n",pos)+len("#endif\n")
            new=new[:start]+new[stop:]
        self.assertEqual(new,orig)

    def test_exact_batched_send_call_only_after_packet_and_not_on_poison(self):
        cpp=read("src/server/game/Grids/Notifiers/GridNotifiers.cpp")
        start=cpp.index("void VisibleNotifier::SendToSelf()")
        end=cpp.index("void VisibleChangesNotifier::Visit(",start)
        section=cpp[start:end]
        self.assertLess(section.index("if (!i_data.HasData())"),
                        section.index("i_player.SendDirectMessage(&packet);"))
        self.assertLess(section.index("i_player.SendDirectMessage(&packet);"),
                        section.index("OnF14ViewerCreatureBatchGuidSendAttempt("))
        self.assertIn("if (_f14PassToken && !i_data.F14CreatureCandidatesPoisoned())",section)
        self.assertIn("for (auto const& candidate : i_data.F14CreatureBatchCandidates())",section)
        self.assertIn("candidate.guid,",section)
        self.assertIn("candidate.direction == CosmicF14::CreatureBatchDirection::Create",section)
        self.assertIn("_f14PassToken);",section)
        self.assertNotIn("clientAck",section)
        self.assertNotIn("ClearRestoreDebt",section)

    def test_only_no_op_script_hook_not_registered_positive_policy(self):
        h=read("src/server/game/Scripting/ScriptDefines/WorldObjectScript.h")
        sm=read("src/server/game/Scripting/ScriptMgr.h")
        cpp=read("src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp")
        key="OnF14ViewerCreatureBatchGuidSendAttempt"
        self.assertIn("WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_BATCH_GUID_SEND_ATTEMPT",h)
        self.assertIn("WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_BATCH_GUID_SEND_ATTEMPT",cpp)
        self.assertIn("virtual void "+key,h)
        self.assertIn("void ScriptMgr::"+key,cpp)
        self.assertIn(key+"(",sm)
        self.assertIn("CALL_ENABLED_HOOKS(",cpp)
        self.assertNotIn("COSMIC_F14_VISIBILITY_REFRESH_HOOK_VERSION",h+sm+cpp)
        self.assertIn("return false;",h)
        self.assertIn("OnF14ShouldSuppressCreatureForViewer",h)
        self.assertIn("OnF14ShouldSuppressPlayerForViewer",h)


if __name__=="__main__":
    unittest.main()
