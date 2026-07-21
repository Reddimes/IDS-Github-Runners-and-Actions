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

    /* Phase 1: free inherited console, allocate our own */
    FreeConsole();
    if (!AllocConsole()) {
        fail("AllocConsole");
    }
    fprintf(stderr, "AllocConsole done\n");

    /* Get the screen buffer handle — WriteConsoleA writes to the attached
       console's screen buffer, which is what GetStdHandle(STD_OUTPUT_HANDLE) returns */
    HANDLE hScreen = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hScreen == INVALID_HANDLE_VALUE || hScreen == NULL) {
        fail("GetStdHandle STD_OUTPUT_HANDLE");
    }
    fprintf(stderr, "Screen buffer handle: %p\n", hScreen);

    /* Create pipes for stdin (child needs a valid stdin handle) */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hStdInRead, hStdInWrite;
    CreatePipe(&hStdInRead, &hStdInWrite, &sa, 0);
    SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);

    /* Phase 2: create child — NO CREATE_NEW_CONSOLE, NO CREATE_NO_WINDOW
       Child will inherit parent's console and WriteConsoleA will target parent's screen buffer.
       Use EXTENDED_STARTUPINFO_PRESENT to pass explicit handles via STARTUPINFOEX. */
    STARTUPINFOEXA siex = { 0 };
    siex.StartupInfo.cb = sizeof(siex);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    siex.StartupInfo.wShowWindow = SW_HIDE;
    siex.StartupInfo.hStdInput = hStdInRead;
    siex.StartupInfo.hStdOutput = hScreen;
    siex.StartupInfo.hStdError = hScreen;

    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE,
            EXTENDED_STARTUPINFO_PRESENT,
            NULL, NULL, &siex.StartupInfo, &pi)) {
        fail("CreateProcess");
    }
    fprintf(stderr, "Child created: PID=%lu\n", pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(hStdInRead);

    /* Phase 3: wait for child to exit */
    DWORD startTick = GetTickCount();
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    fprintf(stderr, "Child exited: code=%lu (after %lu ms)\n",
        exitCode, GetTickCount() - startTick);
    CloseHandle(pi.hProcess);
    CloseHandle(hStdInWrite);

    /* Phase 4: read the screen buffer the child wrote to */
    char buf[16384] = { 0 };
    CONSOLE_SCREEN_BUFFER_INFO sbi;

    if (!GetConsoleScreenBufferInfo(hScreen, &sbi)) {
        fprintf(stderr, "GetConsoleScreenBufferInfo failed: %lu\n", GetLastError());
    } else {
        fprintf(stderr, "Screen: %dx%d, cursor at (%d,%d), window=(%d,%d)-(%d,%d)\n",
            sbi.dwSize.X, sbi.dwSize.Y,
            sbi.dwCursorPosition.X, sbi.dwCursorPosition.Y,
            sbi.srWindow.Left, sbi.srWindow.Top,
            sbi.srWindow.Right, sbi.srWindow.Bottom);

        /* Read the full visible window area */
        int rows = sbi.srWindow.Bottom - sbi.srWindow.Top + 1;
        int cols = sbi.dwSize.X;
        if (rows <= 0) rows = 1;
        if (cols <= 0) cols = 80;
        DWORD cellCount = (DWORD)(rows * cols);
        if (cellCount > sizeof(buf) - 1) cellCount = (DWORD)(sizeof(buf) - 1);

        COORD origin = { 0, 0 };
        DWORD readCount = 0;
        if (ReadConsoleOutputCharacterA(hScreen, buf, cellCount, origin, &readCount)) {
            fprintf(stderr, "ReadConsoleOutputCharacterA: requested=%lu, returned=%lu\n",
                cellCount, readCount);
        } else {
            fprintf(stderr, "ReadConsoleOutputCharacterA failed: %lu\n", GetLastError());
        }
    }

    /* Phase 5: write captured buffer to stdout (parent's stdout for GitHub Actions) */
    /* Trim trailing whitespace/newlines for comparison */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n' || buf[len-1] == ' ')) {
        buf[--len] = '\0';
    }
    if (len > 0) {
        fprintf(stderr, "Writing %zu chars to stdout\n", len);
        fwrite(buf, 1, len, stdout);
        fprintf(stdout, "\n");
        fflush(stdout);
    } else {
        fprintf(stderr, "Buffer is empty after reading\n");
    }

    FreeConsole();
    return (int)exitCode;
}
