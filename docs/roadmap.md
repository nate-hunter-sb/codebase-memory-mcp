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
- ✅ `show_token_savings` now reports *actual savings* — per-tool baseline cost model implemented in v0.10.3. `baseline_tokens`, `saved_tokens`, `total_saved_tokens`, and `total_baseline_tokens` fields added. Exact savings for `get_code_snippet`/`manage_adr`; estimated (8 KB/file) for `search_graph`/`search_code`/`get_architecture`/`trace_path`; fixed offset for `detect_changes`.
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

## Implementation Notes

### show_token_savings: true savings vs. raw usage

**Status: ✅ Implemented in v0.10.3.**

`baseline_bytes` field added to `cbm_tool_stats_t`; `record_tool_baseline()` called from inside each handler on success paths. `show_token_savings` now reports `baseline_tokens`, `saved_tokens` (clamped ≥ 0 per tool), and root-level `total_saved_tokens`/`total_baseline_tokens`. Kept below for historical reference.

---

**Problem.** The current implementation (`src/mcp/mcp.c`, `show_token_savings` handler) tracks tokens
*spent on tool calls* — args JSON size (input) and response size (output), each divided by 4. It does
not compute a delta against what the agent would have spent reading files directly.

**What true savings requires.**

1. **Per-call baseline cost** — for each tool invocation, estimate the token cost of the naive
   alternative (reading raw files). This varies by tool:

   | Tool | Naive alternative | Baseline estimate |
   |------|-------------------|-------------------|
   | `get_code_snippet` | Read the full source file | `file_size_bytes / 4` — exact, file size is known |
   | `search_graph` / `search_code` | Grep + read N matching files | `result_count × avg_file_tokens` — needs a configurable multiplier |
   | `get_architecture` | Read all top-level source files | `indexed_file_count × avg_file_tokens` — approximate |
   | `trace_path` | Manually follow call chain across files | Hard to estimate; use a fixed multiplier per hop |
   | `index_status` / `list_projects` | No real alternative; savings ≈ 0 | 0 |
   | `query_graph` | Read all relevant files and reason manually | Too variable; skip or use fixed multiplier |

2. **Saved tokens = baseline − actual output tokens.** The actual output tokens are already tracked.
   Add `baseline_tokens` and `saved_tokens` counters alongside the existing `input_tokens` /
   `output_tokens` atomics in the per-tool stats struct.

3. **For `get_code_snippet` specifically** — the handler already knows `file_path` and the node span.
   Call `cbm_get_file_size(file_path)` (or equivalent) at snippet-serve time, divide by 4, store as
   `baseline_tokens`. This gives an exact, defensible savings number for the highest-value tool.

4. **For search tools** — inject a configurable `avg_file_tokens` constant (default: 2,000). Multiply
   by the number of unique files in the result set. This is an estimate but good enough for reporting.

5. **Output change.** Add `baseline_tokens` and `saved_tokens` to each entry in `by_tool[]` and to
   the top-level totals. Keep the existing `input_tokens` / `output_tokens` fields for transparency.

**Suggested implementation order:**
1. `get_code_snippet` exact savings (file size lookup) — highest value, most defensible
2. `search_graph` / `search_code` estimated savings (result count × multiplier)
3. `get_architecture` / `trace_path` approximate savings (fixed multipliers)
4. Add `saved_tokens` to the top-level total and surface it first in the output JSON
