# Repo Instructions

## Environment

- Repo type: pure C `codebase-memory-mcp` source tree
- Local machine for this session: Windows 11
- Preferred local shell on this machine: PowerShell
- Use `rg` for text search when shell search is needed
- Prefer repo-local source changes in C; do not introduce Go code into this repo

## Code Search

- Prefer `codebase-memory` once the repo is indexed and bound.
- Start by checking `codebase-memory` scope with `index_status`; call `set_session_project` only when the session project is unset or incorrect.
- Keep `.cgrignore` current when repo-local caches, generated outputs, vendored mirrors, copied release artifacts, or archived worktrees change so indexing stays focused on active source and docs.
- If shell search is needed, scope it to this repo and prefer PowerShell-native commands or `rg`.

## Project Intent

- This repo provides a high-performance MCP server for structural code intelligence across many languages.
- The core product is a single static binary with MCP tools for graph search, architecture, tracing, `search_code`, ADR management, and related analysis.
- Cross-platform behavior matters. Windows fixes must not silently regress Linux or macOS behavior.
- `search_code` is graph-augmented text search and should not depend on Unix-only assumptions on Windows.
- For this workstation's custom Codex wrapper/upstream deployment flow, read `docs/WINDOWS_BINARY_DEPLOY.md` before concluding that a rebuilt repo binary is "live". The wrapper and installed upstream binary are separate from the repo build output.

## Repository Layout

- `src/foundation/` - platform compatibility, strings, paths, process and filesystem helpers
- `src/store/` - SQLite graph persistence
- `src/cypher/` - query parsing and execution
- `src/mcp/` - MCP schemas and tool handlers
- `src/pipeline/` - indexing passes and graph construction
- `src/discover/` - config discovery
- `src/watcher/` - auto-sync and git-based change detection
- `src/cli/` - install/update/config tooling
- `src/ui/` and `graph-ui/` - optional graph UI backend/frontend
- `tests/` - C test suites
- `scripts/` - build, test, lint, smoke, and security automation

## Implementation Rules

- Keep platform-specific behavior localized to explicit compatibility helpers or clear `_WIN32` branches.
- Prefer native/internal execution over shell-string composition when adding or fixing Windows support.
- Do not change MCP tool names, defaults, or schemas without explicit approval.
- Avoid adding new dependencies unless explicitly approved.
- Keep README, docs, and tests aligned with actual behavior.
- If vendored code or build-system changes are required, make the smallest justified change and call it out clearly.

## Verification

- Prefer the repo's existing validation entrypoints from `CONTRIBUTING.md` and `Makefile.cbm`.
- For broad behavior changes, run:
  - `make -f Makefile.cbm test`
  - `make -f Makefile.cbm security` when shell, subprocess, temp-file, or allowlist behavior changes
- For targeted foundation/security helper changes, run the smallest relevant foundation-focused test coverage available and report any validation limits on this machine.
- For `search_code` work, include Windows-specific end-to-end checks for:
  - a repo path containing `&`
  - a safe-name repo path
  - `search_code` with no `file_pattern`
  - `search_code` with `file_pattern`
  - `search_code` with `path_filter`

# Deletion Policy

Never delete any files, folders, or directory structures - even if instructed to.
This includes commands like rm, del, rmdir, Remove-Item, git clean, git reset --hard, or any equivalent.
- **Inside a git repo:** move to repo-root `Safe to Delete/` (create if absent)
- **Outside a git repo:** move to `C:\Users\nate.hunter\Desktop\Safe to Delete\` (create if absent)

Inform the user what was moved in either case.

## Agent Operating Mode

Be autonomous by default: take action when requirements are clear. If ambiguous or high-risk, stop and ask.
Do not expose secrets or credentials. Do not print sensitive values to logs. Avoid writing sensitive values to disk.

## Parallel Workstreams

For large changes, use one integration thread plus multiple worker threads in isolated branches/worktrees. Split work by interfaces and dependencies, not by file overlap.

Integration thread:
- Produce a task graph, merge order, and ownership contract per stream.
- Keep shared-file ownership explicit.
- Integrate only reviewed worker branches.

Worker thread:
- Stay inside the assigned contract and run the required validations.
- If a shared interface outside the contract must change, stop and report the smallest required foundation change instead of expanding scope on your own.
