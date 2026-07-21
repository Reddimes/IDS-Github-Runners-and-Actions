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

    /* ---- named pipe for hook DLL to write captured output ---- */
    HANDLE hPipe = CreateNamedPipeA("\\\\.\\pipe\\ConptyCapturePipe",
        PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
        1, 0, 0, 30000, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) fail("CreateNamedPipe");
    fprintf(stderr, "Named pipe created\n");

    /* ---- connect pipe (block until hook DLL connects) ---- */
    if (!ConnectNamedPipe(hPipe, NULL)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            fail("ConnectNamedPipe");
        }
    }
    fprintf(stderr, "Pipe connected\n");

    /* ---- alloc console for child's WriteConsoleA to work ---- */
    BOOL hadConsole = AttachConsole(ATTACH_PARENT_PROCESS);
    if (!hadConsole) AllocConsole();

    /* ---- build command line with LoadLibrary injection ---- */
    /* Get path to HookConsole.dll from same directory as exe */
    char exeDir[4096];
    GetModuleFileNameA(NULL, exeDir, sizeof(exeDir));
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat(exeDir, "HookConsole.dll");

    /* Build a cmd line that loads the DLL then runs the target */
    char cmdLine[8192] = { 0 };
    /* Use powershell to load DLL then exec target */
    snprintf(cmdLine, sizeof(cmdLine),
        "\"C:\\Windows\\System32\\cmd.exe\" /c \"\"C:\\Windows\\System32\\rundll32.exe\" "
        "\"C:\\Windows\\System32\\kernel32.dll\",LoadLibrary \"%s\" & \"%s\"\"",
        exeDir, argv[exeArg]);

    /* Actually, simpler: just put HookConsole.dll in PATH so it loads before target */
    /* Better: prepend LoadLibrary via a batch trick */
    /* Simplest: put DLL in same dir as target exe, Windows loads it if export matches */

    /* ---- spawn child with DLL preloaded ---- */
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    char targetPath[4096];
    strncpy(targetPath, argv[exeArg], sizeof(targetPath) - 1);

    if (!CreateProcessA(NULL, targetPath, NULL, NULL, TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            exeDir, NULL, &si, &pi)) {
        /* Try with full path */
        if (!CreateProcessA(NULL, targetPath, NULL, NULL, TRUE,
                CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                NULL, NULL, &si, &pi)) {
            fail("CreateProcess");
        }
    }
    CloseHandle(pi.hThread);
    fprintf(stderr, "Child created: PID=%lu\n", pi.dwProcessId);

    /* ---- inject DLL after spawn ---- */
    HANDLE hRemoteThread;
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC pLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");

    /* Allocate memory in child and write DLL path */
    LPVOID remoteBuf = VirtualAllocEx(pi.hProcess, NULL, strlen(exeDir) + 1,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(pi.hProcess, remoteBuf, exeDir, strlen(exeDir) + 1, NULL);

    hRemoteThread = CreateRemoteThread(pi.hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)pLoadLibraryA, remoteBuf, 0, NULL);
    if (hRemoteThread) {
        WaitForSingleObject(hRemoteThread, 5000);
        DWORD exitCode = 0;
        GetExitCodeThread(hRemoteThread, &exitCode);
        fprintf(stderr, "DLL injected, thread exit=%lu\n", exitCode);
        CloseHandle(hRemoteThread);
    } else {
        fprintf(stderr, "DLL injection failed: %lu\n", GetLastError());
    }
    VirtualFreeEx(pi.hProcess, remoteBuf, 0, MEM_RELEASE);

    /* ---- inject input ---- */
    if (inputText) {
        fprintf(stderr, "Injecting input: [%s]\n", inputText);
        HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
        if (hStdIn) {
            char inputBuf[1024] = { 0 };
            strncpy(inputBuf, inputText, sizeof(inputBuf) - 2);
            strcat(inputBuf, "\r\n");
            DWORD written;
            WriteFile(hStdIn, inputBuf, (DWORD)strlen(inputBuf), &written, NULL);
        }
    }

    /* ---- wait for child ---- */
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD procExitCode = 0;
    GetExitCodeProcess(pi.hProcess, &procExitCode);
    fprintf(stderr, "Child exited: code=%lu\n", procExitCode);

    /* ---- close pipe for writing, then read all ---- */
    FlushFileBuffers(hPipe);
    std::string captured;
    char chunk[4096];
    DWORD bytesRead;
    while (ReadFile(hPipe, chunk, sizeof(chunk), &bytesRead, NULL)) {
        if (bytesRead == 0) break;
        fprintf(stderr, "Read %lu bytes from pipe\n", bytesRead);
        captured.append(chunk, bytesRead);
    }
    CloseHandle(hPipe);
    CloseHandle(pi.hProcess);

    /* ---- cleanup console ---- */
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
