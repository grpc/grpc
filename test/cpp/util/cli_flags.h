/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_TEST_CPP_UTIL_CLI_FLAGS_H
#define GRPC_TEST_CPP_UTIL_CLI_FLAGS_H

#include <gflags/gflags.h>

namespace grpc {
namespace testing {

// Declare all flags used in gRPC cli.
DECLARE_bool(l);
DECLARE_bool(remotedb);
DECLARE_string(metadata);
DECLARE_string(proto_path);
DECLARE_string(protofiles);
DECLARE_bool(binary_input);
DECLARE_bool(binary_output);
DECLARE_string(default_service_config);
DECLARE_bool(display_peer_address);
DECLARE_bool(json_input);
DECLARE_bool(json_output);
DECLARE_string(infile);
DECLARE_bool(batch);
DECLARE_double(timeout);

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_CLI_FLAGS_H
