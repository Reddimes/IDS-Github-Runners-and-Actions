#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char* msg) {
    fprintf(stderr, "ERROR: %s (Win32: %lu)\n", msg, GetLastError());
    exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ConptyCapture.exe <exe-path> [args...]\n");
        return 1;
    }

    /* build command line */
    char cmdLine[4096] = { 0 };
    strcat(cmdLine, "\"");
    strcat(cmdLine, argv[1]);
    strcat(cmdLine, "\"");
    for (int i = 2; i < argc; i++) {
        strcat(cmdLine, " ");
        strcat(cmdLine, argv[i]);
    }

    /* detach from our own console (if any) so we can attach to child's */
    if (GetConsoleWindow() && !FreeConsole()) {
        fprintf(stderr, "Warning: FreeConsole failed: %lu\n", GetLastError());
    }
    fprintf(stderr, "Detached from parent console\n");

    /* create child in its own console */
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE,
            CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        fail("CreateProcess");
    }
    CloseHandle(pi.hThread);
    fprintf(stderr, "Child created: PID=%lu\n", pi.dwProcessId);

    /* wait for child to write its output — poll every 50ms up to 10s */
    DWORD exitCode = STILL_ACTIVE;
    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < 10000) {
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            break;
        }
        Sleep(50);
    }
    fprintf(stderr, "Child exited: code=%lu (after %lu ms)\n", exitCode, GetTickCount() - startTick);

    /* close child handles so we can attach */
    CloseHandle(pi.hProcess);

    /* attach to the child's console */
    if (!AttachConsole(pi.dwProcessId)) {
        fprintf(stderr, "AttachConsole(%lu) failed: %lu\n", pi.dwProcessId, GetLastError());
        /* try forcing attach */
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            fprintf(stderr, "AttachConsole(ATTACH_PARENT_PROCESS) failed: %lu\n", GetLastError());
        }
    }
    fprintf(stderr, "AttachConsole done: %lu\n", GetLastError());

    /* try to get console handle */
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "GetStdHandle(STD_OUTPUT) = INVALID: %lu\n", GetLastError());
        hConsole = NULL;
    } else {
        fprintf(stderr, "Console handle: %p\n", hConsole);

        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(hConsole, &info)) {
            fprintf(stderr, "Screen: %dx%d, cursor at %d,%d\n",
                info.dwSize.X, info.dwSize.Y, info.dwCursorPosition.X, info.dwCursorPosition.Y);
        } else {
            fprintf(stderr, "GetConsoleScreenBufferInfo failed: %lu\n", GetLastError());
        }

        /* read console buffer */
        char buf[4096] = { 0 };
        COORD origin = { 0, 0 };
        SMALL_RECT rect;
        DWORD read;

        if (GetConsoleScreenBufferInfo(hConsole, &info)) {
            rect = info.srWindow;
            DWORD cellCount = (rect.Right - rect.Left + 1) * (rect.Bottom - rect.Top + 1);
            if (cellCount > sizeof(buf) - 1) cellCount = sizeof(buf) - 1;

            if (ReadConsoleOutputCharacterA(hConsole, buf, (DWORD)cellCount, origin, &read)) {
                fprintf(stderr, "ReadConsoleOutput: %lu chars\n", read);
                buf[read] = '\0';
                fprintf(stderr, "Console content: [%s]\n", buf);
            } else {
                fprintf(stderr, "ReadConsoleOutputCharacterA failed: %lu\n", GetLastError());
            }
        }
    }

    /* also try reading via ReadConsoleA */
    if (hConsole) {
        char rbuf[4096] = { 0 };
        DWORD r = 0;
        if (ReadConsoleA(hConsole, rbuf, sizeof(rbuf) - 1, &r, NULL)) {
            fprintf(stderr, "ReadConsoleA: %lu bytes: [%.*s]\n", r, r, rbuf);
        } else {
            fprintf(stderr, "ReadConsoleA failed: %lu\n", GetLastError());
        }
    }

    /* free console and exit */
    FreeConsole();
    return (int)exitCode;
}
