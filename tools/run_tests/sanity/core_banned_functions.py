#!/usr/bin/env python3

# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Explicitly ban select functions from being used in src/core/**.

Most of these functions have internal versions that should be used instead."""

import os
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

# map of banned function signature to allowlist
BANNED_EXCEPT = {
    'grpc_slice_from_static_buffer(': ['src/core/lib/slice/slice.cc'],
    'grpc_resource_quota_ref(': ['src/core/lib/iomgr/resource_quota.cc'],
    'grpc_resource_quota_unref(': [
        'src/core/lib/iomgr/resource_quota.cc', 'src/core/lib/surface/server.cc'
    ],
    'grpc_slice_buffer_destroy(': ['src/core/lib/slice/slice_buffer.cc'],
    'grpc_slice_buffer_reset_and_unref(': [
        'src/core/lib/slice/slice_buffer.cc'
    ],
    'grpc_slice_ref(': ['src/core/lib/slice/slice.cc'],
    'grpc_slice_unref(': ['src/core/lib/slice/slice.cc'],
    'grpc_error_create(': [
        'src/core/lib/iomgr/error.cc', 'src/core/lib/iomgr/error_cfstream.cc'
    ],
    'grpc_error_ref(': ['src/core/lib/iomgr/error.cc'],
    'grpc_error_unref(': ['src/core/lib/iomgr/error.cc'],
    'grpc_os_error(': ['src/core/lib/iomgr/error.cc'],
    'grpc_wsa_error(': ['src/core/lib/iomgr/error.cc'],
    'grpc_log_if_error(': ['src/core/lib/iomgr/error.cc'],
    'grpc_slice_malloc(': ['src/core/lib/slice/slice.cc'],
    'grpc_call_cancel(': ['src/core/lib/surface/call.cc'],
    'grpc_closure_create(': ['src/core/lib/iomgr/closure.cc'],
    'grpc_closure_init(': ['src/core/lib/iomgr/closure.cc'],
    'grpc_closure_sched(': ['src/core/lib/iomgr/closure.cc'],
    'grpc_closure_run(': ['src/core/lib/iomgr/closure.cc'],
    'grpc_closure_list_sched(': ['src/core/lib/iomgr/closure.cc'],
}

errors = 0
num_files = 0
for root, dirs, files in os.walk('src/core'):
    if root.startswith('src/core/tsi'):
        continue
    for filename in files:
        num_files += 1
        path = os.path.join(root, filename)
        if os.path.splitext(path)[1] != '.cc':
            continue
        with open(path) as f:
            text = f.read()
        for banned, exceptions in BANNED_EXCEPT.items():
            if path in exceptions:
                continue
            if banned in text:
                print('Illegal use of "%s" in %s' % (banned, path))
                errors += 1

assert errors == 0
# This check comes about from this issue:
# https://github.com/grpc/grpc/issues/15381
# Basically, a change rendered this script useless and we did not realize it.
# This dumb check ensures that this type of issue doesn't occur again.
assert num_files > 300  # we definitely have more than 300 files
