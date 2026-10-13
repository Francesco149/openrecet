/* rh_files.c — hermetic file overlay for retail (copy-on-write into the run dir).
 *
 * The game dir (data files) stays pristine and is never written. Every retail
 * file API that names a path inside the game dir (or a relative path — cwd is the
 * game dir) is resolved through an overlay:
 *   read  : <rundir>\rel if it exists, else <gamedir>\rel (unless whited-out)
 *   write : <rundir>\rel (copied from the game dir first when the open keeps
 *           existing content)
 * save.dat is whited-out unless the run dir provides one, so a run never sees the
 * user's save by accident. GetTempPathA → <rundir>\tmp\. Covers CreateFileA/W
 * (CRT fopen goes through CreateFileA), DeleteFileA, Get/WritePrivateProfile*
 * (recet.ini). Paths outside the game dir pass through untouched. */
#include "rhost.h"
#include <shlwapi.h>

static char G[MAX_PATH], R[MAX_PATH];
static size_t GL;
static char g_white[16][64]; static int g_nwhite;
static long g_nopen;

static int is_white(const char *rel)
{
    for (int i = 0; i < g_nwhite; i++) if (!_stricmp(g_white[i], rel)) return 1;
    return 0;
}
static void unwhite(const char *rel)
{
    for (int i = 0; i < g_nwhite; i++) if (!_stricmp(g_white[i], rel)) { strcpy(g_white[i], g_white[--g_nwhite]); return; }
}

/* path → game-relative part ("" if outside the game dir). Returns 1 if inside. */
static int game_rel(const char *path, char *rel, size_t n)
{
    char full[MAX_PATH];
    if (!path || !*path) return 0;
    if (!GetFullPathNameA(path, sizeof full, full, NULL)) return 0;   /* relative → cwd (= game dir) */
    if (_strnicmp(full, G, GL) || (full[GL] != '\\' && full[GL] != 0)) return 0;
    const char *r = full + GL; while (*r == '\\') r++;
    snprintf(rel, n, "%s", r);
    return *rel != 0;
}
static int exists(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static void mkparents(const char *p)
{
    char t[MAX_PATH]; snprintf(t, sizeof t, "%s", p);
    for (char *s = t + strlen(R) + 1; *s; s++) if (*s == '\\') { *s = 0; CreateDirectoryA(t, NULL); *s = '\\'; }
}

/* resolve for read (want_write=0) or write (1; keep=1 → copy-on-write) */
static const char *resolve(const char *path, int want_write, int keep, char *out, size_t n)
{
    char rel[MAX_PATH];
    if (!game_rel(path, rel, sizeof rel)) return path;
    char rp[MAX_PATH], gp[MAX_PATH];
    snprintf(rp, sizeof rp, "%s\\%s", R, rel);
    snprintf(gp, sizeof gp, "%s\\%s", G, rel);
    if (want_write) {
        mkparents(rp);
        if (keep && !exists(rp) && !is_white(rel) && exists(gp)) CopyFileA(gp, rp, TRUE);
        unwhite(rel);
        snprintf(out, n, "%s", rp);
    } else if (exists(rp)) snprintf(out, n, "%s", rp);
    else if (is_white(rel)) snprintf(out, n, "%s", rp);    /* → not found */
    else snprintf(out, n, "%s", gp);
    return out;
}

static HANDLE WINAPI my_CreateFileA(LPCSTR p, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl)
{
    int w = (acc & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) || disp == CREATE_ALWAYS ||
            disp == CREATE_NEW || disp == TRUNCATE_EXISTING;
    int keep = disp == OPEN_EXISTING || disp == OPEN_ALWAYS;
    char buf[MAX_PATH];
    const char *q = resolve(p, w, keep, buf, sizeof buf);
    HANDLE h = CreateFileA(q, acc, share, sa, disp, fl, tmpl);
    g_nopen++;
    if (O.log_files) rh_log("file %s %s -> %s = %s (f%u)\n", w ? "W" : "R", p, q, h == INVALID_HANDLE_VALUE ? "FAIL" : "ok", g_frame);
    return h;
}
static HANDLE WINAPI my_CreateFileW(LPCWSTR wp, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl)
{
    char p[MAX_PATH];
    if (!WideCharToMultiByte(CP_ACP, 0, wp, -1, p, sizeof p, NULL, NULL)) return CreateFileW(wp, acc, share, sa, disp, fl, tmpl);
    char buf[MAX_PATH];
    int w = (acc & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) || disp == CREATE_ALWAYS || disp == CREATE_NEW || disp == TRUNCATE_EXISTING;
    const char *q = resolve(p, w, disp == OPEN_EXISTING || disp == OPEN_ALWAYS, buf, sizeof buf);
    WCHAR wq[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, q, -1, wq, MAX_PATH);
    HANDLE h = CreateFileW(wq, acc, share, sa, disp, fl, tmpl);
    g_nopen++;
    if (O.log_files) rh_log("fileW %s %s -> %s = %s (f%u)\n", w ? "W" : "R", p, q, h == INVALID_HANDLE_VALUE ? "FAIL" : "ok", g_frame);
    return h;
}
static BOOL WINAPI my_DeleteFileA(LPCSTR p)
{
    char rel[MAX_PATH];
    if (!game_rel(p, rel, sizeof rel)) return DeleteFileA(p);
    char rp[MAX_PATH]; snprintf(rp, sizeof rp, "%s\\%s", R, rel);
    DeleteFileA(rp);
    if (g_nwhite < 16 && !is_white(rel)) snprintf(g_white[g_nwhite++], 64, "%s", rel);
    if (O.log_files) rh_log("delete %s (f%u)\n", p, g_frame);
    return TRUE;
}
static UINT WINAPI my_GetPrivateProfileIntA(LPCSTR app, LPCSTR key, INT def, LPCSTR file)
{
    char buf[MAX_PATH]; return GetPrivateProfileIntA(app, key, def, resolve(file, 0, 0, buf, sizeof buf));
}
static BOOL WINAPI my_WritePrivateProfileStringA(LPCSTR app, LPCSTR key, LPCSTR val, LPCSTR file)
{
    char buf[MAX_PATH]; return WritePrivateProfileStringA(app, key, val, resolve(file, 1, 1, buf, sizeof buf));
}
/* temp files: retail sees a run-INDEPENDENT path (<gamedir>\tmp\, which the overlay
 * maps into <rundir>\tmp\) and deterministic names — the real API's names depend on
 * the clock and on what already exists, and the run-dir name must never leak into
 * retail memory (it did: "smoke_a" vs "smoke_b" in a D3DX path buffer). */
static DWORD WINAPI my_GetTempPathA(DWORD n, LPSTR buf)
{
    char t[MAX_PATH]; snprintf(t, sizeof t, "%s\\tmp", R); CreateDirectoryA(t, NULL);
    snprintf(t, sizeof t, "%s\\tmp\\", G);
    snprintf(buf, n, "%s", t); return (DWORD)strlen(t);
}
static UINT g_tmpseq;
static HANDLE WINAPI my_CreateFileA(LPCSTR p, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl);
static UINT WINAPI my_GetTempFileNameA(LPCSTR path, LPCSTR prefix, UINT unique, LPSTR out)
{
    UINT u = unique ? unique : ++g_tmpseq;
    snprintf(out, MAX_PATH, "%s%s%.3s%04X.tmp", path, (*path && path[strlen(path) - 1] != '\\') ? "\\" : "", prefix ? prefix : "", u & 0xffff);
    if (!unique) {
        HANDLE h = my_CreateFileA(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return 0;
        CloseHandle(h);
    }
    return u;
}

void rh_files_install(void)
{
    GetFullPathNameA(O.gamedir, sizeof G, G, NULL);
    GetFullPathNameA(O.rundir, sizeof R, R, NULL);
    for (size_t l = strlen(G); l && G[l - 1] == '\\'; ) G[--l] = 0;
    for (size_t l = strlen(R); l && R[l - 1] == '\\'; ) R[--l] = 0;
    GL = strlen(G);
    CreateDirectoryA(R, NULL);
    char sp[MAX_PATH]; snprintf(sp, sizeof sp, "%s\\save.dat", R);
    if (!exists(sp)) snprintf(g_white[g_nwhite++], 64, "save.dat");
    char ip[MAX_PATH]; snprintf(ip, sizeof ip, "%s\\recet.ini", R);
    if (!exists(ip)) rh_log("WARNING: no recet.ini in the run dir — the game dir's copy leaks in (non-hermetic)\n");
    iat_override("CreateFileA", my_CreateFileA);
    iat_override("CreateFileW", my_CreateFileW);
    iat_override("DeleteFileA", my_DeleteFileA);
    iat_override("GetPrivateProfileIntA", my_GetPrivateProfileIntA);
    iat_override("WritePrivateProfileStringA", my_WritePrivateProfileStringA);
    iat_override("GetTempPathA", my_GetTempPathA);
    iat_override("GetTempFileNameA", my_GetTempFileNameA);
    rh_log("overlay: game=%s run=%s save=%s\n", G, R, exists(sp) ? "run-dir" : "none (whited-out)");
}
