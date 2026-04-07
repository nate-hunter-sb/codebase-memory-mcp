/*
 * compat_fs.c — Portable file system operations.
 *
 * POSIX: direct wrappers around opendir/readdir/closedir, popen/pclose, mkdir, unlink.
 * Windows: FindFirstFile/FindNextFile, _popen/_pclose, _mkdir, _unlink.
 */
#include "foundation/constants.h"
#include "foundation/compat_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

/* ── Windows implementation ───────────────────────────────────── */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <errno.h>
#include <windows.h>
#include <direct.h> /* _mkdir */
#include <io.h>     /* _unlink */
#include <process.h>

struct cbm_dir {
    HANDLE find_handle;
    WIN32_FIND_DATAA find_data;
    cbm_dirent_t entry;
    bool first;
    bool done;
};

cbm_dir_t *cbm_opendir(const char *path) {
    if (!path) {
        return NULL;
    }
    /* Build search pattern: "path\*" */
    size_t len = strlen(path);
    char *pattern = (char *)malloc(len + 3);
    if (!pattern) {
        return NULL;
    }
    memcpy(pattern, path, len);
    if (len > 0 && path[len - SKIP_ONE] != '\\' && path[len - SKIP_ONE] != '/') {
        pattern[len++] = '\\';
    }
    pattern[len++] = '*';
    pattern[len] = '\0';

    cbm_dir_t *d = (cbm_dir_t *)calloc(CBM_ALLOC_ONE, sizeof(cbm_dir_t));
    if (!d) {
        free(pattern);
        return NULL;
    }

    d->find_handle = FindFirstFileA(pattern, &d->find_data);
    free(pattern);
    if (d->find_handle == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    d->first = true;
    d->done = false;
    return d;
}

cbm_dirent_t *cbm_readdir(cbm_dir_t *d) {
    if (!d || d->done) {
        return NULL;
    }
    if (!d->first) {
        if (!FindNextFileA(d->find_handle, &d->find_data)) {
            d->done = true;
            return NULL;
        }
    }
    d->first = false;

    /* Skip "." and ".." */
    while (d->find_data.cFileName[0] == '.' &&
           (d->find_data.cFileName[1] == '\0' ||
            (d->find_data.cFileName[1] == '.' && d->find_data.cFileName[2] == '\0'))) {
        if (!FindNextFileA(d->find_handle, &d->find_data)) {
            d->done = true;
            return NULL;
        }
    }

    size_t nlen = strlen(d->find_data.cFileName);
    if (nlen >= CBM_DIRENT_NAME_MAX) {
        nlen = CBM_DIRENT_NAME_MAX - SKIP_ONE;
    }
    memcpy(d->entry.name, d->find_data.cFileName, nlen);
    d->entry.name[nlen] = '\0';
    d->entry.is_dir = (d->find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    d->entry.d_type = 0; /* Not meaningful on Windows */
    return &d->entry;
}

void cbm_closedir(cbm_dir_t *d) {
    if (d) {
        if (d->find_handle != INVALID_HANDLE_VALUE) {
            FindClose(d->find_handle);
        }
        free(d);
    }
}

FILE *cbm_popen(const char *cmd, const char *mode) {
    return _popen(cmd, mode);
}

int cbm_pclose(FILE *f) {
    return _pclose(f);
}

bool cbm_mkdir_p(const char *path, int mode) {
    (void)mode; /* Windows ignores POSIX permissions */
    /* Simple recursive mkdir: try creating, if fail walk parents */
    if (_mkdir(path) == 0) {
        return true;
    }
    /* Walk path and create each component */
    char *tmp = _strdup(path);
    if (!tmp) {
        return false;
    }
    for (char *p = tmp + SKIP_ONE; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            _mkdir(tmp); /* ignore errors for intermediate dirs */
            *p = '\\';
        }
    }
    int mkdir_rc = _mkdir(tmp);
    bool ok = mkdir_rc == 0 || errno == EEXIST;
    free(tmp);
    return ok;
}

int cbm_unlink(const char *path) {
    return _unlink(path);
}

int cbm_rmdir(const char *path) {
    return _rmdir(path);
}

int cbm_exec_no_shell(const char *const *argv) {
    if (!argv || !argv[0]) {
        return CBM_NOT_FOUND;
    }
    return (int)_spawnvp(_P_WAIT, argv[0], argv);
}

static int append_bytes(char **buf, size_t *len, size_t *cap, const char *data, size_t data_len) {
    if (!buf || !len || !cap) {
        return CBM_NOT_FOUND;
    }
    if (data_len == 0) {
        return 0;
    }
    size_t needed = *len + data_len + SKIP_ONE;
    if (needed > *cap) {
        size_t new_cap = *cap ? *cap : CBM_SZ_256;
        while (new_cap < needed) {
            new_cap *= PAIR_LEN;
        }
        char *tmp = realloc(*buf, new_cap);
        if (!tmp) {
            return CBM_NOT_FOUND;
        }
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, data, data_len);
    *len += data_len;
    (*buf)[*len] = '\0';
    return 0;
}

static bool windows_arg_needs_quotes(const char *arg) {
    if (!arg || !arg[0]) {
        return true;
    }
    for (const char *p = arg; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '"') {
            return true;
        }
    }
    return false;
}

static size_t windows_quoted_arg_len(const char *arg) {
    if (!arg) {
        return 0;
    }
    if (!windows_arg_needs_quotes(arg)) {
        return strlen(arg);
    }

    size_t len = PAIR_LEN; /* opening + closing quote */
    int backslashes = 0;
    for (const char *p = arg; *p; p++) {
        if (*p == '\\') {
            backslashes++;
            continue;
        }
        if (*p == '"') {
            len += (size_t)(backslashes * PAIR_LEN) + PAIR_LEN;
            backslashes = 0;
        } else {
            len += (size_t)backslashes + SKIP_ONE;
            backslashes = 0;
        }
    }
    len += (size_t)(backslashes * PAIR_LEN);
    return len;
}

static char *windows_write_quoted_arg(char *dst, const char *arg) {
    if (!windows_arg_needs_quotes(arg)) {
        size_t len = strlen(arg);
        memcpy(dst, arg, len);
        return dst + len;
    }

    *dst++ = '"';
    int backslashes = 0;
    for (const char *p = arg; *p; p++) {
        if (*p == '\\') {
            backslashes++;
            continue;
        }
        if (*p == '"') {
            for (int i = 0; i < backslashes * PAIR_LEN + SKIP_ONE; i++) {
                *dst++ = '\\';
            }
            *dst++ = '"';
            backslashes = 0;
            continue;
        }
        while (backslashes-- > 0) {
            *dst++ = '\\';
        }
        backslashes = 0;
        *dst++ = *p;
    }
    while (backslashes-- > 0) {
        *dst++ = '\\';
        *dst++ = '\\';
    }
    *dst++ = '"';
    return dst;
}

static char *build_windows_cmdline(const char *const *argv) {
    if (!argv || !argv[0]) {
        return NULL;
    }

    size_t total = SKIP_ONE;
    int argc = 0;
    while (argv[argc]) {
        total += windows_quoted_arg_len(argv[argc]) + SKIP_ONE;
        argc++;
    }

    char *cmdline = malloc(total);
    if (!cmdline) {
        return NULL;
    }

    char *dst = cmdline;
    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            *dst++ = ' ';
        }
        dst = windows_write_quoted_arg(dst, argv[i]);
    }
    *dst = '\0';
    return cmdline;
}

static int read_windows_handle(HANDLE handle, char **stdout_out) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    char chunk[CBM_SZ_4K];
    DWORD got = 0;

    for (;;) {
        if (!ReadFile(handle, chunk, sizeof(chunk), &got, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE) {
                break;
            }
            free(buf);
            return CBM_NOT_FOUND;
        }
        if (got == 0) {
            break;
        }
        if (stdout_out && append_bytes(&buf, &len, &cap, chunk, (size_t)got) != 0) {
            free(buf);
            return CBM_NOT_FOUND;
        }
    }

    if (stdout_out) {
        if (!buf) {
            buf = malloc(SKIP_ONE);
            if (!buf) {
                return CBM_NOT_FOUND;
            }
            buf[0] = '\0';
        }
        *stdout_out = buf;
    }
    return 0;
}

int cbm_exec_capture(const char *const *argv, char **stdout_out, int *exit_code) {
    if (stdout_out) {
        *stdout_out = NULL;
    }
    if (exit_code) {
        *exit_code = CBM_NOT_FOUND;
    }
    if (!argv || !argv[0]) {
        return CBM_NOT_FOUND;
    }

    char *cmdline = build_windows_cmdline(argv);
    if (!cmdline) {
        return CBM_NOT_FOUND;
    }

    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    HANDLE null_input = INVALID_HANDLE_VALUE;
    HANDLE null_output = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi = {0};
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        free(cmdline);
        return CBM_NOT_FOUND;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        free(cmdline);
        return CBM_NOT_FOUND;
    }

    null_input = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    null_output = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (null_input == INVALID_HANDLE_VALUE || null_output == INVALID_HANDLE_VALUE) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        if (null_input != INVALID_HANDLE_VALUE) {
            CloseHandle(null_input);
        }
        if (null_output != INVALID_HANDLE_VALUE) {
            CloseHandle(null_output);
        }
        free(cmdline);
        return CBM_NOT_FOUND;
    }

    si.hStdInput = null_input;
    si.hStdOutput = write_pipe;
    si.hStdError = null_output;

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        CloseHandle(null_input);
        CloseHandle(null_output);
        free(cmdline);
        return CBM_NOT_FOUND;
    }

    CloseHandle(write_pipe);
    write_pipe = NULL;
    CloseHandle(null_input);
    CloseHandle(null_output);
    null_input = INVALID_HANDLE_VALUE;
    null_output = INVALID_HANDLE_VALUE;
    free(cmdline);

    char *captured = NULL;
    int read_rc = read_windows_handle(read_pipe, stdout_out ? &captured : NULL);
    CloseHandle(read_pipe);
    read_pipe = NULL;

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD child_exit = 0;
    if (!GetExitCodeProcess(pi.hProcess, &child_exit)) {
        child_exit = (DWORD)CBM_NOT_FOUND;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (read_rc != 0) {
        free(captured);
        return CBM_NOT_FOUND;
    }

    if (stdout_out) {
        *stdout_out = captured;
    }
    if (exit_code) {
        *exit_code = (int)child_exit;
    }
    return 0;
}

#else /* POSIX */

/* ── POSIX implementation ─────────────────────────────────────── */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct cbm_dir {
    DIR *dir;
    cbm_dirent_t entry;
};

cbm_dir_t *cbm_opendir(const char *path) {
    if (!path) {
        return NULL;
    }
    DIR *dir = opendir(path);
    if (!dir) {
        return NULL;
    }
    cbm_dir_t *d = (cbm_dir_t *)calloc(CBM_ALLOC_ONE, sizeof(cbm_dir_t));
    if (!d) {
        closedir(dir);
        return NULL;
    }
    d->dir = dir;
    return d;
}

cbm_dirent_t *cbm_readdir(cbm_dir_t *d) {
    if (!d || !d->dir) {
        return NULL;
    }
    struct dirent *de;
    while ((de = readdir(d->dir)) != NULL) {
        /* Skip "." and ".." */
        if (de->d_name[0] == '.' &&
            (de->d_name[SKIP_ONE] == '\0' ||
             (de->d_name[SKIP_ONE] == '.' && de->d_name[PAIR_LEN] == '\0'))) {
            continue;
        }
        size_t nlen = strlen(de->d_name);
        if (nlen >= CBM_DIRENT_NAME_MAX) {
            nlen = CBM_DIRENT_NAME_MAX - SKIP_ONE;
        }
        memcpy(d->entry.name, de->d_name, nlen);
        d->entry.name[nlen] = '\0';
        d->entry.is_dir = (de->d_type == DT_DIR);
        d->entry.d_type = de->d_type;
        return &d->entry;
    }
    return NULL;
}

void cbm_closedir(cbm_dir_t *d) {
    if (d) {
        if (d->dir) {
            closedir(d->dir);
        }
        free(d);
    }
}

FILE *cbm_popen(const char *cmd, const char *mode) {
    return popen(cmd, mode);
}

int cbm_pclose(FILE *f) {
    return pclose(f);
}

bool cbm_mkdir_p(const char *path, int mode) {
    /* Try direct mkdir first */
    if (mkdir(path, (mode_t)mode) == 0) {
        return true;
    }
    /* Walk path and create each component */
    char *tmp = strdup(path);
    if (!tmp) {
        return false;
    }
    for (char *p = tmp + SKIP_ONE; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, (mode_t)mode); /* ignore intermediate errors */
            *p = '/';
        }
    }
    bool ok = (mkdir(tmp, (mode_t)mode) == 0 || errno == EEXIST) != 0;
    free(tmp);
    return ok;
}

int cbm_unlink(const char *path) {
    return unlink(path);
}

int cbm_rmdir(const char *path) {
    return rmdir(path);
}

int cbm_exec_no_shell(const char *const *argv) {
    if (!argv || !argv[0]) {
        return CBM_NOT_FOUND;
    }
    pid_t pid = fork();
    if (pid < 0) {
        return CBM_NOT_FOUND;
    }
    if (pid == 0) {
        /* Child: exec directly — no shell interpretation */
        /* 127 = standard "command not found" exit code (POSIX convention) */
        enum { EXEC_NOT_FOUND = 127 };
        execvp(argv[0], (char *const *)argv);
        _exit(EXEC_NOT_FOUND);
    }
    /* Parent: wait for child */
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return CBM_NOT_FOUND;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return CBM_NOT_FOUND; /* killed by signal */
}

static int append_bytes(char **buf, size_t *len, size_t *cap, const char *data, size_t data_len) {
    if (!buf || !len || !cap) {
        return CBM_NOT_FOUND;
    }
    if (data_len == 0) {
        return 0;
    }
    size_t needed = *len + data_len + SKIP_ONE;
    if (needed > *cap) {
        size_t new_cap = *cap ? *cap : CBM_SZ_256;
        while (new_cap < needed) {
            new_cap *= PAIR_LEN;
        }
        char *tmp = realloc(*buf, new_cap);
        if (!tmp) {
            return CBM_NOT_FOUND;
        }
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, data, data_len);
    *len += data_len;
    (*buf)[*len] = '\0';
    return 0;
}

static int read_posix_fd(int fd, char **stdout_out) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    char chunk[CBM_SZ_4K];

    for (;;) {
        ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            return CBM_NOT_FOUND;
        }
        if (got == 0) {
            break;
        }
        if (stdout_out && append_bytes(&buf, &len, &cap, chunk, (size_t)got) != 0) {
            free(buf);
            return CBM_NOT_FOUND;
        }
    }

    if (stdout_out) {
        if (!buf) {
            buf = malloc(SKIP_ONE);
            if (!buf) {
                return CBM_NOT_FOUND;
            }
            buf[0] = '\0';
        }
        *stdout_out = buf;
    }
    return 0;
}

int cbm_exec_capture(const char *const *argv, char **stdout_out, int *exit_code) {
    if (stdout_out) {
        *stdout_out = NULL;
    }
    if (exit_code) {
        *exit_code = CBM_NOT_FOUND;
    }
    if (!argv || !argv[0]) {
        return CBM_NOT_FOUND;
    }

    int pipefd[PAIR_LEN] = {CBM_NOT_FOUND, CBM_NOT_FOUND};
    if (cbm_pipe(pipefd) != 0) {
        return CBM_NOT_FOUND;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return CBM_NOT_FOUND;
    }

    if (pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        close(pipefd[1]);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    close(pipefd[1]);
    int read_rc = read_posix_fd(pipefd[0], stdout_out);
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            if (stdout_out && *stdout_out) {
                free(*stdout_out);
                *stdout_out = NULL;
            }
            return CBM_NOT_FOUND;
        }
    }

    if (read_rc != 0) {
        if (stdout_out && *stdout_out) {
            free(*stdout_out);
            *stdout_out = NULL;
        }
        return CBM_NOT_FOUND;
    }

    if (exit_code) {
        *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : CBM_NOT_FOUND;
    }
    return 0;
}

#endif /* _WIN32 */
