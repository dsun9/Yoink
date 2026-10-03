#include "pointy/platform.h"
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

static bool file_error(PPError *error, const char *message) {
    if (error) {
        error->code = PP_ERROR_PLATFORM;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}
static wchar_t *wide_path(const char *path, PPError *error) {
    int n = path ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0) : 0;
    if (!n) {
        file_error(error, "Invalid UTF-8 file path");
        return NULL;
    }
    wchar_t *wide = malloc((size_t)n * sizeof(*wide));
    if (!wide) {
        file_error(error, "Cannot allocate file path");
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n)) {
        free(wide);
        file_error(error, "Cannot decode UTF-8 file path");
        return NULL;
    }
    return wide;
}
FILE *pp_platform_open_file(const char *path, const char *mode, PPError *error) {
    wchar_t *wide = wide_path(path, error);
    if (!wide)
        return NULL;
    const wchar_t *wmode = NULL;
    if (mode && !strcmp(mode, "rb"))
        wmode = L"rb";
    if (mode && !strcmp(mode, "wb"))
        wmode = L"wb";
    FILE *file = wmode ? _wfopen(wide, wmode) : NULL;
    free(wide);
    if (!file)
        file_error(error, "Cannot open file");
    return file;
}
FILE *pp_platform_temporary_file(const char *target, char **out, PPError *error) {
    if (out)
        *out = NULL;
    if (!target || !*target || !out) {
        file_error(error, "Missing target or temporary path output");
        return NULL;
    }
    size_t n = strlen(target);
    if (n > SIZE_MAX - 80) {
        file_error(error, "Temporary file path is too long");
        return NULL;
    }
    char *path = malloc(n + 80);
    if (!path) {
        file_error(error, "Cannot allocate temporary file path");
        return NULL;
    }
    static unsigned long sequence;
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        snprintf(path, n + 80, "%s.pointy-%lu-%llu-%lu.tmp", target, GetCurrentProcessId(),
                 GetTickCount64(), ++sequence);
        wchar_t *wide = wide_path(path, error);
        if (!wide)
            break;
        int fd = -1;
        errno_t result = _wsopen_s(&fd, wide, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _SH_DENYRW,
                                   _S_IREAD | _S_IWRITE);
        free(wide);
        if (!result) {
            FILE *file = _fdopen(fd, "w+b");
            if (file) {
                *out = path;
                return file;
            }
            _close(fd);
            pp_platform_remove_file(path, NULL);
            break;
        }
        if (result != EEXIST)
            break;
    }
    free(path);
    file_error(error, "Cannot create same-directory temporary project file");
    return NULL;
}
bool pp_platform_file_seek(FILE *file, uint64_t offset) {
    return file && offset <= INT64_MAX && !_fseeki64(file, (__int64)offset, SEEK_SET);
}
bool pp_platform_file_size(FILE *file, uint64_t *size) {
    if (!file || !size || _fseeki64(file, 0, SEEK_END))
        return false;
    __int64 n = _ftelli64(file);
    if (n < 0)
        return false;
    *size = (uint64_t)n;
    return pp_platform_file_seek(file, 0);
}
bool pp_platform_flush_file(FILE *file, PPError *error) {
    return (file && !fflush(file) && !_commit(_fileno(file))) ||
           file_error(error, "Cannot flush project bytes to storage");
}
bool pp_platform_replace_file(const char *temporary, const char *target, PPError *error) {
    wchar_t *from = wide_path(temporary, error), *to = wide_path(target, error);
    bool ok =
        from && to && MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(from);
    free(to);
    return ok || file_error(error, "Cannot atomically replace project file");
}
bool pp_platform_remove_file(const char *path, PPError *error) {
    wchar_t *wide = wide_path(path, error);
    bool ok = wide && DeleteFileW(wide);
    free(wide);
    return ok || file_error(error, "Cannot remove owned temporary file");
}
