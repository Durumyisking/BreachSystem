---
name: unreal-editor-automation
description: Design or implement repeatable Unreal Editor tools, Editor subsystems, commandlets, or Python workflows for deterministic asset inspection and mutation. Use when an Editor or asset task repeats or needs a reusable headless/batch path; do not use for runtime gameplay code or one-off aesthetic edits.
---

# Unreal Editor Automation

Turn a repeated Editor workflow into the smallest reusable and verifiable tool.

## Choose The Mechanism

- Use Editor Python for short diagnostics and ordinary Editor API orchestration.
- Use an Editor module or subsystem for durable UI actions, typed engine access, transactions, or repeated asset mutation.
- Use a commandlet for deterministic headless batch work and CI-friendly execution.
- Keep the task manual when success is mainly aesthetic or spatial and a precise tool contract would cost more than the work.

## Tool Contract

1. Define exact input assets, output assets/state, exclusions, save behavior, and observable success.
2. Separate inspection/planning from mutation for broad or hard-to-reverse operations.
3. Return structured changed/skipped/warning/failure results.
4. Check pointer dependencies in each function, use explicit logged fallback outcomes, and stop only the affected operation on local failure.
5. Use transactions where interactive undo is part of the expected workflow.
6. Save only exact packages owned by the operation; never blanket-save as recovery.

## Verification

Run the tool on the smallest representative target, independently re-query the result, inspect changed packages, and run relevant asset compilation, automation, PIE, or visual checks. Report unsaved dirty packages and any manual verification still required.
