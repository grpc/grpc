#!/usr/bin/env python

# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from __future__ import print_function

import os
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

# map of banned function signature to whitelist
BANNED_EXCEPT = {
    'grpc_resource_quota_ref(': ['src/core/lib/iomgr/resource_quota.c'],
    'grpc_resource_quota_unref(': ['src/core/lib/iomgr/resource_quota.c'],
    'grpc_slice_buffer_destroy(': ['src/core/lib/slice/slice_buffer.c'],
    'grpc_slice_buffer_reset_and_unref(': ['src/core/lib/slice/slice_buffer.c'],
    'grpc_slice_ref(': ['src/core/lib/slice/slice.c'],
    'grpc_slice_unref(': ['src/core/lib/slice/slice.c'],
    'grpc_error_create(': ['src/core/lib/iomgr/error.c'],
    'grpc_error_ref(': ['src/core/lib/iomgr/error.c'],
    'grpc_error_unref(': ['src/core/lib/iomgr/error.c'],
    'grpc_os_error(': ['src/core/lib/iomgr/error.c'],
    'grpc_wsa_error(': ['src/core/lib/iomgr/error.c'],
    'grpc_log_if_error(': ['src/core/lib/iomgr/error.c'],
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
        print('Illegal use of "%s" in %s' % (banned, path))
        errors += 1

assert errors == 0
