# MCP integration plan

MCP is a future integration, not a current capability. The runtime's ordinary REST API and its future MCP adapter should share internal use-case services rather than having MCP call arbitrary shell commands.

## Initial scope proposal

Start with a separate `local-ai-runtime-mcp` adapter process using MCP `stdio`, so MCP clients launch it as a child process and no extra network port is opened. Expose a small allowlist of tools only:

- `runtime_health`: return runtime version/status.
- `list_engines`: report configured engine IDs, capabilities and process status.
- `start_engine` / `stop_engine`: only for engine IDs already present in the user's configuration, with clear confirmation/permission behavior.
- `list_models`: use the configured local engine's model-list endpoint.
- `chat`: only through the selected configured local engine; validate message sizes and never accept arbitrary upstream URLs.

Do not initially expose arbitrary filesystem access, shell/terminal execution, model download URLs, arbitrary URL fetching, or network binding changes.

## Security requirements before implementation

- Follow the upstream Model Context Protocol transport specification version pinned in the implementation documentation.
- For stdio, write only protocol JSON-RPC messages to stdout; send logs to stderr.
- If a Streamable HTTP transport is introduced later, validate `Origin`, bind loopback by default, implement authentication where appropriate, and test DNS-rebinding protections.
- Require explicit allowlists for engine operations, bound input sizes and timeouts, redact secrets from logs, and document that a compromised MCP client can request any tool it is allowed to call.
- Keep MCP support optional; the core runtime must work without any MCP packages.

References:

- https://modelcontextprotocol.io/specification/2026-07-28/basic/transports
- https://modelcontextprotocol.io/specification/2026-07-28/basic/authorization
