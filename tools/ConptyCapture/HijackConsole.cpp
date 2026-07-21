#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

/* ------------------------------------------------------------------ */
/*  ConPTY screen-buffer capture + INPUT_RECORD injection             */
/*  Attaches to child's console to read screen buffer.                */
/*  Usage: HijackConsole.exe [--input "text"] <exe> [args...]         */
/* ------------------------------------------------------------------ */

static void fail(const char* msg) {
    fprintf(stderr, "ERROR: %s (Win32: %lu)\n", msg, GetLastError());
    exit(1);
}

/* --- ConPTY function pointers --- */
typedef HRESULT(WINAPI* PFN_CREATE_PSEUDO_CONSOLE)(COORD, HANDLE, HANDLE, DWORD, HANDLE*);
typedef VOID(WINAPI* PFN_CLOSE_PSEUDO_CONSOLE)(HANDLE);

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

    /* ---- create inheritable pipes for ConPTY ---- */
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
    fprintf(stderr, "PseudoConsole created: hPty=%p\n", hPty);

    /* ---- clear inherit flags ---- */
    SetHandleInformation(hInputRead,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputRead,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputWrite, HANDLE_FLAG_INHERIT, 0);

    /* ---- build attribute list ---- */
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

    /* ---- spawn child ---- */
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

    /* ---- close ConPTY-side pipe ends ---- */
    CloseHandle(hInputRead);
    CloseHandle(hOutputWrite);

    /* ---- inject input immediately ---- */
    if (inputText) {
        fprintf(stderr, "Injecting input: [%s]\n", inputText);
        InjectInput(hInputWrite, inputText);
    }

    /* ---- wait for child to exit ---- */
    DWORD startTick = GetTickCount();
    DWORD exitCode = STILL_ACTIVE;
    while (GetTickCount() - startTick < 60000) {
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            fprintf(stderr, "Child exited: code=%lu (after %lu ms)\n",
                exitCode, GetTickCount() - startTick);
            break;
        }
        Sleep(50);
    }

    /* ---- drain output pipe ---- */
    CloseHandle(hInputWrite);
    std::string captured;
    char chunk[4096];
    DWORD bytesRead;
    int pipeReads = 0;
    while (ReadFile(hOutputRead, chunk, sizeof(chunk), &bytesRead, NULL)) {
        pipeReads++;
        if (bytesRead == 0) break;
        fprintf(stderr, "Pipe read #%d: %lu bytes\n", pipeReads, bytesRead);
        captured.append(chunk, bytesRead);
    }
    fprintf(stderr, "Pipe drained: %d reads, %zu bytes\n", pipeReads, captured.length());

    /* ---- attach to child console, read screen buffer ---- */
    fprintf(stderr, "Attempting AttachConsole(PID=%lu)...\n", pi.dwProcessId);
    BOOL attached = AttachConsole(pi.dwProcessId);
    if (attached) {
        fprintf(stderr, "Attached to child console. Reading screen buffer...\n");

        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        fprintf(stderr, "StdOutputHandle=%p\n", hStdOut);

        CONSOLE_SCREEN_BUFFER_INFO csbi;
        BOOL gotInfo = GetConsoleScreenBufferInfo(hStdOut, &csbi);
        fprintf(stderr, "GetConsoleScreenBufferInfo: %s\n", gotInfo ? "OK" : "FAIL");

        if (gotInfo) {
            DWORD cols = (DWORD)csbi.dwSize.X;
            DWORD rows = (DWORD)csbi.dwSize.Y;
            DWORD totalChars = cols * rows;
            fprintf(stderr, "Buffer: %lux%lu = %lu chars\n", cols, rows, totalChars);

            char* buf = (char*)malloc(totalChars + 1);
            DWORD read;
            BOOL ok = ReadConsoleOutputCharacterA(hStdOut, buf, (DWORD)totalChars,
                { 0, 0 }, &read);
            fprintf(stderr, "ReadConsoleOutputCharacter: %s, read=%lu\n",
                ok ? "OK" : "FAIL", ok ? read : 0);

            if (ok && read > 0) {
                buf[read] = '\0';
                fprintf(stderr, "Screen buffer content [%d chars]:\n", read);
                for (DWORD i = 0; i < read && i < 200; i++) {
                    fprintf(stderr, "  [%lu] 0x%02x '%c'\n", i, (unsigned char)buf[i],
                        (buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.');
                }

                /* Use screen buffer if pipe was empty */
                if (captured.empty()) {
                    captured.append(buf, read);
                }
            }
            free(buf);
        }

        FreeConsole();
        fprintf(stderr, "Detached from child console\n");
    } else {
        fprintf(stderr, "AttachConsole failed: %lu\n", GetLastError());
    }

    /* ---- cleanup ---- */
    CloseHandle(pi.hProcess);
    CloseHandle(hOutputRead);
    pClosePty(hPty);
    DeleteProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf);
    free(attrBuf);

    /* ---- trim trailing CR/LF and emit ---- */
    const char* data = captured.c_str();
    size_t len = captured.length();
    while (len > 0 && (data[len - 1] == '\r' || data[len - 1] == '\n'))
        len--;

    fprintf(stderr, "Final captured: %zu bytes\n", len);
    fwrite(data, 1, len, stdout);
    fflush(stdout);

    return (int)exitCode;
}
