# Adopt This Pack In An Unreal C++ Project

1. Merge the root `AGENTS.md` sections into the target repository's existing instructions; do not overwrite stricter project rules.
2. Copy `Docs/Rules/Unreal-Development-Rules.md` and `Docs/Rules/Unreal-Cpp-Rules.md` into the project.
3. Copy the skill folders under `.agents/skills/` and run the Codex skill validator for each `SKILL.md`.
4. Record the project's safe build command, automation entry point, editor target/configuration, runtime/editor module boundaries, and asset-save policy in a separate project-context rule.
5. Copy the MCP rule and skill only as a version-gated future path when the project is below UE 5.8. Do not enable plugins or add an MCP server config until the engine migration and ordinary build/test baseline pass.
6. Test routing with three real requests: one C++ gameplay change, one repeated Editor asset task, and one MCP inspection request. Confirm each loads only the relevant skill and project rules.

This pack intentionally contains no generic build script. Unreal engine paths, targets, configurations, plugins, and editor-lock behavior are project-specific and must not be guessed.
