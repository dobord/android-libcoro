#!/usr/bin/env python3
import json, sys

if len(sys.argv) != 3:
    print("usage: clang_tidy_nofconcepts.py <in_compile_commands.json> <out.json>")
    sys.exit(2)

with open(sys.argv[1], 'r') as f:
    db = json.load(f)

for entry in db:
    cmd = entry.get('command')
    if cmd:
        # Remove problematic -fconcepts and -fcoroutines for clang-tidy parsing
        parts = cmd.split()
        parts = [p for p in parts if p not in ('-fconcepts', '-fcoroutines')]
        entry['command'] = ' '.join(parts)
    else:
        args = entry.get('arguments') or []
        args = [a for a in args if a not in ('-fconcepts', '-fcoroutines')]
        entry['arguments'] = args

with open(sys.argv[2], 'w') as f:
    json.dump(db, f)
