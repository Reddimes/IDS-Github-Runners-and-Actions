#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>

typedef BOOL(WINAPI* PFN_WriteConsoleA)(HANDLE, LPCVOID, DWORD, LPDWORD, LPVOID);

static HANDLE gCapturePipe = INVALID_HANDLE_VALUE;
static PFN_WriteConsoleA pRealWriteConsoleA = NULL;
static CRITICAL_SECTION gCs;

BOOL WINAPI HookedWriteConsoleA(HANDLE hConsoleOutput, LPCVOID lpBuffer,
    DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved) {
    EnterCriticalSection(&gCs);
    if (gCapturePipe != INVALID_HANDLE_VALUE && lpBuffer && nNumberOfCharsToWrite > 0) {
        DWORD written;
        WriteFile(gCapturePipe, lpBuffer, nNumberOfCharsToWrite, &written, NULL);
        FlushFileBuffers(gCapturePipe);
    }
    BOOL result = FALSE;
    if (pRealWriteConsoleA) {
        result = pRealWriteConsoleA(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite,
            lpNumberOfCharsWritten, lpReserved);
    } else {
        if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten = nNumberOfCharsToWrite;
        result = TRUE;
    }
    LeaveCriticalSection(&gCs);
    return result;
}

BOOL WINAPI HookedWriteConsoleW(HANDLE hConsoleOutput, LPCVOID lpBuffer,
    DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved) {
    EnterCriticalSection(&gCs);
    if (gCapturePipe != INVALID_HANDLE_VALUE && lpBuffer && nNumberOfCharsToWrite > 0) {
        wchar_t* wbuf = (wchar_t*)lpBuffer;
        char* abuf = (char*)malloc(nNumberOfCharsToWrite * 3);
        if (abuf) {
            int alen = WideCharToMultiByte(CP_UTF8, 0, wbuf, nNumberOfCharsToWrite,
                abuf, nNumberOfCharsToWrite * 3, NULL, NULL);
            if (alen > 0) {
                DWORD written;
                WriteFile(gCapturePipe, abuf, alen, &written, NULL);
                FlushFileBuffers(gCapturePipe);
            }
            free(abuf);
        }
    }
    if (lpNumberOfCharsWritten) *lpNumberOfCharsWritten = nNumberOfCharsToWrite;
    LeaveCriticalSection(&gCs);
    return TRUE;
}

static void DetourFunction(LPVOID* target, LPVOID hook) {
    DWORD oldProtect;
    VirtualProtect(target, 8, PAGE_EXECUTE_READWRITE, &oldProtect);
    *(void**)target = hook;
    VirtualProtect(target, 8, oldProtect, &oldProtect);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void)hModule;
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&gCs);

        /* ---- connect to named pipe ---- */
        for (int retry = 0; retry < 10; retry++) {
            gCapturePipe = CreateFileA("\\\\.\\pipe\\ConptyCapturePipe",
                GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (gCapturePipe != INVALID_HANDLE_VALUE) break;
            Sleep(500);
        }

        if (gCapturePipe == INVALID_HANDLE_VALUE) {
            return TRUE;
        }

        /* ---- hook WriteConsoleA/W ---- */
        HMODULE hKernel = GetModuleHandleA("kernel32.dll");
        if (hKernel) {
            pRealWriteConsoleA = (PFN_WriteConsoleA)GetProcAddress(hKernel, "WriteConsoleA");
            if (pRealWriteConsoleA) {
                DetourFunction((LPVOID*)&pRealWriteConsoleA, (LPVOID)HookedWriteConsoleA);
            }

            void* pRealWCW = GetProcAddress(hKernel, "WriteConsoleW");
            if (pRealWCW) {
                DetourFunction((LPVOID*)&pRealWCW, (LPVOID)HookedWriteConsoleW);
            }
        }
    }
    return TRUE;
}
