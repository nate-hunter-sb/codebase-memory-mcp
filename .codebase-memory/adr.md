## PURPOSE
codebase-memory-mcp is a pure C MCP server for structural code intelligence. It indexes repositories into a persistent graph and exposes MCP tools for search, architecture, tracing, impact analysis, ADR management, and graph queries.

The current Windows focus has two recently completed areas:
- `search_code` now has a Windows-safe backend path that works on repos with `&` in the path, safe-name repos, `file_pattern`, and `path_filter`.
- Windows project identity and cache compatibility now canonicalize drive letters consistently and recover legacy lowercase-drive caches so `index_status`, `get_architecture`, `manage_adr`, and related flows remain usable.

## STACK
- Pure C codebase with a single static binary target
- SQLite-backed graph storage and cache files
- Vendored parsing/runtime dependencies including tree-sitter grammars, yyjson, sqlite3, mongoose, and related support libraries
- MCP server over stdio plus optional HTTP graph UI
- Cross-platform compatibility layers under `src/foundation/`

## ARCHITECTURE
### Core Areas
- `src/foundation/` - portability, filesystem/process compatibility, string/path helpers, and common utilities
- `src/store/` - SQLite graph persistence and search/storage helpers
- `src/cypher/` - Cypher-like query translation and execution
- `src/mcp/` - MCP tool schemas and request handlers, including `search_code`
- `src/pipeline/` - indexing passes, route extraction, call resolution, and graph construction
- `src/discover/` - project/user config loading and repository discovery
- `src/watcher/` - background change detection and auto-sync
- `src/cli/` - install, update, uninstall, config, and CLI wrappers around MCP operations
- `src/ui/` and `graph-ui/` - optional graph visualization server and frontend
- `tests/` - C-level unit, integration, MCP, security, and smoke-oriented coverage
- `docs/WINDOWS_BINARY_DEPLOY.md` - repo-local deployment notes for the user's custom Codex wrapper and installed upstream-binary workflow

## SEARCH AND PLATFORM NOTES
- `search_graph` is graph-native and works correctly on Windows in the current local state.
- `search_code` now supports the Windows scenarios that drove the recent work:
  - repo paths containing `&`
  - safe-name repo paths
  - no `file_pattern`
  - `file_pattern`
  - `path_filter`
- Windows project roots are canonicalized to uppercase drive letters with `/` separators for stable project identity.
- Legacy lowercase-drive cache identities are handled for compatibility so canonical callers can still reach and migrate older Windows caches.
- A rebuilt repo binary is not automatically the binary Codex uses on this workstation. Codex is launched through a wrapper/upstream arrangement documented in `docs/WINDOWS_BINARY_DEPLOY.md`.

## TRADEOFFS
- The single-binary approach keeps install friction low, but it makes cross-platform shell-based helper paths more sensitive to portability bugs.
- Vendored dependencies keep the runtime self-contained, but they add a large body of code that should usually stay out of repo-focused indexing and agent exploration.
- The custom Codex wrapper deployment adds an extra release step on this machine: after rebuilding and validating the repo binary, the installed upstream target must be updated and Codex must be restarted before the fix is live.
- Some old local cache entries may still exist from previous Windows experiments; stale cache debris should not be confused with the current deployed binary state.

## PHILOSOPHY
- Prefer structural queries first and use text search as a scoped complement, not the default primitive for everything.
- Keep public MCP behavior stable while moving platform-specific logic into explicit compatibility or backend layers.
- Treat Windows support as first-class behavior, not a best-effort afterthought layered on Unix shell assumptions.
- For this workstation, treat "code is fixed" and "Codex is running the fixed binary" as separate verification steps.
