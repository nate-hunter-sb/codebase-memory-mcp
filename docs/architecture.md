# Architecture

## Overview

`codebase-memory-mcp` is a single-binary structural analysis backend. It discovers source files, indexes them into a graph, persists that graph in SQLite, and exposes read/write operations through MCP tools and a small CLI surface.

At a high level, the runtime separates into four layers:

1. Compatibility and utilities in `src/foundation/`
2. Storage and query execution in `src/store/` and `src/cypher/`
3. Index construction in `src/pipeline/`, `internal/cbm/`, and `src/discover/`
4. Interfaces in `src/mcp/`, `src/cli/`, `src/watcher/`, and `src/ui/`

## Main Components

### Foundation

`src/foundation/` holds the portability boundary for filesystem, temp-path, process, threading, logging, diagnostics, and common string/path helpers. Windows-specific behavior should stay localized here or behind clear `_WIN32` branches in callers.

### Store and Query

`src/store/` owns SQLite-backed persistence for projects, nodes, edges, hashes, and metadata. `src/cypher/` translates Cypher-like queries into the internal query flow. These layers define the persistent graph contract and support both MCP queries and local CLI flows.

### Indexing Pipeline

`src/pipeline/` orchestrates file discovery, parsing, extraction, relationship resolution, and graph dumping. `internal/cbm/` provides the tree-sitter-driven extraction runtime and language-specific extraction helpers. `src/discover/` and related config helpers determine project scope and ignore behavior before indexing begins.

### MCP and Interfaces

`src/mcp/` owns the JSON-RPC server, request validation, tool dispatch, cache/store selection, and higher-level workflows such as project lookup, graph search, and source snippet retrieval. `get_code_snippet` supports qualified-name lookup and indexed file+line lookup; its raw file-line fallback is restricted to files returned by the project index, not merely any file under the repository root. `src/cli/` wraps installation, update, and configuration flows. `src/ui/` serves the optional graph UI, while `src/watcher/` handles background auto-sync and git-based change detection.

## Important Boundaries

- Keep temp-path and shell-facing behavior platform-safe. Prefer the temp helpers in `src/foundation/compat.*` and validate inputs before they reach backend execution.
- Keep file-backed MCP reads scoped to the indexed project surface unless a deliberate, reviewed API change approves broader filesystem access.
- Keep MCP tool names, defaults, and payload contracts stable unless a deliberate API change is approved.
- Keep Windows deployment concerns separate from repo build concerns. Repo build output does not automatically replace the installed upstream binary used by local wrappers.
- Keep ignore-file policy aligned with the local workflow: `.cgrignore` for indexing focus and `.gitignore` for staging hygiene.

## Operational Notes

- The project graph is persisted under the local cache directory and reused across MCP sessions.
- The optional graph UI is a lightweight sidecar mode in the same binary, not a separate backend service.
- Search and indexing reliability on Windows depend on native-safe path handling, cache identity normalization, and avoiding implicit POSIX shell assumptions.
