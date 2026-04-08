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

As of 2026-04-08, the wrapper launcher was updated to point at:

- `C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\codebase-memory-mcp-upstream-windows-cache-identity-20260408.exe`

That file was copied from the repo build output and matched it byte-for-byte by SHA-256 at the time of deployment.

## What To Do After Making Repo Fixes

If an agent makes code changes that must affect the binary Codex uses, do all of the following:

1. Merge the fix into the branch the user wants to treat as source of truth.
   - On 2026-04-08, local `main` was fast-forwarded to include:
   - `e6a5722` `fix(windows): land compatibility follow-ups`
   - `affddf1` `fix(windows): restore cache identity compatibility`

2. Rebuild the repo binary.
   - Do not assume the stock Windows make path works unchanged in this shell.
   - If the standard build is blocked by toolchain issues, document exactly what was required to get a working local build.

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

6. Restart Codex after the launcher change.
   - A repo build alone is not enough.
   - A launcher-file edit alone is not enough.
   - The running Codex wrapper process must be restarted to pick up the new upstream target.

7. Verify the live process tree after restart.
   - Confirm whether Codex is running:
   - `codebase-memory-mcp-codex-wrapper-trace-path.exe`
   - Confirm which upstream binary the wrapper now points to.
   - Do not confuse Claude-owned processes with Codex-owned processes.

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
Get-FileHash 'C:\Users\nate.hunter\AppData\Local\codebase-memory-mcp\codebase-memory-mcp-upstream-windows-cache-identity-20260408.exe' -Algorithm SHA256
```

## Summary For Future Agents

If the repo code is fixed but Codex still behaves like the old binary, the most likely cause is that the wrapper launcher is still pointing at an older installed upstream executable. Check `launch-codebase-memory-mcp-wrapper.cmd`, not just the repo build output.
