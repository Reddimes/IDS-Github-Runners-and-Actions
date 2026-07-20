#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef HRESULT(WINAPI* PFN_CREATE_PSEUDO_CONSOLE)(COORD, HANDLE, HANDLE, DWORD, HANDLE*);
typedef VOID(WINAPI* PFN_CLOSE_PSEUDO_CONSOLE)(HANDLE);

static void fail(const char* msg) {
    fprintf(stderr, "ERROR: %s (Win32: %lu)\n", msg, GetLastError());
    exit(1);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ConptyCapture.exe <exe-path> [args...]\n");
        return 1;
    }

    // Build command line
    char cmdLine[4096] = { 0 };
    strcat(cmdLine, "\"");
    strcat(cmdLine, argv[1]);
    strcat(cmdLine, "\"");
    for (int i = 2; i < argc; i++) {
        strcat(cmdLine, " ");
        strcat(cmdLine, argv[i]);
    }

    // Load ConPTY functions
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) fail("GetModuleHandle kernel32");

    PFN_CREATE_PSEUDO_CONSOLE pCreatePty = (PFN_CREATE_PSEUDO_CONSOLE)GetProcAddress(hKernel32, "CreatePseudoConsole");
    PFN_CLOSE_PSEUDO_CONSOLE pClosePty = (PFN_CLOSE_PSEUDO_CONSOLE)GetProcAddress(hKernel32, "ClosePseudoConsole");

    if (!pCreatePty || !pClosePty) {
        fprintf(stderr, "CreatePseudoConsole not available\n");
        return 1;
    }

    // Create pipes with inheritable handles
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    HANDLE hInputRead, hInputWrite;
    if (!CreatePipe(&hInputRead, &hInputWrite, &sa, 0)) fail("CreatePipe input");

    HANDLE hOutputRead, hOutputWrite;
    if (!CreatePipe(&hOutputRead, &hOutputWrite, &sa, 0)) fail("CreatePipe output");

    fprintf(stderr, "Pipes: iR=%p iW=%p oR=%p oW=%p\n", hInputRead, hInputWrite, hOutputRead, hOutputWrite);

    // Create pseudo-console
    HANDLE hPty = NULL;
    COORD size = { 120, 30 };
    HRESULT hr = pCreatePty(size, hInputRead, hOutputWrite, 0, &hPty);
    if (FAILED(hr)) {
        fprintf(stderr, "CreatePseudoConsole failed: 0x%lx\n", hr);
        return 20;
    }
    fprintf(stderr, "PTY created: hPty=%p\n", hPty);

    // Clear inherit flag on pipe handles after PTY creation
    SetHandleInformation(hInputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputWrite, HANDLE_FLAG_INHERIT, 0);

    // Build attribute list
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) fail("InitAttrList query");

    LPVOID attrBuf = malloc(attrSize);
    if (!attrBuf) fail("malloc attrBuf");

    if (!InitializeProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf, 1, 0, &attrSize)) fail("InitAttrList init");
    if (!UpdateProcThreadAttribute((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPty, sizeof(HANDLE), NULL, NULL))
        fail("UpdateProcThreadAttribute");

    // Create process
    STARTUPINFOEXA siex = { 0 };
    siex.StartupInfo.cb = sizeof(siex);
    siex.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf;

    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        NULL, NULL, &siex.StartupInfo, &pi)) {
        fail("CreateProcess");
    }

    CloseHandle(pi.hThread);
    fprintf(stderr, "Process created: PID=%lu\n", pi.dwProcessId);

    // Close PTY-side pipe handles (we keep parent-side: hInputWrite, hOutputRead)
    CloseHandle(hInputRead);
    CloseHandle(hOutputWrite);

    // Wait for process to exit first
    DWORD exitCode = 0;
    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < 15000) {
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            fprintf(stderr, "Process exited: code=%lu\n", exitCode);
            break;
        }
        Sleep(50);
    }
    fprintf(stderr, "Process exited: code=%lu\n", exitCode);

    // Close parent-side input write handle — signals stdin EOF to the ConPTY.
    CloseHandle(hInputWrite);

    // Poll-read available output bytes using PeekNamedPipe (non-blocking).
    // We read before ClosePseudoConsole to capture all buffered data.
    // Once PeekNamedPipe reports 0 available for two consecutive polls,
    // the ConPTY has flushed everything.
    char outputBuf[65536] = { 0 };
    DWORD totalRead = 0;
    DWORD bytesRead;
    int noDataPolls = 0;
    while (totalRead < sizeof(outputBuf) - 1 && noDataPolls < 20) {
        DWORD available = 0;
        if (!PeekNamedPipe(hOutputRead, NULL, 0, NULL, &available, NULL)) {
            fprintf(stderr, "PeekNamedPipe error: %lu\n", GetLastError());
            break;
        }
        if (available == 0) {
            noDataPolls++;
            Sleep(50);
            continue;
        }
        noDataPolls = 0;
        DWORD toRead = available;
        if (toRead > sizeof(outputBuf) - 1 - totalRead)
            toRead = sizeof(outputBuf) - 1 - totalRead;
        if (!ReadFile(hOutputRead, outputBuf + totalRead, (unsigned int)toRead, &bytesRead, NULL)) {
            fprintf(stderr, "ReadFile error: %lu\n", GetLastError());
            break;
        }
        if (bytesRead == 0) break;
        totalRead += bytesRead;
        fprintf(stderr, "Read %lu bytes (total %lu, available=%lu)\n", bytesRead, totalRead, available);
    }
    fprintf(stderr, "Total bytes read: %lu\n", totalRead);

    // Close the pseudo-console now that data is drained.
    pClosePty(hPty);
    fprintf(stderr, "Pseudo-console closed\n");

    // Cleanup
    CloseHandle(pi.hProcess);
    CloseHandle(hOutputRead);
    DeleteProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf);
    free(attrBuf);

    fprintf(stderr, "Output [%lu bytes]: [%.*s]\n", totalRead, totalRead, outputBuf);
    // Strip trailing \r\n for clean output
    while (totalRead > 0 && (outputBuf[totalRead - 1] == '\r' || outputBuf[totalRead - 1] == '\n'))
        outputBuf[--totalRead] = '\0';
    fwrite(outputBuf, 1, totalRead, stdout);

    return (int)exitCode;
}
