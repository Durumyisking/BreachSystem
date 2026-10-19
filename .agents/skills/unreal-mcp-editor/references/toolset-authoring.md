# Unreal MCP Toolset Authoring

## Choose Python Or C++

- Prefer Python for ordinary Editor APIs, inspection, asset orchestration, and rapid iteration.
- Use C++ when required engine functionality is not exposed to Python, Tool schemas need reflected `USTRUCT` types, or measured call frequency makes the boundary cost relevant.

## Contract

- Group related Tools under one focused Toolset.
- Keep each Tool to one responsibility with explicit typed parameters.
- Return structured results with changed targets, skipped targets, warnings, failures, and save state.
- Validate every pointer dependency in the Tool function.
- Define a logged fallback for missing tags, settings, classes, and assets; reject only the affected operation when no state-safe default exists.
- Require an inspection or dry-run mode before broad mutation when practical.
- Never hide asset saving inside a read-like Tool name.

## Lifecycle And Verification

- Refresh the tool registry after body or Python changes.
- Adding a new C++ `UFUNCTION(AICallable)` requires an Editor restart; Live Coding cannot add the reflected declaration.
- Test with the smallest representative asset, verify the returned schema, independently re-query Editor state, and inspect dirty/saved packages.
