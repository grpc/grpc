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


# Explicitly ban select functions from being used in gRPC.
# Any new instance of a deprecated function being used in the code will be
# flagged by the script. If there is a new instance of a deprecated function in
# a Pull Request, then the Sanity tests will fail for the Pull Request.
# The allow list has a list of files where clean up of deprecated functions is
# pending.

import os
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), "../../.."))

# More files may be added to the RUBY_PHP_ALLOW_LIST
# if they belong to the PHP or RUBY folder.
RUBY_PHP_ALLOW_LIST = [
    "./include/grpc/support/log.h",
    "./src/core/util/log.cc",
    "./src/php/ext/grpc/call_credentials.c",
    "./src/php/ext/grpc/channel.c",
    "./src/ruby/ext/grpc/rb_call.c",
    "./src/ruby/ext/grpc/rb_call_credentials.c",
    "./src/ruby/ext/grpc/rb_channel.c",
    "./src/ruby/ext/grpc/rb_event_thread.c",
    "./src/ruby/ext/grpc/rb_grpc.c",
    "./src/ruby/ext/grpc/rb_server.c",
]

#  Map of deprecated functions to allowlist files
DEPRECATED_FUNCTION_TEMP_ALLOW_LIST = {
    # These experimental logging functions are only for php and ruby.
    "grpc_absl_log(": RUBY_PHP_ALLOW_LIST,
    "grpc_absl_log_int(": RUBY_PHP_ALLOW_LIST,
    "grpc_absl_log_str(": RUBY_PHP_ALLOW_LIST,
    # These have been deprecated.
    # Most of these have been deleted.
    # Putting this check here just to prevent people from
    # submitting PRs with any of these commented out.
    "gpr_assertion_failed": [],  # Safe to delete this entry after Nov 2024.
    "gpr_log(": [],  # Safe to delete this entry after Nov 2024.
    "gpr_log_func_args": [],  # Safe to delete this entry after Nov 2024.
    "gpr_log_message": [],  # Safe to delete this entry after Nov 2024.
    "gpr_log_severity_string": [],  # Safe to delete this entry after Nov 2024.
    "gpr_set_log_function": [],  # Safe to delete this entry after Nov 2024.
    "gpr_set_log_verbosity": [],  # Safe to delete this entry after Nov 2024.
    "gpr_should_log": [],  # Safe to delete this entry after Nov 2024.
    "GPR_ASSERT": [],  # Safe to delete this entry after Nov 2024.
    "GPR_DEBUG_ASSERT": [],  # Safe to delete this entry after Nov 2024.
}

errors = 0
num_files = 0
for root, dirs, files in os.walk("."):
    for filename in files:
        num_files += 1
        path = os.path.join(root, filename)
        if os.path.splitext(path)[1] not in (".h", ".cc", ".c"):
            continue
        with open(path) as f:
            text = f.read()
        for deprecated, allowlist in list(
            DEPRECATED_FUNCTION_TEMP_ALLOW_LIST.items()
        ):
            if path in allowlist:
                continue
            if deprecated in text:
                print(
                    (
                        'Illegal use of "%s" in %s. Use absl functions instead.'
                        % (deprecated, path)
                    )
                )
                errors += 1

assert errors == 0
if errors > 0:
    print(("Number of errors : %d " % (errors)))

# This check comes about from this issue:
# https://github.com/grpc/grpc/issues/15381
# Basically, a change rendered this script useless and we did not realize it.
# This check ensures that this type of issue doesn't occur again.
assert num_files > 18000
print("Number of files checked : %d " % (num_files))
