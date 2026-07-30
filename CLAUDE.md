# testai — x86 MASM Assembly Project

## What This Is
An x86 assembly language project using MASM (Microsoft Macro Assembler) and the Irvine32 library. Mount a single `.asm` file to `/test.asm` and the container compiles, runs, and validates it automatically.

## Current Setup
- **Source:** `src/` — `.asm` files mounted individually to `/test.asm`
- **CI:** Two GitHub Actions workflows (native Windows + Linux via Wine) — both passing on manual dispatch
- **Docker:** `docker-test/` — single-file compilation container with auto-validation

## Dockerfile Layer Order (cache-optimized)
1. **apt packages** (rarely changes)
2. **Wine prefix init** (fast, ~5s)
3. **MSVC install** (~60s, cached unless L1/L2 change)
4. **Irvine32 + SDK libs + sed patch** (fast, ~2s, single layer)
5. **WORKDIR/ENV/CMD** (instant)

## Testing

### Quick start
```bash
# Build once
docker build -f docker-test/Dockerfile -t masm-run .

# Compile, run, and validate any .asm file
docker run --rm -v src/RevStr.asm:/test.asm masm-run
# → PASS: RevStr
```

### Adding a new test
Edit `docker-test/entrypoint.sh` and add a new expected output block:
```sh
printf 'expected output line1
expected output line2' > /tmp/expected_NewProg.out
if [ "$(cat "$ACTUAL")" = "$(cat /tmp/expected_NewProg.out)" ]; then
    MATCHED="NewProg"
fi
```

### Manual docker exec
```bash
# Compile — ml requires z: paths (/ is interpreted as a flag)
docker run --rm --entrypoint sh masm-run -c 'ml /c /Foz:\\test.obj z:\\test.asm'

# Link — must pass Irvine32 libs explicitly
docker run --rm --entrypoint sh masm-run -c 'link /SUBSYSTEM:CONSOLE /OUT:z:\\test.exe z:\\test.obj z:\\opt\\irvine32\\Irvine32.lib z:\\opt\\irvine32\\kernel32.lib z:\\opt\\irvine32\\user32.lib'

# Run — requires script for pty allocation
docker run --rm --entrypoint sh masm-run -c 'script -q -c "wine z:\\test.exe" /dev/null'
```

### Key limitation
Wine's `ReadConsoleA`/`WriteConsoleA` produce no stdout without a pty. The `script` command allocates one, and `sed` strips ANSI escape codes and carriage returns from the captured output. This is the only reliable way to get console output in headless Docker.

### Key Paths Inside Container
| Path | Contents |
|---|---|
| `/opt/msvc/bin/x86/` | Wrapper scripts (`ml`, `link`) + actual `.exe` binaries |
| `/opt/irvine32/` | Irvine32 library (`.inc`, `.lib`, `kernel32.lib`, `user32.lib`) |
| `/opt/msvc/bin/x86/msvcenv.sh` | Env script sourced by wrappers; patched with `z:\opt\irvine32` paths |
| `/test.asm` | Mounted source file (volume mount at runtime) |

### Legacy docker-compose (deprecated)
The `docker-test/docker-compose.yml` and `docker-test/run.sh` are legacy. The single-file container approach is the current standard. They remain for reference but are not actively maintained.

## Windows Workflow Notes (`masm-windows.yml`)
- **Runner:** `windows-latest` (VS 2026 / v18, MASM 14.29)
- **`ml.exe` path:** `C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\MSVC\14.29.30133\bin\HostX64\x86\ml.exe` (hardcoded — `setup-masm` doesn't put it on cmd PATH)
- **Must source `vcvarsall.bat x86`** before `link.exe` so linker finds kernel32/user32 SDK libs
- **Irvine32:** Downloaded via pwsh step to `C:\Irvine32`; set `INCLUDE=C:\Irvine32` env var for `ml.exe`
- **`ml.exe` defaults `.obj` to repo root** (not source dir) when no `/Fo` is given
- **Shells:** `pwsh` for Irvine32 download (needs `$env:TEMP`), `cmd` for compile/link (needs `vcvarsall.bat` env)

## Workflow Polling
- Workflows complete in ~60s. Poll with `sleep 40` then `gh run view`.

## User Identity
- Jeff Young <jeffyoung1990@gmail.com>
