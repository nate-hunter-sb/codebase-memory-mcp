/*
 * watcher.c — Git-based file change watcher.
 *
 * Strategy: git status + HEAD tracking (the most reliable approach).
 * For non-git projects, the watcher skips polling (no fsnotify/dirmtime yet).
 *
 *
 * Per-project state tracks:
 *   - Last git HEAD hash (detects commits, checkout, pull)
 *   - Last poll time + adaptive interval
 *   - Whether the project is a git repo
 *
 * Adaptive interval: 5s base + 1s per 500 files, capped at 60s.
 * Matches the Go watcher's `pollInterval()` logic.
 */
#include <stdint.h>
#include "watcher/watcher.h"
#include "store/store.h"
#include "foundation/constants.h"
#include "foundation/log.h"
#include "foundation/hash_table.h"
#include "foundation/compat.h"
#include "foundation/compat_fs.h"
#include "foundation/platform.h"
#include "foundation/str_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <sys/stat.h>

/* ── Per-project state ──────────────────────────────────────────── */

typedef struct {
    char *project_name;
    char *root_path;
    char last_head[CBM_SZ_64]; /* git HEAD hash */
    bool is_git;               /* false → skip polling */
    bool baseline_done;        /* true after first poll */
    int file_count;            /* approximate, for interval calc */
    int interval_ms;           /* adaptive poll interval */
    int64_t next_poll_ns;      /* next poll time (monotonic ns) */
} project_state_t;

/* ── Watcher struct ─────────────────────────────────────────────── */

struct cbm_watcher {
    cbm_store_t *store;
    cbm_index_fn index_fn;
    void *user_data;
    CBMHashTable *projects; /* name → project_state_t* */
    atomic_int stopped;
};

/* ── Constants ─────────────────────────────────────────────────── */

/* Time unit conversions */
#define NS_PER_SEC 1000000000LL
#define US_PER_MS 1000000LL

/* Adaptive poll interval parameters (ms) */
#define POLL_BASE_MS 5000
#define POLL_FILE_STEP 500 /* add 1s per this many files */
#define POLL_MAX_MS 60000

/* Sleep chunk for responsive shutdown (ms) */
#define SLEEP_CHUNK_MS 500

/* ── Time helper ────────────────────────────────────────────────── */

static int64_t now_ns(void) {
    struct timespec ts;
    cbm_clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((int64_t)ts.tv_sec * NS_PER_SEC) + ts.tv_nsec;
}

/* ── Adaptive interval ──────────────────────────────────────────── */

int cbm_watcher_poll_interval_ms(int file_count) {
    int ms = POLL_BASE_MS + ((file_count / POLL_FILE_STEP) * CBM_MSEC_PER_SEC);
    if (ms > POLL_MAX_MS) {
        ms = POLL_MAX_MS;
    }
    return ms;
}

/* ── Git helpers ────────────────────────────────────────────────── */

static int git_exec_capture(const char *const *argv, char **output) {
    char *captured = NULL;
    int exit_code = 0;
    if (cbm_exec_capture(argv, output ? &captured : NULL, &exit_code) != 0) {
        return CBM_NOT_FOUND;
    }
    if (exit_code != 0) {
        free(captured);
        return CBM_NOT_FOUND;
    }
    if (output) {
        *output = captured;
    } else {
        free(captured);
    }
    return 0;
}

static int git_capture_first_line(const char *const *argv, char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return CBM_NOT_FOUND;
    }

    char *output = NULL;
    if (git_exec_capture(argv, &output) != 0) {
        return CBM_NOT_FOUND;
    }

    char *line = output;
    while (*line == '\r' || *line == '\n') {
        line++;
    }
    if (*line == '\0') {
        free(output);
        return CBM_NOT_FOUND;
    }

    char *end = strpbrk(line, "\r\n");
    if (end) {
        *end = '\0';
    }
    snprintf(out, out_size, "%s", line);
    free(output);
    return 0;
}

static bool git_output_has_content(const char *const *argv) {
    char *output = NULL;
    if (git_exec_capture(argv, &output) != 0) {
        return false;
    }
    bool has_content = output[0] != '\0';
    free(output);
    return has_content;
}

static int count_output_lines(const char *text) {
    if (!text || !text[0]) {
        return 0;
    }

    int count = 0;
    bool saw_char = false;
    for (const char *p = text; *p; p++) {
        if (*p != '\r' && *p != '\n') {
            saw_char = true;
        }
        if (*p == '\n') {
            count++;
        }
    }
    size_t len = strlen(text);
    if (saw_char && text[len - SKIP_ONE] != '\n') {
        count++;
    }
    return count;
}

static bool path_is_absolute(const char *path) {
    if (!path || !path[0]) {
        return false;
    }
#ifdef _WIN32
    if ((isalpha((unsigned char)path[0]) && path[1] == ':' &&
         (path[2] == '/' || path[2] == '\\')) ||
        path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return false;
#else
    return path[0] == '/';
#endif
}

static void trim_trailing_sep(char *path) {
    if (!path) {
        return;
    }
    size_t len = strlen(path);
    while (len > 1 && path[len - SKIP_ONE] == '/') {
#ifdef _WIN32
        if (len == 3 && path[1] == ':' && path[2] == '/') {
            break;
        }
#endif
        path[--len] = '\0';
    }
}

static bool canonicalize_existing_dir(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0 || !cbm_is_dir(path)) {
        return false;
    }
#ifdef _WIN32
    if (!_fullpath(out, path, out_size)) {
        return false;
    }
#else
    if (!realpath(path, out)) {
        return false;
    }
#endif
    cbm_normalize_path_sep(out);
#ifdef _WIN32
    for (char *p = out; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
#endif
    trim_trailing_sep(out);
    return true;
}

static bool resolve_submodule_path(const char *root_path, const char *raw_path, char *out,
                                   size_t out_size) {
    if (!root_path || !raw_path || !out || out_size == 0) {
        return false;
    }

    while (*raw_path == ' ' || *raw_path == '\t') {
        raw_path++;
    }
    if (!raw_path[0] || path_is_absolute(raw_path)) {
        return false;
    }

    char joined[CBM_PATH_MAX];
    if (snprintf(joined, sizeof(joined), "%s/%s", root_path, raw_path) >= (int)sizeof(joined)) {
        return false;
    }

    char norm_root[CBM_PATH_MAX];
    char norm_candidate[CBM_PATH_MAX];
    if (!canonicalize_existing_dir(root_path, norm_root, sizeof(norm_root)) ||
        !canonicalize_existing_dir(joined, norm_candidate, sizeof(norm_candidate))) {
        return false;
    }

    size_t root_len = strlen(norm_root);
    if (root_len == 0 || strcmp(norm_root, norm_candidate) == 0) {
        return false;
    }
    if (strncmp(norm_candidate, norm_root, root_len) != 0 || norm_candidate[root_len] != '/') {
        return false;
    }

    snprintf(out, out_size, "%s", norm_candidate);
    return true;
}

static bool git_submodules_dirty_recursive(const char *root_path, int depth) {
    if (depth > CBM_SZ_16) {
        return false;
    }

    const char *argv[] = {"git", "-C", root_path, "config", "--file", ".gitmodules",
                          "--get-regexp", "path", NULL};
    char *output = NULL;
    if (git_exec_capture(argv, &output) != 0) {
        return false;
    }

    for (char *line = output; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            *next = '\0';
            next++;
            while (*next == '\r' || *next == '\n') {
                next++;
            }
        } else {
            next = line + strlen(line);
        }

        char *value = strpbrk(line, " \t");
        if (value) {
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            if (*value) {
                char submodule_path[CBM_PATH_MAX];
                if (resolve_submodule_path(root_path, value, submodule_path,
                                           sizeof(submodule_path))) {
                    const char *status_argv[] = {"git",         "--no-optional-locks",
                                                 "-C",          submodule_path,
                                                 "status",      "--porcelain",
                                                 "--untracked-files=normal", NULL};
                    if (git_output_has_content(status_argv) ||
                        git_submodules_dirty_recursive(submodule_path, depth + SKIP_ONE)) {
                        free(output);
                        return true;
                    }
                }
            }
        }
        line = next;
    }

    free(output);
    return false;
}

static bool is_git_repo(const char *root_path) {
    const char *argv[] = {"git", "-C", root_path, "rev-parse", "--git-dir", NULL};
    return git_exec_capture(argv, NULL) == 0;
}

static int git_head(const char *root_path, char *out, size_t out_size) {
    const char *argv[] = {"git", "-C", root_path, "rev-parse", "HEAD", NULL};
    return git_capture_first_line(argv, out, out_size);
}

/* Returns true if working tree has changes (modified, untracked, etc.).
 * Also checks submodules by walking them directly to avoid shell nesting. */
static bool git_is_dirty(const char *root_path) {
    const char *status_argv[] = {"git",         "--no-optional-locks", "-C",
                                 root_path,     "status",              "--porcelain",
                                 "--untracked-files=normal",           NULL};
    if (git_output_has_content(status_argv)) {
        return true;
    }
    return git_submodules_dirty_recursive(root_path, 0);
}

/* Count tracked files via git ls-files */
static int git_file_count(const char *root_path) {
    const char *argv[] = {"git", "-C", root_path, "ls-files", NULL};
    char *output = NULL;
    if (git_exec_capture(argv, &output) != 0) {
        return 0;
    }

    int count = count_output_lines(output);
    free(output);
    return count;
}

/* ── Project state lifecycle ────────────────────────────────────── */

static project_state_t *state_new(const char *name, const char *root_path) {
    project_state_t *s = calloc(CBM_ALLOC_ONE, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->project_name = strdup(name);
    s->root_path = strdup(root_path);
    s->interval_ms = POLL_BASE_MS;
    return s;
}

static void state_free(project_state_t *s) {
    if (!s) {
        return;
    }
    free(s->project_name);
    free(s->root_path);
    free(s);
}

/* Hash table foreach callback to free state entries */
static void free_state_entry(const char *key, void *val, void *ud) {
    (void)key;
    (void)ud;
    state_free(val);
}

/* ── Watcher lifecycle ──────────────────────────────────────────── */

cbm_watcher_t *cbm_watcher_new(cbm_store_t *store, cbm_index_fn index_fn, void *user_data) {
    cbm_watcher_t *w = calloc(CBM_ALLOC_ONE, sizeof(*w));
    if (!w) {
        return NULL;
    }
    w->store = store;
    w->index_fn = index_fn;
    w->user_data = user_data;
    w->projects = cbm_ht_create(CBM_SZ_32);
    atomic_init(&w->stopped, 0);
    return w;
}

void cbm_watcher_free(cbm_watcher_t *w) {
    if (!w) {
        return;
    }
    cbm_ht_foreach(w->projects, free_state_entry, NULL);
    cbm_ht_free(w->projects);
    free(w);
}

/* ── Watch list management ──────────────────────────────────────── */

void cbm_watcher_watch(cbm_watcher_t *w, const char *project_name, const char *root_path) {
    if (!w || !project_name || !root_path) {
        return;
    }

    /* Reject control characters before passing paths to git argv helpers. */
    if (!cbm_validate_path_arg(root_path)) {
        cbm_log_warn("watcher.watch.reject", "project", project_name, "reason",
                     "path contains invalid characters");
        return;
    }

    /* Remove old entry first (key points to state's project_name) */
    project_state_t *old = cbm_ht_get(w->projects, project_name);
    if (old) {
        cbm_ht_delete(w->projects, project_name);
        state_free(old);
    }

    project_state_t *s = state_new(project_name, root_path);
    cbm_ht_set(w->projects, s->project_name, s);
    cbm_log_info("watcher.watch", "project", project_name, "path", root_path);
}

void cbm_watcher_unwatch(cbm_watcher_t *w, const char *project_name) {
    if (!w || !project_name) {
        return;
    }
    project_state_t *s = cbm_ht_get(w->projects, project_name);
    if (s) {
        cbm_ht_delete(w->projects, project_name);
        state_free(s);
        cbm_log_info("watcher.unwatch", "project", project_name);
    }
}

void cbm_watcher_touch(cbm_watcher_t *w, const char *project_name) {
    if (!w || !project_name) {
        return;
    }
    project_state_t *s = cbm_ht_get(w->projects, project_name);
    if (s) {
        /* Reset backoff — poll immediately on next cycle */
        s->next_poll_ns = 0;
    }
}

int cbm_watcher_watch_count(const cbm_watcher_t *w) {
    if (!w) {
        return 0;
    }
    return (int)cbm_ht_count(w->projects);
}

/* ── Single poll cycle ──────────────────────────────────────────── */

/* Init baseline for a project: check if git, get HEAD, count files */
static void init_baseline(project_state_t *s) {
    struct stat st;
    if (stat(s->root_path, &st) != 0) {
        cbm_log_warn("watcher.root_gone", "project", s->project_name, "path", s->root_path);
        s->baseline_done = true;
        s->is_git = false;
        return;
    }

    s->is_git = is_git_repo(s->root_path);
    s->baseline_done = true;

    if (s->is_git) {
        git_head(s->root_path, s->last_head, sizeof(s->last_head));
        s->file_count = git_file_count(s->root_path);
        s->interval_ms = cbm_watcher_poll_interval_ms(s->file_count);
        cbm_log_info("watcher.baseline", "project", s->project_name, "strategy", "git", "files",
                     s->file_count > 0 ? "yes" : "0");
    } else {
        cbm_log_info("watcher.baseline", "project", s->project_name, "strategy", "none");
    }

    s->next_poll_ns = now_ns() + ((int64_t)s->interval_ms * US_PER_MS);
}

/* Check if a project has changes. Returns true if reindex needed. */
static bool check_changes(project_state_t *s) {
    if (!s->is_git) {
        return false;
    }

    /* Check HEAD movement */
    char head[CBM_SZ_64] = {0};
    if (git_head(s->root_path, head, sizeof(head)) == 0) {
        if (s->last_head[0] != '\0' && strcmp(head, s->last_head) != 0) {
            /* HEAD moved — commit, checkout, pull */
            strncpy(s->last_head, head, sizeof(s->last_head) - 1);
            return true;
        }
        strncpy(s->last_head, head, sizeof(s->last_head) - 1);
    }

    /* Check working tree */
    return git_is_dirty(s->root_path);
}

/* Context for poll_once foreach callback */
typedef struct {
    cbm_watcher_t *w;
    int64_t now;
    int reindexed;
} poll_ctx_t;

static void poll_project(const char *key, void *val, void *ud) {
    (void)key;
    poll_ctx_t *ctx = ud;
    project_state_t *s = val;
    if (!s) {
        return;
    }

    /* Initialize baseline on first poll */
    if (!s->baseline_done) {
        init_baseline(s);
        return;
    }

    /* Skip non-git projects */
    if (!s->is_git) {
        return;
    }

    /* Respect adaptive interval */
    if (ctx->now < s->next_poll_ns) {
        return;
    }

    /* Check for changes */
    bool changed = check_changes(s);
    if (!changed) {
        s->next_poll_ns = ctx->now + ((int64_t)s->interval_ms * US_PER_MS);
        return;
    }

    /* Trigger reindex */
    cbm_log_info("watcher.changed", "project", s->project_name, "strategy", "git");
    if (ctx->w->index_fn) {
        int rc = ctx->w->index_fn(s->project_name, s->root_path, ctx->w->user_data);
        if (rc == 0) {
            ctx->reindexed++;
            /* Update HEAD after successful reindex */
            git_head(s->root_path, s->last_head, sizeof(s->last_head));
            /* Refresh file count for interval */
            s->file_count = git_file_count(s->root_path);
            s->interval_ms = cbm_watcher_poll_interval_ms(s->file_count);
        } else {
            cbm_log_warn("watcher.index.err", "project", s->project_name);
        }
    }

    s->next_poll_ns = ctx->now + ((int64_t)s->interval_ms * US_PER_MS);
}

int cbm_watcher_poll_once(cbm_watcher_t *w) {
    if (!w) {
        return 0;
    }

    poll_ctx_t ctx = {
        .w = w,
        .now = now_ns(),
        .reindexed = 0,
    };
    cbm_ht_foreach(w->projects, poll_project, &ctx);
    return ctx.reindexed;
}

/* ── Blocking run loop ──────────────────────────────────────────── */

void cbm_watcher_stop(cbm_watcher_t *w) {
    if (w) {
        atomic_store(&w->stopped, 1);
    }
}

int cbm_watcher_run(cbm_watcher_t *w, int base_interval_ms) {
    if (!w) {
        return CBM_NOT_FOUND;
    }
    if (base_interval_ms <= 0) {
        base_interval_ms = POLL_BASE_MS;
    }

    cbm_log_info("watcher.start", "interval_ms", base_interval_ms > 999 ? "multi-sec" : "fast");

    while (!atomic_load(&w->stopped)) {
        cbm_watcher_poll_once(w);

        /* Sleep in small increments to allow responsive shutdown */
        int slept = 0;
        while (slept < base_interval_ms && !atomic_load(&w->stopped)) {
            int chunk = base_interval_ms - slept;
            if (chunk > SLEEP_CHUNK_MS) {
                chunk = SLEEP_CHUNK_MS;
            }
            cbm_usleep((unsigned)chunk * CBM_MSEC_PER_SEC);
            slept += chunk;
        }
    }

    cbm_log_info("watcher.stop");
    return 0;
}
