/*
 * tools/supervisor/run-supervised.c — Win32 launcher that guarantees no
 * stray openrecet child can outlive its test run.
 *
 * Wraps an arbitrary Win32 child inside a Job Object configured with
 * JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. When this supervisor process
 * exits for *any* reason — normal exit, --timeout-ms expiry, Ctrl+C,
 * SIGKILL from WSL, parent-shell death — the kernel cleans up our
 * handle table, which closes the Job Object handle, which causes the
 * kernel to terminate every process attached to that job.
 *
 * That's unconditional and not routed through the child's message
 * pump, so it works even when openrecet's main loop is parked in
 * WaitMessage with g_paused=TRUE (the historic stray-process bug).
 * And because the reap targets the child's PID (via the per-launch
 * job), parallel openrecet runs from independent supervisors can't
 * collateral-kill each other the way `taskkill /F /IM openrecet.exe`
 * would.
 *
 * Usage:
 *     run-supervised.exe <timeout-ms> <child-exe> [child-args...]
 *
 *     <timeout-ms>:  0 → wait forever (until Ctrl+C / parent close),
 *                    N → reap the child after N milliseconds.
 *     <child-exe>:   MUST be a Windows-form path. Callers from WSL
 *                    should pre-translate via `wslpath -w`.
 *
 * Exit codes:
 *     <child exit code>  child exited normally before the deadline
 *     124                timeout reached (matches coreutils `timeout`)
 *     125                supervisor self-error (Win32 API failure)
 *     130                Ctrl+C / console-close (128 + SIGINT convention)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXIT_TIMEOUT          124
#define EXIT_SUPERVISOR_ERROR 125
#define EXIT_CTRL_C           130

static const char *g_prog = "supervisor";

static void supervisor_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", g_prog);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

static void die_winapi(const char *what)
{
    DWORD e = GetLastError();
    supervisor_log("%s failed: win32 error %lu", what, (unsigned long)e);
    ExitProcess(EXIT_SUPERVISOR_ERROR);
}

/*
 * Append `arg` to `dst` using the Microsoft "CommandLineToArgvW"
 * escape rules (https://learn.microsoft.com/cpp/c-language/parsing-cpp-command-line-arguments)
 * so the child's CRT parses it back into exactly the same argv slot.
 * Caller handles separators (spaces) between arguments.
 */
static void append_quoted_arg(char *dst, size_t cap, const char *arg)
{
    size_t len  = strlen(dst);
    size_t alen = strlen(arg);

    int needs_quote = (alen == 0);
    for (size_t i = 0; !needs_quote && i < alen; i++) {
        char c = arg[i];
        if (c == ' ' || c == '\t' || c == '"' || c == '\n' || c == '\v')
            needs_quote = 1;
    }

    if (!needs_quote) {
        if (len + alen + 1 > cap) goto overflow;
        memcpy(dst + len, arg, alen + 1);
        return;
    }

    if (len + 2 > cap) goto overflow;
    dst[len++] = '"';

    for (size_t i = 0; i < alen; ) {
        size_t bs = 0;
        while (i < alen && arg[i] == '\\') { bs++; i++; }
        if (i == alen) {
            if (len + 2 * bs + 1 + 1 > cap) goto overflow;
            for (size_t k = 0; k < 2 * bs; k++) dst[len++] = '\\';
            break;
        }
        if (arg[i] == '"') {
            if (len + 2 * bs + 2 + 1 > cap) goto overflow;
            for (size_t k = 0; k < 2 * bs; k++) dst[len++] = '\\';
            dst[len++] = '\\';
            dst[len++] = '"';
        } else {
            if (len + bs + 1 + 1 > cap) goto overflow;
            for (size_t k = 0; k < bs; k++) dst[len++] = '\\';
            dst[len++] = arg[i];
        }
        i++;
    }

    if (len + 2 > cap) goto overflow;
    dst[len++] = '"';
    dst[len]   = '\0';
    return;

overflow:
    supervisor_log("command line exceeds %zu bytes", cap);
    ExitProcess(EXIT_SUPERVISOR_ERROR);
}

static BOOL WINAPI ctrl_handler(DWORD which)
{
    (void)which;
    /* ExitProcess closes our handles, including the job, so the kernel
     * reaps the child. Distinct exit code so callers can tell Ctrl+C
     * apart from a normal child exit. */
    ExitProcess(EXIT_CTRL_C);
    return TRUE;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
            "usage: %s <timeout-ms|0> <exe> [args...]\n"
            "  timeout-ms=0  wait forever (until Ctrl+C / parent close)\n"
            "  exe           Windows-form path (callers from WSL: wslpath -w)\n",
            argc > 0 ? argv[0] : "run-supervised");
        return EXIT_SUPERVISOR_ERROR;
    }

    char *endp = NULL;
    unsigned long ms = strtoul(argv[1], &endp, 10);
    if (!endp || *endp) {
        supervisor_log("bad timeout-ms: %s", argv[1]);
        return EXIT_SUPERVISOR_ERROR;
    }
    DWORD timeout = (ms == 0) ? INFINITE : (DWORD)ms;
    const char *exe = argv[2];

    /* Create the job FIRST so we never have a non-jobbed live child,
     * even momentarily. CREATE_SUSPENDED closes the remaining gap. */
    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (!job) die_winapi("CreateJobObject");

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    ZeroMemory(&info, sizeof(info));
    info.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &info, sizeof info))
        die_winapi("SetInformationJobObject");

    /* Build the child command line. argv[0] (= exe path) is the first
     * lpCommandLine token by convention; the CRT treats it as the
     * program name and doesn't pass it to main(). */
    static char cmd[32768];
    cmd[0] = '\0';
    append_quoted_arg(cmd, sizeof cmd, exe);
    for (int i = 3; i < argc; i++) {
        size_t l = strlen(cmd);
        if (l + 2 > sizeof cmd) {
            supervisor_log("command line too long");
            return EXIT_SUPERVISOR_ERROR;
        }
        cmd[l]     = ' ';
        cmd[l + 1] = '\0';
        append_quoted_arg(cmd, sizeof cmd, argv[i]);
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof si);
    si.cb         = sizeof si;
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);

    if (!CreateProcessA(exe, cmd, NULL, NULL,
                        /* bInheritHandles */ TRUE,
                        /* dwCreationFlags */ CREATE_SUSPENDED,
                        NULL, NULL, &si, &pi))
        die_winapi("CreateProcess");

    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        DWORD e = GetLastError();
        TerminateProcess(pi.hProcess, EXIT_SUPERVISOR_ERROR);
        supervisor_log("AssignProcessToJobObject failed: win32 error %lu",
                       (unsigned long)e);
        return EXIT_SUPERVISOR_ERROR;
    }

    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        DWORD e = GetLastError();
        TerminateProcess(pi.hProcess, EXIT_SUPERVISOR_ERROR);
        supervisor_log("ResumeThread failed: win32 error %lu",
                       (unsigned long)e);
        return EXIT_SUPERVISOR_ERROR;
    }
    CloseHandle(pi.hThread);

    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    supervisor_log("pid=%lu timeout_ms=%s",
                   (unsigned long)pi.dwProcessId,
                   timeout == INFINITE ? "infinite" : argv[1]);

    DWORD wr = WaitForSingleObject(pi.hProcess, timeout);
    int exit_code;
    if (wr == WAIT_TIMEOUT) {
        supervisor_log("timeout reached (%lu ms); closing job to reap child",
                       (unsigned long)timeout);
        exit_code = EXIT_TIMEOUT;
    } else if (wr == WAIT_OBJECT_0) {
        DWORD ec = 0;
        GetExitCodeProcess(pi.hProcess, &ec);
        supervisor_log("child exited exit_code=%lu", (unsigned long)ec);
        exit_code = (int)ec;
    } else {
        supervisor_log("WaitForSingleObject returned %lu (err %lu)",
                       (unsigned long)wr, (unsigned long)GetLastError());
        exit_code = EXIT_SUPERVISOR_ERROR;
    }

    /* Process-exit would close these too, but doing it explicitly
     * documents the intent — closing `job` is what reaps the child. */
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return exit_code;
}
