#!/usr/bin/env bash
set -euo pipefail

# run.sh — Compile and run an .asm file via docker exec
# Usage: ./run.sh <basename>    (e.g., ./run.sh RevStr)

COMPOSE_FILE="$(cd "$(dirname "$0")" && pwd)/docker-compose.yml"
IRVINE="/opt/irvine32"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <basename>"
    echo "  e.g., $0 RevStr"
    exit 1
fi

BASENAME="$1"
SRC="/app/src/${BASENAME}.asm"
OBJ="/app/src/${BASENAME}.obj"
EXE="/app/src/${BASENAME}.exe"

docker compose -f "$COMPOSE_FILE" up -d

run_in() {
    docker compose -f "$COMPOSE_FILE" exec -t masm-test "$@"
}

echo "=== Compiling ${BASENAME}.asm ==="
run_in ml /c /Fo"$OBJ" "$SRC"

echo "=== Linking ${BASENAME}.obj ==="
run_in link /SUBSYSTEM:CONSOLE /OUT:"$EXE" "$OBJ" "$IRVINE/Irvine32.lib" "$IRVINE/kernel32.lib" "$IRVINE/user32.lib"

echo "=== Running ${BASENAME}.exe ==="
run_in wine "$EXE"
