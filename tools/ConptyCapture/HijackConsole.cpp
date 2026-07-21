#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

/* ------------------------------------------------------------------ */
/*  ConPTY screen-buffer capture + INPUT_RECORD injection             */
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

/* --- Read current screen buffer contents --- */
static DWORD ReadScreenBuffer(HANDLE hPty, char* buf, DWORD bufSize) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hPty, &csbi)) return 0;

    DWORD cols = (DWORD)csbi.dwSize.X;
    DWORD rows = (DWORD)csbi.dwSize.Y;
    DWORD totalChars = cols * rows;

    if (totalChars > bufSize) totalChars = bufSize;
    if (totalChars == 0) return 0;

    DWORD read;
    if (!ReadConsoleOutputCharacterA(hPty, buf, (DWORD)totalChars, { 0, 0 }, &read))
        return 0;
    buf[read] = '\0';
    return read;
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

    /* ---- create inheritable pipes for ConPTY input ---- */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    HANDLE hInputRead, hInputWrite;
    if (!CreatePipe(&hInputRead, &hInputWrite, &sa, 0))
        fail("CreatePipe input");

    /* Dummy pipe for ConPTY output (we read screen buffer instead) */
    HANDLE hDummyRead, hDummyWrite;
    if (!CreatePipe(&hDummyRead, &hDummyWrite, &sa, 0))
        fail("CreatePipe dummy output");

    /* ---- create pseudo-console ---- */
    HANDLE hPty = NULL;
    COORD  size = { 120, 50 };
    HRESULT hr = pCreatePty(size, hInputRead, hDummyWrite, 0, &hPty);
    if (FAILED(hr)) {
        fprintf(stderr, "CreatePseudoConsole failed: 0x%lx\n", hr);
        return 20;
    }

    /* ---- clear inherit flags (ConPTY holds its own copies) ---- */
    SetHandleInformation(hInputRead,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hDummyRead,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hDummyWrite, HANDLE_FLAG_INHERIT, 0);

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

    /* ---- close ConPTY-side pipe ends (ConPTY owns copies) ---- */
    CloseHandle(hInputRead);
    CloseHandle(hDummyWrite);

    /* ---- inject input if requested ---- */
    if (inputText) {
        fprintf(stderr, "Injecting input: [%s]\n", inputText);
        InjectInput(hInputWrite, inputText);
    }

    /* ---- poll screen buffer until child exits ---- */
    std::string gCaptured;
    DWORD lastPos = 0;

    /* Get buffer dimensions once */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD bufSize = 120 * 50;
    char* buf = (char*)malloc(bufSize + 1);
    if (!buf) fail("malloc buf");

    DWORD startTick = GetTickCount();
    DWORD exitCode = STILL_ACTIVE;

    while (GetTickCount() - startTick < 60000) {
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE)
            break;

        Sleep(50);

        DWORD read = ReadScreenBuffer(hPty, buf, bufSize);
        if (read > lastPos) {
            gCaptured.append(buf + lastPos, read - lastPos);
            lastPos = read;
        }
    }

    /* Final read after child exits */
    Sleep(100);
    DWORD read = ReadScreenBuffer(hPty, buf, bufSize);
    if (read > lastPos) {
        gCaptured.append(buf + lastPos, read - lastPos);
        lastPos = read;
    }

    free(buf);

    fprintf(stderr, "Child exited: code=%lu (after %lu ms), captured=%zu bytes\n",
        exitCode, GetTickCount() - startTick, gCaptured.length());

    /* ---- cleanup ---- */
    CloseHandle(pi.hProcess);
    CloseHandle(hInputWrite);
    CloseHandle(hDummyRead);
    pClosePty(hPty);
    DeleteProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf);
    free(attrBuf);

    /* ---- trim trailing CR/LF and emit ---- */
    const char* data = gCaptured.c_str();
    size_t len = gCaptured.length();
    while (len > 0 && (data[len - 1] == '\r' || data[len - 1] == '\n'))
        len--;

    fprintf(stderr, "Trimmed to %zu bytes\n", len);
    fwrite(data, 1, len, stdout);
    fflush(stdout);

    return (int)exitCode;
}
