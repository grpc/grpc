// Copyright 2023 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/core/ext/transport/chaotic_good/config.h"

#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
#include "test/core/test_util/fuzzing_channel_args.h"
#include "test/core/test_util/fuzzing_channel_args.pb.h"

namespace grpc_core {

void ConfigTest(grpc::testing::FuzzingChannelArgs client_args_proto,
                grpc::testing::FuzzingChannelArgs server_args_proto) {
  // Create channel args
  testing::FuzzingEnvironment client_environment;
  testing::FuzzingEnvironment server_environment;
  const auto client_args = testing::CreateChannelArgsFromFuzzingConfiguration(
      client_args_proto, client_environment);
  const auto server_args = testing::CreateChannelArgsFromFuzzingConfiguration(
      server_args_proto, server_environment);
  // Initialize configs
  chaotic_good::Config client_config(client_args);
  chaotic_good::Config server_config(server_args);
  // Perform handshake
  chaotic_good_frame::Settings client_settings;
  client_config.PrepareOutgoingSettings(client_settings);
  CHECK_OK(server_config.ReceiveIncomingSettings(client_settings));
  chaotic_good_frame::Settings server_settings;
  server_config.PrepareOutgoingSettings(server_settings);
  CHECK_OK(client_config.ReceiveIncomingSettings(server_settings));
  // Generate results
  const chaotic_good::ChaoticGoodTransport::Options client_options =
      client_config.MakeTransportOptions();
  const chaotic_good::ChaoticGoodTransport::Options server_options =
      server_config.MakeTransportOptions();
  const chaotic_good::MessageChunker client_chunker =
      client_config.MakeMessageChunker();
  const chaotic_good::MessageChunker server_chunker =
      server_config.MakeMessageChunker();
  // Validate results
  EXPECT_EQ(client_options.encode_alignment, server_options.decode_alignment);
  EXPECT_EQ(client_options.decode_alignment, server_options.encode_alignment);
  EXPECT_EQ(client_chunker.alignment(), client_options.encode_alignment);
  EXPECT_EQ(server_chunker.alignment(), server_options.encode_alignment);
  EXPECT_LE(server_config.max_recv_chunk_size(),
            client_config.max_send_chunk_size());
  EXPECT_LE(client_config.max_recv_chunk_size(),
            server_config.max_send_chunk_size());
  if (auto a = client_args.GetInt(GRPC_ARG_CHAOTIC_GOOD_ALIGNMENT);
      a.has_value() && *a > 0) {
    EXPECT_EQ(client_options.decode_alignment, *a);
  }
  if (auto a = server_args.GetInt(GRPC_ARG_CHAOTIC_GOOD_ALIGNMENT);
      a.has_value() && *a > 0) {
    EXPECT_EQ(server_options.decode_alignment, *a);
  }
  if (auto a = client_args.GetInt(
          GRPC_ARG_CHAOTIC_GOOD_INLINED_PAYLOAD_SIZE_THRESHOLD);
      a.has_value()) {
    EXPECT_EQ(client_options.inlined_payload_size_threshold, *a);
  }
  if (auto a = server_args.GetInt(
          GRPC_ARG_CHAOTIC_GOOD_INLINED_PAYLOAD_SIZE_THRESHOLD);
      a.has_value()) {
    EXPECT_EQ(server_options.inlined_payload_size_threshold, *a);
  }
}
FUZZ_TEST(MyTestSuite, ConfigTest);

}  // namespace grpc_core
