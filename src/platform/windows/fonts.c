#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "pointy/platform.h"
#include "pointy/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PP_FONT_PATH 32768
#define PP_FONT_BYTES ((size_t)256 * 1024 * 1024)

static bool font_error(PPError *error, int code, const char *message) {
    if (error) {
        error->code = code;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return false;
}

static bool family_in_file(const wchar_t *path, const char *family, PPBlob *out) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length;
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 || length.QuadPart > PP_FONT_BYTES) {
        CloseHandle(file);
        return false;
    }
    PPBlob blob = {malloc((size_t)length.QuadPart), (size_t)length.QuadPart};
    DWORD read = 0;
    bool loaded = blob.data && ReadFile(file, blob.data, (DWORD)blob.size, &read, NULL) && read == blob.size;
    CloseHandle(file);
    if (loaded) {
        /* Lookup selects family by actual metadata, not filename or cache layout.
         * Full collection bytes are returned; importers inspect/select its face. */
        for (int face = 0; face < 256; face++) {
            PPFontInfo info;
            if (!pp_font_inspect((PPBytes){blob.data, blob.size}, face, &info, NULL)) break;
            char styled[sizeof(info.family)+sizeof(info.style)+2];
            snprintf(styled,sizeof(styled),"%s %s",info.family,info.style);
            if (!_stricmp(info.family, family) || !_stricmp(styled,family)) {
                *out = blob;
                return true;
            }
        }
    }
    pp_blob_destroy(&blob);
    return false;
}

static bool registered_fonts(HKEY hive, const char *family, bool hinted, PPBlob *out) {
    HKEY key;
    if (RegOpenKeyExW(hive, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
            0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    wchar_t *name = calloc(PP_FONT_PATH, sizeof(wchar_t));
    wchar_t *value = calloc(PP_FONT_PATH, sizeof(wchar_t));
    wchar_t *path = calloc(PP_FONT_PATH, sizeof(wchar_t));
    bool found = false;
    if (!name || !value || !path) goto done;
    wchar_t requested[256];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,family,-1,requested,256)) goto done;
    size_t requested_length=wcslen(requested);
    /* Registry display names are only search hints. First inspect likely files,
     * then retain the full metadata-based fallback for aliases/collections.
     * Do not treat "Segoe UI Bold" as a hint for regular "Segoe UI". */
    for (DWORD index = 0; ; index++) {
        DWORD name_count = PP_FONT_PATH, data_bytes = (PP_FONT_PATH - 1) * sizeof(wchar_t), type;
        LONG result = RegEnumValueW(key, index, name, &name_count, NULL, &type, (BYTE *)value, &data_bytes);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) continue;
        name[name_count]=0;
        bool hint=false;
        if (!_wcsnicmp(name,requested,requested_length)) {
            const wchar_t *suffix=name+requested_length;
            while (*suffix==L' ') suffix++;
            hint=!*suffix || *suffix==L'(';
        }
        if (hint != hinted) continue;
        value[data_bytes / sizeof(wchar_t)] = 0;
        if (type == REG_EXPAND_SZ) {
            DWORD n = ExpandEnvironmentStringsW(value, path, PP_FONT_PATH);
            if (!n || n > PP_FONT_PATH) continue;
        } else if (value[1] == L':' || value[0] == L'\\') {
            if (swprintf(path, PP_FONT_PATH, L"%ls", value) < 0) continue;
        } else {
            wchar_t root[MAX_PATH] = {0};
            DWORD root_count;
            if (hive == HKEY_LOCAL_MACHINE) {
                root_count = GetWindowsDirectoryW(root, MAX_PATH);
                if (!root_count || root_count >= MAX_PATH) continue;
                if (swprintf(path, PP_FONT_PATH, L"%ls\\Fonts\\%ls", root, value) < 0) continue;
            } else {
                root_count = GetEnvironmentVariableW(L"LOCALAPPDATA", root, MAX_PATH);
                if (!root_count || root_count >= MAX_PATH) continue;
                if (swprintf(path, PP_FONT_PATH, L"%ls\\Microsoft\\Windows\\Fonts\\%ls", root, value) < 0) continue;
            }
        }
        if (family_in_file(path, family, out)) { found = true; break; }
    }
done:
    free(name);
    free(value);
    free(path);
    RegCloseKey(key);
    return found;
}

static bool font_directory(const wchar_t *directory, unsigned depth, const char *family, PPBlob *out) {
    if (depth > 6) return false;
    wchar_t *path = calloc(PP_FONT_PATH, sizeof(wchar_t));
    if (!path) return false;
    if (swprintf(path, PP_FONT_PATH, L"%ls\\*", directory) < 0) { free(path); return false; }
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(path, &data);
    if (search == INVALID_HANDLE_VALUE) { free(path); return false; }
    bool found = false;
    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..") ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (swprintf(path, PP_FONT_PATH, L"%ls\\%ls", directory, data.cFileName) < 0) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            found = font_directory(path, depth + 1, family, out);
        } else {
            const wchar_t *extension = wcsrchr(data.cFileName, L'.');
            if (extension && (!_wcsicmp(extension, L".ttf") || !_wcsicmp(extension, L".ttc") ||
                              !_wcsicmp(extension, L".otf")))
                found = family_in_file(path, family, out);
        }
        if (found) break;
    } while (FindNextFileW(search, &data));
    FindClose(search);
    free(path);
    return found;
}

bool pp_platform_find_font(const char *family, PPBlob *out, PPError *error) {
    if (!family || !*family || !out || out->data || out->size)
        return font_error(error, PP_ERROR_ARGUMENT, "Font lookup requires a family and empty owned blob");
    if (registered_fonts(HKEY_CURRENT_USER, family, true, out) ||
        registered_fonts(HKEY_LOCAL_MACHINE, family, true, out) ||
        registered_fonts(HKEY_CURRENT_USER, family, false, out) ||
        registered_fonts(HKEY_LOCAL_MACHINE, family, false, out)) return true;
    struct FontLocation { const wchar_t *variable, *suffix; } locations[] = {
        /* Enumerate Office cache versions under the standard per-user root;
         * never assume a user's name, version number, or numeric font filename. */
        {L"LOCALAPPDATA", L"\\Microsoft\\FontCache"},
        {L"LOCALAPPDATA", L"\\Microsoft\\Office\\16.0\\Fonts"},
        {L"ProgramFiles", L"\\Microsoft Office\\root\\vfs\\Fonts\\private"},
        {L"ProgramFiles(x86)", L"\\Microsoft Office\\root\\vfs\\Fonts\\private"},
        {L"CommonProgramFiles", L"\\Microsoft Shared\\Fonts"},
        {L"CommonProgramFiles(x86)", L"\\Microsoft Shared\\Fonts"}
    };
    wchar_t *root = calloc(PP_FONT_PATH, sizeof(wchar_t));
    wchar_t *path = calloc(PP_FONT_PATH, sizeof(wchar_t));
    if (!root || !path) {
        free(root);
        free(path);
        return font_error(error, PP_ERROR_MEMORY, "Could not allocate font lookup path");
    }
    bool found = false;
    for (size_t i = 0; i < sizeof(locations) / sizeof(locations[0]); i++) {
        DWORD n = GetEnvironmentVariableW(locations[i].variable, root, PP_FONT_PATH);
        if (!n || n >= PP_FONT_PATH) continue;
        if (swprintf(path, PP_FONT_PATH, L"%ls%ls", root, locations[i].suffix) < 0) continue;
        if (font_directory(path, 0, family, out)) { found = true; break; }
    }
    free(root);
    free(path);
    if (found) return true;
    return font_error(error, PP_ERROR_PLATFORM, "Requested font family is unavailable; import permitted font bytes or use emoji fallback");
}
