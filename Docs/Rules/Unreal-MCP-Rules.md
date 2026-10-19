# Unreal MCP Rules

## Version And Availability Gate
- Use this workflow only with UE 5.8 or newer and the official `ModelContextProtocol` plugin present in the installed engine.
- Treat Unreal MCP as Experimental. Verify tools, schemas, plugin identifiers, and version-specific behavior against the installed engine and matching official documentation.
- Do not treat another AI assistant plugin as equivalent to `ModelContextProtocol` based on its display name.

## Connection Safety
- Keep the server on loopback. Do not expose it beyond the local machine; the UE 5.8 server has no authentication layer by default.
- The server executes tool invocations serially on the game thread. Do not issue overlapping MCP calls to the same Editor, including from parallel agents.
- Keep the MCP server optional in Codex configuration so ordinary code work can start while the Editor is closed.
- Begin adoption with approval required for every MCP tool call. Relax approval only for individually reviewed read-only tools.

## Operation Contract
- Prefer filesystem editing and the ordinary build loop for C++ source. Use MCP for authoritative Editor state such as selection, actors, levels, assets, materials, animation, Slate, and Editor automation.
- Before mutation, confirm engine version, connected project, target toolset, selected objects/assets, dirty packages, and exact save scope.
- Execute `inspect -> plan -> mutate -> re-read -> verify -> save exact packages`.
- Treat tool return text as evidence about the call, not proof of the final Editor state. Verify by independent re-query, Blueprint/asset compilation, automation, PIE, or visual inspection as appropriate.
- Never rename, move, delete, resave, or broadly modify assets outside the authorized target set.
- Use an Unreal Sandbox for exploratory MCP mutations when that workflow and asset type support it; persist only verified changes.

## Toolset Scope
- Use shipped dedicated tools before generic script execution.
- Promote repeated low-level MCP sequences into a focused project Toolset or Editor tool with structured inputs and outputs.
- Prefer Python Toolsets for ordinary Editor APIs. Use C++ when required functionality is unavailable to Python, reflected structured types are needed, or measured call cost justifies it.
- Keep one Tool to one responsibility and return structured data instead of prose-only success messages.
- After adding a C++ `UFUNCTION(AICallable)`, expect a full Editor restart; Live Coding does not add new reflected declarations.
