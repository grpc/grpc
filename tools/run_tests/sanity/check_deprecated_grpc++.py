#!/usr/bin/env python

# Copyright 2018 gRPC authors.
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

expected_files = [
    "include/grpc++/create_channel_posix.h", "include/grpc++/server_builder.h",
    "include/grpc++/resource_quota.h", "include/grpc++/create_channel.h",
    "include/grpc++/alarm.h", "include/grpc++/server.h",
    "include/grpc++/server_context.h", "include/grpc++/client_context.h",
    "include/grpc++/server_posix.h", "include/grpc++/grpc++.h",
    "include/grpc++/health_check_service_interface.h",
    "include/grpc++/completion_queue.h", "include/grpc++/channel.h",
    "include/grpc++/support/sync_stream.h", "include/grpc++/support/status.h",
    "include/grpc++/support/config.h",
    "include/grpc++/support/status_code_enum.h",
    "include/grpc++/support/byte_buffer.h",
    "include/grpc++/support/error_details.h",
    "include/grpc++/support/async_unary_call.h",
    "include/grpc++/support/channel_arguments.h",
    "include/grpc++/support/async_stream.h", "include/grpc++/support/slice.h",
    "include/grpc++/support/stub_options.h",
    "include/grpc++/support/string_ref.h", "include/grpc++/support/time.h",
    "include/grpc++/security/auth_metadata_processor.h",
    "include/grpc++/security/credentials.h",
    "include/grpc++/security/server_credentials.h",
    "include/grpc++/security/auth_context.h",
    "include/grpc++/impl/rpc_method.h",
    "include/grpc++/impl/server_builder_option.h", "include/grpc++/impl/call.h",
    "include/grpc++/impl/service_type.h", "include/grpc++/impl/grpc_library.h",
    "include/grpc++/impl/client_unary_call.h",
    "include/grpc++/impl/channel_argument_option.h",
    "include/grpc++/impl/rpc_service_method.h",
    "include/grpc++/impl/method_handler_impl.h",
    "include/grpc++/impl/server_builder_plugin.h",
    "include/grpc++/impl/sync_cxx11.h",
    "include/grpc++/impl/server_initializer.h",
    "include/grpc++/impl/serialization_traits.h",
    "include/grpc++/impl/sync_no_cxx11.h",
    "include/grpc++/impl/codegen/sync_stream.h",
    "include/grpc++/impl/codegen/channel_interface.h",
    "include/grpc++/impl/codegen/config_protobuf.h",
    "include/grpc++/impl/codegen/status.h",
    "include/grpc++/impl/codegen/core_codegen.h",
    "include/grpc++/impl/codegen/config.h",
    "include/grpc++/impl/codegen/core_codegen_interface.h",
    "include/grpc++/impl/codegen/status_code_enum.h",
    "include/grpc++/impl/codegen/metadata_map.h",
    "include/grpc++/impl/codegen/rpc_method.h",
    "include/grpc++/impl/codegen/server_context.h",
    "include/grpc++/impl/codegen/byte_buffer.h",
    "include/grpc++/impl/codegen/async_unary_call.h",
    "include/grpc++/impl/codegen/server_interface.h",
    "include/grpc++/impl/codegen/call.h",
    "include/grpc++/impl/codegen/client_context.h",
    "include/grpc++/impl/codegen/service_type.h",
    "include/grpc++/impl/codegen/grpc_library.h",
    "include/grpc++/impl/codegen/async_stream.h",
    "include/grpc++/impl/codegen/slice.h",
    "include/grpc++/impl/codegen/client_unary_call.h",
    "include/grpc++/impl/codegen/proto_utils.h",
    "include/grpc++/impl/codegen/stub_options.h",
    "include/grpc++/impl/codegen/rpc_service_method.h",
    "include/grpc++/impl/codegen/method_handler_impl.h",
    "include/grpc++/impl/codegen/string_ref.h",
    "include/grpc++/impl/codegen/completion_queue_tag.h",
    "include/grpc++/impl/codegen/call_hook.h",
    "include/grpc++/impl/codegen/completion_queue.h",
    "include/grpc++/impl/codegen/serialization_traits.h",
    "include/grpc++/impl/codegen/create_auth_context.h",
    "include/grpc++/impl/codegen/time.h",
    "include/grpc++/impl/codegen/security/auth_context.h",
    "include/grpc++/ext/health_check_service_server_builder_option.h",
    "include/grpc++/ext/proto_server_reflection_plugin.h",
    "include/grpc++/generic/async_generic_service.h",
    "include/grpc++/generic/generic_stub.h",
    "include/grpc++/test/mock_stream.h",
    "include/grpc++/test/server_context_test_spouse.h"
]

file_template = '''/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

// DEPRECATED: The headers in include/grpc++ are deprecated. Please include the
// headers in include/grpcpp instead. This header exists only for backwards
// compatibility.

#ifndef GRPCXX_FILE_PATH_NAME_UPPER
#define GRPCXX_FILE_PATH_NAME_UPPER

#include <grpcpp/FILE_PATH_NAME_LOWER>

#endif  // GRPCXX_FILE_PATH_NAME_UPPER
'''

errors = 0

path_files = []
for root, dirs, files in os.walk('include/grpc++'):
    for filename in files:
        path_file = os.path.join(root, filename)
        path_files.append(path_file)

if path_files.sort() != expected_files.sort():
    diff_plus = [file for file in path_files if file not in expected_files]
    diff_minus = [file for file in expected_files if file not in path_files]
    for file in diff_minus:
        print('- ', file)
    for file in diff_plus:
        print('+ ', file)
    errors += 1

if errors > 0:
    sys.exit(errors)

for path_file in expected_files:
    relative_path_file = path_file.split('/', 2)[2]

    replace_lower = relative_path_file.replace('+', 'p')

    replace_upper = relative_path_file.replace('/', '_')
    replace_upper = replace_upper.replace('.', '_')
    replace_upper = replace_upper.upper().replace('+', 'X')

    expected_content = file_template.replace('FILE_PATH_NAME_LOWER',
                                             replace_lower)
    expected_content = expected_content.replace('FILE_PATH_NAME_UPPER',
                                                replace_upper)

    path_file_expected = path_file + '.expected'
    with open(path_file_expected, "w") as fo:
        fo.write(expected_content)

    if 0 != os.system('diff %s %s' % (path_file_expected, path_file)):
        print('Difference found in file:', path_file)
        errors += 1

    os.remove(path_file_expected)

check_extensions = [".h", ".cc", ".c", ".m"]

for root, dirs, files in os.walk('src'):
    for filename in files:
        path_file = os.path.join(root, filename)
        for ext in check_extensions:
            if path_file.endswith(ext):
                try:
                    with open(path_file, "r") as fi:
                        content = fi.read()
                        if '#include <grpc++/' in content:
                            print(
                                'Failed: invalid include of deprecated headers in include/grpc++ in %s'
                                % path_file)
                            errors += 1
                except IOError:
                    pass

sys.exit(errors)
