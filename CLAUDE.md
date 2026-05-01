# codebase-memory-mcp

Claude-facing tactical context. Codex-facing routing stays in `AGENTS.md` and `CONTEXT.md`.

Open-source C MCP server — 15 MCP tools over a SQLite knowledge graph, tree-sitter AST
extraction, and semantic embeddings. Core files: `src/mcp/mcp.c` (~5100 lines),
`src/store/store.c` (~5000 lines), `tests/test_mcp.c` (~2890 lines).

Always use `cstol_read` for these files; never plain `Read`.

---

## Build — Windows / MinGW WinLibs

```
mingw32-make -f Makefile.cbm test
```

`Makefile.cbm` detects Windows via `OS`, `COMSPEC`/`ComSpec`, or
`SYSTEMROOT`/`SystemRoot`. It auto-selects WinLibs `gcc`/`g++`, GCC-only flags,
MinGW support, and Windows libs when no explicit compiler override is provided.
Intentional `CC`/`CXX` overrides should stay respected.

| Focused fallback | Use only when |
|------------------|---------------|
| `SANITIZE=` | Local WinLibs is missing `libsanitizer.spec` |
| `WIN32_LIBS=...` without `-static` | Local WinLibs/zlib lacks static zlib and link fails on `-lz` |

**Running tests:** If the `test` make target builds successfully but the local shell
blocks the recipe's run step, run the binary directly:

```powershell
.\build\c\test-runner.exe
```

**Clean:** `Remove-Item -Recurse -Force build\c` then `New-Item -ItemType Directory -Force build\c`.
(`clean-c` target uses `rm` which isn't on the MinGW PATH.)

---

## Pre-existing Windows test failures (ignore)

These 3 fail on Windows only — unrelated to snippet/MCP code; do not investigate:
- `tool_snippet_short_name` (`test_incremental.c:1782`)
- `tool_snippet_include_neighbors` (`test_incremental.c:1813`)
- `tool_snippet_neighbors_false` (`test_incremental.c:2747`)

---

## Key code landmarks

| Symbol | Location | Notes |
|--------|----------|-------|
| `handle_get_code_snippet` | `src/mcp/mcp.c:2989` | QN path + file+line path |
| `resolve_snippet_source` | `src/mcp/mcp.c:2842` | Root containment check then `read_file_lines` |
| `build_snippet_response` | `src/mcp/mcp.c:~2887` | Builds JSON for indexed nodes |
| `load_scoped_indexed_files` | `src/mcp/mcp.c:~3262` | Wraps `cbm_store_list_files`; static — defined late |
| `free_indexed_file_list` | `src/mcp/mcp.c:~3252` | Static, defined late — **do not call before its definition** |
| `cbm_store_list_files` | `src/store/store.c:1742` | `SELECT DISTINCT file_path FROM nodes WHERE project=?` — canonical indexed surface |
| `cbm_store_find_nodes_by_file_overlap` | `src/store/store.c:1616` | **Always** `malloc`s result on `CBM_STORE_OK` (free even when count==0); does NOT allocate on `CBM_STORE_ERR` |
| `setup_snippet_server` | `tests/test_mcp.c:~848` | Test fixture: `main.go` with HandleRequest(3–5), ProcessOrder(7–9), Run(11–13) |

---

## C gotchas

- **Static function ordering** — `mcp.c` is ~5100 lines processed top-to-bottom. If a
  static function is defined at line N, you cannot call it from line M < N. Inline the
  logic or restructure. The `free_indexed_file_list` / `load_scoped_indexed_files` cluster
  lives at ~line 3252.
- **Path normalization** — `cbm_normalize_path_sep` converts `\` → `/` in-place on
  Windows. Apply to both sides before any `strcmp` path comparison.
- **`heap_strdup`** — project malloc+strcpy; safe to use anywhere in `mcp.c`.
- **yyjson borrowing** — `yyjson_mut_obj_add_str` borrows (does not copy) the string.
  Strings must outlive `yy_doc_to_str`. Use `yyjson_mut_obj_add_strcpy` when the source
  may be freed before serialization.
- **`cbm_mcp_get_int_arg` returns 0 for missing** — distinguish "not provided" (0) from
  "provided as 0" (also 0) by checking both args at once in validation logic.

---

## Snippet tool (file+line path) — added v0.10.0, hardened v0.10.1

`get_code_snippet` accepts `(file, start_line, end_line)` as an alternative to
`qualified_name`. Dispatch:
1. Node overlaps range → `build_snippet_response` with `match_method: file_line`
2. No node, but file is indexed (`cbm_store_list_files`) → raw source, `match_method: file_line_raw`
3. File not in indexed surface → error "not part of the indexed project surface"

---

## Version

Current: `0.10.1`

## ADR

`manage_adr(mode="update", project="codebase-memory-mcp")` via codebase-memory-mcp MCP.
