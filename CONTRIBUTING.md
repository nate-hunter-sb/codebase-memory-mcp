# Contributing to codebase-memory-mcp

Contributions are welcome. This guide covers setup, testing, and PR guidelines.

> **Important**: This project is a **pure C binary** (rewritten from Go in v0.5.0). Please submit C code, not Go. Go PRs may be ported but cannot be merged directly.

## Build from Source

**Prerequisites**: C compiler (gcc or clang), make-compatible build runner, zlib, Git. Optional: Node.js 22+ (for graph UI).

```bash
git clone https://github.com/DeusData/codebase-memory-mcp.git
cd codebase-memory-mcp
git config core.hooksPath scripts/hooks  # activates pre-commit security checks
scripts/build.sh
```

On Windows, local shells may expose `mingw32-make` instead of `make`. With the WinLibs MinGW toolchain on this machine, `Makefile.cbm` detects Windows hosts, defaults GNU make's built-in `CC=cc` to `gcc`, defaults `CXX` to `g++`, and sets Windows GCC/MinGW flags without requiring `IS_GCC=yes` or `IS_MINGW=yes`.

```powershell
mingw32-make -f Makefile.cbm test
```

If this workstation's WinLibs install is missing sanitizer support or static zlib, document that as a local toolchain/library limitation and use the smallest focused fallback needed for validation, for example `SANITIZE=` or a non-static `WIN32_LIBS`. Explicit `CC`/`CXX` overrides are still respected when intentionally testing another compiler.

`Makefile.cbm` now preflights those common Windows gaps before compiling. If
sanitizer flags are enabled but the selected compiler cannot find
`libsanitizer.spec`, install a WinLibs package with sanitizer support or rerun
the focused local validation with `SANITIZE=`. If static MinGW linking is
enabled but `libz.a` is unavailable, install static zlib for the selected
WinLibs toolchain or use a deliberate non-static `WIN32_LIBS` override for that
validation run. Do not silently disable sanitizers or static linking in the
Makefile to work around a local toolchain install.

macOS: `xcode-select --install` provides clang.
Linux: `sudo apt install build-essential zlib1g-dev` (Debian/Ubuntu) or `sudo dnf install gcc zlib-devel` (Fedora).

The binary is output to `build/c/codebase-memory-mcp`.

## Run Tests

```bash
scripts/test.sh
```

This builds with ASan + UBSan and runs all tests (~2040 cases). Key test files:
- `tests/test_pipeline.c` — pipeline integration tests
- `tests/test_httplink.c` — HTTP route extraction and linking
- `tests/test_mcp.c` — MCP protocol and tool handler tests
- `tests/test_store_*.c` — SQLite graph store tests

## Run Linter

```bash
scripts/lint.sh
```

Runs clang-tidy, cppcheck, and clang-format. All must pass before committing (also enforced by pre-commit hook).

## Run Security Audit

```bash
make -f Makefile.cbm security
```

Runs 8 security layers: static allow-list audit, binary string scan, UI audit, install audit, network egress test, MCP robustness (fuzz), vendored dependency integrity, and frontend integrity.

## Project Structure

```
src/
  foundation/       Arena allocator, hash table, string utils, platform compat
  store/            SQLite graph storage (WAL mode, FTS5)
  cypher/           Cypher query → SQL translation
  mcp/              MCP server (JSON-RPC 2.0 over stdio, 14 tools)
  pipeline/         Multi-pass indexing pipeline
    pass_*.c        Individual pipeline passes (definitions, calls, usages, etc.)
    httplink.c      HTTP route extraction (Go/Express/Laravel/Ktor/Python)
  discover/         File discovery with gitignore support
  watcher/          Git-based background auto-sync
  cli/              CLI subcommands (install, update, uninstall, config)
  ui/               Graph visualization HTTP server (mongoose)
internal/cbm/       Tree-sitter AST extraction (64 languages, vendored C grammars)
vendored/           sqlite3, yyjson, mongoose, mimalloc, xxhash, tre
graph-ui/           React/Three.js frontend for graph visualization
scripts/            Build, test, lint, security audit scripts
tests/              All C test files
```

## Adding or Fixing Language Support

Language support is split between two layers:

1. **Tree-sitter extraction** (`internal/cbm/`): Grammar loading, AST node type configuration in `lang_specs.c`, function/call/import extraction in `extract_*.c`
2. **Pipeline passes** (`src/pipeline/`): Call resolution, usage tracking, HTTP route linking

**Workflow for language fixes:**

1. Check the language spec in `internal/cbm/lang_specs.c`
2. Use regression tests to verify extraction: `tests/test_extraction.c`
3. Check parity tests: `internal/cbm/regression_test.go` (legacy, being migrated)
4. Add a test case in `tests/test_pipeline.c` for integration-level fixes
5. Verify with a real open-source repo

### Infrastructure Languages (Infra-Pass Pattern)

Languages like **Dockerfile**, **docker-compose**, **Kubernetes manifests**, and **Kustomize** do not require a new tree-sitter grammar. Instead they follow an *infra-pass* pattern, reusing the existing tree-sitter YAML grammar where applicable:

1. **Detection helpers** in `src/pipeline/pass_infrascan.c` — functions like `cbm_is_dockerfile()`, `cbm_is_k8s_manifest()`, `cbm_is_kustomize_file()` identify files by name and/or content heuristics (e.g., presence of `apiVersion:`).
2. **Custom extractors** in `internal/cbm/extract_k8s.c` — tree-sitter-based parsers that walk the YAML AST (using the tree-sitter YAML grammar) and populate `CBMFileResult` with imports and definitions.
3. **Pipeline pass** (`pass_k8s.c`, `pass_infrascan.c`) — calls the extractor and emits graph nodes/edges. K8s manifests emit `Resource` nodes; Kustomize files emit `Module` nodes with `IMPORTS` edges to referenced resource files.

**When adding a new infrastructure language:**
- Add a detection helper (`cbm_is_<lang>_file()`) in `pass_infrascan.c` or a new `pass_<lang>.c`.
- Add the `CBM_LANG_<LANG>` enum value in `internal/cbm/cbm.h` and a row in the language table in `lang_specs.c`.
- Write a custom extractor that returns `CBMFileResult*` — do not add a tree-sitter grammar.
- Register the pass in `pipeline.c`.
- Add tests in `tests/test_pipeline.c` following the `TEST(infra_is_dockerfile)` and `TEST(k8s_extract_manifest)` patterns.

## Commit Format

Use conventional commits: `type(scope): description`

| Type | When to use |
|------|-------------|
| `feat` | New feature or capability |
| `fix` | Bug fix |
| `test` | Adding or updating tests |
| `refactor` | Code change that neither fixes a bug nor adds a feature |
| `perf` | Performance improvement |
| `docs` | Documentation only |
| `chore` | Build scripts, CI, dependency updates |

Examples: `fix(store): set busy_timeout before WAL`, `feat(cli): add --progress flag`

## Pull Request Guidelines

### Before You Write Code

- **Open an issue first — always.** Every PR must reference a tracking issue (`Fixes #N` or `Closes #N`). Describe what you want to change and why. Wait for maintainer feedback before implementing. PRs without a prior issue discussion will be closed.
- **Bug fixes and test additions** are the exception — these are welcome without prior discussion, as long as they're focused.

### What Requires Explicit Maintainer Approval

The following changes will not be merged without prior design discussion in an issue:

- **API surface changes** — adding, removing, renaming, or changing defaults of MCP tools
- **New pipeline passes or indexing algorithms** — anything that changes what gets extracted or how
- **Build system / Makefile changes** — beyond trivial fixes
- **Project configuration** — CLAUDE.md, skill files, .mcp.json, CI workflows
- **New dependencies** — vendored or otherwise
- **Breaking changes** of any kind

If in doubt, open an issue and ask.

### PR Scope and Size

- **One issue per PR.** Each PR must address exactly one bug, one feature, or one refactor. Do not bundle multiple fixes or feature additions into a single PR. Kitchen-sink PRs will be closed with a request to split.
- **Keep PRs small.** A good PR is under 500 lines. If your change is larger, split it into reviewable increments that each stand on their own.
- **Don't mix features with fixes.** If you find a bug while implementing a feature, submit the bug fix as a separate PR.

### Code Requirements

- **C code only** — this project was rewritten from Go to pure C in v0.5.0. Go PRs will be acknowledged and potentially ported, but cannot be merged directly.
- Include tests for new functionality
- Run `scripts/test.sh` and `scripts/lint.sh` before submitting
- Keep PRs focused — avoid unrelated reformatting or refactoring

### Windows-safe search and temp files

Changes that touch `search_code` or any other shell-backed search path must preserve native Windows behavior.

- Do not hardcode `/tmp` or other POSIX-only temp paths. Use the platform temp helpers already provided by the codebase and delete temporary files on every exit path.
- Prefer `cbm_get_tmpdir()`, `cbm_temp_path()`, and `cbm_temp_template()` when callers need owned storage for a temp-backed path. Use `cbm_mkstemp()` and related helpers for temporary file creation rather than rebuilding paths ad hoc.
- Treat `project`, resolved root paths, and `file_pattern` as shell-facing input. Validate them before execution instead of interpolating unchecked values into command strings.
- Keep Windows execution native. Do not introduce new mandatory dependencies on GNU `grep`, `xargs`, MSYS2, Git Bash, or Cygwin just to make search work.
- Preserve the current search contract: scope to indexed files when possible, then fall back to a recursive project-root search when scoped execution is unavailable.
- If you change the search backend, update [docs/WINDOWS_SEARCH.md](docs/WINDOWS_SEARCH.md) in the same PR so user-visible behavior and maintainer expectations stay aligned.
- If you create or retain archived worktrees or review snapshots under repo-root `Safe to Delete/`, keep `.gitignore` and `.cgrignore` aligned so they stay out of both indexing and staging.

## Security

We take security seriously. All PRs go through:
- Manual security review (dangerous calls, network access, file writes, prompt injection)
- Automated 8-layer security audit in CI
- Vendored dependency integrity checks

If you add a new `system()`, `popen()`, `fork()`, or network call, it must be justified and added to `scripts/security-allowlist.txt`.

## Good First Issues

Check [issues labeled `good first issue`](https://github.com/DeusData/codebase-memory-mcp/labels/good%20first%20issue) for beginner-friendly tasks with clear scope and guidance.

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
