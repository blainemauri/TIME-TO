#!/usr/bin/env bash
# Build and run the host unit tests for the scheduling core.
# Usage: tests/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."

out="${TMPDIR:-/tmp}/tt_test_core"
cc="${CC:-cc}"

"$cc" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -o "$out" tests/test_core.c src/c/core/*.c

status=0
for tz in UTC America/New_York Europe/Berlin Asia/Kolkata Pacific/Auckland; do
  TZ="$tz" "$out" || status=1
done
exit $status
