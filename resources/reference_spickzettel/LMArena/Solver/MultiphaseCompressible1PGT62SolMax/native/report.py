#!/usr/bin/env python3
"""Build the verification panel from the executable's measurements."""
import json
import subprocess
import sys


run = subprocess.run([sys.argv[1], '--tsv'], check=True, text=True, capture_output=True)
checks = []
for line in run.stdout.splitlines():
    key, label, value, limit, compare, ok = line.split('\t')
    checks.append(dict(group=key, label=label, value=float(value), limit=float(limit), compare=compare, pass_=ok == '1'))
with open(sys.argv[2], 'w') as out:
    json.dump(dict(compiler='g++ / C++20 -O2', checks=checks), out, indent=2)