---
name: unreal-cpp-implementation
description: Implement or modify Unreal Engine C++ classes, reflection declarations, module dependencies, and Blueprint-facing native APIs. Use for requested .h/.cpp or Build.cs changes; do not use for review-only, Blueprint-only, asset-only, or build-log diagnosis requests.
---

# Unreal C++ Implementation

Implement the requested native change through the project's established ownership, build, and test contracts.

## Required Context

1. Read root `AGENTS.md`, `Docs/Rules/Unreal-Development-Rules.md`, `Docs/Rules/Unreal-Cpp-Rules.md`, and project-specific coding rules.
2. Inspect the owning declaration/implementation, direct callers, relevant `Build.cs`, Blueprint-facing contract, tests, and the user-confirmed build entry point.
3. State the behavior and proof of success. For a deterministic regression with a usable test seam, make the test fail for the intended reason before implementing.

## Implementation Contract

Before editing, identify:

- owner and module;
- input/output and failure/fallback behavior;
- lifetime and initialization/teardown;
- native and Blueprint API impact;
- persistence, replication, or asset impact only when relevant;
- includes/module dependencies;
- build and narrow test evidence.

Create matching `.h/.cpp` files for a new non-header-only class. For existing code, touch only necessary files while checking both declaration and implementation whenever their contract can drift.

Apply the project's explicit pointer checks, fallback logging, temporary runtime-module ownership rule, reflection conventions, and naming style. Do not introduce speculative systems or unrelated cleanup.

## Explicit Control Flow

- Keep dependency validation, state checks, initialization, and result checks as separate visible steps.
- Do not combine different stages with `&&` or `||`, or hide initialization/mutation calls inside another validation expression.
- When an operation depends on a state check, use nested `if` blocks to make execution order explicit.
- Preserve the existing function structure, diagnostics, and explanatory comments. Do not extract helpers or change failure policy merely to shorten code.

```cpp
UBreachSourceComponent* Source = DamageCauser->FindComponentByClass<UBreachSourceComponent>();
if (!Source)
{
    return;
}

if (!bSurfaceInitialized)
{
    if (!InitializeSurface())
    {
        return;
    }
}
```

## Verification

1. Review the diff against the request and project rules.
2. Run the established safe build command, respecting active Editor protections.
3. Run the narrowest relevant automation test and inspect affected Blueprint assets when reflection changed.
4. Report changed files, commands/results, fallback behavior added, skipped checks, and residual risk.

Route an existing compile/UHT/link failure to the project's build-triage workflow instead of broadening this skill into diagnosis.
