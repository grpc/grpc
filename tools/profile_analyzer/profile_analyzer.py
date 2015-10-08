#!/usr/bin/env python2.7
import json
import collections

data = collections.defaultdict(list)
with open('latency_trace.txt') as f:
  for line in f:
    inf = json.loads(line)
    thd = inf['thd']
    del inf['thd']
    data[thd].append(inf)

print data

