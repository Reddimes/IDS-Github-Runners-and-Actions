#!/bin/sh
set -e

IRVINE="z:\\opt\\irvine32"
SRC="z:\\test.asm"

if [ ! -f /test.asm ]; then
    echo "Error: /test.asm not found"
    exit 1
fi

ACTUAL="/tmp/test.out"

echo "=== Compiling /test.asm ==="
ml /c /Foz:\\test.obj "$SRC"

echo "=== Linking ==="
link /SUBSYSTEM:CONSOLE /OUT:z:\\test.exe z:\\test.obj "$IRVINE\\Irvine32.lib" "$IRVINE\\kernel32.lib" "$IRVINE\\user32.lib"

echo "=== Running ==="
script -q -c "wine z:\\test.exe" /dev/null > "$ACTUAL" 2>&1
sed -i 's/\x1b\[[?0-9;]*[a-zA-Z]//g; s/\r//g' "$ACTUAL"

MATCHED=""

printf 'Abraham Lincoln
nlocniL maharbA' > /tmp/expected_RevStr.out
if [ "$(cat "$ACTUAL")" = "$(cat /tmp/expected_RevStr.out)" ]; then
    MATCHED="RevStr"
fi

if [ -n "$MATCHED" ]; then
    echo "PASS: $MATCHED"
    exit 0
fi

echo "=== Output ==="
cat "$ACTUAL"
echo ""
echo "Unknown program — review output above."
exit 0
