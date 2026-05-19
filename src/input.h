/*
 * input.h — DirectInput 8 keyboard + multi-joystick init.
 *
 * Mirrors FUN_0047af52 / FUN_0047b0ef ("init dinput ok" + shutdown) and the
 * paired Acquire/Unacquire dance done by the original's WM_ACTIVATE handler.
 * See docs/findings/winmain-and-bootstrap.md §"DirectInput 8 init" for the
 * full RE writeup, including the joystick enum cb (LAB_0047b167) and the
 * per-object axis-range cb (FUN_0047b1f2).
 */
#ifndef OPENRECET_INPUT_H
#define OPENRECET_INPUT_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Up to 4 attached joysticks — matches the original's `cmp dword 4; setl`
 * stop condition in the LAB_0047b167 enum callback. */
#define INPUT_MAX_JOYS 4

BOOL input_init(HINSTANCE hInst, HWND hwnd);
void input_shutdown(void);

/* WM_ACTIVATE pair: unacquire on deactivate, reacquire on activate. */
void input_unacquire_all(void);
void input_acquire_all(void);

#endif /* OPENRECET_INPUT_H */
