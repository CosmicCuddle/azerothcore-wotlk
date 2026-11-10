"""F14 Core9 per-viewer creature visibility source contracts.

Pinned read-only baseline: Core9 #9 865724cee8d0fb94d7496854c0f426f60bac2232.
Only describes *current* source behavior. No positive hide, provider, live client,
client ACK, restored NPC, quest/loot/assist rule, or global route is validated.
"""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAYER = ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp"
QUERY = ROOT / "src/server/game/Entities/Player/F14CreatureClientVisibilityQuery.h"
WORLD_H = ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h"
GRID = ROOT / "src/server/game/Grids/Notifiers/GridNotifiers.cpp"

GUARD = "IsF14CreatureSuppressedFromClientView(this, target)"


def section(text: str, first: str, next_one: str) -> str:
    assert first in text, f"missing start boundary: {first}"
    remainder = text.split(first, 1)[1]
    assert next_one in remainder, f"missing end boundary: {next_one}"
    return remainder.split(next_one, 1)[0]


def methods(player: str) -> tuple[str, str]:
    batch = section(
        player,
        "void Player::UpdateVisibilityOf(T* target, UpdateData& data,",
        "void Player::GetInitialVisiblePackets(",
    )
    direct = section(
        player,
        "void Player::UpdateVisibilityOf(WorldObject* target)",
        "void Player::UpdateTriggerVisibility()",
    )
    return batch, direct


def creature_helper(player: str) -> str:
    return section(
        player,
        "static bool IsF14CreatureSuppressedFromClientView(",
        "// F14 player-character client CREATE/OUT query ONLY;",
    )


def compact(text: str) -> str:
    return re.sub(r"\\s+", "", text)


def require_two_sided_guard(body: str, destroy: str, create: str) -> None:
    """Check OUT/CREATE exact logic AND placement, not mere text occurrence."""
    collapsed = compact(body)
    out = (
        "if(!CanSeeOrDetect(target,false,true)||"
        "IsF14CreatureSuppressedFromClientView(this,target)||"
        "IsF14PlayerSuppressedFromClientView(this,target))"
    )
    yes = (
        "if(CanSeeOrDetect(target,false,true)&&"
        "!IsF14CreatureSuppressedFromClientView(this,target)&&"
        "!IsF14PlayerSuppressedFromClientView(this,target))"
    )
    assert collapsed.count(out) == 1, "missing/mis-signed OUT condition"
    assert collapsed.count(yes) == 1, "missing/mis-signed CREATE condition"
    assert collapsed.count(GUARD) == 2, "unexpected per-viewer query count"
    first = collapsed.index(out)
    second = collapsed.index(yes)
    destroy_index = collapsed.index(compact(destroy))
    create_index = collapsed.index(compact(create))
    assert first < destroy_index < second < create_index, (
        "OUT/CREATE checks must guard their own separate send blocks"
    )


def require_disabled_branch_false(helper: str) -> None:
    """The explicit compile-disabled branch must be side-effect-free, FALSE."""
    assert helper.count("#else") == 1 and helper.count("#endif") == 1
    disabled = helper.split("#else", 1)[1].split("#endif", 1)[0]
    assert compact(disabled) == "(void)viewer;(void)target;returnfalse;", (
        "disabled F14 creature helper must only return FALSE"
    )


def require_creature_predicate(helper: str, query: str) -> None:
    assert "return MayAskScript(c) &&" in helper, (
        "creature hook must retain its own safe predicate"
    )
    expected = (
        "returnc.targetIsCreature&&!c.gmModeViewer&&!c.ownedOrCharmed"
        "&&!c.pet&&!c.summon&&!c.guardian&&!c.totem;"
    )
    assert expected in compact(query), "creature predicate polarity changed"


def require_default_false(world: str) -> None:
    assert re.search(
        r"OnF14ShouldSuppressCreatureForViewer\s*\(\s*"
        r"Player const\*[^,]+,\s*WorldObject const\*[^)]*\)"
        r"\s*\{\s*return false;\s*\}", world
    ), "Core script default must not create positive concealment authority"


class Core9ViewerRouteSource(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.player = PLAYER.read_text(encoding="utf-8")
        cls.query = QUERY.read_text(encoding="utf-8")
        cls.world = WORLD_H.read_text(encoding="utf-8")
        cls.grid = GRID.read_text(encoding="utf-8")
        cls.batch, cls.direct = methods(cls.player)

    def test_explicit_off_guard_in_helper(self) -> None:
        helper = creature_helper(self.player)
        self.assertIn("COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT", helper)
        self.assertIn("COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT >= 1", helper)
        self.assertIn("sScriptMgr->ShouldF14SuppressCreatureForViewer(viewer, creature)", helper)
        require_disabled_branch_false(helper)
        self.assertIn("!viewer || !target || !target->IsCreature()", helper)

    def test_script_query_is_default_false(self) -> None:
        require_default_false(self.world)

    def test_uncontrolled_creature_filter_is_not_a_hide_decision(self) -> None:
        require_creature_predicate(creature_helper(self.player), self.query)
        for forbidden in ("SetPhaseMask(", "UpdateObjectVisibility(",
                         "ModifyMoney(", "CanSeeOrDetect("):
            self.assertNotIn(forbidden, self.query)
        self.assertIn("MayAskScript(c) &&", creature_helper(self.player))

    def test_batched_out_and_create_use_exact_viewer_guard(self) -> None:
        require_two_sided_guard(
            self.batch, "target->BuildOutOfRangeUpdateBlock(&data)",
            "target->BuildCreateUpdateBlockForPlayer(&data, this)",
        )
        self.assertIn("GetObjectVisibilityContainer().UnlinkWorldObjectVisibility", self.batch)

    def test_direct_out_and_create_use_exact_viewer_guard(self) -> None:
        require_two_sided_guard(
            self.direct, "target->DestroyForPlayer(this)",
            "target->SendUpdateToPlayer(this)",
        )
        self.assertIn("GetObjectVisibilityContainer().LinkWorldObjectVisibility", self.direct)
        self.assertIn("GetObjectVisibilityContainer().UnlinkWorldObjectVisibility", self.direct)

    def test_pairwise_routes_do_not_broadcast_ai_relocation(self) -> None:
        for body in (self.batch, self.direct):
            self.assertNotIn("UpdateObjectVisibility(true)", body)
            self.assertNotIn("AIRelocationNotifier", body)
            self.assertNotIn("MoveInLineOfSight", body)

    def test_actual_batch_send_call_before_tagged_guid_callback(self) -> None:
        sender = section(self.grid, "void VisibleNotifier::SendToSelf()", "void VisibleChangesNotifier::Visit(")
        self.assertLess(
            sender.index("i_player.SendDirectMessage(&packet);"),
            sender.index("sScriptMgr->OnF14ViewerCreatureBatchGuidSendAttempt("),
        )
        self.assertIn("if (_f14PassToken && !i_data.F14CreatureCandidatesPoisoned())", sender)
        self.assertIn("COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT", sender)
        self.assertIn("COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT", self.grid)

    def test_immediate_route_does_not_claim_guid_send_provenance(self) -> None:
        # It sends directly, without the notifier batch token or GUID candidate callback.
        self.assertIn("target->SendUpdateToPlayer(this)", self.direct)
        self.assertNotIn("OnF14ViewerCreatureBatchGuidSendAttempt", self.direct)
        self.assertNotIn("F14RecordCreatureBatchCandidate", self.direct)

    def test_core9_api_does_not_claim_ip_lifecycle_or_restore_hooks(self) -> None:
        for absent in (
            "OnF14AttackTargetTransition", "OnF14CreatureCorpseReady",
            "OnF14CreatureRespawned", "ShouldF14RefreshCreatureVisibility",
            "WORLDOBJECTHOOK_ON_F14_BATCH_CREATE_ATTRIBUTION_QUERY",
            "WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_DECIDED",
            "COSMIC_F14_ATTACK_EVENT_HOOK_VERSION",
            "COSMIC_F14_VISIBILITY_REFRESH_HOOK_VERSION",
        ):
            self.assertNotIn(absent, self.world)
        self.assertIn("OnF14ViewerCreatureBatchGuidSendAttempt(", self.world)
        self.assertIn("OnF14ViewerVisibilityPassEnd(", self.world)

    def test_negative_mutation_missing_direct_guard_is_rejected(self) -> None:
        changed = self.direct.replace(GUARD, "false", 1)
        self.assertNotEqual(changed, self.direct)
        with self.assertRaises(AssertionError):
            require_two_sided_guard(changed, "target->DestroyForPlayer(this)", "target->SendUpdateToPlayer(this)")

    def test_negative_mutation_missing_batch_create_guard_is_rejected(self) -> None:
        first = self.batch.index(GUARD)
        changed = self.batch[:first + len(GUARD)] + self.batch[first + len(GUARD):].replace(GUARD, "false", 1)
        self.assertNotEqual(changed, self.batch)
        with self.assertRaises(AssertionError):
            require_two_sided_guard(changed, "target->BuildOutOfRangeUpdateBlock(&data)",
                                    "target->BuildCreateUpdateBlockForPlayer(&data, this)")

    def test_negative_mutation_create_polarity_rejected_for_both_routes(self) -> None:
        for body, destroy, create in (
            (self.batch, "target->BuildOutOfRangeUpdateBlock(&data)",
             "target->BuildCreateUpdateBlockForPlayer(&data, this)"),
            (self.direct, "target->DestroyForPlayer(this)",
             "target->SendUpdateToPlayer(this)"),
        ):
            changed = body.replace("!" + GUARD, GUARD, 1)
            self.assertNotEqual(changed, body)
            with self.assertRaises(AssertionError):
                require_two_sided_guard(changed, destroy, create)

    def test_negative_mutation_out_logic_operator_rejected(self) -> None:
        for body, destroy, create in (
            (self.batch, "target->BuildOutOfRangeUpdateBlock(&data)",
             "target->BuildCreateUpdateBlockForPlayer(&data, this)"),
            (self.direct, "target->DestroyForPlayer(this)",
             "target->SendUpdateToPlayer(this)"),
        ):
            changed = body.replace("    if (!CanSeeOrDetect(target, false, true) ||",
                                   "    if (!CanSeeOrDetect(target, false, true) &&", 1)
            self.assertNotEqual(changed, body)
            with self.assertRaises(AssertionError):
                require_two_sided_guard(changed, destroy, create)

    def test_negative_mutation_off_fallback_true_is_rejected(self) -> None:
        helper = creature_helper(self.player)
        changed = helper.replace(
            "#else\n    (void)viewer;\n    (void)target;\n    return false;",
            "#else\n    (void)viewer;\n    (void)target;\n    return true;", 1,
        )
        self.assertNotEqual(changed, helper)
        with self.assertRaises(AssertionError):
            require_disabled_branch_false(changed)

    def test_negative_mutation_creature_predicate_removed_is_rejected(self) -> None:
        helper = creature_helper(self.player)
        changed = helper.replace("MayAskScript(c) &&", "true &&", 1)
        self.assertNotEqual(changed, helper)
        with self.assertRaises(AssertionError):
            require_creature_predicate(changed, self.query)

    def test_negative_mutation_creature_predicate_polarity_rejected(self) -> None:
        changed = self.query.replace("return c.targetIsCreature &&",
                                     "return !c.targetIsCreature &&", 1)
        self.assertNotEqual(changed, self.query)
        with self.assertRaises(AssertionError):
            require_creature_predicate(creature_helper(self.player), changed)

    def test_negative_mutation_positive_default_is_rejected(self) -> None:
        changed = self.world.replace("/*creature*/) { return false; }", "/*creature*/) { return true; }", 1)
        self.assertNotEqual(changed, self.world)
        with self.assertRaises(AssertionError):
            require_default_false(changed)


if __name__ == "__main__":
    unittest.main()
