#!/usr/bin/env bash
set -euo pipefail

HOOKS_DIR=".git/hooks"
mkdir -p "$HOOKS_DIR"
cp -f scripts/pre-commit "$HOOKS_DIR/pre-commit"
chmod +x "$HOOKS_DIR/pre-commit"
echo "Installed pre-commit hook."
