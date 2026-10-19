# Unreal Development Rules

## Evidence Order
- Inspect the local `.uproject`, `Target.cs`, `Build.cs`, source, config, assets referenced by source, tests, and project scripts before relying on generic advice.
- For general Unreal APIs, use documentation for the installed engine version first and installed engine source when exact signatures, module ownership, deprecation, or lifecycle behavior remains unclear.
- Treat community examples as leads, not authority. Revalidate their engine version, module dependencies, and editor/runtime assumptions.

## Choose The Correct Execution Path
- Edit C++ source, configuration, and build metadata as text, then verify through the project-owned build path.
- Change Blueprint graphs, levels, materials, animation, widgets, and other binary assets through Unreal Editor, a reviewed Editor tool, a commandlet, or supported MCP tools.
- Use Editor Python for short diagnostics and low-risk Editor API orchestration.
- Use an Editor module, Editor subsystem, or commandlet for repeatable asset mutations, deterministic batch processing, or workflows that must be reviewed and rerun.
- Put behavior in runtime C++ only when it must exist in game targets. Keep Editor-only dependencies out of runtime modules.
- Use visual inspection or PIE evidence for spatial, animation, UI, lighting, and material outcomes; code compilation alone does not prove them.

## Editor Automation Promotion
- Promote a task to a reusable tool when its inputs and outputs are stable, it has repeated or will predictably repeat, and success can be checked independently.
- Keep a one-off manual workflow when the task is primarily aesthetic, spatially ambiguous, or cheaper to verify by hand than to specify safely.
- Separate discovery from mutation. An asset-changing tool should identify targets and planned changes before saving when the operation is broad or difficult to reverse.
- Return structured results: changed assets, skipped assets, warnings, failures, and whether packages were saved.
- Save only exact packages owned by the operation. Never use blanket save-all as a recovery mechanism.

## Context And Architecture
- Limit the working set by following callers, owners, configuration, and tests from the requested behavior. Do not split files solely to reduce model context.
- A human-approved requirement and ownership decision outranks an attractive generated architecture.
- Tests should derive from product decisions and observable invariants so an implementation cannot claim success while substituting a different behavior.
- For a deterministic behavior change with an existing test seam, create or adjust the narrow regression test before the implementation. For refactors, establish a passing baseline first. For visual/asset work, capture pre-change state and define the post-change inspection.

## Verification Matrix
- C++ or reflection change: safe project build, then the narrowest relevant automation test.
- Blueprint-facing native API change: build plus affected Blueprint compile/asset-drift inspection.
- Editor tool or commandlet: dry-run or target inventory when supported, apply, structured log review, and exact changed-package inspection.
- Asset or level change: independent re-query plus visual/PIE validation.
- Cook/package change: the project-owned cook/package command and produced artifact, not an Editor-only test.
- Report commands that actually ran separately from recommended or blocked checks.
