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

    char cmdLine[4096] = { 0 };
    strcat(cmdLine, "\"");
    strcat(cmdLine, argv[1]);
    strcat(cmdLine, "\"");
    for (int i = 2; i < argc; i++) {
        strcat(cmdLine, " ");
        strcat(cmdLine, argv[i]);
    }

    /* Phase 1: free any inherited console, allocate our own */
    FreeConsole();
    if (!AllocConsole()) {
        fprintf(stderr, "AllocConsole failed: %lu\n", GetLastError());
    }
    fprintf(stderr, "AllocConsole done\n");

    /* Get our console output handle — this is what the child will write to */
    HANDLE hOurConsole = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hOurConsole == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile CONOUT$ failed: %lu, falling back to GetStdHandle\n", GetLastError());
        hOurConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    fprintf(stderr, "Our console handle: %p\n", hOurConsole);

    /* Phase 2: create pipes for capturing stdout (in case WriteConsoleA somehow goes to pipe) */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hStdOutRead, hStdOutWrite;
    CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0);
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    /* Phase 3: create child with our console as stdout/stderr */
    STARTUPINFOEXA siex = { 0 };
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    siex.StartupInfo.wShowWindow = SW_HIDE;
    /* Give the child our console handle as stdout/stderr so WriteConsoleA targets our buffer */
    siex.StartupInfo.hStdOut = hOurConsole;
    siex.StartupInfo.hStdErr = hOurConsole;
    /* stdin from pipe (unused by RevStr but needed) */
    HANDLE hStdInRead, hStdInWrite;
    CreatePipe(&hStdInRead, &hStdInWrite, &sa, 0);
    SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);
    siex.StartupInfo.hStdInput = hStdInRead;

    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
            NULL, NULL, &siex.StartupInfo, &pi)) {
        fail("CreateProcess");
    }
    fprintf(stderr, "Child created: PID=%lu\n", pi.dwProcessId);
    CloseHandle(pi.hThread);

    /* Close parent-side pipe handles we don't need */
    CloseHandle(hStdInRead);
    CloseHandle(hStdOutWrite);

    /* Phase 4: wait for child to exit */
    DWORD exitCode = STILL_ACTIVE;
    DWORD startTick = GetTickCount();
    WaitForSingleObject(pi.hProcess, 10000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    fprintf(stderr, "Child exited: code=%lu (after %lu ms)\n", exitCode, GetTickCount() - startTick);
    CloseHandle(pi.hProcess);

    /* Signal stdin EOF */
    CloseHandle(hStdInWrite);

    /* Phase 5: read our console screen buffer — child wrote here via WriteConsoleA */
    char buf[8192] = { 0 };
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(hOurConsole, &info)) {
        fprintf(stderr, "Screen: %dx%d, cursor at %d,%d\n",
            info.dwSize.X, info.dwSize.Y, info.dwCursorPosition.X, info.dwCursorPosition.Y);

        /* Read from (0,0) to cursor position */
        int rows = info.dwCursorPosition.Y + 1;
        int cols = info.dwSize.X;
        DWORD cellCount = (DWORD)(rows * cols);
        if (cellCount > sizeof(buf) - 1) cellCount = sizeof(buf) - 1;

        if (ReadConsoleOutputCharacterA(hOurConsole, buf, cellCount, (COORD){0, 0}, NULL)) {
            fprintf(stderr, "ReadConsoleOutput: %lu chars\n", cellCount);
            fwrite(buf, 1, cellCount, stdout);
            fflush(stdout);
        } else {
            fprintf(stderr, "ReadConsoleOutputCharacterA failed: %lu\n", GetLastError());
        }
    } else {
        fprintf(stderr, "GetConsoleScreenBufferInfo failed: %lu\n", GetLastError());
    }

    /* Also try reading the pipe (in case anything went there) */
    char pbuf[4096] = { 0 };
    DWORD pr = 0;
    if (ReadFile(hStdOutRead, pbuf, sizeof(pbuf) - 1, &pr, NULL) && pr > 0) {
        fprintf(stderr, "Pipe output: %lu bytes\n", pr);
    }
    CloseHandle(hStdOutRead);

    FreeConsole();
    return (int)exitCode;
}
