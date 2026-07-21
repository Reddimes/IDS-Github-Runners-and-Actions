#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

/* ------------------------------------------------------------------ */
/*  ConPTY real-time capture + INPUT_RECORD injection                  */
/*  Usage: HijackConsole.exe [--input "text"] <exe> [args...]         */
/* ------------------------------------------------------------------ */

static void fail(const char* msg) {
    fprintf(stderr, "ERROR: %s (Win32: %lu)\n", msg, GetLastError());
    exit(1);
}

/* --- ConPTY function pointers --- */
typedef HRESULT(WINAPI* PFN_CREATE_PSEUDO_CONSOLE)(COORD, HANDLE, HANDLE, DWORD, HANDLE*);
typedef VOID(WINAPI* PFN_CLOSE_PSEUDO_CONSOLE)(HANDLE);

/* --- Shared state for background read thread --- */
static std::string gCaptured;
static HANDLE      gReadDone = NULL;

static DWORD WINAPI ReadThread(LPVOID lpParam) {
    HANDLE hRead = (HANDLE)lpParam;
    char chunk[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, chunk, sizeof(chunk), &bytesRead, NULL)) {
        if (bytesRead == 0) break;
        gCaptured.append(chunk, bytesRead);
    }
    DWORD err = GetLastError();
    fprintf(stderr, "ReadThread exited: lastError=%lu (broken pipe: %d)\n",
        err, (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED));
    SetEvent(gReadDone);
    return 0;
}

/* --- Inject keystrokes + Enter into ConPTY input pipe --- */
static void InjectInput(HANDLE hInputWrite, const char* text) {
    DWORD written;
    for (int i = 0; text[i]; i++) {
        INPUT_RECORD ir;
        ZeroMemory(&ir, sizeof(ir));
        ir.EventType = KEY_EVENT;
        ir.Event.KeyEvent.bKeyDown = TRUE;
        ir.Event.KeyEvent.uChar.AsciiChar = text[i];
        ir.Event.KeyEvent.wRepeatCount = 1;
        ir.Event.KeyEvent.wVirtualKeyCode = (WORD)text[i];
        ir.Event.KeyEvent.wVirtualScanCode = 0;
        ir.Event.KeyEvent.dwControlKeyState = 0;
        WriteFile(hInputWrite, &ir, sizeof(ir), &written, NULL);
    }
    /* Send Enter (key down) */
    INPUT_RECORD enter;
    ZeroMemory(&enter, sizeof(enter));
    enter.EventType = KEY_EVENT;
    enter.Event.KeyEvent.bKeyDown = TRUE;
    enter.Event.KeyEvent.uChar.AsciiChar = '\r';
    enter.Event.KeyEvent.wRepeatCount = 1;
    enter.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
    enter.Event.KeyEvent.wVirtualScanCode = 0;
    enter.Event.KeyEvent.dwControlKeyState = 0;
    WriteFile(hInputWrite, &enter, sizeof(enter), &written, NULL);
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

    /* ---- build command line ---- */
    char cmdLine[4096] = { 0 };
    strcat(cmdLine, "\"");
    strcat(cmdLine, argv[exeArg]);
    strcat(cmdLine, "\"");
    for (int i = exeArg + 1; i < argc; i++) {
        strcat(cmdLine, " ");
        strcat(cmdLine, argv[i]);
    }

    /* ---- dynamic load ConPTY functions ---- */
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) fail("GetModuleHandle kernel32");

    PFN_CREATE_PSEUDO_CONSOLE pCreatePty = (PFN_CREATE_PSEUDO_CONSOLE)
        GetProcAddress(hKernel32, "CreatePseudoConsole");
    PFN_CLOSE_PSEUDO_CONSOLE  pClosePty  = (PFN_CLOSE_PSEUDO_CONSOLE)
        GetProcAddress(hKernel32, "ClosePseudoConsole");

    if (!pCreatePty || !pClosePty) {
        fprintf(stderr, "CreatePseudoConsole/ClosePseudoConsole not available\n");
        return 1;
    }

    /* ---- create inheritable pipes ---- */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    HANDLE hInputRead, hInputWrite;
    if (!CreatePipe(&hInputRead, &hInputWrite, &sa, 0))
        fail("CreatePipe input");

    HANDLE hOutputRead, hOutputWrite;
    if (!CreatePipe(&hOutputRead, &hOutputWrite, &sa, 0))
        fail("CreatePipe output");

    /* ---- create pseudo-console ---- */
    HANDLE hPty = NULL;
    COORD  size = { 120, 50 };
    HRESULT hr = pCreatePty(size, hInputRead, hOutputWrite, 0, &hPty);
    if (FAILED(hr)) {
        fprintf(stderr, "CreatePseudoConsole failed: 0x%lx\n", hr);
        return 20;
    }

    /* ---- clear inherit flags (ConPTY holds its own copies) ---- */
    SetHandleInformation(hInputRead,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputWrite, HANDLE_FLAG_INHERIT, 0);

    /* ---- build attribute list for child ---- */
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
    LPVOID attrBuf = malloc(attrSize);
    if (!attrBuf) fail("malloc attrBuf");

    if (!InitializeProcThreadAttributeList(
            (LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf, 1, 0, &attrSize))
        fail("InitAttrList init");
    if (!UpdateProcThreadAttribute(
            (LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPty, sizeof(HANDLE), NULL, NULL))
        fail("UpdateProcThreadAttribute");

    /* ---- spawn child process ---- */
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
    fprintf(stderr, "Child created: PID=%lu\n", pi.dwProcessId);

    /* ---- close PTY-side pipe ends (ConPTY owns copies) ---- */
    CloseHandle(hInputRead);
    CloseHandle(hOutputWrite);

    /* ---- spawn background read thread ---- */
    gReadDone = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE hReadThread = CreateThread(NULL, 0, ReadThread, hOutputRead, 0, NULL);

    /* ---- inject input if requested ---- */
    if (inputText) {
        fprintf(stderr, "Injecting input: [%s]\n", inputText);
        InjectInput(hInputWrite, inputText);
    }

    /* ---- wait for child to exit ---- */
    DWORD exitCode = STILL_ACTIVE;
    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < 60000) {
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            fprintf(stderr, "Child exited: code=%lu (after %lu ms)\n",
                exitCode, GetTickCount() - startTick);
            break;
        }
        Sleep(50);
    }

    /* ---- signal stdin EOF to ConPTY so it flushes output ---- */
    CloseHandle(hInputWrite);
    fprintf(stderr, "Input pipe closed, waiting for read thread...\n");

    /* ---- wait for read thread to drain pipe ---- */
    DWORD readWait = WaitForSingleObject(gReadDone, 5000);
    if (readWait == WAIT_TIMEOUT) {
        fprintf(stderr, "ReadThread timeout — forcing termination\n");
        TerminateThread(hReadThread, 0);
    }
    CloseHandle(hReadThread);

    /* ---- close pseudo-console ---- */
    pClosePty(hPty);
    fprintf(stderr, "Pseudo-console closed\n");

    /* ---- cleanup handles ---- */
    CloseHandle(pi.hProcess);
    CloseHandle(hOutputRead);
    CloseHandle(gReadDone);
    DeleteProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf);
    free(attrBuf);

    /* ---- trim trailing CR/LF and emit ---- */
    const char* data = gCaptured.c_str();
    size_t len = gCaptured.length();
    while (len > 0 && (data[len - 1] == '\r' || data[len - 1] == '\n'))
        len--;

    fprintf(stderr, "Captured %zu bytes\n", len);
    fwrite(data, 1, len, stdout);
    fflush(stdout);

    return (int)exitCode;
}
