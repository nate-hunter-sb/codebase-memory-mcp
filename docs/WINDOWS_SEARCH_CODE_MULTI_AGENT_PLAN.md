# Windows `search_code` Multi-Agent Worktree Plan

## Goal

Fix Windows support for `search_code` from multiple angles without tripping over each other in the same files.

Confirmed Windows failure classes:

1. Valid Windows repo paths can be rejected as unsafe because `search_code` validates a repo path as if it were a shell argument.
2. Temp-file creation is hardcoded to `/tmp`, which fails on Windows.
3. The actual search execution path is Unix-specific today: `grep`, `xargs`, single-quote shell quoting, and `/dev/null`.

## Strategy

Use one orchestration session plus five implementation sessions in separate git worktrees:

- `orchestrator`: owns coordination, integration, conflict resolution, and final merge back to `main`
- `path-validation`: owns Windows-safe path/security semantics
- `tempfile-platform`: owns cross-platform temp-file helpers and cleanup behavior
- `search-backend`: owns the real Windows-capable `search_code` execution path
- `regression-tests`: owns MCP/integration/smoke regression coverage
- `docs`: owns README/docs/contributor notes

Only one implementation stream is allowed to touch [mcp.c](C:/Users/nate.hunter/Documents/Playground/codebase-memory-mcp/src/mcp/mcp.c): `search-backend`.

## Rules

- Every top-level session should explicitly say `use your standard subagent workflow`.
- Each worker should use standard subagent workflow during the task.
- Each worker should use exactly one `reviewer` subagent once, at the end of the worktree, on the full diff in one pass.
- After the reviewer responds, the same worker session should fix findings and rerun the smallest useful validation set.
- If a worker needs to change a file outside its ownership boundary, it should stop and report the smallest required contract change back to the orchestrator instead of freelancing.
- The orchestrator should resolve merge conflicts only in the orchestration worktree, not by asking multiple workers to rewrite around each other after the fact.

## Worktree Layout

| Session | Branch | Worktree Path | Owned Files | Must Not Change |
|---|---|---|---|---|
| Orchestrator | `codex/windows-search-orchestrator` | `C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-orchestrator` | Integration branch only; merge conflict resolutions; final polish if needed | Feature work inside worker-owned files before merge |
| Path Validation | `codex/windows-search-path-validation` | `C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-path-validation` | `src/foundation/str_util.c`, `src/foundation/str_util.h`, `tests/test_security.c`, `tests/test_str_util.c` | `src/mcp/mcp.c`, `tests/test_incremental.c`, `tests/test_mcp.c`, docs |
| Tempfile Platform | `codex/windows-search-tempfile-platform` | `C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-tempfile-platform` | `src/foundation/compat.c`, `src/foundation/compat.h`, `src/foundation/compat_fs.c`, `src/foundation/compat_fs.h`, any temp-helper-specific test file | `src/mcp/mcp.c`, `tests/test_incremental.c`, `tests/test_mcp.c`, docs |
| Search Backend | `codex/windows-search-backend` | `C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-backend` | `src/mcp/mcp.c`, any new `src/mcp/search_code_*` helper files | Foundation security/temp helper files unless orchestrator approves an expansion |
| Regression Tests | `codex/windows-search-regression-tests` | `C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-regression-tests` | `tests/test_incremental.c`, `tests/test_mcp.c`, `scripts/smoke-test.sh` | `src/`, foundation helpers, docs |
| Docs | `codex/windows-search-docs` | `C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-docs` | `README.md`, `CONTRIBUTING.md`, `docs/`, optionally a new Windows-specific note under `docs/` | `src/`, `tests/`, scripts |

## Suggested Setup Commands

Run these from [codebase-memory-mcp](C:/Users/nate.hunter/Documents/Playground/codebase-memory-mcp):

```powershell
$wtRoot = 'C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees'
New-Item -ItemType Directory -Force $wtRoot | Out-Null

git worktree add "$wtRoot\windows-search-orchestrator" -b codex/windows-search-orchestrator
git worktree add "$wtRoot\windows-search-path-validation" -b codex/windows-search-path-validation
git worktree add "$wtRoot\windows-search-tempfile-platform" -b codex/windows-search-tempfile-platform
git worktree add "$wtRoot\windows-search-backend" -b codex/windows-search-backend
git worktree add "$wtRoot\windows-search-regression-tests" -b codex/windows-search-regression-tests
git worktree add "$wtRoot\windows-search-docs" -b codex/windows-search-docs
```

## Session Prompts

Copy and paste these prompts into new Codex sessions opened in the matching worktrees.

For safety, paste the worktree-location line exactly as shown at the top of each prompt instead of relying on cwd inference alone.

### 1. Orchestrator Prompt

```text
You are running in the worktree at C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-orchestrator. Perform all integration and merges only from this worktree.

You are the orchestrator session for a multi-worktree Windows fix in the codebase-memory-mcp repo. Use your standard subagent workflow.

Goal:
- coordinate parallel worktrees that fix Windows support for search_code
- preserve clear ownership boundaries
- merge everything back to main cleanly and safely

Confirmed bug surfaces:
1. search_code rejects valid Windows repo paths such as paths containing &
2. search_code writes temp files to /tmp on Windows
3. search_code uses Unix-only grep/xargs/single-quote shell quoting and /dev/null

Your job:
- keep the ownership contract below intact
- ask for concise worker updates that include changed files, validations run, and known merge risks
- do not implement worker-owned feature work before merge unless you are resolving merge conflicts in the integration branch
- use code_mapper early for conflict-surface analysis
- use test_checker to maintain the merge-gate validation plan
- after all worker branches are merged, run exactly one reviewer subagent on the integrated diff, fix findings, rerun final validation, and prepare the branch to merge to main

Ownership contract:
- path-validation owns: src/foundation/str_util.c, src/foundation/str_util.h, tests/test_security.c, tests/test_str_util.c
- tempfile-platform owns: src/foundation/compat.c, src/foundation/compat.h, src/foundation/compat_fs.c, src/foundation/compat_fs.h, temp-helper-specific tests
- search-backend owns: src/mcp/mcp.c and any new src/mcp/search_code_* helper files
- regression-tests owns: tests/test_incremental.c, tests/test_mcp.c, scripts/smoke-test.sh
- docs owns: README.md, CONTRIBUTING.md, docs/

Rules:
- only the search-backend stream may touch src/mcp/mcp.c
- each worker must use exactly one reviewer subagent once at the end on the full worktree diff, then fix findings
- if a worker needs a boundary change, make them stop and report the smallest required change instead of expanding on their own
- resolve merge conflicts in the orchestrator worktree only
- on startup, do not merge anything, do not rebase anything, and do not edit worker-owned files
- remain in coordination mode until the user explicitly says to begin integration or all worker handoff notes are complete
- treat worker branches as in-progress by default; never assume they are ready to merge just because the branch exists

Merge order:
1. path-validation
2. tempfile-platform
3. search-backend
4. regression-tests
5. docs

Validation gates after each merge:
- after 1 and 2: run the smallest useful foundation-focused validation plus a sanity build if needed
- after 3: run full test coverage for MCP/search behavior
- after 4: run the smoke path that exercises search_code
- after 5 and before main merge: run full tests, run security if subprocess/shell code changed, and run a Windows end-to-end probe for:
  - a repo path containing &
  - a safe-name repo path
  - search_code with no file_pattern
  - search_code with file_pattern
  - search_code with path_filter

Deliverables:
- a concise integration log
- clean merge commits in the integration branch
- final status with changed branches, validations, conflicts resolved, and any residual risk

Startup behavior:
- first action is to inspect branch/worktree state and create a simple coordination checklist
- then wait for worker handoff notes
- do not run merge commands until the user explicitly authorizes integration
```

### 2. Path Validation Prompt

```text
You are running in the worktree at C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-path-validation. Stay in this worktree only.

You are the path-validation worker for the Windows search_code fix in codebase-memory-mcp. Use your standard subagent workflow.

Your lane:
- own src/foundation/str_util.c
- own src/foundation/str_util.h
- own tests/test_security.c
- own tests/test_str_util.c

Goal:
- make Windows path handling safe for this bug without weakening shell-injection protections used by existing shell-based code
- prefer introducing a path-safe validation/helper concept over globally loosening shell-arg validation if that would reduce security

Constraints:
- do not edit src/mcp/mcp.c
- do not edit tests/test_incremental.c or tests/test_mcp.c
- do not edit docs
- if you conclude the correct fix requires a boundary change, stop and report the smallest required contract change to the orchestrator

Required workflow:
- use your standard subagent workflow
- use code_mapper first if you need to confirm who else consumes cbm_validate_shell_arg or related helpers
- use test_checker before final validation to confirm minimum useful tests
- after implementation is complete, run exactly one reviewer subagent on the full worktree diff in one pass
- fix reviewer findings in the same worktree

What to produce:
- focused code changes only in your lane
- tests that preserve shell-injection defenses while allowing the backend stream to handle valid Windows paths correctly
- a short handoff note for the orchestrator: changed files, validation run, and any contract assumptions the backend stream should know
```

### 3. Tempfile Platform Prompt

```text
You are running in the worktree at C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-tempfile-platform. Stay in this worktree only.

You are the tempfile-platform worker for the Windows search_code fix in codebase-memory-mcp. Use your standard subagent workflow.

Your lane:
- own src/foundation/compat.c
- own src/foundation/compat.h
- own src/foundation/compat_fs.c
- own src/foundation/compat_fs.h
- own any temp-helper-specific test file you need

Goal:
- provide a clean cross-platform temp-file helper path that removes the current /tmp assumption on Windows
- keep the helpers reusable and safe
- avoid changing search_code call flow directly; the backend stream or orchestrator will consume the helper in mcp code

Constraints:
- do not edit src/mcp/mcp.c
- do not edit tests/test_incremental.c or tests/test_mcp.c
- do not edit docs
- keep your work within platform/helper plumbing

Required workflow:
- use your standard subagent workflow
- use code_mapper to inspect current temp-file helper patterns such as cbm_mkstemp and related compatibility layers
- use test_checker to scope the smallest useful validation set
- after implementation is complete, run exactly one reviewer subagent on the full worktree diff in one pass
- fix reviewer findings in the same worktree

What to produce:
- helper-level code only in your lane
- temp-file-focused regression coverage
- a short handoff note for the orchestrator: changed files, validation run, exported helper contracts, and any integration assumptions
```

### 4. Search Backend Prompt

```text
You are running in the worktree at C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-backend. Stay in this worktree only.

You are the search-backend worker for the Windows search_code fix in codebase-memory-mcp. Use your standard subagent workflow.

Your lane:
- own src/mcp/mcp.c
- own any new src/mcp/search_code_* helper files

Goal:
- make search_code actually work on Windows
- remove Unix-only assumptions from the Windows path
- keep non-Windows behavior stable unless a correctness fix is required

Preferred direction:
- a native or internal Windows-capable search execution path is preferred over building more shell strings
- if you keep any subprocess path on Windows, it must be safe, quote-correct, and compatible with tools actually available on Windows

Constraints:
- do not edit foundation helper/security files unless the orchestrator explicitly expands your contract
- do not edit tests/test_incremental.c, tests/test_mcp.c, scripts/smoke-test.sh, or docs except for the absolute minimum required to compile
- you are the only stream allowed to touch src/mcp/mcp.c

Required workflow:
- use your standard subagent workflow
- use code_mapper first to map the full search_code flow and any neighboring grep/subprocess abstractions
- use powershell-5.1-expert if you need precise Windows shell/process behavior reasoning
- use test_checker before final validation
- after implementation is complete, run exactly one reviewer subagent on the full worktree diff in one pass
- fix reviewer findings in the same worktree

What to produce:
- Windows-capable backend changes in your lane
- no new Unix-only assumptions on the Windows code path
- a short handoff note for the orchestrator: changed files, validations run, known merge risks, and any required foundation helper usage
```

### 5. Regression Tests Prompt

```text
You are running in the worktree at C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-regression-tests. Stay in this worktree only.

You are the regression-tests worker for the Windows search_code fix in codebase-memory-mcp. Use your standard subagent workflow.

Your lane:
- own tests/test_incremental.c
- own tests/test_mcp.c
- own scripts/smoke-test.sh

Goal:
- add regression coverage for the known Windows failures and preserve existing search_code behavior
- cover:
  - repo path containing &
  - safe-name repo path
  - temp-file failure regression
  - file_pattern
  - path_filter
  - compact/full/files modes
  - regex, limit, and context behavior that should remain stable

Constraints:
- do not edit src/
- do not edit docs
- if you discover the current test harness cannot express a required Windows scenario, report the smallest missing hook to the orchestrator instead of expanding into src/

Required workflow:
- use your standard subagent workflow
- use code_mapper to understand the existing search_code test layout and fixtures
- use test_checker early to shape the coverage plan
- after implementation is complete, run exactly one reviewer subagent on the full worktree diff in one pass
- fix reviewer findings in the same worktree

What to produce:
- regression tests and smoke coverage only in your lane
- a short handoff note for the orchestrator: changed files, validations run, and which regressions are now covered
```

### 6. Docs Prompt

```text
You are running in the worktree at C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp.worktrees\windows-search-docs. Stay in this worktree only.

You are the docs worker for the Windows search_code fix in codebase-memory-mcp. Use your standard subagent workflow.

Your lane:
- own README.md
- own CONTRIBUTING.md
- own docs/

Goal:
- document the Windows search_code issue and the resulting behavior after the fix
- document any platform limitations that remain
- document any maintainer expectations for Windows-safe temp files, path validation, and search execution

Constraints:
- do not edit src/
- do not edit tests/
- do not edit scripts/
- avoid speculative internal design details that are not part of the final merged behavior

Required workflow:
- use your standard subagent workflow
- use code_mapper only to locate relevant behavior references in docs or nearby code comments
- use test_checker only if you need to cross-check documented validation commands
- after implementation is complete, run exactly one reviewer subagent on the full worktree diff in one pass
- fix reviewer findings in the same worktree

What to produce:
- documentation-only changes in your lane
- a short handoff note for the orchestrator: changed files, documentation scope, and any assumptions that should be verified against the merged code
```

## Reviewer Subagent Prompt Template

Each worker should use a single `reviewer` subagent once at the end with something close to this:

```text
Review this entire worktree diff in one pass. Prioritize:
- correctness
- Windows behavior
- security regressions
- shell/subprocess safety
- merge-conflict risk with the other planned streams
- missing tests

Do not propose a broad rewrite. Give findings ordered by severity with file references. Focus on what would block a safe merge.
```

## Validation Expectations

### Stream-Local Minimum

- `path-validation`: focused foundation/security tests in its lane
- `tempfile-platform`: helper-focused tests in its lane; expand only if helper integration requires it
- `search-backend`: full MCP/search validation because behavior changes live here
- `regression-tests`: full test coverage for touched test harnesses plus smoke-path updates
- `docs`: docs-only verification and cross-check against current code/comments

### Per-Worktree Reviewer Gate

After each worker believes the branch is ready:

1. run exactly one `reviewer` subagent on the full diff
2. fix findings in the same worktree
3. rerun the smallest validation set that covers the fixes
4. report:
   - changed files
   - validations run
   - open risks
   - likely merge-conflict points, if any

### Final Integration Gate

The orchestrator should not merge back to `main` until the integrated branch has passed:

- full tests for the merged code
- security validation if subprocess/shell behavior changed
- a Windows end-to-end probe for both previously failing repo-name cases
- one final integrated `reviewer` pass on the whole combined diff

## Orchestrator Merge Procedure

1. Start from the orchestration worktree on `codex/windows-search-orchestrator`.
2. Pull the latest `main` with fast-forward only.
3. Confirm all worker branches have completed their own reviewer pass and posted:
   - changed files
   - validations run
   - known conflicts
4. Merge branches in this order:
   1. `codex/windows-search-path-validation`
   2. `codex/windows-search-tempfile-platform`
   3. `codex/windows-search-backend`
   4. `codex/windows-search-regression-tests`
   5. `codex/windows-search-docs`
5. After each merge:
   - run `git status`
   - check for unresolved conflicts
   - if conflicts exist, resolve them only in the orchestration worktree
   - run the smallest useful validation gate before proceeding
6. After merging `search-backend`, run the full `search_code`-relevant test gate before merging more streams.
7. After merging `regression-tests`, run the smoke path that exercises `search_code`.
8. After merging `docs`, run the final integrated gate:
   - full tests
   - security validation if subprocess/shell code changed
   - Windows end-to-end probe for:
     - path containing `&`
     - safe-name path
     - `search_code` with no `file_pattern`
     - `search_code` with `file_pattern`
     - `search_code` with `path_filter`
9. Run exactly one final `reviewer` subagent on the integrated diff.
10. Fix findings in the orchestration worktree and rerun the final gate.
11. Rebase the orchestration branch on the latest `main` if `main` moved during the effort.
12. Rerun the final gate if the rebase changed code.
13. Merge the orchestration branch back to `main` with fast-forward only if possible; otherwise do a normal non-destructive merge in the orchestrator worktree after validation.

## Safe Merge Commands

These are the commands the orchestrator should prefer.

```powershell
git switch codex/windows-search-orchestrator
git fetch origin
git switch main
git pull --ff-only
git switch codex/windows-search-orchestrator
git rebase main

git merge --no-ff codex/windows-search-path-validation
git merge --no-ff codex/windows-search-tempfile-platform
git merge --no-ff codex/windows-search-backend
git merge --no-ff codex/windows-search-regression-tests
git merge --no-ff codex/windows-search-docs

# run validation gates here

git switch main
git merge --ff-only codex/windows-search-orchestrator
```

If `--ff-only` to `main` is not possible because `main` moved, switch back to the orchestrator branch, rebase onto `main`, rerun final validation, and only then merge.

## Worker Return Format

Ask every worker to return the same short summary:

- branch name
- changed files
- validations run
- reviewer findings fixed
- known risks
- likely merge-conflict points

That makes the orchestration session much easier to run cleanly.
