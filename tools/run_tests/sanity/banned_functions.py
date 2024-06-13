#!/usr/bin/env python3

# Copyright 2024 gRPC authors.
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

"""Explicitly ban select functions from being used in gRPC."""

import os
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))

# map of banned function signature to allowlist
BANNED_EXCEPT = {
    "gpr_log_severity": [
        "./include/grpc/support/log.h",
        "./src/core/lib/channel/channel_stack.cc",
        "./src/core/lib/channel/channel_stack.h",
        "./src/core/lib/surface/call.h",
        "./src/core/lib/surface/call_log_batch.cc",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./src/php/ext/grpc/php_grpc.c",
        "./src/ruby/ext/grpc/rb_grpc_imports.generated.c",
        "./src/ruby/ext/grpc/rb_grpc_imports.generated.h",
        "./test/core/end2end/tests/no_logging.cc",
    ],
    "gpr_log_severity_string(": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./src/php/ext/grpc/php_grpc.c",
    ],
    # Cleanup remaining before we can ban this.
    # "gpr_log(": [
    # ],
    "gpr_should_log(": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./src/ruby/ext/grpc/rb_call_credentials.c",
        "./test/core/end2end/tests/no_logging.cc",
    ],
    "gpr_log_message(": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
    ],
    "gpr_set_log_verbosity(": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./test/core/end2end/tests/no_logging.cc",
    ],
    "gpr_log_verbosity_init(": [
        "./include/grpc/support/log.h",
        "./src/core/lib/surface/init.cc",
        "./src/core/util/log.cc",
        "./test/core/promise/mpsc_test.cc",
        "./test/core/promise/observable_test.cc",
        "./test/core/resource_quota/memory_quota_fuzzer.cc",
        "./test/core/resource_quota/memory_quota_test.cc",
        "./test/core/resource_quota/periodic_update_test.cc",
        "./test/core/test_util/test_config.cc",
        "./test/core/transport/interception_chain_test.cc",
    ],
    "gpr_log_func_args": [
        "./include/grpc/support/log.h",
        "./src/core/util/android/log.cc",
        "./src/core/util/linux/log.cc",
        "./src/core/util/log.cc",
        "./src/core/util/posix/log.cc",
        "./src/core/util/windows/log.cc",
        "./src/php/ext/grpc/php_grpc.c",
        "./test/core/end2end/tests/no_logging.cc",
        "./test/cpp/interop/stress_test.cc",
    ],
    "gpr_set_log_function(": [
        "./include/grpc/support/log.h",
        "./src/core/util/log.cc",
        "./src/php/ext/grpc/php_grpc.c",
        "./test/core/end2end/tests/no_logging.cc",
        "./test/cpp/interop/stress_test.cc",
    ],
    "gpr_assertion_failed(": [
        "./include/grpc/support/log.h",
        "./src/core/util/log.cc",
    ],
    "GPR_ASSERT(": [
        "./include/grpc/support/log.h",
        "./src/cpp/ext/otel/otel_plugin.cc",
    ],
    "GPR_DEBUG_ASSERT(": [],
}

errors = 0
num_files = 0
for root, dirs, files in os.walk("."):
    if root.startswith("./tools"):
        continue
    if root.startswith("./third_party"):
        continue
    if root.startswith("./src/python"):
        continue
    for filename in files:
        num_files += 1
        path = os.path.join(root, filename)
        if os.path.splitext(path)[1] not in (".h", ".cc", ".c"):
            continue
        with open(path) as f:
            text = f.read()
        # print(path) # DELETE DELETE
        for banned, exceptions in list(BANNED_EXCEPT.items()):
            if path in exceptions:
                continue
            if banned in text:
                print(('Illegal use of "%s" in %s . Use absl functions instead.' % (banned, path)))
                errors += 1

assert errors == 0
if errors > 0:
    print(('Number of errors : %d ' % (errors)))

# This check comes about from this issue:
# https://github.com/grpc/grpc/issues/15381
# Basically, a change rendered this script useless and we did not realize it.
# This check ensures that this type of issue doesn't occur again.

# print(('Number of files checked : %d ' % (num_files)))
assert num_files > 18000  # we have more files
