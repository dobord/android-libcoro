#!/usr/bin/env bash
set -euo pipefail

# Check C/C++ formatting with clang-format according to .clang-format

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format not found in PATH" >&2
  exit 127
fi

mapfile -t FILES < <(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(third_party/|build/)' || true)
if [ ${#FILES[@]} -eq 0 ]; then
  echo "No C/C++ source files to check."
  exit 0
fi

echo "Checking clang-format on ${#FILES[@]} files..."
# --dry-run (-n) with -Werror will fail on diffs without modifying files
if ! clang-format -n -Werror "${FILES[@]}"; then
  echo
  echo "clang-format check failed. To fix locally, run:"
  echo "  clang-format -i ${FILES[*]}"
  exit 1
fi

echo "clang-format check passed."
