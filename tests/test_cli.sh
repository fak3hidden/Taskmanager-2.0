#!/bin/sh
# integration tests for the command-line surface of the binary
BIN=${1:-build/taskmgr}; fail=0
t() { if "$@" >/dev/null 2>&1; then echo "  ok   $*"; else echo "  FAIL $*"; fail=1; fi; }
echo "cli tests ($BIN)"
t sh -c "$BIN --help | grep -q usage"
t sh -c "$BIN --dump | grep -q '^Task Manager'"
t sh -c "$BIN --dump | grep -q '^CPU:'"
t sh -c "$BIN --dump | grep -q 'taskmgr'"
t sh -c "$BIN --dump --tab 2 | grep -q 'Command line'"
t sh -c "$BIN --screenshot /tmp/tm_cli.bmp --size 640x400 && [ \$(stat -c %s /tmp/tm_cli.bmp) -eq \$((54 + 640*3*400)) ]"
t sh -c "$BIN --screenshot /tmp/tm_cli.bmp --tab 1 --size 800x500"
t sh -c "$BIN --screenshot /tmp/tm_cli.bmp --theme dark --size 640x400"
t sh -c "! $BIN --bogus"
t sh -c "! $BIN --theme neon"
t sh -c "[ \$(stat -c %s $BIN) -lt 1048576 ]"   # well under 1 MB
rm -f /tmp/tm_cli.bmp
[ $fail = 0 ] && echo "cli tests passed"
exit $fail
