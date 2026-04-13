# codebase-memory-mcp Context

## Role

This is the single current-state and handoff file for the repository.

Use this file when you need to understand what the repo is, what matters right now, what local workflow constraints apply on this workstation, and which other docs are canonical for deeper detail.

## Purpose

`codebase-memory-mcp` is a pure C MCP server for structural code intelligence. It indexes repositories into a persistent graph and exposes MCP tools for search, architecture, tracing, impact analysis, ADR management, and graph queries.

## Current Product Shape

- Single static binary with MCP server over stdio
- Optional HTTP graph UI served from the same binary
- SQLite-backed project graph cache and metadata
- Vendored parser/runtime dependencies, including tree-sitter grammars and supporting libraries
- Cross-platform compatibility layers under `src/foundation/`

## Core Areas

- `src/foundation/` - compatibility, strings, paths, process helpers, diagnostics, and filesystem utilities
- `src/store/` - SQLite persistence, graph storage, and query helpers
- `src/cypher/` - Cypher-like query parsing and execution
- `src/mcp/` - MCP tool schemas, request handling, cache selection, and server orchestration
- `src/pipeline/` - indexing passes, route extraction, call resolution, and graph construction
- `src/discover/` - repository/config discovery and ignore handling
- `src/watcher/` - background change detection and auto-sync
- `src/cli/` - install, update, uninstall, config, and helper CLI commands
- `src/ui/` and `graph-ui/` - optional graph visualization backend/frontend
- `tests/` - C-level unit, integration, MCP, UI, CLI, and security coverage

## Current Priorities

- Preserve native Windows reliability across temp paths, search execution, cache identity, and local deployment.
- Keep MCP tool contracts stable while improving correctness and performance behind the existing interfaces.
- Reduce the warning-as-error backlog so focused fixes can be validated with full local builds more reliably.
- Keep repo-local workflow docs and ignore files aligned with the real workstation setup.

## Windows Notes

- Native Windows support is a first-class requirement, not a POSIX-compatibility afterthought.
- Shell-backed flows must avoid hardcoded POSIX temp paths and prefer the compatibility helpers in `src/foundation/compat.*`.
- When callers need owned temp-path storage, prefer `cbm_get_tmpdir()`, `cbm_temp_path()`, and `cbm_temp_template()` instead of relying on shared pointer-returning helpers.
- On this workstation, repo build output and the live Codex-installed binary are separate concerns. See `docs/WINDOWS_BINARY_DEPLOY.md` before assuming a rebuilt repo binary is already live.

## Current Handoff Notes

- Public and product-facing material belongs in `README.md`.
- Structural boundaries and subsystem responsibilities belong in `docs/architecture.md`.
- Forward-looking priorities belong in `docs/roadmap.md`.
- Windows `search_code` behavior and platform constraints belong in `docs/WINDOWS_SEARCH.md`.
- Local wrapper/upstream deployment notes for this workstation belong in `docs/WINDOWS_BINARY_DEPLOY.md`.
- `.codebase-memory/adr.md` is intentionally left as-is for the codebase-memory MCP workflow and should not be treated as the editable handoff file.

## Repo Hygiene

- Keep `.cgrignore` focused on active source and docs so indexing excludes local caches, vendored mirrors, build output, and archived worktrees.
- Keep `.gitignore` aligned with the same local-workflow artifacts that should never be staged, including the repo-root `Safe to Delete/` archive area when it exists.
- Treat archived worktrees, copied comparison trees, and generated local validation output as local state, not source.

## Validation Expectations

- Preferred broad validation remains `make -f Makefile.cbm test` and `make -f Makefile.cbm security`.
- On this Windows machine, `mingw32-make` may be available even when `make` is not on `PATH`; document any tooling limitations you hit during validation.
- For targeted platform helper work, the smallest relevant foundation or syntax-only checks are acceptable when the full `-Werror` build is blocked by unrelated warning backlog.
