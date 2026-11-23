# Native role mutation fixtures

Maintained for CosmicCuddle's F14 lifecycle integration. Run the executable with
`python3 -B tests/cosmic_role_state_change/run_role_state_change_test.py --compiler clang++`
and the source checks with
`python3 -B -m unittest discover -s tests/cosmic_role_state_change -p 'test_role*.py' -v`.

The executable extracts the complete current native GM/security setters, observer
dispatch, GM command bodies and GM spell effect bodies. Engine, script registry,
chat and caster objects are explicit doubles. It checks paired notifications,
unchanged-role refreshes, nested transitions, unwind pairing, command notification
and trigger-visibility side effects, and a non-player spell caster.

The source inventory records current direct security/GM-bit writes and native
login allocation/load/publication ordering. Account access changes update RBAC
and the database separately from session security; these tests do not redefine
that authority or claim those transactions emit role notifications. Inventory
coverage is limited to the checked-in Core paths, not out-of-tree modules.

This is not a Worldserver link, command authorization/registration execution,
spell binding/cast execution, database, concurrent ownership or gameplay test.
No live state, production configuration or defaults are changed.
