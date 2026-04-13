# Roadmap

## Current Priorities

### 1. Windows Reliability

Keep native Windows behavior fully supported across indexing, search, cache identity, temp-path handling, and local deployment. Avoid reintroducing POSIX-only assumptions in shell-backed or filesystem-backed flows.

### 2. Stable MCP Contracts

Preserve the current MCP tool surface while improving correctness, performance, and recovery behavior behind the existing interfaces.

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
- Keep `docs/WINDOWS_SEARCH.md` and `docs/WINDOWS_BINARY_DEPLOY.md` synchronized with the merged implementation and local deployment workflow.

## Notable Non-Goals

- Do not turn the repo into a generic multi-service platform.
- Do not add new runtime dependencies casually.
- Do not hide platform-specific behavior inside undocumented shell assumptions.
