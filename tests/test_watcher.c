/*
 * test_watcher.c — Tests for the file change watcher module.
 *
 * Covers: adaptive interval, watch/unwatch lifecycle, git change detection,
 * poll_once behavior.
 */
#include "../src/foundation/compat.h"
#include "test_framework.h"
#include "test_helpers.h"
#include <watcher/watcher.h>
#include <store/store.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

/* ══════════════════════════════════════════════════════════════════
 *  ADAPTIVE INTERVAL
 * ══════════════════════════════════════════════════════════════════ */

TEST(poll_interval_base) {
    /* 0 files → 5s base */
    int ms = cbm_watcher_poll_interval_ms(0);
    ASSERT_EQ(ms, 5000);
    PASS();
}

TEST(poll_interval_scaling) {
    /* 1000 files → 5000 + 2*1000 = 7000ms */
    int ms = cbm_watcher_poll_interval_ms(1000);
    ASSERT_EQ(ms, 7000);

    /* 5000 files → 5000 + 10*1000 = 15000ms */
    ms = cbm_watcher_poll_interval_ms(5000);
    ASSERT_EQ(ms, 15000);
    PASS();
}

TEST(poll_interval_cap) {
    /* 100K files → capped at 60s */
    int ms = cbm_watcher_poll_interval_ms(100000);
    ASSERT_EQ(ms, 60000);
    PASS();
}

TEST(poll_interval_small) {
    /* 499 files → 5000 + 0*1000 = 5000ms (integer division) */
    int ms = cbm_watcher_poll_interval_ms(499);
    ASSERT_EQ(ms, 5000);

    /* 500 files → 5000 + 1*1000 = 6000ms */
    ms = cbm_watcher_poll_interval_ms(500);
    ASSERT_EQ(ms, 6000);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  LIFECYCLE
 * ══════════════════════════════════════════════════════════════════ */

TEST(watcher_create_free) {
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);
    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_watch_unwatch) {
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    cbm_watcher_watch(w, "project-a", "/tmp/project-a");
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);

    cbm_watcher_watch(w, "project-b", "/tmp/project-b");
    ASSERT_EQ(cbm_watcher_watch_count(w), 2);

    cbm_watcher_unwatch(w, "project-a");
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);

    cbm_watcher_unwatch(w, "project-b");
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_unwatch_nonexistent) {
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    /* Should not crash */
    cbm_watcher_unwatch(w, "nonexistent");
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_watch_replace) {
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    cbm_watcher_watch(w, "project-a", "/tmp/old-path");
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);

    /* Replace with new path */
    cbm_watcher_watch(w, "project-a", "/tmp/new-path");
    ASSERT_EQ(cbm_watcher_watch_count(w), 1); /* still 1 */

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_null_safety) {
    /* All functions should be NULL-safe */
    cbm_watcher_free(NULL);
    cbm_watcher_watch(NULL, "x", "/x");
    cbm_watcher_unwatch(NULL, "x");
    cbm_watcher_touch(NULL, "x");
    ASSERT_EQ(cbm_watcher_watch_count(NULL), 0);
    ASSERT_EQ(cbm_watcher_poll_once(NULL), 0);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  POLL WITH REAL GIT REPO
 * ══════════════════════════════════════════════════════════════════ */

/* Index callback counter */
static int index_call_count = 0;
static int index_callback(const char *name, const char *path, void *ud) {
    (void)name;
    (void)path;
    (void)ud;
    index_call_count++;
    return 0;
}

typedef struct watcher_repo_file {
    const char *path;
    const char *content;
} watcher_repo_file_t;

static int watcher_init_repo(const char *repo_path, const watcher_repo_file_t *files,
                             size_t file_count) {
    if (th_git_init_repo(repo_path) != 0) {
        return -1;
    }

    for (size_t i = 0; i < file_count; i++) {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", repo_path, files[i].path);
        if (th_write_file(full_path, files[i].content) != 0) {
            return -1;
        }
    }

    return th_git_commit_all(repo_path, "init");
}

static int watcher_commit_all(const char *repo_path, const char *message) {
    return th_git_commit_all(repo_path, message);
}

static int watcher_reinit_repo(const char *repo_path, const char *message) {
    char git_dir[1024];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", repo_path);
    th_rmtree(git_dir);
    if (th_git_init_repo(repo_path) != 0) {
        return -1;
    }
    return th_git_commit_all(repo_path, message);
}

TEST(watcher_poll_no_projects) {
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    int reindexed = cbm_watcher_poll_once(w);
    ASSERT_EQ(reindexed, 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_poll_nonexistent_path) {
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    cbm_watcher_watch(w, "ghost", "/tmp/cbm_test_nonexistent_path_12345");

    /* First poll → init_baseline (path doesn't exist → skip) */
    index_call_count = 0;
    int reindexed = cbm_watcher_poll_once(w);
    ASSERT_EQ(reindexed, 0);
    ASSERT_EQ(index_call_count, 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_poll_this_repo) {
    /* Use this project's own repo as a real git repo test */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    /* Watch our own repo root (we know it's a git repo) */
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        cbm_watcher_free(w);
        cbm_store_close(store);
        SKIP("getcwd failed");
    }

    cbm_watcher_watch(w, "self", cwd);

    /* First poll: init baseline (no reindex expected) */
    index_call_count = 0;
    int reindexed = cbm_watcher_poll_once(w);
    ASSERT_EQ(reindexed, 0); /* baseline only */

    /* Second poll: check for changes. This repo has dirty working tree
     * (from the tests we just created), so it should detect changes.
     * But the adaptive interval hasn't elapsed yet, so it won't poll. */

    /* Touch to reset interval, then poll */
    cbm_watcher_touch(w, "self");
    reindexed = cbm_watcher_poll_once(w);
    /* May or may not reindex depending on whether working tree is dirty.
     * In CI, working tree might be clean. Just verify it doesn't crash. */

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_stop_flag) {
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    /* Set stop flag */
    cbm_watcher_stop(w);

    /* Run should return immediately */
    int rc = cbm_watcher_run(w, 1000);
    ASSERT_EQ(rc, 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  GIT CHANGE DETECTION (with temp repo)
 * ══════════════════════════════════════════════════════════════════ */

TEST(watcher_detects_git_commit) {
    /* Create a temporary git repo */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_test_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    cbm_watcher_watch(w, "temp-repo", tmpdir);
    index_call_count = 0;

    /* First poll: baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Make a change: new commit */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "world\n");
    }
    ASSERT_EQ(watcher_commit_all(tmpdir, "add world"), 0);

    /* Touch to bypass interval, then poll */
    cbm_watcher_touch(w, "temp-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1); /* should detect HEAD change */

    /* Poll again without changes → no reindex */
    cbm_watcher_touch(w, "temp-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1); /* still 1, no new changes */

    /* Cleanup */
    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_detects_dirty_worktree) {
    /* Create a temporary git repo */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_dirty_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    cbm_watcher_watch(w, "dirty-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);

    /* Make working tree dirty (uncommitted change) */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "modified\n");
    }

    /* Poll → should detect dirty worktree */
    cbm_watcher_touch(w, "dirty-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    /* Cleanup */
    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_detects_dirty_worktree_special_path) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm watcher & dirty_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "dirty-special", tmpdir);
    index_call_count = 0;

    cbm_watcher_poll_once(w);

    {
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s/file.txt", tmpdir);
        th_append_file(file_path, "special path\n");
    }

    cbm_watcher_touch(w, "dirty-special");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(git_helpers_capture_output_and_exit_code) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_git_capture_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    char *output = NULL;
    int exit_code = -1;
    const char *ls_args[] = {"ls-files", NULL};
    ASSERT_EQ(th_git_capture(tmpdir, ls_args, &output, &exit_code), 0);
    ASSERT_EQ(exit_code, 0);
    ASSERT(strstr(output, "file.txt") != NULL);
    free(output);

    output = NULL;
    const char *bad_args[] = {"rev-parse", "refs/does-not-exist", NULL};
    ASSERT_EQ(th_git_capture(tmpdir, bad_args, &output, &exit_code), 0);
    ASSERT_NEQ(exit_code, 0);
    free(output);

    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_detects_new_file) {
    /* Create a temporary git repo */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_newf_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    cbm_watcher_watch(w, "newf-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Add a new untracked file */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/newfile.go", tmpdir);
        th_write_file(_p, "new content\n");
    }

    /* Touch to bypass interval, then poll */
    cbm_watcher_touch(w, "newf-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1); /* should detect untracked file */

    /* Cleanup */
    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_no_change_no_reindex) {
    /* Create a temporary git repo */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_nochg_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    cbm_watcher_watch(w, "nochg-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Poll multiple times with no changes — never triggers reindex */
    for (int i = 0; i < 5; i++) {
        cbm_watcher_touch(w, "nochg-repo");
        cbm_watcher_poll_once(w);
    }
    ASSERT_EQ(index_call_count, 0);

    /* Cleanup */
    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_multiple_projects) {
    /* Create two temporary git repos */
    char tmpdirA[256];
    snprintf(tmpdirA, sizeof(tmpdirA), "/tmp/cbm_watcher_mA_XXXXXX");
    char tmpdirB[256];
    snprintf(tmpdirB, sizeof(tmpdirB), "/tmp/cbm_watcher_mB_XXXXXX");
    if (!cbm_mkdtemp(tmpdirA) || !cbm_mkdtemp(tmpdirB))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files_a[] = {{"a.txt", "a\n"}};
    watcher_repo_file_t files_b[] = {{"b.txt", "b\n"}};
    if (watcher_init_repo(tmpdirA, files_a, 1) != 0) {
        th_rmtree(tmpdirA);
        th_rmtree(tmpdirB);
        SKIP("git not available");
    }
    if (watcher_init_repo(tmpdirB, files_b, 1) != 0) {
        th_rmtree(tmpdirA);
        th_rmtree(tmpdirB);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    cbm_watcher_watch(w, "projA", tmpdirA);
    cbm_watcher_watch(w, "projB", tmpdirB);
    ASSERT_EQ(cbm_watcher_watch_count(w), 2);
    index_call_count = 0;

    /* Baseline both */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Modify only A */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/a.txt", tmpdirA);
        th_append_file(_p, "modified\n");
    }

    /* Poll — only A should trigger */
    cbm_watcher_touch(w, "projA");
    cbm_watcher_touch(w, "projB");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1); /* only A changed */

    /* Cleanup */
    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdirA);
    th_rmtree(tmpdirB);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  NON-GIT PROJECT
 * ══════════════════════════════════════════════════════════════════ */

TEST(watcher_non_git_skips) {
    /* Non-git dir → baseline sets is_git=false → poll never reindexes.
     * Port of TestProbeStrategyNonGit behavior. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_nongit_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    /* Create a file so it's not empty */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_write_file(_p, "hello\n");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "nongit", tmpdir);
    index_call_count = 0;

    /* Baseline — should detect non-git and set is_git=false */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Modify file */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "modified\n");
    }

    /* Touch + poll — should NOT trigger (non-git projects are skipped) */
    cbm_watcher_touch(w, "nongit");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Even add a new file — still no reindex */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/new.txt", tmpdir);
        th_write_file(_p, "new\n");
    }
    cbm_watcher_touch(w, "nongit");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  ADAPTIVE INTERVAL BEHAVIOR
 * ══════════════════════════════════════════════════════════════════ */

TEST(watcher_interval_blocks_repoll) {
    /* After baseline, the adaptive interval (5s minimum) should block
     * immediate re-polling. Without touch(), the next poll is a no-op.
     * Port of TestWatcherGitNoChanges' interval behavior. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_intv_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "intv-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Make repo dirty */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "dirty\n");
    }

    /* Poll WITHOUT touch — interval should block checking */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0); /* blocked by interval */

    /* Now touch to bypass interval */
    cbm_watcher_touch(w, "intv-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1); /* now detected */

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_large_repo_file_count_sets_interval) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_manyfiles_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    if (th_git_init_repo(tmpdir) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    for (int i = 0; i < 550; i++) {
        char file_path[1024];
        char content[64];
        snprintf(file_path, sizeof(file_path), "%s/file_%03d.go", tmpdir, i);
        snprintf(content, sizeof(content), "package main\n\nvar File%d = %d\n", i, i);
        ASSERT_EQ(th_write_file(file_path, content), 0);
    }
    ASSERT_EQ(th_git_commit_all(tmpdir, "init"), 0);

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "manyfiles", tmpdir);
    index_call_count = 0;

    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    {
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s/file_000.go", tmpdir);
        th_append_file(file_path, "var Changed = true\n");
    }

    cbm_usleep(5200 * 1000);
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    cbm_usleep(1200 * 1000);
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_poll_interval_full_table) {
    /* Full table-driven test matching Go TestPollInterval exactly */
    struct {
        int files;
        int expected_ms;
    } tests[] = {
        {0, 5000},     {70, 5000},     {499, 5000},    {500, 6000},     {2000, 9000},
        {5000, 15000}, {10000, 25000}, {50000, 60000}, {100000, 60000},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    for (int i = 0; i < n; i++) {
        int got = cbm_watcher_poll_interval_ms(tests[i].files);
        if (got != tests[i].expected_ms) {
            fprintf(stderr, "FAIL pollInterval(%d) = %d, want %d\n", tests[i].files, got,
                    tests[i].expected_ms);
            return 1;
        }
    }
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  GIT REMOVAL + CONTINUED DIRTY + BASELINE DIRTY
 * ══════════════════════════════════════════════════════════════════ */

TEST(watcher_git_removed_no_crash) {
    /* Init git repo, baseline, remove .git, poll → should not crash.
     * Port of TestStrategyDowngradeGitToDirMtime behavior (C version
     * doesn't downgrade — just git commands fail silently). */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_rmgit_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "rmgit-repo", tmpdir);
    index_call_count = 0;

    /* Baseline — detects git */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Remove .git — git commands will fail */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/.git", tmpdir);
        th_rmtree(_p);
    }

    /* Poll — should not crash, git_head() and git_is_dirty() fail gracefully */
    cbm_watcher_touch(w, "rmgit-repo");
    cbm_watcher_poll_once(w);
    /* No assertion on index_call_count — behavior is implementation-defined.
     * Main assertion: no crash, no ASan violation. */

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_continued_dirty) {
    /* If working tree stays dirty, each poll should re-trigger reindex.
     * Port of repeated git sentinel detection behavior. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_cont_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "cont-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Make dirty */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "dirty\n");
    }

    /* First detection */
    cbm_watcher_touch(w, "cont-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    /* Still dirty — should detect again */
    cbm_watcher_touch(w, "cont-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 2);

    /* Commit to clean up, then poll — should not trigger */
    ASSERT_EQ(watcher_commit_all(tmpdir, "clean"), 0);

    /* HEAD changed → will trigger one more reindex */
    cbm_watcher_touch(w, "cont-repo");
    cbm_watcher_poll_once(w);
    /* HEAD change from commit → reindex again (count = 3) */

    /* Now truly clean — no more reindexes */
    cbm_watcher_touch(w, "cont-repo");
    cbm_watcher_poll_once(w);
    int final_count = index_call_count;

    /* Touch and poll one more time to verify stability */
    cbm_watcher_touch(w, "cont-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, final_count); /* stable */

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_baseline_dirty_repo) {
    /* Baseline on a repo that already has uncommitted changes.
     * Port of TestGitSentinelDetectsEdit (dirty from the start). */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_bld_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    /* Make dirty BEFORE baseline */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "dirty from start\n");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "bld-repo", tmpdir);
    index_call_count = 0;

    /* Baseline — captures HEAD but doesn't check for dirty */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0); /* baseline never triggers */

    /* First real poll — should detect the pre-existing dirty state */
    cbm_watcher_touch(w, "bld-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_unwatch_prunes_state) {
    /* Watch, baseline, unwatch → project state removed.
     * Port of TestPollAllPrunesUnwatched + TestWatcherPrunesDeletedProjects. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_prune_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "prune-repo", tmpdir);
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);

    /* Unwatch — should remove project state immediately */
    cbm_watcher_unwatch(w, "prune-repo");
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);

    /* Make dirty + poll — nothing should happen */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "dirty\n");
    }
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0); /* no projects to poll */

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_watch_after_unwatch) {
    /* Re-watching after unwatch should start fresh (new baseline).
     * Tests lifecycle correctness. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_rewatch_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    /* Watch → baseline → unwatch */
    cbm_watcher_watch(w, "rewatch-repo", tmpdir);
    cbm_watcher_poll_once(w); /* baseline */
    cbm_watcher_unwatch(w, "rewatch-repo");
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);

    /* Make dirty while unwatched */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "dirty\n");
    }

    /* Re-watch — needs fresh baseline */
    cbm_watcher_watch(w, "rewatch-repo", tmpdir);
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);
    index_call_count = 0;

    /* Baseline again (first poll after re-watch) */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0); /* baseline never triggers */

    /* Second poll — detects dirty */
    cbm_watcher_touch(w, "rewatch-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  FSNOTIFY PORTS (adapted for git-based change detection)
 *
 *  The Go watcher has fsnotify/dir-mtime strategies alongside git.
 *  The C watcher is git-only. These tests verify the same SEMANTIC
 *  behaviors (file create, delete, subdir, cleanup) through git.
 * ══════════════════════════════════════════════════════════════════ */

TEST(watcher_detects_file_delete) {
    /* Port of TestFSNotifyDetectsFileDelete:
     * Delete a tracked file → git status shows change → reindex triggered. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_del_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}, {"todelete.go", "todelete\n"}};
    if (watcher_init_repo(tmpdir, files, 2) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "del-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Delete tracked file → dirty worktree */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/todelete.go", tmpdir);
        cbm_unlink(_p);
    }

    /* Touch + poll → should detect deletion */
    cbm_watcher_touch(w, "del-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_detects_subdir_file) {
    /* Port of TestFSNotifyWatchesNewSubdir:
     * Create new subdir + file in it → git detects untracked → reindex. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_sub_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"main.go", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "sub-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Create new subdir and file in it */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/pkg/lib.go", tmpdir);
        th_write_file(_p, "package pkg\n");
    }

    /* Touch + poll → should detect untracked file in subdir */
    cbm_watcher_touch(w, "sub-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_detects_dirty_submodule) {
    char parent_dir[256];
    char submodule_src[256];
    snprintf(parent_dir, sizeof(parent_dir), "/tmp/cbm_watcher_submodule_parent_XXXXXX");
    snprintf(submodule_src, sizeof(submodule_src), "/tmp/cbm_watcher_submodule_src_XXXXXX");
    if (!cbm_mkdtemp(parent_dir) || !cbm_mkdtemp(submodule_src))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t child_files[] = {{"lib.go", "package dep\n"}};
    watcher_repo_file_t parent_files[] = {{"main.go", "package main\n"}};
    if (watcher_init_repo(submodule_src, child_files, 1) != 0 ||
        watcher_init_repo(parent_dir, parent_files, 1) != 0) {
        th_rmtree(parent_dir);
        th_rmtree(submodule_src);
        SKIP("git not available");
    }

    const char *submodule_args[] = {"-c",      "protocol.file.allow=always", "submodule",
                                    "add",     submodule_src,                "deps/sub",
                                    NULL};
    const char *ignore_args[] = {"config", "submodule.deps/sub.ignore", "all", NULL};
    if (th_git_run(parent_dir, submodule_args) != 0 ||
        th_git_run(parent_dir, ignore_args) != 0 ||
        watcher_commit_all(parent_dir, "add submodule") != 0) {
        th_rmtree(parent_dir);
        th_rmtree(submodule_src);
        SKIP("git submodule unavailable");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "submodule-repo", parent_dir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    {
        char submodule_file[1024];
        snprintf(submodule_file, sizeof(submodule_file), "%s/deps/sub/lib.go", parent_dir);
        th_append_file(submodule_file, "var Dirty = true\n");
    }

    /* The parent repo ignores submodule dirtiness, so only the direct
     * submodule probe should see this change. */
    const char *status_args[] = {"status", "--porcelain", "--untracked-files=normal", NULL};
    char *parent_status = NULL;
    int status_exit = 0;
    ASSERT_EQ(th_git_capture(parent_dir, status_args, &parent_status, &status_exit), 0);
    ASSERT_EQ(status_exit, 0);
    ASSERT_TRUE(parent_status && parent_status[0] == '\0');
    free(parent_status);

    cbm_watcher_touch(w, "submodule-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(parent_dir);
    th_rmtree(submodule_src);
    PASS();
}

TEST(watcher_ignores_submodule_paths_outside_repo) {
    char parent_dir[256];
    char outside_dir[256];
    snprintf(parent_dir, sizeof(parent_dir), "/tmp/cbm_watcher_escape_parent_XXXXXX");
    snprintf(outside_dir, sizeof(outside_dir), "/tmp/cbm_watcher_escape_outside_XXXXXX");
    if (!cbm_mkdtemp(parent_dir) || !cbm_mkdtemp(outside_dir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t parent_files[] = {{"main.go", "package main\n"}};
    watcher_repo_file_t outside_files[] = {{"lib.go", "package outside\n"}};
    if (watcher_init_repo(parent_dir, parent_files, 1) != 0 ||
        watcher_init_repo(outside_dir, outside_files, 1) != 0) {
        th_rmtree(parent_dir);
        th_rmtree(outside_dir);
        SKIP("git not available");
    }

    const char *outside_name = strrchr(outside_dir, '/');
    if (!outside_name) {
        outside_name = outside_dir;
    } else {
        outside_name++;
    }
    char gitmodules[512];
    snprintf(gitmodules, sizeof(gitmodules),
             "[submodule \"escape\"]\n"
             "\tpath = ../%s\n"
             "\turl = ../%s\n",
             outside_name, outside_name);
    ASSERT_EQ(th_write_file(TH_PATH(parent_dir, ".gitmodules"), gitmodules), 0);
    ASSERT_EQ(watcher_commit_all(parent_dir, "add gitmodules"), 0);

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "escape-repo", parent_dir);
    index_call_count = 0;

    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    {
        char outside_file[1024];
        snprintf(outside_file, sizeof(outside_file), "%s/lib.go", outside_dir);
        th_append_file(outside_file, "var Dirty = true\n");
    }

    const char *status_args[] = {"status", "--porcelain", "--untracked-files=normal", NULL};
    char *parent_status = NULL;
    int parent_exit = 0;
    ASSERT_EQ(th_git_capture(parent_dir, status_args, &parent_status, &parent_exit), 0);
    ASSERT_EQ(parent_exit, 0);
    ASSERT_TRUE(parent_status && parent_status[0] == '\0');
    free(parent_status);

    char *outside_status = NULL;
    int outside_exit = 0;
    ASSERT_EQ(th_git_capture(outside_dir, status_args, &outside_status, &outside_exit), 0);
    ASSERT_EQ(outside_exit, 0);
    ASSERT_TRUE(outside_status && outside_status[0] != '\0');
    free(outside_status);

    cbm_watcher_touch(w, "escape-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(parent_dir);
    th_rmtree(outside_dir);
    PASS();
}

TEST(watcher_free_idempotent) {
    /* Port of TestFSNotifyCleanup:
     * Verify that free() properly cleans up, and free(NULL) is safe.
     * Tests resource cleanup correctness. */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);
    ASSERT_NOT_NULL(w);

    /* Watch some projects to create internal state */
    cbm_watcher_watch(w, "proj-a", "/tmp/a");
    cbm_watcher_watch(w, "proj-b", "/tmp/b");
    ASSERT_EQ(cbm_watcher_watch_count(w), 2);

    /* Free the watcher — should clean up all project state */
    cbm_watcher_free(w);

    /* Free(NULL) should be safe (already tested in null_safety,
     * but repeated here for parity with Go's close() test) */
    cbm_watcher_free(NULL);

    cbm_store_close(store);
    PASS();
}

TEST(watcher_full_flow_new_file) {
    /* Port of TestWatcherFSNotifyDetectsNewFile:
     * Full lifecycle: watch → baseline → add file → detect change.
     * This is a more thorough version of watcher_detects_new_file
     * that mirrors the Go test's structure exactly. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_ffnf_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"main.go", "package main\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "ffnf-repo", tmpdir);
    index_call_count = 0;

    /* Baseline — sets up git strategy, captures HEAD */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Poll again immediately — should be blocked by interval */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Create a new file */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/util.go", tmpdir);
        th_write_file(_p, "package main\n");
    }

    /* Touch to bypass interval, then poll — should detect */
    cbm_watcher_touch(w, "ffnf-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_fallback_still_detects) {
    /* Port of TestFSNotifyFallbackToDirMtime:
     * Even when the "primary" strategy has issues, the watcher
     * still detects changes. In C, we test that after removing .git
     * and re-creating it, changes are still detected on re-watch. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_fb_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"main.go", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "fb-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Remove .git and re-init (simulates strategy reset) */
    ASSERT_EQ(watcher_reinit_repo(tmpdir, "reinit"), 0);

    /* Re-watch with fresh state */
    cbm_watcher_unwatch(w, "fb-repo");
    cbm_watcher_watch(w, "fb-repo", tmpdir);
    cbm_watcher_poll_once(w); /* new baseline */

    /* Add new file */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/new.go", tmpdir);
        th_write_file(_p, "package main\n");
    }

    /* Detect change with fresh git strategy */
    cbm_watcher_touch(w, "fb-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_poll_only_watched_projects) {
    /* Port of TestPollAllOnlyWatched:
     * Two repos exist, only one is watched → only the watched one
     * gets polled and can trigger reindex. */
    char tmpdirA[256];
    snprintf(tmpdirA, sizeof(tmpdirA), "/tmp/cbm_watcher_owA_XXXXXX");
    char tmpdirB[256];
    snprintf(tmpdirB, sizeof(tmpdirB), "/tmp/cbm_watcher_owB_XXXXXX");
    if (!cbm_mkdtemp(tmpdirA) || !cbm_mkdtemp(tmpdirB))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files_a[] = {{"a.txt", "a\n"}};
    watcher_repo_file_t files_b[] = {{"b.txt", "b\n"}};
    if (watcher_init_repo(tmpdirA, files_a, 1) != 0) {
        th_rmtree(tmpdirA);
        th_rmtree(tmpdirB);
        SKIP("git not available");
    }
    if (watcher_init_repo(tmpdirB, files_b, 1) != 0) {
        th_rmtree(tmpdirA);
        th_rmtree(tmpdirB);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);

    /* Only watch A — B is NOT watched */
    cbm_watcher_watch(w, "projA-ow", tmpdirA);
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Make BOTH repos dirty */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/a.txt", tmpdirA);
        th_append_file(_p, "dirty\n");
    }
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/b.txt", tmpdirB);
        th_append_file(_p, "dirty\n");
    }

    /* Poll — only A should trigger (B is not watched) */
    cbm_watcher_touch(w, "projA-ow");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdirA);
    th_rmtree(tmpdirB);
    PASS();
}

TEST(watcher_touch_resets_immediate) {
    /* Port of TestTouchProjectUpdatesTimestamp:
     * Verify that touch() resets the adaptive backoff so the next
     * poll actually checks for changes immediately. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_tch_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "tch-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Make dirty */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "dirty\n");
    }

    /* Without touch: interval blocks poll */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0); /* blocked */

    /* With touch: poll proceeds */
    cbm_watcher_touch(w, "tch-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1); /* detected */

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_modify_tracked_file) {
    /* Port of TestWatcherTriggersOnChange / TestWatcherGitDetectsEdit:
     * Modify tracked file content (not just create/delete) → detected.
     * Similar to watcher_detects_dirty_worktree but modifies specific
     * tracked file content rather than appending. */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_mod_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"main.go", "package main\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "mod-repo", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* No-change poll */
    cbm_watcher_touch(w, "mod-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 0);

    /* Overwrite file with new content */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/main.go", tmpdir);
        th_write_file(_p, "package main\n\nfunc main() {}\n");
    }

    /* Touch + poll → should detect modification */
    cbm_watcher_touch(w, "mod-repo");
    cbm_watcher_poll_once(w);
    ASSERT_EQ(index_call_count, 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  RESOURCE MANAGEMENT & AUTO-INDEXING BEHAVIOR
 * ══════════════════════════════════════════════════════════════════ */

TEST(watcher_null_store_handling) {
    /* watcher_new with NULL store — verify behavior */
    cbm_watcher_t *w = cbm_watcher_new(NULL, NULL, NULL);
    /* Implementation may return NULL or a valid watcher.
     * Either is acceptable — key is no crash. */
    if (w) {
        ASSERT_EQ(cbm_watcher_watch_count(w), 0);
        cbm_watcher_free(w);
    }
    PASS();
}

TEST(watcher_free_null_safe) {
    /* Explicit test: free(NULL) must not crash */
    cbm_watcher_free(NULL);
    cbm_watcher_free(NULL);
    PASS();
}

TEST(watcher_empty_count) {
    /* Fresh watcher with no projects → count 0 */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);
    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_watch_multiple_verify_count) {
    /* Watch 5 projects, verify count at each step */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    for (int i = 0; i < 5; i++) {
        char name[32], path[64];
        snprintf(name, sizeof(name), "proj-%d", i);
        snprintf(path, sizeof(path), "/tmp/proj-%d", i);
        cbm_watcher_watch(w, name, path);
        ASSERT_EQ(cbm_watcher_watch_count(w), i + 1);
    }

    /* Unwatch all */
    for (int i = 0; i < 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "proj-%d", i);
        cbm_watcher_unwatch(w, name);
    }
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_watch_same_project_idempotent) {
    /* Watching the same project twice updates the path, count stays 1 */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    cbm_watcher_watch(w, "proj", "/tmp/path-a");
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);
    cbm_watcher_watch(w, "proj", "/tmp/path-b");
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);
    cbm_watcher_watch(w, "proj", "/tmp/path-c");
    ASSERT_EQ(cbm_watcher_watch_count(w), 1);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_unwatch_nonexistent_safe) {
    /* Unwatch a project that was never watched — no crash */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    cbm_watcher_unwatch(w, "never-existed");
    cbm_watcher_unwatch(w, "also-never-existed");
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_touch_nonexistent_project) {
    /* touch() on a project not in the watch list — no crash */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    cbm_watcher_touch(w, "nonexistent-project");
    ASSERT_EQ(cbm_watcher_watch_count(w), 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_poll_interval_zero_files) {
    /* 0 files → base interval (5000ms) */
    int ms = cbm_watcher_poll_interval_ms(0);
    ASSERT_EQ(ms, 5000);
    PASS();
}

TEST(watcher_poll_interval_small_files) {
    /* 100 files → should be close to base (5000ms) */
    int ms = cbm_watcher_poll_interval_ms(100);
    ASSERT_GTE(ms, 5000);
    /* 100 files / 500 = 0 extra seconds of scaling → 5000ms */
    ASSERT_EQ(ms, 5000);
    PASS();
}

TEST(watcher_poll_interval_medium_files) {
    /* 10000 files → 5000 + 20*1000 = 25000ms */
    int ms = cbm_watcher_poll_interval_ms(10000);
    ASSERT_EQ(ms, 25000);
    PASS();
}

TEST(watcher_poll_interval_capped) {
    /* 100000 files → capped at 60000ms */
    int ms = cbm_watcher_poll_interval_ms(100000);
    ASSERT_EQ(ms, 60000);
    /* Even larger → still capped */
    ms = cbm_watcher_poll_interval_ms(500000);
    ASSERT_EQ(ms, 60000);
    PASS();
}

TEST(watcher_poll_interval_negative) {
    /* Negative file count → should handle gracefully (no crash) */
    int ms = cbm_watcher_poll_interval_ms(-1);
    /* Result should be at least the base interval or 0 — just no crash */
    ASSERT_GTE(ms, 0);
    PASS();
}

TEST(watcher_poll_empty_returns_zero) {
    /* poll_once with empty watch list → 0 reindexed */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    index_call_count = 0;

    int reindexed = cbm_watcher_poll_once(w);
    ASSERT_EQ(reindexed, 0);
    ASSERT_EQ(index_call_count, 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_poll_non_git_dir) {
    /* poll_once with a non-git directory → 0 changes detected */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_ng2_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    /* Create a regular file so directory is not empty */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_write_file(_p, "hello\n");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, index_callback, NULL);
    cbm_watcher_watch(w, "nongit2", tmpdir);
    index_call_count = 0;

    /* Baseline */
    cbm_watcher_poll_once(w);

    /* Modify file */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "world\n");
    }

    /* Poll — non-git directory, should not trigger reindex */
    cbm_watcher_touch(w, "nongit2");
    int reindexed = cbm_watcher_poll_once(w);
    ASSERT_EQ(reindexed, 0);
    ASSERT_EQ(index_call_count, 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_stop_prevents_run) {
    /* Setting stop before run → run returns immediately */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    cbm_watcher_stop(w);
    int rc = cbm_watcher_run(w, 60000);
    ASSERT_EQ(rc, 0);

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

TEST(watcher_watch_unwatch_rapid_cycle) {
    /* Rapid watch/unwatch cycles — stress lifecycle management */
    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, NULL, NULL);

    for (int i = 0; i < 20; i++) {
        cbm_watcher_watch(w, "rapid", "/tmp/rapid");
        ASSERT_EQ(cbm_watcher_watch_count(w), 1);
        cbm_watcher_unwatch(w, "rapid");
        ASSERT_EQ(cbm_watcher_watch_count(w), 0);
    }

    cbm_watcher_free(w);
    cbm_store_close(store);
    PASS();
}

/* Callback and state for watcher_callback_data_passed test */
static int g_cbdata_value = 42;
static int *g_cbdata_received = NULL;

static int capture_data_callback(const char *name, const char *path, void *ud) {
    (void)name;
    (void)path;
    g_cbdata_received = (int *)ud;
    return 0;
}

TEST(watcher_callback_data_passed) {
    /* Verify that user_data pointer is accessible in the callback */
    g_cbdata_received = NULL;

    /* Create a temp git repo */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/cbm_watcher_cbdata_XXXXXX");
    if (!cbm_mkdtemp(tmpdir))
        SKIP("cbm_mkdtemp failed");

    watcher_repo_file_t files[] = {{"file.txt", "hello\n"}};
    if (watcher_init_repo(tmpdir, files, 1) != 0) {
        th_rmtree(tmpdir);
        SKIP("git not available");
    }

    cbm_store_t *store = cbm_store_open_memory();
    cbm_watcher_t *w = cbm_watcher_new(store, capture_data_callback, &g_cbdata_value);
    cbm_watcher_watch(w, "cbdata-repo", tmpdir);

    /* Baseline */
    cbm_watcher_poll_once(w);

    /* Make dirty to trigger callback */
    {
        char _p[1024];
        snprintf(_p, sizeof(_p), "%s/file.txt", tmpdir);
        th_append_file(_p, "dirty\n");
    }

    cbm_watcher_touch(w, "cbdata-repo");
    cbm_watcher_poll_once(w);

    /* If callback was invoked, g_cbdata_received should point to g_cbdata_value */
    if (g_cbdata_received) {
        ASSERT_EQ(*g_cbdata_received, 42);
    }

    cbm_watcher_free(w);
    cbm_store_close(store);
    th_rmtree(tmpdir);
    PASS();
}

TEST(watcher_null_poll_once) {
    /* poll_once(NULL) → 0 */
    int reindexed = cbm_watcher_poll_once(NULL);
    ASSERT_EQ(reindexed, 0);
    PASS();
}

TEST(watcher_null_watch_count) {
    /* watch_count(NULL) → 0 */
    int count = cbm_watcher_watch_count(NULL);
    ASSERT_EQ(count, 0);
    PASS();
}

/* ══════════════════════════════════════════════════════════════════
 *  SUITE
 * ══════════════════════════════════════════════════════════════════ */

SUITE(watcher) {
    /* Adaptive interval */
    RUN_TEST(poll_interval_base);
    RUN_TEST(poll_interval_scaling);
    RUN_TEST(poll_interval_cap);
    RUN_TEST(poll_interval_small);

    /* Lifecycle */
    RUN_TEST(watcher_create_free);
    RUN_TEST(watcher_watch_unwatch);
    RUN_TEST(watcher_unwatch_nonexistent);
    RUN_TEST(watcher_watch_replace);
    RUN_TEST(watcher_null_safety);

    /* Polling */
    RUN_TEST(watcher_poll_no_projects);
    RUN_TEST(watcher_poll_nonexistent_path);
    RUN_TEST(watcher_poll_this_repo);
    RUN_TEST(watcher_stop_flag);

    /* Git change detection */
    RUN_TEST(watcher_detects_git_commit);
    RUN_TEST(watcher_detects_dirty_worktree);
    RUN_TEST(watcher_detects_dirty_worktree_special_path);
    RUN_TEST(git_helpers_capture_output_and_exit_code);
    RUN_TEST(watcher_detects_new_file);
    RUN_TEST(watcher_no_change_no_reindex);
    RUN_TEST(watcher_multiple_projects);

    /* Non-git project */
    RUN_TEST(watcher_non_git_skips);

    /* Adaptive interval behavior */
    RUN_TEST(watcher_interval_blocks_repoll);
    RUN_TEST(watcher_large_repo_file_count_sets_interval);
    RUN_TEST(watcher_poll_interval_full_table);

    /* Git removal + continued dirty + baseline dirty */
    RUN_TEST(watcher_git_removed_no_crash);
    RUN_TEST(watcher_continued_dirty);
    RUN_TEST(watcher_baseline_dirty_repo);
    RUN_TEST(watcher_unwatch_prunes_state);
    RUN_TEST(watcher_watch_after_unwatch);

    /* FSNotify ports (adapted for git-based detection) */
    RUN_TEST(watcher_detects_file_delete);
    RUN_TEST(watcher_detects_subdir_file);
    RUN_TEST(watcher_detects_dirty_submodule);
    RUN_TEST(watcher_ignores_submodule_paths_outside_repo);
    RUN_TEST(watcher_free_idempotent);
    RUN_TEST(watcher_full_flow_new_file);
    RUN_TEST(watcher_fallback_still_detects);
    RUN_TEST(watcher_poll_only_watched_projects);
    RUN_TEST(watcher_touch_resets_immediate);
    RUN_TEST(watcher_modify_tracked_file);

    /* Resource management & auto-indexing behavior */
    RUN_TEST(watcher_null_store_handling);
    RUN_TEST(watcher_free_null_safe);
    RUN_TEST(watcher_empty_count);
    RUN_TEST(watcher_watch_multiple_verify_count);
    RUN_TEST(watcher_watch_same_project_idempotent);
    RUN_TEST(watcher_unwatch_nonexistent_safe);
    RUN_TEST(watcher_touch_nonexistent_project);
    /* Poll interval edge cases */
    RUN_TEST(watcher_poll_interval_zero_files);
    RUN_TEST(watcher_poll_interval_small_files);
    RUN_TEST(watcher_poll_interval_medium_files);
    RUN_TEST(watcher_poll_interval_capped);
    RUN_TEST(watcher_poll_interval_negative);
    /* Poll edge cases */
    RUN_TEST(watcher_poll_empty_returns_zero);
    RUN_TEST(watcher_poll_non_git_dir);
    RUN_TEST(watcher_stop_prevents_run);
    RUN_TEST(watcher_watch_unwatch_rapid_cycle);
    RUN_TEST(watcher_callback_data_passed);
    RUN_TEST(watcher_null_poll_once);
    RUN_TEST(watcher_null_watch_count);
}
