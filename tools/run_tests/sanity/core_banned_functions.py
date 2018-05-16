#!/usr/bin/env python

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
    'grpc_slice_malloc(': ['src/core/lib/slice/slice.c'],
    'grpc_closure_create(': ['src/core/lib/iomgr/closure.c'],
    'grpc_closure_init(': ['src/core/lib/iomgr/closure.c'],
    'grpc_closure_sched(': ['src/core/lib/iomgr/closure.c'],
    'grpc_closure_run(': ['src/core/lib/iomgr/closure.c'],
    'grpc_closure_list_sched(': ['src/core/lib/iomgr/closure.c'],
    'gpr_getenv_silent(': [
        'src/core/lib/gpr/log.c', 'src/core/lib/gpr/env_linux.c',
        'src/core/lib/gpr/env_posix.c', 'src/core/lib/gpr/env_windows.c'
    ],
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
