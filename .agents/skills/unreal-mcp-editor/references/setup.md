# Unreal MCP Setup

## Engine Side

1. Verify the installed engine is UE 5.8 or newer.
2. Enable the official Unreal MCP plugin (`ModelContextProtocol`) and required Toolset plugins. Use `AllToolsets` only for initial capability discovery; narrow the enabled plugins after the required surface is known.
3. Keep the default listener on `http://127.0.0.1:8000/mcp`. Do not expose it remotely.
4. Start on demand or enable auto-start according to the project's Editor workflow.

## Codex Side

Merge the server into the trusted project's existing `.codex/config.toml`; do not delete unrelated settings merely because Unreal's generator refuses to overwrite an existing TOML file.

```toml
[mcp_servers.unreal-mcp]
url = "http://127.0.0.1:8000/mcp"
enabled = true
required = false
default_tools_approval_mode = "prompt"
```

Verify the configured server and reconnect only after the Editor server is running. Keep it optional because ordinary source tasks must still work with the Editor closed.

## First Adoption

Start with read-only questions about selection and available Toolsets. Then test one small reversible mutation in an Unreal Sandbox when supported. Verify state independently before expanding the allowed tool surface or approval policy.
