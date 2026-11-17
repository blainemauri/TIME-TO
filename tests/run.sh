#!/usr/bin/env bash
# Build and run the host unit tests for the scheduling core, then check the
# JavaScript port (src/common/ttrows.js) gives the same answers as the C.
# Usage: tests/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."

tmp="${TMPDIR:-/tmp}"
cc="${CC:-cc}"

"$cc" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -o "$tmp/tt_test_core" tests/test_core.c src/c/core/*.c
"$cc" -std=c11 -Wall -Wextra -Werror -O1 -o "$tmp/tt_vectors" tests/vectors.c src/c/core/*.c

status=0
for tz in UTC America/New_York Europe/Berlin Asia/Kolkata Pacific/Auckland; do
  TZ="$tz" "$tmp/tt_test_core" || status=1
  if command -v node >/dev/null; then
    TZ="$tz" "$tmp/tt_vectors" | TZ="$tz" node tests/check_js.js || status=1
    if [ -f src/generated/plugin.js ]; then TZ="$tz" node tests/test_plugin.js || status=1; fi
  fi
done
exit $status
