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

// ---------- shared read state ----------
static char   gBuf[65536];
static size_t gBufLen = 0;
static HANDLE gReadDone = NULL;

static DWORD WINAPI ReadThread(LPVOID lpParam) {
    HANDLE hRead = (HANDLE)lpParam;
    char chunk[4096];
    DWORD bytesRead;
    DWORD chunkCount = 0;

    while (gBufLen < sizeof(gBuf)) {
        DWORD toRead = (DWORD)(sizeof(gBuf) - gBufLen);
        if (toRead > sizeof(chunk)) toRead = (DWORD)sizeof(chunk);
        if (!ReadFile(hRead, chunk, toRead, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                fprintf(stderr, "ReadThread: pipe closed after %lu chunks, total %zu bytes\n",
                    chunkCount, gBufLen);
                break;
            }
            fprintf(stderr, "ReadThread: ReadFile error: %lu\n", err);
            break;
        }
        if (bytesRead == 0) {
            fprintf(stderr, "ReadThread: EOF after %lu chunks, total %zu bytes\n",
                chunkCount, gBufLen);
            break;
        }
        memcpy(gBuf + gBufLen, chunk, bytesRead);
        gBufLen += bytesRead;
        chunkCount++;
        fprintf(stderr, "ReadThread chunk %lu: +%lu bytes (total %zu)\n",
            chunkCount, bytesRead, gBufLen);
    }

    SetEvent(gReadDone);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ConptyCapture.exe <exe-path> [args...]\n");
        return 1;
    }

    /* build command line */
    char cmdLine[4096] = { 0 };
    strcat(cmdLine, "\"");
    strcat(cmdLine, argv[1]);
    strcat(cmdLine, "\"");
    for (int i = 2; i < argc; i++) {
        strcat(cmdLine, " ");
        strcat(cmdLine, argv[i]);
    }

    /* load ConPTY functions */
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) fail("GetModuleHandle kernel32");

    PFN_CREATE_PSEUDO_CONSOLE pCreatePty  = (PFN_CREATE_PSEUDO_CONSOLE)GetProcAddress(hKernel32, "CreatePseudoConsole");
    PFN_CLOSE_PSEUDO_CONSOLE  pClosePty   = (PFN_CLOSE_PSEUDO_CONSOLE)GetProcAddress(hKernel32, "ClosePseudoConsole");

    if (!pCreatePty || !pClosePty) {
        fprintf(stderr, "CreatePseudoConsole not available\n");
        return 1;
    }

    /* create inheritable pipes */
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    HANDLE hInputRead, hInputWrite;
    if (!CreatePipe(&hInputRead, &hInputWrite, &sa, 0)) fail("CreatePipe input");

    HANDLE hOutputRead, hOutputWrite;
    if (!CreatePipe(&hOutputRead, &hOutputWrite, &sa, 0)) fail("CreatePipe output");

    fprintf(stderr, "Pipes: iR=%p iW=%p oR=%p oW=%p\n",
        hInputRead, hInputWrite, hOutputRead, hOutputWrite);

    /* create pseudo-console — gives hInputRead + hOutputWrite to ConPTY */
    HANDLE hPty = NULL;
    COORD  size = { 120, 30 };
    HRESULT hr = pCreatePty(size, hInputRead, hOutputWrite, 0, &hPty);
    if (FAILED(hr)) {
        fprintf(stderr, "CreatePseudoConsole failed: 0x%lx\n", hr);
        return 20;
    }
    fprintf(stderr, "PTY created: hPty=%p\n", hPty);

    /* clear inherit flags on all pipe handles now that ConPTY has copies */
    SetHandleInformation(hInputRead,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hInputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutputWrite, HANDLE_FLAG_INHERIT, 0);

    /* build attribute list */
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) fail("InitAttrList query");

    LPVOID attrBuf = malloc(attrSize);
    if (!attrBuf) fail("malloc attrBuf");

    if (!InitializeProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf, 1, 0, &attrSize))
        fail("InitAttrList init");
    if (!UpdateProcThreadAttribute((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPty, sizeof(HANDLE), NULL, NULL))
        fail("UpdateProcThreadAttribute");

    /* start the child process */
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

    /* close the PTY-side pipe ends (ConPTY holds its own copies) */
    CloseHandle(hInputRead);
    CloseHandle(hOutputWrite);

    /* spawn background thread that reads from hOutputRead concurrently */
    gReadDone = CreateEventA(NULL, TRUE, FALSE, NULL);  // manual-reset
    HANDLE hReadThread = CreateThread(NULL, 0, ReadThread, hOutputRead, 0, NULL);

    /* wait for the child to exit */
    DWORD exitCode = STILL_ACTIVE;
    DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < 15000) {
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            fprintf(stderr, "Process exited: code=%lu\n", exitCode);
            break;
        }
        Sleep(50);
    }
    fprintf(stderr, "Process exited: code=%lu\n", exitCode);

    /* signal stdin EOF to the ConPTY worker thread so it flushes remaining data */
    CloseHandle(hInputWrite);
    fprintf(stderr, "Input write handle closed\n");

    /* wait for the read thread to finish (pipe EOF / broken pipe) */
    DWORD readWait = WaitForSingleObject(gReadDone, 5000);
    if (readWait == WAIT_TIMEOUT) {
        fprintf(stderr, "ReadThread still running after 5s — terminating\n");
        TerminateThread(hReadThread, 0);
    }
    CloseHandle(hReadThread);

    /* close the pseudo-console (may trigger final flush, already captured) */
    pClosePty(hPty);
    fprintf(stderr, "Pseudo-console closed\n");

    /* cleanup */
    CloseHandle(pi.hProcess);
    CloseHandle(hOutputRead);
    CloseHandle(gReadDone);
    DeleteProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf);
    free(attrBuf);

    fprintf(stderr, "Final output [%zu bytes]: [%.*s]\n", gBufLen, (int)gBufLen, gBuf);

    /* strip trailing CR/LF */
    while (gBufLen > 0 && (gBuf[gBufLen - 1] == '\r' || gBuf[gBufLen - 1] == '\n'))
        gBuf[--gBufLen] = '\0';

    fwrite(gBuf, 1, gBufLen, stdout);
    return (int)exitCode;
}
