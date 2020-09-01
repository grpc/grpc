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

#include "test/cpp/util/cli_flags.h"

namespace grpc {
namespace testing {

// Define all flags used in gRPC cli.
DEFINE_bool(l, false, "Use a long listing format");
DEFINE_bool(remotedb, true, "Use server types to parse and format messages");
DEFINE_string(metadata, "",
              "Metadata to send to server, in the form of key1:val1:key2:val2");
DEFINE_string(proto_path, ".", "Path to look for the proto file.");
DEFINE_string(protofiles, "", "Name of the proto file.");
DEFINE_bool(binary_input, false, "Input in binary format");
DEFINE_bool(binary_output, false, "Output in binary format");
DEFINE_string(
    default_service_config, "",
    "Default service config to use on the channel, if non-empty. Note that "
    "this will be ignored if the name resolver returns a service config.");
DEFINE_bool(display_peer_address, false,
            "Log the peer socket address of the connection that each RPC is "
            "made on to stderr.");
DEFINE_bool(json_input, false, "Input in json format");
DEFINE_bool(json_output, false, "Output in json format");
DEFINE_string(infile, "", "Input file (default is stdin)");
DEFINE_bool(batch, false,
            "Input contains multiple requests. Please do not use this to send "
            "more than a few RPCs. gRPC CLI has very different performance "
            "characteristics compared with normal RPC calls which make it "
            "unsuitable for loadtesting or significant production traffic.");
DEFINE_double(timeout, -1,
              "Specify timeout in seconds, used to set the deadline for all "
              "RPCs. The default value of -1 means no deadline has been set.");

}  // namespace testing
}  // namespace grpc
