# BreachTest Project Instructions

## Work Contract
- Treat the repository root and its `.uproject` as the authoritative project boundary.
- Inspect the installed engine association, module layout, target files, build scripts, and current tests before proposing commands or APIs.
- Verify project behavior from local code and configuration first. Verify general Unreal API behavior from the matching official documentation, then installed engine source when documentation is insufficient.
- Define the requested outcome, excluded scope, and observable verification before implementation.

## Editor And Asset Safety
- Always build this project by running `BuildProject_Editor.bat` from the repository root.
- Do not invoke Unreal Engine's `Build.bat`, `UnrealBuildTool.exe`, or `UnrealBuildTool.dll` directly for this project.
- Do not substitute IDE Build or Live Coding for the required `BuildProject_Editor.bat` build unless the user explicitly requests it.
- Treat `.uasset` and `.umap` files as Editor-owned binary state. Change them through the Editor, a reviewed Editor tool, a commandlet, or supported MCP tools; never patch their bytes directly.
- Do not save, rename, move, or delete assets beyond the exact user-authorized scope.

## Implementation Discipline
- Prefer one owner and one authoritative execution path for initialization, mutation, persistence, restoration, synchronization, and invalidation.
- Keep durable state and domain rules in C++; use Blueprint for presentation, authored composition, and stable high-level hooks.
- Split files by responsibility, ownership, or lifecycle, not merely to reduce context size.
- When a deterministic Editor task repeats or repeatedly needs the same user input, consider promoting it to a reusable Editor tool instead of regenerating one-off code.
- Completion requires evidence appropriate to the change: a safe project build for C++, the narrowest relevant automation test, asset reinspection or Blueprint compilation for asset work, and a visual or PIE check for spatial or presentation work.

## Rule And Skill Routing
- For every Unreal task, read `Docs/Rules/Unreal-Development-Rules.md`.
- For C++ implementation, reflection, module dependency, or Blueprint-facing native API work, read `Docs/Rules/Unreal-Cpp-Rules.md` and use `unreal-cpp-implementation`.
- For repeated Editor or asset workflows, use `unreal-editor-automation`.
- For Unreal MCP work, read `Docs/Rules/Unreal-MCP-Rules.md` and use `unreal-mcp-editor`; do not proceed unless the installed engine is UE 5.8 or newer and the official plugin is present.
- Project-specific build commands, test filters, module ownership, save rules, and naming conventions belong in project-local rules and override this reusable baseline.
