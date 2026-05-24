/*
 * msgbox_hook.h — global MessageBox-to-stderr redirector.
 *
 * Installs an in-process IAT hook that catches ALL calls to
 * user32!MessageBoxA / MessageBoxW from the current process, regardless
 * of which module made the call.  This covers:
 *
 *   - Our own MessageBoxA call sites in main.c / storage.c / tables_*.c.
 *     (mingw-linked binaries call MessageBoxA via the user32 IAT slot,
 *     so a single IAT patch catches our own source code calls too — no
 *     per-call-site rewriting needed.)
 *
 *   - MessageBoxes thrown by the DirectX runtime (d3d8.dll, dinput8.dll,
 *     dsound.dll) and other system DLLs on init/device failures.  These
 *     popups would otherwise silently block openrecet.exe during
 *     autonomous test runs since stderr never gets the message.
 *
 * Each intercepted call:
 *   - prints a loud banner + caption + text + button-type flags to
 *     stderr (line-buffered so harness captures see it immediately),
 *   - returns IDOK (auto-dismiss) so the calling code's "if (result ==
 *     IDYES) { ... }" branches behave as if the user clicked the
 *     default button,
 *   - does NOT pop a modal window — the exe never blocks waiting for
 *     human input.
 *
 * Limits:
 *   - DLLs loaded AFTER msgbox_install_global_hook() returns won't be
 *     patched.  For openrecet today this is a non-issue — d3d8 /
 *     dinput8 / dsound are statically imported and load at process
 *     start, before main() runs.  If a future code path uses
 *     LoadLibrary on a DLL that imports MessageBox, call
 *     msgbox_install_global_hook() again after the LoadLibrary.
 *   - Calls via GetProcAddress + raw call pointer aren't caught.  Rare
 *     in practice for MessageBox.
 *
 * Opt-out: pass --no-msgbox-hook on the command line.  Useful when
 * debugging interactively and you actually want to see a modal popup.
 */
#ifndef MSGBOX_HOOK_H
#define MSGBOX_HOOK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Walk every loaded module's IAT and replace user32!MessageBoxA/W slots
 * with redirector trampolines.  Safe to call multiple times; already-
 * patched slots are skipped on the second pass. */
void msgbox_install_global_hook(void);

#ifdef __cplusplus
}
#endif

#endif /* MSGBOX_HOOK_H */
