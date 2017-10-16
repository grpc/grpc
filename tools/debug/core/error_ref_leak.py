#!/bin/python

import sys
import re

data = sys.stdin.readlines()

errs = []
for line in data:
  if re.search(r'error.cc', line):
    line = line.partition('error.cc:')[-1]
    line = re.sub(r'\d+] ', r'', line)
    line = line.strip().split()
    err = line[0].strip(":")
    if line[1] == "create":
      assert(err not in errs)
      errs.append(err)
    elif line[0] == "realloc":
      errs.remove(line[1])
      errs.append(line[3])
    elif line[1] == "1" and line[3] == "0":
      # print line
      # print err, errs
      assert(err in errs)
      errs.remove(err)

print "leaked:", errs
