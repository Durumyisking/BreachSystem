---
name: unreal-mcp-editor
description: Inspect or modify Unreal Editor state through the official UE 5.8+ ModelContextProtocol server, including actors, levels, assets, materials, animation, Slate, and Editor automation. Do not use below UE 5.8, for ordinary source edits, or when the official plugin is unavailable.
---

# Unreal MCP Editor

Use MCP as a version-gated Editor control surface, not as a replacement for source editing and project verification.

## Route

- For connection or first-time project setup, read `references/setup.md`.
- For creating a repeated project-specific Toolset, read `references/toolset-authoring.md`.
- For ordinary inspection or mutation, follow the operation loop below without loading both references.

## Operation Loop

1. Confirm UE 5.8+, the official `ModelContextProtocol` server, the connected project, and the target Toolset.
2. Keep calls serial. Do not delegate overlapping calls to the same Editor.
3. Inspect target objects/assets, current values, selection, dirty packages, and exact save scope.
4. Present or record the planned mutation when it affects multiple assets or is difficult to reverse.
5. Call the smallest dedicated Tool; avoid broad script execution when a typed Tool exists.
6. Re-read affected state independently, then compile/test/PIE/visually inspect as appropriate.
7. Save only exact authorized packages and report changed, skipped, failed, and still-dirty items.

Never infer completion from a tool's success string alone. Stop on project mismatch, missing required Toolsets, schema uncertainty that changes the result, or an unauthorized save/delete/rename boundary.
