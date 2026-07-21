#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static void fail(const char* msg) {
    fprintf(stderr, "ERROR: %s (Win32: %lu)\n", msg, GetLastError());
    exit(1);
}

int main(int argc, char* argv[]) {
    const char* inputText = NULL;
    int exeArg = 1;
    if (argc > 1 && strcmp(argv[1], "--input") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: HijackConsole.exe [--input \"text\"] <exe> [args...]\n");
            return 1;
        }
        inputText = argv[2];
        exeArg = 3;
    }
    if (argc < exeArg) {
        fprintf(stderr, "Usage: HijackConsole.exe [--input \"text\"] <exe> [args...]\n");
        return 1;
    }

    /* ---- named pipe (overlapped) for hook DLL ---- */
    HANDLE hPipe = CreateNamedPipeA("\\\\.\\pipe\\ConptyCapturePipe",
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
        1, 0, 0, 30000, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) fail("CreateNamedPipe");
    fprintf(stderr, "Named pipe created\n");

    /* Start async connect */
    OVERLAPPED ol = { 0 };
    ol.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    ConnectNamedPipe(hPipe, &ol);
    if (GetLastError() != ERROR_IO_PENDING) {
        fprintf(stderr, "ConnectNamedPipe immediate error: %lu\n", GetLastError());
    }

    /* ---- alloc console for child ---- */
    BOOL hadConsole = AttachConsole(ATTACH_PARENT_PROCESS);
    if (!hadConsole) AllocConsole();

    /* ---- get DLL path ---- */
    char exeDir[4096];
    GetModuleFileNameA(NULL, exeDir, sizeof(exeDir));
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    char dllPath[4096];
    snprintf(dllPath, sizeof(dllPath), "%sHookConsole.dll", exeDir);
    fprintf(stderr, "DLL path: %s\n", dllPath);

    /* ---- spawn child SUSPENDED ---- */
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(NULL, argv[exeArg], NULL, NULL, TRUE,
            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
            NULL, NULL, &si, &pi)) {
        fail("CreateProcess");
    }
    fprintf(stderr, "Child created (suspended): PID=%lu\n", pi.dwProcessId);

    /* ---- inject DLL while suspended ---- */
    /* Both parent and child are x86, so kernel32.dll offsets match. */
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC pLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");
    fprintf(stderr, "LoadLibraryA (parent): %p\n", pLoadLibraryA);

    LPVOID remoteBuf = VirtualAllocEx(pi.hProcess, NULL, strlen(dllPath) + 1,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBuf) {
        fprintf(stderr, "VirtualAllocEx failed: %lu\n", GetLastError());
    } else {
        if (!WriteProcessMemory(pi.hProcess, remoteBuf, dllPath, strlen(dllPath) + 1, NULL)) {
            fprintf(stderr, "WriteProcessMemory failed: %lu\n", GetLastError());
        } else {
            HANDLE hRemoteThread = CreateRemoteThread(pi.hProcess, NULL, 0,
                (LPTHREAD_START_ROUTINE)pLoadLibraryA, remoteBuf, 0, NULL);
            if (hRemoteThread) {
                WaitForSingleObject(hRemoteThread, 5000);
                DWORD threadExit = 0;
                GetExitCodeThread(hRemoteThread, &threadExit);
                fprintf(stderr, "DLL injected, HMODULE=%lu\n", threadExit);
                CloseHandle(hRemoteThread);
            } else {
                fprintf(stderr, "CreateRemoteThread failed: %lu\n", GetLastError());
            }
        }
        VirtualFreeEx(pi.hProcess, remoteBuf, 0, MEM_RELEASE);
    }

    /* ---- resume child ---- */
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    fprintf(stderr, "Child resumed\n");

    /* ---- wait for child ---- */
    DWORD startTick = GetTickCount();
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD procExitCode = 0;
    GetExitCodeProcess(pi.hProcess, &procExitCode);
    fprintf(stderr, "Child exited: code=%lu (after %lu ms)\n",
        procExitCode, GetTickCount() - startTick);

    /* ---- wait for pipe connect to complete ---- */
    fprintf(stderr, "Waiting for pipe connect...\n");
    DWORD connectResult = WaitForSingleObject(ol.hEvent, 10000);
    fprintf(stderr, "Pipe connect result: %lu\n", connectResult);

    /* ---- read from pipe ---- */
    std::string captured;
    char chunk[4096];
    DWORD bytesRead;
    int reads = 0;

    /* First check if pipe is connected */
    if (connectResult != WAIT_OBJECT_0) {
        fprintf(stderr, "Pipe not connected (timeout or child exited too fast)\n");
    } else {
        while (ReadFile(hPipe, chunk, sizeof(chunk), &bytesRead, NULL)) {
            reads++;
            if (bytesRead == 0) break;
            fprintf(stderr, "Pipe read #%d: %lu bytes\n", reads, bytesRead);
            captured.append(chunk, bytesRead);
        }
        fprintf(stderr, "Pipe drained: %d reads, %zu bytes\n", reads, captured.length());
    }

    CloseHandle(hPipe);
    CloseHandle(ol.hEvent);
    CloseHandle(pi.hProcess);
    FreeConsole();

    /* ---- emit ---- */
    const char* data = captured.c_str();
    size_t len = captured.length();
    while (len > 0 && (data[len - 1] == '\r' || data[len - 1] == '\n'))
        len--;
    fprintf(stderr, "Final captured: %zu bytes\n", len);
    fwrite(data, 1, len, stdout);
    fflush(stdout);

    return (int)procExitCode;
}
