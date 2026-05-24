/*
 * msgbox_hook.c — see msgbox_hook.h.
 *
 * Implementation: walk every module's import descriptor, for each
 * IMAGE_THUNK_DATA slot compare the resolved function pointer to the
 * real MessageBoxA / MessageBoxW from user32.  If it matches, flip the
 * slot (after VirtualProtect to PAGE_READWRITE) to point at our fake.
 *
 * Pointer-compare approach (vs. name-walking via OriginalFirstThunk)
 * lets us catch bound imports (where OFT == 0), ordinal imports, and
 * the unusual case where some DLL imports MessageBoxA via a different
 * name resolution path.  The cost is iterating every imported function
 * of every module — still microseconds total since it's all
 * pointer-compare in a process-private page.
 */

#include "msgbox_hook.h"

#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>

static int (WINAPI *real_MessageBoxA)(HWND, LPCSTR,  LPCSTR,  UINT);
static int (WINAPI *real_MessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT);

/* ─── redirector ────────────────────────────────────────────────────── */

static const char *button_flags_str(UINT type, char *buf, size_t cap)
{
    /* MB_OK is value 0, so fall through to "OK" if no other button bits set. */
    static const struct { UINT mask; const char *name; } btn[] = {
        { MB_OKCANCEL,         "OKCANCEL" },
        { MB_ABORTRETRYIGNORE, "ABORTRETRYIGNORE" },
        { MB_YESNOCANCEL,      "YESNOCANCEL" },
        { MB_YESNO,            "YESNO" },
        { MB_RETRYCANCEL,      "RETRYCANCEL" },
    };
    UINT bg = type & 0xF;
    for (size_t i = 0; i < sizeof btn / sizeof btn[0]; i++) {
        if (bg == btn[i].mask) {
            snprintf(buf, cap, "%s", btn[i].name);
            return buf;
        }
    }
    snprintf(buf, cap, "OK");
    return buf;
}

static const char *icon_flags_str(UINT type)
{
    UINT ig = type & 0xF0;
    if (ig == MB_ICONERROR)       return "ICONERROR";
    if (ig == MB_ICONWARNING)     return "ICONWARNING";
    if (ig == MB_ICONINFORMATION) return "ICONINFORMATION";
    if (ig == MB_ICONQUESTION)    return "ICONQUESTION";
    return "noicon";
}

static void banner_print_a(LPCSTR text, LPCSTR caption, UINT type)
{
    char btn_buf[32];
    fprintf(stderr,
        "\n"
        "========== [openrecet] MessageBoxA REDIRECTED ==========\n"
        "  caption: %s\n"
        "  text:    %s\n"
        "  buttons: %s (%s, raw=0x%x)\n"
        "  return:  IDOK (auto-dismissed; would otherwise block)\n"
        "========================================================\n"
        "\n",
        caption ? caption : "(null)",
        text    ? text    : "(null)",
        button_flags_str(type, btn_buf, sizeof btn_buf),
        icon_flags_str(type), type);
    fflush(stderr);
}

static void banner_print_w(LPCWSTR text, LPCWSTR caption, UINT type)
{
    /* WideCharToMultiByte to UTF-8 so non-ASCII captions render the same
     * way our own UTF-8 logs do (console CP is already CP_UTF8 from
     * main.c).  Truncate to 1KB per field — the goal is the operator
     * notices the message, not faithful long-text reproduction. */
    char text_buf[1024];
    char cap_buf[256];
    if (text) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, text_buf, sizeof text_buf,
                            NULL, NULL);
        text_buf[sizeof text_buf - 1] = '\0';
    } else {
        strcpy(text_buf, "(null)");
    }
    if (caption) {
        WideCharToMultiByte(CP_UTF8, 0, caption, -1, cap_buf, sizeof cap_buf,
                            NULL, NULL);
        cap_buf[sizeof cap_buf - 1] = '\0';
    } else {
        strcpy(cap_buf, "(null)");
    }
    char btn_buf[32];
    fprintf(stderr,
        "\n"
        "========== [openrecet] MessageBoxW REDIRECTED ==========\n"
        "  caption: %s\n"
        "  text:    %s\n"
        "  buttons: %s (%s, raw=0x%x)\n"
        "  return:  IDOK (auto-dismissed; would otherwise block)\n"
        "========================================================\n"
        "\n",
        cap_buf, text_buf,
        button_flags_str(type, btn_buf, sizeof btn_buf),
        icon_flags_str(type), type);
    fflush(stderr);
}

static int WINAPI fake_MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption,
                                   UINT type)
{
    (void)owner;
    banner_print_a(text, caption, type);
    return IDOK;
}

static int WINAPI fake_MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption,
                                   UINT type)
{
    (void)owner;
    banner_print_w(text, caption, type);
    return IDOK;
}

/* ─── IAT patching ──────────────────────────────────────────────────── */

static void patch_iat_of(HMODULE mod)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    IMAGE_DATA_DIRECTORY *dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir->VirtualAddress == 0 || dir->Size == 0) return;

    IMAGE_IMPORT_DESCRIPTOR *imp =
        (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);

    for (; imp->Name; imp++) {
        /* FirstThunk after the loader runs IS the IAT — each slot
         * holds the resolved function pointer.  Compare slot values
         * against the real MessageBoxA/W; no need to walk
         * OriginalFirstThunk name tables. */
        IMAGE_THUNK_DATA *iat =
            (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; iat->u1.Function; iat++) {
            void *slot_val = (void *)iat->u1.Function;
            void *fake = NULL;
            if (slot_val == (void *)real_MessageBoxA) {
                fake = (void *)&fake_MessageBoxA;
            } else if (slot_val == (void *)real_MessageBoxW) {
                fake = (void *)&fake_MessageBoxW;
            }
            if (!fake) continue;

            DWORD old_prot;
            if (VirtualProtect(&iat->u1.Function, sizeof(void *),
                               PAGE_READWRITE, &old_prot)) {
                iat->u1.Function = (uintptr_t)fake;
                VirtualProtect(&iat->u1.Function, sizeof(void *),
                               old_prot, &old_prot);
            }
        }
    }
}

void msgbox_install_global_hook(void)
{
    /* Resolve real fn pointers once.  user32.dll is always loaded for a
     * GUI process; LoadLibraryA is idempotent so calling it here costs
     * one ref-count bump even if it's already in. */
    HMODULE user32 = LoadLibraryA("user32.dll");
    if (!user32) {
        fprintf(stderr,
            "msgbox_hook: LoadLibraryA(\"user32.dll\") failed — skipping hook\n");
        return;
    }
    real_MessageBoxA = (int (WINAPI *)(HWND, LPCSTR, LPCSTR, UINT))
        GetProcAddress(user32, "MessageBoxA");
    real_MessageBoxW = (int (WINAPI *)(HWND, LPCWSTR, LPCWSTR, UINT))
        GetProcAddress(user32, "MessageBoxW");
    if (!real_MessageBoxA && !real_MessageBoxW) {
        fprintf(stderr,
            "msgbox_hook: GetProcAddress for both A/W failed — skipping hook\n");
        return;
    }

    /* Walk every loaded module's IAT.  256 module slots is generous —
     * a Win32 process with d3d8 + dinput8 + dsound + system DLLs
     * typically has 30-50 modules. */
    HMODULE mods[256];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &needed)) {
        fprintf(stderr,
            "msgbox_hook: EnumProcessModules failed (err=%lu) — skipping hook\n",
            GetLastError());
        return;
    }
    DWORD n = needed / sizeof(HMODULE);
    if (n > sizeof mods / sizeof mods[0]) n = sizeof mods / sizeof mods[0];

    int patched = 0;
    for (DWORD i = 0; i < n; i++) {
        patch_iat_of(mods[i]);
        patched++;
    }
    fprintf(stderr,
        "msgbox_hook: installed; scanned %d modules for user32!MessageBox{A,W} "
        "imports (auto-dismiss → stderr)\n", patched);
    fflush(stderr);
}
