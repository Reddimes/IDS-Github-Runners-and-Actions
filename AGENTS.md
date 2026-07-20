# MASM Assembly Project — opencode Agent Instructions

## Project Overview
x86 assembly language project using MASM and the Irvine32 library. Two CI paths: native Windows (GitHub Actions) and Linux via Wine (Docker).

## Source Layout
- `src/` — `.asm` files (e.g., `RevStr.asm`)
- `tools/ConptyCapture/ConptyCapture.cpp` — native C++ ConPTY capture helper for Windows CI
- `docker-test/` — single-file Docker compilation container with auto-validation
- `.github/workflows/masm-windows.yml` — native Windows workflow (ConPTY capture)
- `.github/workflows/masm-linux.yml` — Linux/Wine workflow

## Active Goal
Capture `WriteConsoleA` output from `RevStr.exe` in headless GitHub Actions CI. `RevStr.asm` is **read-only** — do not modify it. The approach is a native C++ ConPTY capture tool (`ConptyCapture.cpp`) compiled with MSVC `cl.exe` in CI.

## Key Constraints
- **Never modify `src/RevStr.asm`** — it uses Irvine32 `WriteString` → `WriteConsoleA`, bypassing stdout.
- ConPTY (`CreatePseudoConsole`) is the only reliable way to capture console buffer output in headless Windows CI.
- `ConptyCapture.cpp` must compile with MSVC `cl.exe` (C++ strict typing: explicit casts for `LPPROC_THREAD_ATTRIBUTE_LIST`).
- Windows workflow uses `cmd` shell for compile/link steps (needs `vcvarsall.bat`), `pwsh` for Irvine32 download and test validation.

## Build/Compile Commands
```bash
# Docker (local)
docker build -f docker-test/Dockerfile -t masm-run .
docker run --rm -v src/RevStr.asm:/test.asm masm-run
```

```cmd
# Windows CI — ConptyCapture tool
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
cl /O2 /EHsc /Fe:tools\ConptyCapture\publish\ConptyCapture.exe tools\ConptyCapture\ConptyCapture.cpp
```

## Workflow Polling
- Workflows complete in ~60s. Poll with `sleep 40` then `gh run view <run-id>`.
- Use `gh run view <run-id> --log-failed` to inspect failed step logs.

## Known Issues
- ConPTY pipe handles must be created with `bInheritHandle=TRUE`, then cleared with `SetHandleInformation` after PTY creation.
- MSVC C++ requires explicit `(LPPROC_THREAD_ATTRIBUTE_LIST)` casts on `LPVOID` for proc thread attribute APIs.
- `CreatePseudoConsole` flag `0` (not `PSEUDOCONSOLE_INHERIT_CURSOR`) — cursor handling caused hangs.

## User Identity
Jeff Young <jeffyoung1990@gmail.com>
