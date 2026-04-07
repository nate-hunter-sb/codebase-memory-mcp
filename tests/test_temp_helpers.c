/*
 * test_temp_helpers.c - Focused regression coverage for temp-path helpers.
 */
#include "test_framework.h"
#include "test_helpers.h"

#include "../src/foundation/compat.h"
#include "../src/foundation/compat_fs.h"
#include "../src/foundation/constants.h"

#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define cbm_close_fd _close
#else
#include <unistd.h>
#define cbm_close_fd close
#endif

static int has_suffix(const char *s, const char *suffix) {
    size_t s_len;
    size_t suffix_len;

    if (!s || !suffix) {
        return 0;
    }
    s_len = strlen(s);
    suffix_len = strlen(suffix);
    if (suffix_len > s_len) {
        return 0;
    }
    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

TEST(temp_path_uses_process_tmpdir) {
    char dir[CBM_PATH_MAX];
    char path[CBM_PATH_MAX];

    ASSERT_EQ(cbm_get_tmpdir(dir, sizeof(dir)), 0);
    ASSERT_EQ(cbm_temp_path(path, sizeof(path), "cbm-helper.txt"), 0);
    ASSERT_TRUE(strncmp(path, dir, strlen(dir)) == 0);
    ASSERT_TRUE(has_suffix(path, "cbm-helper.txt"));
    PASS();
}

TEST(temp_template_appends_suffix) {
    char tmpl[CBM_PATH_MAX];

    ASSERT_EQ(cbm_temp_template(tmpl, sizeof(tmpl), "cbm-template-"), 0);
    ASSERT_TRUE(has_suffix(tmpl, "XXXXXX"));
    ASSERT_TRUE(strstr(tmpl, "cbm-template-") != NULL);
    PASS();
}

TEST(mkstemp_creates_file_from_helper_template) {
    char tmpl[CBM_PATH_MAX];
    struct stat st;
    int fd;

    ASSERT_EQ(cbm_temp_template(tmpl, sizeof(tmpl), "cbm-file-"), 0);
    fd = cbm_mkstemp(tmpl);
    ASSERT_GTE(fd, 0);
    ASSERT_EQ(stat(tmpl, &st), 0);
    ASSERT_FALSE(S_ISDIR(st.st_mode));
    ASSERT_EQ(cbm_close_fd(fd), 0);
    ASSERT_EQ(cbm_unlink(tmpl), 0);
    PASS();
}

TEST(mkdtemp_creates_dir_from_helper_template) {
    char tmpl[CBM_PATH_MAX];
    struct stat st;

    ASSERT_EQ(cbm_temp_template(tmpl, sizeof(tmpl), "cbm-dir-"), 0);
    ASSERT_NOT_NULL(cbm_mkdtemp(tmpl));
    ASSERT_EQ(stat(tmpl, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_EQ(th_rmtree(tmpl), 0);
    PASS();
}

TEST(legacy_tmp_template_still_works) {
    char tmpl[CBM_PATH_MAX] = "/tmp/cbm-legacy-XXXXXX";
    int fd = cbm_mkstemp(tmpl);

    ASSERT_GTE(fd, 0);
#ifdef _WIN32
    ASSERT_TRUE(strncmp(tmpl, "/tmp/", CBM_SZ_5) != 0);
#else
    ASSERT_TRUE(strncmp(tmpl, "/tmp/", CBM_SZ_5) == 0);
#endif
    ASSERT_EQ(cbm_close_fd(fd), 0);
    ASSERT_EQ(cbm_unlink(tmpl), 0);
    PASS();
}

TEST(mkdir_p_rejects_stale_last_error_success) {
#ifdef _WIN32
    char tmpdir[CBM_PATH_MAX];
    char file_path[CBM_PATH_MAX];
    char child_path[CBM_PATH_MAX];

    ASSERT_EQ(cbm_temp_template(tmpdir, sizeof(tmpdir), "cbm-mkdir-"), 0);
    ASSERT_NOT_NULL(cbm_mkdtemp(tmpdir));
    snprintf(file_path, sizeof(file_path), "%s/existing.txt", tmpdir);
    ASSERT_EQ(th_write_file(file_path, "x"), 0);
    snprintf(child_path, sizeof(child_path), "%s/existing.txt/child", tmpdir);

    SetLastError(ERROR_ALREADY_EXISTS);
    ASSERT_FALSE(cbm_mkdir_p(child_path, 0755));
    ASSERT_EQ(th_rmtree(tmpdir), 0);
#endif
    PASS();
}

SUITE(temp_helpers) {
    RUN_TEST(temp_path_uses_process_tmpdir);
    RUN_TEST(temp_template_appends_suffix);
    RUN_TEST(mkstemp_creates_file_from_helper_template);
    RUN_TEST(mkdtemp_creates_dir_from_helper_template);
    RUN_TEST(legacy_tmp_template_still_works);
    RUN_TEST(mkdir_p_rejects_stale_last_error_success);
}
