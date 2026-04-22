## PURPOSE
codebase-memory-mcp is a pure C MCP server for structural code intelligence. It indexes repositories into a persistent graph and exposes MCP tools for search, architecture, tracing, impact analysis, ADR management, graph queries, and session-level token savings reporting.

The current Windows focus has two recently completed areas:
- `search_code` now has a Windows-safe backend path that works on repos with `&` in the path, safe-name repos, `file_pattern`, and `path_filter`.
- Windows project identity and cache compatibility now canonicalize drive letters consistently and recover legacy lowercase-drive caches so `index_status`, `get_architecture`, `manage_adr`, and related flows remain usable.

A 15th MCP tool was added:
- `show_token_savings` reports per-tool call counts and estimated input/output token volumes for the current process lifetime. Stats are accumulated in `g_tool_stats[]` (file-scope atomics in `src/mcp/mcp.c`) and are process-wide across all server instances (stdio and HTTP). The tool is intentionally excluded from its own response totals since `record_tool_stats` is called after the handler returns.

## TRADEOFFS
- The single-binary approach keeps install friction low, but it makes cross-platform shell-based helper paths more sensitive to portability bugs.
- Vendored dependencies keep the runtime self-contained, but they add a large body of code that should usually stay out of repo-focused indexing and agent exploration.
- The custom Codex wrapper deployment adds an extra release step on this machine: after rebuilding and validating the repo binary, the installed upstream target must be updated and Codex must be restarted before the fix is live.
- Some old local cache entries may still exist from previous Windows experiments; stale cache debris should not be confused with the current deployed binary state.
- `show_token_savings` stats are stored as process-wide file-scope atomics rather than per-`cbm_mcp_server_t` fields. This means the tool aggregates traffic across both the stdio and HTTP server instances when both are active. Moving stats per-server-t would require threading them through every handler call signature; process-wide scope was chosen as the simpler design consistent with how CSTOL and RTK report metrics. The `note` field in the response documents this explicitly.