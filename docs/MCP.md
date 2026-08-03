# Meat2D MCP tooling

Meat2D exposes a transport-neutral `McpGateway` in `Meat2D::Tools`. It is the
engine-side boundary for future Model Context Protocol adapters. A stdio or
loopback HTTP adapter should parse MCP JSON-RPC, authenticate the request, and
forward only validated operations to this gateway.

## Current surface

The gateway deliberately starts with one discoverable tool:

| Tool | Read actions | Write actions |
| --- | --- | --- |
| `scene` | `inspect`, `list_entities` | `select`, `clear_selection`, `undo`, `redo` |

The request flow is discovery-first:

1. Authenticate with the configured capability token.
2. Call `search` to find a tool.
3. Call `describe` to inspect its actions and permission class.
4. Call `execute` with bounded parameters.
5. Supply `consent=write` for any state-changing action.

Responses use stable line-oriented payloads for now. The transport adapter is
responsible for mapping them to MCP content blocks; engine code never parses
untrusted JSON or executes arbitrary commands.

## Security contract

- Tokens are compared without early-exit string comparison.
- Empty capability tokens disable the gateway.
- Request parameters are capped at 256 bytes.
- Tool names and actions must be registered; unknown values are rejected.
- Writes require both a valid capability token and explicit write consent.
- Scene mutations use `SceneEditor`, so selection and history semantics remain
  identical to the graphical editor.
- There is no shell, filesystem, network, publish, or arbitrary C++ callback
  surface in the gateway.

The next transport phase should add loopback-only Streamable HTTP and stdio
adapters, per-session rate limits, request IDs, audit events, and opt-in
capability scopes. Non-loopback listening must fail closed unless a token,
rate limit, and TLS policy are explicitly configured.

## Design references

The discovery gateway, dual-transport boundary, capability-token model, and
graceful transport separation are informed by the public
[`Unreal_mcp`](https://github.com/ChiR24/Unreal_mcp) project. Meat2D uses its
own implementation and keeps the initial surface smaller and engine-specific.

For AI model integration, the educational
[`Neural-Network-from-scratch-in-Cpp`](https://github.com/SorawitChok/Neural-Network-from-scratch-in-Cpp)
project is useful for understanding layers, activations, losses, and training.
The production boundary is informed by
[`mlpack`](https://github.com/mlpack/mlpack): optional offline tooling may use
specialized algorithms and serialization, but the runtime should consume a
validated, bounded model artifact rather than depend on a heavyweight training
stack in authoritative ticks.
