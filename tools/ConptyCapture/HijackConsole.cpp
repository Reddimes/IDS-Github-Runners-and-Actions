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
    /* ---- parse args ---- */
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

    /* ---- anonymous pipe for child stdout capture ---- */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        fail("CreatePipe");
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    /* ---- alloc a real console for the child ---- */
    BOOL hadConsole = AttachConsole(ATTACH_PARENT_PROCESS);
    if (!hadConsole) {
        fprintf(stderr, "No parent console, AllocConsole...\n");
        AllocConsole();
    } else {
        fprintf(stderr, "Attached to parent console\n");
    }

    /* ---- redirect stdout/stderr to pipe ---- */
    HANDLE hOldStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hOldStdErr = GetStdHandle(STD_ERROR_HANDLE);
    SetStdHandle(STD_OUTPUT_HANDLE, hWrite);
    SetStdHandle(STD_ERROR_HANDLE, hWrite);

    /* ---- build command line ---- */
    char cmdLine[4096] = { 0 };
    strcat(cmdLine, "\"");
    strcat(cmdLine, argv[exeArg]);
    strcat(cmdLine, "\"");
    for (int i = exeArg + 1; i < argc; i++) {
        strcat(cmdLine, " ");
        strcat(cmdLine, argv[i]);
    }

    /* ---- spawn child ---- */
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            NULL, NULL, &si, &pi)) {
        fail("CreateProcess");
    }
    CloseHandle(pi.hThread);
    fprintf(stderr, "Child created: PID=%lu\n", pi.dwProcessId);

    /* ---- parent: close write end, restore console handles ---- */
    CloseHandle(hWrite);
    SetStdHandle(STD_OUTPUT_HANDLE, hOldStdOut);
    SetStdHandle(STD_ERROR_HANDLE, hOldStdErr);

    /* ---- inject input to child's stdin ---- */
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
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    fprintf(stderr, "Child exited: code=%lu\n", exitCode);

    /* ---- drain pipe ---- */
    std::string captured;
    char chunk[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, chunk, sizeof(chunk), &bytesRead, NULL)) {
        if (bytesRead == 0) break;
        fprintf(stderr, "Read %lu bytes from pipe\n", bytesRead);
        captured.append(chunk, bytesRead);
    }
    CloseHandle(hRead);
    CloseHandle(pi.hProcess);

    /* ---- restore console and detach ---- */
    if (!hadConsole) FreeConsole();
    else if (hadConsole) FreeConsole();

    /* ---- emit ---- */
    const char* data = captured.c_str();
    size_t len = captured.length();
    while (len > 0 && (data[len - 1] == '\r' || data[len - 1] == '\n'))
        len--;
    fprintf(stderr, "Final captured: %zu bytes\n", len);
    fwrite(data, 1, len, stdout);
    fflush(stdout);

    return (int)exitCode;
}
