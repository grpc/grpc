#!/usr/bin/python3

import sys

changes = 0
for line in sys.stdin:
  if line.startswith('-'):
    changes -= 1
  if line.startswith('+'):
    changes += 1

if changes:
  #print("changes: ", changes)
  exit(1)

exit(0)
