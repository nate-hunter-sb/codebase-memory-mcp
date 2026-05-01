codebase-memory-mcp is a pure C MCP server for structural code intelligence. It indexes repositories into a persistent SQLite graph and exposes MCP tools for search, architecture, tracing, impact analysis, ADR management, graph queries, source snippets, and session-level token savings reporting.

Current notable MCP/tool behavior:
- The tool surface is 15 MCP tools, including `show_token_savings` and `get_code_snippet`.
- `show_token_savings` reports process-lifetime per-tool call counts and estimated input/output token volumes. Stats are process-wide file-scope atomics in `src/mcp/mcp.c`, so stdio and HTTP server instances share the counters when both are active.
- `get_code_snippet` supports both qualified-name lookup and file/start/end lookup. For file+line requests, indexed node overlaps return `match_method: file_line`; raw gap fallback returns `match_method: file_line_raw` only when the requested file is part of the indexed project surface from `cbm_store_list_files`. Root-contained but unindexed files must be rejected.
- MCP tool inputSchemas must not use top-level `anyOf`/`allOf`/`oneOf` — Claude's tool validator rejects these with HTTP 400 and poisons the full tool list. As of v0.10.2 the `get_code_snippet` schema was flattened to `required: ["project"]`; mutual-exclusion between `qualified_name` and `file+start_line+end_line` is enforced in the handler. A regression test `mcp_tools_no_top_level_anyof` in `tests/test_mcp.c` guards this constraint.

Current Windows state:
- `search_code` has a Windows-safe backend path that works on repos with `&` in the path, safe-name repos, `file_pattern`, and `path_filter`.
- Windows project identity and cache compatibility canonicalize drive letters consistently and recover legacy lowercase-drive caches so `index_status`, `get_architecture`, `manage_adr`, and related flows remain usable.
- `Makefile.cbm` detects Windows hosts through `OS`, `COMSPEC`/`ComSpec`, and `SYSTEMROOT`/`SystemRoot`; for default GNU make compiler values it selects WinLibs `gcc`/`g++`, sets Windows GCC/MinGW detection without Unix shell probes, and preserves explicit `CC`/`CXX` overrides.

## TRADEOFFS
- The single-binary approach keeps install friction low, but cross-platform shell/build helper paths remain sensitive to POSIX assumptions.
- Vendored dependencies keep the runtime self-contained, but they should usually stay out of repo-focused indexing and agent exploration.
- The custom Codex wrapper deployment on this workstation is separate from the repo build output: after rebuilding and validating the repo binary, the installed upstream target must be updated and Codex restarted before changes are live.
- Local WinLibs failures such as missing `libsanitizer.spec` or `-lz` are toolchain/library gaps, not application regressions. Document focused fallbacks such as `SANITIZE=` or non-static `WIN32_LIBS` rather than broadening the Makefile silently.
- Claude-facing tactical context may live in tracked `CLAUDE.md`, but Codex-facing routing should stay in `AGENTS.md` and `CONTEXT.md`; `.claude/` is local tool state and should not be staged.
- MCP tool inputSchema design must stay compatible with all supported agent validators. Top-level composition keywords (`anyOf`, `allOf`, `oneOf`) cause HTTP 400 at tool-load time and break the entire tool list — not just the offending tool. Prefer flat `required` arrays and runtime handler validation.