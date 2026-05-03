# Windows Binary Deploy Notes

This file exists so future agents do not need prior thread history to understand how this repo's Windows binary is actually deployed for the user.

## Core Distinction

There are three separate layers on this machine:

1. Repo build output
   - Built from this repo, typically:
   - `C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp\build\c\codebase-memory-mcp.exe`

2. Custom Codex wrapper install folder
   - Lives under:
   - `C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp`
   - Contains wrapper executables and installed upstream binaries.

3. Claude processes
   - Claude also runs `codebase-memory-mcp` binaries from the same install folder.
   - Do not assume every `codebase-memory-mcp.exe` process belongs to Codex.

Rebuilding the repo binary does not automatically update the wrapper install or the live Codex integration.

## Current Codex Wiring

As of 2026-04-08, Codex is launched through:

- Wrapper launcher:
  - `C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\launch-codebase-memory-mcp-wrapper.cmd`
- Wrapper executable:
  - `C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\codebase-memory-mcp-codex-wrapper-trace-path.exe`

That launcher sets `CODEBASE_MEMORY_MCP_UPSTREAM` and then starts the wrapper. The wrapper/upstream arrangement is user-created and is not produced automatically by repo builds.

## Current Upstream Target

As of 2026-05-03, the wrapper launcher was updated to point at:

- `C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\codebase-memory-mcp-upstream-show-token-savings-20260502.exe`

SHA-256: `E62D5C1DBE5C0BD7AC64985246C68E7EA1B9F14CEBDA6FE25BE3C52FE9A80927`

That file was copied from the repo build output (v0.10.3). It includes per-tool baseline savings
reporting in `show_token_savings` (exact for `get_code_snippet`/`manage_adr`; unique-file dedup × 8 KB
for search/trace; mode-aware raw cap for `search_code`; test-file filter for `trace_path`), as well as
the `get_code_snippet` file+line support and tool schema fixes from v0.10.0–v0.10.2.

## What To Do After Making Repo Fixes

If an agent makes code changes that must affect the binary Codex uses, do all of the following:

1. Merge the fix into the branch the user wants to treat as source of truth.
   - On 2026-04-08, local `main` was fast-forwarded to include:
   - `e6a5722` `fix(windows): land compatibility follow-ups`
   - `affddf1` `fix(windows): restore cache identity compatibility`

2. Rebuild the repo binary.
   - On this machine, WinLibs exposes `mingw32-make`, `gcc.exe`, and `g++.exe`; `Makefile.cbm` selects `gcc`/`g++` by default for Windows MinGW runs when no explicit compiler override is provided.
   - Normal rebuild command: `mingw32-make -f Makefile.cbm cbm`.
   - Intentional compiler checks can still pass explicit `CC`/`CXX`; those overrides should remain respected.
   - If the standard build is blocked by local `libsanitizer.spec` or `-lz` availability, document exactly which focused fallback was required, such as `SANITIZE=` for test builds or a non-static `WIN32_LIBS` on a WinLibs install without static zlib.
   - `Makefile.cbm` checks these Windows WinLibs gaps up front. A missing `libsanitizer.spec` diagnostic means the selected compiler cannot support the requested sanitizer flags; use `SANITIZE=` only as an explicit focused fallback. A missing `libz.a` diagnostic means static MinGW linking cannot find static zlib; install static zlib or intentionally override `WIN32_LIBS` for a non-static local validation.

3. Run a focused smoke test against the rebuilt repo binary before swapping the installed upstream target.
   - For Windows search/cache work, include the smallest realistic checks for:
   - repo path containing `&`
   - safe-name repo path
   - `search_code` with no `file_pattern`
   - `search_code` with `file_pattern`
   - `search_code` with `path_filter`
   - Any Windows cache migration/compatibility path relevant to the fix

4. Copy the rebuilt repo binary into the install folder under a new versioned filename.
   - Prefer a descriptive immutable name, for example:
   - `codebase-memory-mcp-upstream-windows-cache-identity-YYYYMMDD.exe`
   - Keep old upstream binaries as rollback targets.

5. Update the wrapper launcher, not the wrapper executable.
   - File:
   - `C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\launch-codebase-memory-mcp-wrapper.cmd`
   - Change only the `CODEBASE_MEMORY_MCP_UPSTREAM=...` target unless wrapper behavior itself must change.

6. Restart or recycle every active client using the launcher after the launcher change.
   - A repo build alone is not enough.
   - A launcher-file edit alone is not enough.
   - Running Claude and Codex wrapper/upstream processes must exit before they pick up the new upstream target.

7. Verify the live process tree after restart.
   - Confirm whether Codex is running:
   - `codebase-memory-mcp-codex-wrapper-trace-path.exe`
   - Confirm which upstream binary the wrapper now points to.
   - Do not confuse Claude-owned processes with Codex-owned processes; both may use this launcher on this machine.

## What Not To Change Carelessly

- Do not overwrite `C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\codebase-memory-mcp.exe` unless the user explicitly wants the shared non-wrapper binary replaced.
- Do not assume the `AppData\Local\codebase-memory-mcp` contents are repo-managed.
- Do not delete old installed upstream binaries. Keep them for rollback unless the user explicitly asks otherwise.

## Process Attribution Notes

When checking live processes:

- Claude-owned MCP processes have shown parent process `claude.exe`.
- Codex-owned wrapper processes appear as:
  - `codebase-memory-mcp-codex-wrapper-trace-path.exe`

If a future agent sees multiple `codebase-memory-mcp.exe` processes, it must identify parent processes before concluding which app is using which binary.

## Practical Verification Commands

Useful checks on this machine:

```powershell
Get-Content 'C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\launch-codebase-memory-mcp-wrapper.cmd'
```

```powershell
Get-Process | Where-Object {
  $_.ProcessName -like 'codebase-memory-mcp-codex-wrapper*' -or
  $_.ProcessName -eq 'codebase-memory-mcp'
} | Select-Object Id, ProcessName, Path, StartTime
```

```powershell
Get-CimInstance Win32_Process | Where-Object {
  $_.Name -like 'codebase-memory-mcp-codex-wrapper*' -or
  $_.Name -eq 'codebase-memory-mcp.exe'
} | Select-Object ProcessId, Name, ExecutablePath, CommandLine, ParentProcessId
```

```powershell
Get-FileHash 'C:\Users\nate.hunter\Documents\Playground\codebase-memory-mcp\build\c\codebase-memory-mcp.exe' -Algorithm SHA256
Get-FileHash 'C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\codebase-memory-mcp-upstream-fix-anyof-schema-20260502.exe' -Algorithm SHA256
```

## Summary For Future Agents

If the repo code is fixed but Claude or Codex still behaves like the old binary, the most likely cause is that the wrapper launcher is still pointing at an older installed upstream executable or an old wrapper/upstream process is still alive. Check `launch-codebase-memory-mcp-wrapper.cmd` and the live process tree, not just the repo build output.
