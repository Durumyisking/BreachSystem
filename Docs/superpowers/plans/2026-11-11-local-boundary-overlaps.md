# Local boundary overlap repair implementation plan

> **For agentic workers:** Use superpowers:executing-plans. Implement the single approved task in the current checkout.

**Goal:** Repair the final Simple collision Difference's reverse overlapping boundary segments without changing coordinates, filled geometry, or discarding small regions.

**Architecture:** Add `BreachSurfaceGeometry::ResolveBoundaryOverlaps(TArray<FGeneralPolygon2d>& Regions, FString& OutError)` in the existing private geometry helper. Split edges only at existing, exactly collinear vertices, cancel matching reverse directed segments, and reconnect closed contours with material on their left. Keep outer/hole winding and containment. Inputs without the target defect remain unchanged. Commit the rebuilt array only on success.

**Tech Stack:** UE 5.8.2 C++, GeometryCore exact predicates, existing FDelaunay2 and Editor automation.

**Spec:** User-approved three-stage boundary visualization and request to implement it. No Clipper replacement, engine edit, Surface-state change, simplification-policy change, asset edit, or automatic commit.

## Global constraints

- Function parameter lists stay on one line; follow Unreal-Cpp-Rules.md.
- Repair only final Simple collision Difference output before triangulation.
- No snapping, positional tolerance, minimum-area removal, or triangulation retry.
- Preserve existing failure policy: return an actionable error, leave caller output unchanged.
- Test-only helpers remain WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS.
- Build only via root BuildProject_Editor.bat; runtime PIE remains user-operated.

## Review focus

- Reverse overlap within one loop and between an outer loop and a hole.
- Several overlap intervals plus point-touching output regions.
- Exact existing coordinates, near-collinear distinct edges, tiny triangles.
- Valid holes, nested islands, clockwise input and already-valid regions.
- Repeated/malformed directed edges must not silently lose filled material.

## Task 1: Repair the captured overlap defect

- [x] Add Editor regression tests and literal reduced fixtures; verify failure with an identity repair stub.
- [x] Implement repair in BreachSurfaceGeometry.h/.cpp and call after the final successful Simple Difference.
- [x] Verify reduced fixtures, all available real failure snapshots, area/coordinate preservation and FDelaunay2 success.
- [x] Run designated build, focused automation, then BreachSystem suite; compare baseline failures.
- [x] Review only this task's diff and report actual evidence and remaining PIE work.

## Execution record

- Initial designated build failed before new implementation: existing BuildMesh test calls omit the newly user-added ComponentArea argument. Adapt only those calls to 1.0 for the existing unscaled fixtures; production signature and area policy remain user-owned.
- Default-sandbox build could not acquire the engine batch lock. Stopped that invocation and ran the same project entry point with approved elevated tool permissions; UBT compiled the source and exposed the pre-existing test signature errors above.
- Initial native RED: identity repair failed Clockwise, ReverseOverlap, OuterHole, PointContact and all 8 actual saved failure shapes. Full baseline 65 tests: 43 success, 9 warning success, 13 failure; 5 failures belonged to the new tests, 8 predated this change. Report: Saved/Automation/BoundaryOverlap-Red/index.json.
- Helper GREEN: 7/7 tests passed, including FDelaunay2 on all 8 actual snapshots, unchanged coordinates, area-one triangle retention, clockwise outer boundaries and near-collinear untouched input. Report: Saved/Automation/BoundaryOverlap-Green/index.json.
- Integration RED: removed only the final Difference's repair call; the integrated BuildMesh test failed on all 8 actual inputs while helper tests passed. Restored the call afterward. Automated duplicate captures created by this RED run were verified from its saved-log lines and AUTOMATION_TEST 1 metadata, then moved under Saved/Automation/BoundaryOverlap-IntegrationRedSnapshots. The 8 user-created captures remain in their original directory.
- Independent read-only review found no Important/Critical defect in the scoped repair. Added a deterministic independent-hole-plus-nested-island fixture and strengthened failure atomicity to cover rejection after an earlier region already rebuilt. Review excludes unrelated dirty changes, prior failures, arbitrary crossing-edge repair, and performance optimization; those do not broaden the approved defect fix.
- Final designated BuildProject_Editor.bat succeeded. Full BreachSystem suite: 69 tests, 52 success, 9 warning success, 8 failure. All 11 new BoundaryOverlap tests passed without warnings. Both direct FDelaunay2 replay and actual BuildMesh replay passed all 8 real captures. Final report: Saved/Automation/BoundaryOverlap-Verified/index.json; compact evidence: summary.json in the same directory.
- Existing failures match the native RED baseline exactly: SimpleCollision.Connected, SimpleCollision.FullRemoval, SimpleCollision.Simplified, SimpleCollisionCache, SimpleCollisionThreshold.Below, SimpleCollisionThreshold.Detached, SimpleCollisionThreshold.Equal, SimpleCollisionThreshold.FullRemoval (all prefixed BreachSystem.Surface.). No change to their area/threshold/simplification policy was included.
- New source blocks passed whitespace checks. Existing trailing whitespace on the user's CollisionRegions initialization was left unchanged. No engine/library/asset edits, commit or push. Actual Editor/PIE breaching remains for the user to verify.
