# Roadmap

## Current Priorities

### 1. Windows Reliability

Keep native Windows behavior fully supported across indexing, search, cache identity, temp-path handling, and local deployment. Avoid reintroducing POSIX-only assumptions in shell-backed or filesystem-backed flows.

### 2. Stable MCP Contracts

Preserve the current MCP tool surface while improving correctness, performance, and recovery behavior behind the existing interfaces.

Security-sensitive read paths such as `get_code_snippet` file+line fallback should stay constrained to the indexed project surface.

MCP tool input schemas must remain compatible with agent-side schema validators — no top-level `anyOf`/`allOf`/`oneOf`. (v0.10.2: removed `anyOf` from `get_code_snippet` inputSchema; constraint now enforced in handler.)

### 3. Indexing and Search Performance

Continue improving indexing throughput, graph persistence efficiency, and targeted search behavior without trading away cross-platform correctness.

### 4. Local Operator Workflow

Keep the repository friendly for local maintainer workflows on this workstation:

- `.cgrignore` should keep indexing focused on active source and docs
- `.gitignore` should keep archived worktrees and local generated state out of staging
- Windows binary deployment notes should stay aligned with the real wrapper/upstream arrangement on this machine

## Near-Term Follow-Through

- Finish tightening Windows-safe temp-path usage anywhere shell-backed or filesystem-backed helper flows still assume implicit shared path storage.
- Continue reducing `-Werror` warning backlog so focused fixes can be validated with full local builds more reliably.
- Keep Windows Makefile defaults conservative: default WinLibs `gcc`/`g++` only when callers did not choose compilers explicitly, and document local sanitizer/zlib gaps separately from application regressions.
- Keep `docs/WINDOWS_SEARCH.md` and `docs/WINDOWS_BINARY_DEPLOY.md` synchronized with the merged implementation and local deployment workflow.
- Improve agent-facing MCP trust and query ergonomics so structural answers are easier to trust during active coding:
  - add explicit freshness metadata to query results where practical, including last index/update time, whether the underlying file changed since the indexed snapshot, and the repo revision or cache basis when available
  - make search/trace/architecture results more source-backed by default with exact file paths, line references, and small raw snippets so agents can jump from graph answers to current source truth faster
  - add tighter scoped query controls for common agent workflows such as current folder/module, production-code-only filtering, and explicit exclusion of tests, docs, vendored code, generated output, runtime artifacts, or archived worktrees
  - improve ranking so likely implementation files and current-source hits beat docs, stale artifacts, generated files, vendored trees, or archived local state when multiple matches are possible
  - add a focused "likely edit surface" or equivalent impact-oriented query mode that returns the small set of files/symbols most likely to matter for a requested change instead of broad graph result sets
  - tighten incremental reindex/update behavior so post-edit graph answers stay trustworthy during active coding sessions and clearly report when the graph may be stale

## Notable Non-Goals

- Do not turn the repo into a generic multi-service platform.
- Do not add new runtime dependencies casually.
- Do not hide platform-specific behavior inside undocumented shell assumptions.
