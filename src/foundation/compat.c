/*
 * compat.c — Implementations for Windows-only shims.
 *
 * On POSIX, these functions are provided by the standard library via
 * macros in compat.h. On Windows, we implement them here.
 */
#include "foundation/compat.h"
#include "foundation/constants.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

/* ── strndup (Windows lacks it) ───────────────────────────────── */

#ifdef _WIN32
char *cbm_strndup(const char *s, size_t n) {
    if (!s) {
        return NULL;
    }
    size_t len = 0;
    while (len < n && s[len]) {
        len++;
    }
    char *d = (char *)malloc(len + SKIP_ONE);
    if (d) {
        memcpy(d, s, len);
        d[len] = '\0';
    }
    return d;
}
#endif

/* ── strcasestr (Windows lacks it) ────────────────────────────── */

#ifdef _WIN32
char *cbm_strcasestr(const char *haystack, const char *needle) {
    if (!needle[0]) {
        return (char *)haystack;
    }
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (_strnicmp(haystack, needle, nlen) == 0) {
            return (char *)haystack;
        }
    }
    return NULL;
}
#endif

/* ── Temp directory helpers ───────────────────────────────────── */

const char *cbm_tmpdir(void) {
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp || !tmp[0]) {
        tmp = getenv("TMP");
    }
    return (tmp && tmp[0]) ? tmp : ".";
#else
    return "/tmp";
#endif
}

int cbm_get_tmpdir(char *buf, size_t size) {
    const char *tmp = cbm_tmpdir();
    if (!buf || size == 0 || !tmp) {
        return CBM_NOT_FOUND;
    }
    int written = snprintf(buf, size, "%s", tmp);
    if (written < 0 || (size_t)written >= size) {
        if (size > 0) {
            buf[size - 1] = '\0';
        }
        return CBM_NOT_FOUND;
    }
    return 0;
}

int cbm_temp_path(char *buf, size_t size, const char *leaf) {
    char tmpdir[CBM_SZ_512];
    if (!buf || size == 0) {
        return CBM_NOT_FOUND;
    }
    if (!leaf || !leaf[0]) {
        return CBM_NOT_FOUND;
    }
    if (cbm_get_tmpdir(tmpdir, sizeof(tmpdir)) != 0) {
        return CBM_NOT_FOUND;
    }
#ifdef _WIN32
    int written = snprintf(buf, size, "%s\\%s", tmpdir, leaf);
#else
    int written = snprintf(buf, size, "%s/%s", tmpdir, leaf);
#endif
    if (written < 0 || (size_t)written >= size) {
        buf[size - 1] = '\0';
        return CBM_NOT_FOUND;
    }
    return 0;
}

int cbm_temp_template(char *buf, size_t size, const char *prefix) {
    char leaf[CBM_SZ_256];
    if (!prefix || !prefix[0]) {
        return CBM_NOT_FOUND;
    }
    int leaf_written = snprintf(leaf, sizeof(leaf), "%sXXXXXX", prefix);
    if (leaf_written < 0 || (size_t)leaf_written >= sizeof(leaf)) {
        return CBM_NOT_FOUND;
    }
    return cbm_temp_path(buf, size, leaf);
}

#ifdef _WIN32
static int cbm_translate_tmp_template(char *dst, size_t dst_size, const char *tmpl) {
    if (!dst || dst_size == 0 || !tmpl) {
        return CBM_NOT_FOUND;
    }
    if (strncmp(tmpl, "/tmp/", 5) == 0) {
        return cbm_temp_path(dst, dst_size, tmpl + 5);
    }
    int written = snprintf(dst, dst_size, "%s", tmpl);
    if (written < 0 || (size_t)written >= dst_size) {
        dst[dst_size - 1] = '\0';
        return CBM_NOT_FOUND;
    }
    return 0;
}
#endif

/* ── mkdtemp (Windows lacks it) ───────────────────────────────── */

#ifdef _WIN32
#include <direct.h>
char *cbm_mkdtemp(char *tmpl) {
    char path[CBM_SZ_512];
    if (cbm_translate_tmp_template(path, sizeof(path), tmpl) != 0) {
        return NULL;
    }
    if (!_mktemp(path)) {
        return NULL;
    }
    if (_mkdir(path) != 0) {
        return NULL;
    }
    strcpy(tmpl, path);
    return tmpl;
}
#endif

/* ── mkstemp (Windows lacks it) ───────────────────────────────── */

#ifdef _WIN32
int cbm_mkstemp(char *tmpl) {
    char path[CBM_SZ_512];
    if (cbm_translate_tmp_template(path, sizeof(path), tmpl) != 0) {
        return CBM_NOT_FOUND;
    }
    if (!_mktemp(path)) {
        return CBM_NOT_FOUND;
    }
    int fd = _open(path, _O_CREAT | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd >= 0) {
        strcpy(tmpl, path);
    }
    return fd;
}
#endif

/* ── clock_gettime (Windows lacks it) ─────────────────────────── */

#ifdef _WIN32
int cbm_clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    tp->tv_sec = (time_t)(count.QuadPart / freq.QuadPart);
    tp->tv_nsec = (long)((count.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
    return 0;
}
#endif

/* ── getline (Windows lacks it) ───────────────────────────────── */

#ifdef _WIN32
ssize_t cbm_getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) {
        return CBM_NOT_FOUND;
    }
    if (!*lineptr || *n == 0) {
        *n = CBM_SZ_128;
        *lineptr = (char *)malloc(*n);
        if (!*lineptr) {
            return CBM_NOT_FOUND;
        }
    }
    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t new_n = *n * PAIR_LEN;
            char *tmp = (char *)realloc(*lineptr, new_n);
            if (!tmp) {
                return CBM_NOT_FOUND;
            }
            *lineptr = tmp;
            *n = new_n;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    if (pos == 0 && c == EOF) {
        return CBM_NOT_FOUND;
    }
    (*lineptr)[pos] = '\0';
    return (ssize_t)pos;
}
#endif
