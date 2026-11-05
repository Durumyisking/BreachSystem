# Breach Simple Collision Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans. Track verification below.

**Goal:** Keep small bullet holes out of Simple collision while opening large, connected, accumulated and detached-wall breaches with convex collision pieces.

**Architecture:** A surface-owned movement-collision cache receives successful cutters and detached regions from `CutPolygon`. It unions only potentially connected openings, tests world area against `ComplexHoleSizeThreshold`, and maintains conservative simplified movement regions. `UpdateMesh` builds convex prisms only when those regions changed and publishes collision synchronously once.

**Tech Stack:** UE 5.8.2, GeometryCore/GeometryAlgorithms, FKAggregateGeom/FKConvexElem, native automation.

**Spec:** `Docs/BreachPlugin/개발노트/2026-11-02 11. 브리칭, 이동 충돌체 분리.md` before `이제 기획대로 구현해보자.`, plus this chat's four cases and area-threshold decision.

## Constraints

- Preserve accurate Complex collision and existing cutting/accumulation ownership.
- Use the existing threshold, default 500, as world square centimeters; equality qualifies.
- Keep small exact openings for later connection without rebuilding Simple geometry.
- Conservative opening simplification, at most 1 cm boundary deviation before clipping back to the exact opening.
- Every generated Simple shape is convex; preserve edge openings, disconnected wall removal, full removal and reset.
- Reuse the current checkout on `codex/breach-movement-collision`; do not change assets or unrelated code.
- Build only through `BuildProject_Editor.bat`.

## Review Focus

- Individually subthreshold holes crossing the threshold only after connection.
- Small cuts detaching a wall island, even below the hole-area threshold.
- Nonuniform/mirrored scale and threshold equality.
- Concave openings, narrow solid portions, and simplification never opening visible solid wall.
- Failed cuts, complete removal, reset, and accurate existing Complex-query tests.

## Tasks

- [x] Add integration regressions for the four cases, scale, convex-only output, capsule sweeps and lifecycle; build and observe missing Simple geometry fail.
- [x] Implement `FBreachSimpleCollision` in `Public/Surface` and `Private/Surface`; initialize/reset it with the surface, update after successful cut validation, and publish dirty Simple shapes from `UpdateMesh`.
- [x] Correct existing threshold units/fallback and make existing bullet-hole tests explicitly query Complex collision.
- [x] Run the safe build and focused/related automation; inspect convex geometry and source diff, then request independent code review.

## Verification record

- RED: safe Editor build succeeded; initial movement suite 5 failed, 2 passed (one with warnings), exposing missing Simple geometry.
- GREEN: safe Editor build succeeded; initial 7 movement cases passed, including mirrored/nonuniform scale and capsule controls.
- Edge RED: 10 passed, 1 failed; full wall removal left Simple collision when the threshold exceeded the complete wall area.
- Scope decisions: connected world area >= threshold qualifies; detached wall pieces and complete wall removal always clear their collision. Boundary simplification uses a 1 cm distance before clipping to exact empty space. The spatial broad phase compares cached bounds; only potentially affected openings undergo polygon union/classification.
- Existing test fixture initialization now uses a public no-effect cut because `InitializeSurface` had already been removed before this change. Bullet-hole tests explicitly request Complex collision.
- Full-suite RED: 49 passed, 1 failed; existing double area getter used a float minimum, rejecting exact 0.1. Changed the minimum literal to double.
- Independent read-only review: no Critical/Important findings. Runtime scale/threshold changes do not retroactively reclassify distant holes; reset/reinitialize for complete reclassification. Cooking still processes retained Simple geometry when Complex changes; skipped shape generation is not a cooking-performance claim. Actual CharacterMovement/PIE and failure injection remain outside the executed checks.
- Final safe build: `BuildProject_Editor.bat`, succeeded (5.14 s); log `Saved/Logs/MovementCollision-Verified-Build.log`.
- Final suite: `UnrealEditor-Cmd.exe BreachTest.uproject -unattended -nop4 -NullRHI -nosound -nosplash -ExecCmds="Automation RunTests BreachSystem" -TestExit="Automation Test Queue Empty"`, 50 passed (8 with expected/fixture warnings), 0 failed; report `Saved/Automation/MovementCollisionVerified/index.json`.
- Actual geometry QA: 96-vertex detailed opening generated 46 convex prisms; solid-material samples and convex-footprint checks passed. Render inspected at `Saved/Automation/MovementCollisionGeometry.png`.
- The small rectangular-hole helper fixture generated 4 pieces in 0.094 ms in this run. This excludes precise mesh reconstruction/cooking and is not a gameplay performance claim.
- Naming follow-up: renamed the helper, component member, source files, trace labels and test suites to `SimpleCollision`. Safe build succeeded; `BreachSystem.Surface.SimpleCollision` ran 11 tests with 0 failures (`Saved/Automation/SimpleCollisionRename/index.json`).
