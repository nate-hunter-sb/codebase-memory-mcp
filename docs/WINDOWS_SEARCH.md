# Windows `search_code`

This note documents the Windows-specific `search_code` failure that motivated the fix, the user-visible behavior that must hold after the paired backend/temp-file change merges, and the maintainer rules for keeping that behavior intact.

Until that change merges, the current implementation still contains POSIX-specific `/tmp` and `grep`/`xargs` assumptions in the `search_code` path. Treat this document as the contract for the merged behavior, not as a statement that the current branch already satisfies it.

## What Broke On Windows

Before the fix, `search_code` assumed a POSIX runtime in two places that are not safe on native Windows:

- Temporary search artifacts were written under `/tmp`.
- Search execution was assembled around POSIX shell tooling such as `grep` and `xargs`.

In native Windows environments, that could fail before any search results were collected. The most obvious user-facing symptom was `search failed: temp file`.

## Behavior Required After The Fix

After the fix, `search_code` must behave the same way on macOS, Linux, and Windows from the caller's point of view.

- Native Windows installs must not require `/tmp`, `grep`, `xargs`, MSYS2, Git Bash, or Cygwin in order to run `search_code`.
- Temporary files used for pattern passing or scoped file lists must be created in the host OS temp directory and removed on both success and error paths.
- Shell-facing inputs must be validated before search execution starts. Invalid path or file-pattern input should fail early instead of reaching the backend command.
- When an indexed file list is available, `search_code` should search only indexed files. If scoped execution is unavailable, it should fall back to a recursive search rooted at the indexed project path.
- Output modes and ranking semantics should remain platform-consistent. The Windows fix is about reliability and safety, not about changing the public shape of `compact`, `full`, or `files` results.

## Remaining Platform Limitations

The Windows fix does not remove every search constraint. These limits still apply and should stay documented:

- `search_code` still needs a writable temp directory on the host OS.
- `file_pattern` is still a file-selection filter, not a general path query language.
- `path_filter` still acts on collected paths after the search backend returns raw hits.
- Search scope is still bounded by the indexed project root. Scoped mode narrows that to indexed files only; fallback mode searches recursively under the project root.

## Maintainer Expectations

If you touch the `search_code` path, keep these rules intact:

- Use platform temp helpers such as `cbm_get_tmpdir()`, `cbm_temp_path()`, `cbm_temp_template()`, and `cbm_mkstemp()` or an equivalent cross-platform abstraction. Prefer caller-owned temp-path storage when building paths that may be reused across threads. Do not reintroduce hardcoded POSIX temp paths.
- Validate any value that will reach a shell command or backend process. Today that includes the resolved project root and `file_pattern`, and the same rule applies to future shell-facing inputs.
- Prefer passing search data through temp files or explicit arguments instead of embedding raw user patterns directly into command strings.
- Keep cleanup symmetric. Temporary files created for pattern passing or scoped file lists must be removed on all return paths, including backend startup failures.
- Preserve the search contract: indexed-file scoping first, recursive project-root fallback second.
- Treat native Windows support as a release requirement. A fix that only works under POSIX-compatible shells is incomplete.

## Current Code Touchpoints

When reviewing or updating the implementation, these are the code paths that define the current contract:

- `src/mcp/mcp.c`: `handle_search_code`, `validate_search_args`, `write_pattern_file`, `write_scoped_filelist`, `build_grep_cmd`
- `src/foundation/compat.h`: temp-path helper declarations
- `src/foundation/compat.c`: `cbm_get_tmpdir`, `cbm_temp_path`, `cbm_temp_template`, `cbm_mkstemp`
- `src/foundation/str_util.c`: `cbm_validate_shell_arg`

If those touchpoints change, keep this document synchronized with the merged behavior.
