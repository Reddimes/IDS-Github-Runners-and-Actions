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

    FreeConsole();
    if (!AllocConsole()) {
        fail("AllocConsole");
    }

    HANDLE hScreen = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hScreen == INVALID_HANDLE_VALUE || hScreen == NULL) {
        fail("GetStdHandle STD_OUTPUT_HANDLE");
    }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hStdInRead, hStdInWrite;
    CreatePipe(&hStdInRead, &hStdInWrite, &sa, 0);
    SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);

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

    DWORD startTick = GetTickCount();
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    fprintf(stderr, "Child exited: code=%lu (after %lu ms)\n",
        exitCode, GetTickCount() - startTick);
    CloseHandle(pi.hProcess);
    CloseHandle(hStdInWrite);

    char raw[16384] = { 0 };
    char buf[32768] = { 0 };
    CONSOLE_SCREEN_BUFFER_INFO sbi;

    if (!GetConsoleScreenBufferInfo(hScreen, &sbi)) {
        fprintf(stderr, "GetConsoleScreenBufferInfo failed: %lu\n", GetLastError());
    } else {
        int rows = sbi.srWindow.Bottom - sbi.srWindow.Top + 1;
        int cols = sbi.dwSize.X;
        if (rows <= 0) rows = 1;
        if (cols <= 0) cols = 80;
        DWORD cellCount = (DWORD)(rows * cols);
        if (cellCount > sizeof(raw) - 1) cellCount = (DWORD)(sizeof(raw) - 1);

        COORD origin = { 0, 0 };
        DWORD readCount = 0;
        if (ReadConsoleOutputCharacterA(hScreen, raw, cellCount, origin, &readCount)) {
            fprintf(stderr, "ReadConsole: cols=%d, rows=%d, requested=%lu, returned=%lu\n",
                cols, rows, cellCount, readCount);

            /* Reconstruct lines: for each row, strip trailing spaces, append \n */
            int outPos = 0;
            int actualRows = (int)(readCount / cols);
            if (actualRows <= 0) actualRows = 1;
            for (int r = 0; r < actualRows; r++) {
                int rowStart = r * cols;
                int rowLen = (r == actualRows - 1)
                    ? (int)(readCount - rowStart)
                    : cols;
                if (rowStart + rowLen > (int)readCount) rowLen = (int)(readCount - rowStart);

                /* Strip trailing spaces from this row */
                int end = rowStart + rowLen - 1;
                while (end >= rowStart && raw[end] == ' ') {
                    end--;
                }
                int contentLen = end - rowStart + 1;
                if (contentLen > 0 && outPos + contentLen + 1 < (int)sizeof(buf)) {
                    memcpy(buf + outPos, raw + rowStart, contentLen);
                    outPos += contentLen;
                    buf[outPos++] = '\n';
                }
            }
            buf[outPos] = '\0';
        } else {
            fprintf(stderr, "ReadConsoleOutputCharacterA failed: %lu\n", GetLastError());
        }
    }

    /* Trim leading/trailing whitespace from entire buffer */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n' || buf[len-1] == ' ')) {
        buf[--len] = '\0';
    }
    /* Strip leading newlines */
    char* start = buf;
    while (*start == '\r' || *start == '\n' || *start == ' ') start++;
    if (start != buf) memmove(buf, start, strlen(start) + 1);

    len = strlen(buf);
    if (len > 0) {
        fprintf(stderr, "Writing %zu chars to stdout\n", len);
        fwrite(buf, 1, len, stdout);
        fflush(stdout);
    } else {
        fprintf(stderr, "Buffer is empty\n");
    }

    FreeConsole();
    return (int)exitCode;
}
