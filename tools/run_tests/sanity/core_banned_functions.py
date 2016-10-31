#!/usr/bin/python

import os
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

# map of banned function signature to whitelist
BANNED_EXCEPT = {
    'grpc_resource_quota_ref(': ('src/core/lib/iomgr/resource_quota.c'),
    'grpc_resource_quota_unref(': ('src/core/lib/iomgr/resource_quota.c'),
    'grpc_slice_buffer_destroy(': ('src/core/lib/slice/slice_buffer.c'),
    'grpc_slice_buffer_reset_and_unref(': ('src/core/lib/slice/slice_buffer.c'),
    'grpc_slice_ref(': ('src/core/lib/slice/slice.c'),
    'grpc_slice_unref(': ('src/core/lib/slice/slice.c'),
}

errors = 0
for root, dirs, files in os.walk('src/core'):
  for filename in files:
    path = os.path.join(root, filename)
    if os.path.splitext(path)[1] != '.c': continue
    with open(path) as f:
      text = f.read()
    for banned, exceptions in BANNED_EXCEPT.items():
      if path in exceptions: continue
      if banned in text:
        print 'Illegal use of "%s" in %s' % (banned, path)
        errors += 1

assert errors == 0

