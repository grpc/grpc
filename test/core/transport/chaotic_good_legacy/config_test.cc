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

#include "src/core/ext/transport/chaotic_good_legacy/config.h"

#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"

namespace grpc_core {
namespace {

// Arguments to a config class - these represent channel args, but in a format
// that the fuzzer can search really quickly.
// We then convert this struct to channel args, and feed it into the config to
// keep things as they would be in production code.
struct FuzzerChannelArgs {
  std::optional<int> alignment;
  std::optional<int> max_recv_chunk_size;
  std::optional<int> max_send_chunk_size;
  std::optional<int> inlined_payload_size_threshold;
  std::optional<bool> tracing_enabled;

  ChannelArgs MakeChannelArgs() {
    ChannelArgs out;
    auto transfer = [&out](auto value, const char* name) {
      if (!value.has_value()) return;
      out = out.Set(name, *value);
    };
    transfer(alignment, GRPC_ARG_CHAOTIC_GOOD_ALIGNMENT);
    transfer(max_recv_chunk_size, GRPC_ARG_CHAOTIC_GOOD_MAX_RECV_CHUNK_SIZE);
    transfer(max_send_chunk_size, GRPC_ARG_CHAOTIC_GOOD_MAX_SEND_CHUNK_SIZE);
    transfer(inlined_payload_size_threshold,
             GRPC_ARG_CHAOTIC_GOOD_INLINED_PAYLOAD_SIZE_THRESHOLD);
    transfer(tracing_enabled, GRPC_ARG_TCP_TRACING_ENABLED);
    return out;
  }
};

class FakeClientConnectionFactory
    : public chaotic_good_legacy::ClientConnectionFactory {
 public:
  chaotic_good_legacy::PendingConnection Connect(
      absl::string_view id) override {
    Crash("Connect not implemented");
  }
  void Orphaned() override {}
};

void ConfigTest(FuzzerChannelArgs client_args_input,
                FuzzerChannelArgs server_args_input) {
  // Create channel args
  const auto client_args = client_args_input.MakeChannelArgs();
  const auto server_args = server_args_input.MakeChannelArgs();
  // Initialize configs
  chaotic_good_legacy::Config client_config(client_args);
  chaotic_good_legacy::Config server_config(server_args);
  VLOG(2) << "client_config: " << client_config;
  VLOG(2) << "server_config: " << server_config;
  // Perform handshake
  chaotic_good_frame::Settings client_settings;
  client_config.PrepareClientOutgoingSettings(client_settings);
  VLOG(2) << "client settings: " << client_settings.ShortDebugString();
  CHECK_OK(server_config.ReceiveClientIncomingSettings(client_settings));
  VLOG(2) << "server_config': " << server_config;
  chaotic_good_frame::Settings server_settings;
  server_config.PrepareServerOutgoingSettings(server_settings);
  VLOG(2) << "server settings: " << server_settings.ShortDebugString();
  FakeClientConnectionFactory fake_factory;
  CHECK_OK(client_config.ReceiveServerIncomingSettings(server_settings,
                                                       fake_factory));
  VLOG(2) << "client_config': " << client_config;
  // Generate results
  const chaotic_good_legacy::ChaoticGoodTransport::Options client_options =
      client_config.MakeTransportOptions();
  const chaotic_good_legacy::ChaoticGoodTransport::Options server_options =
      server_config.MakeTransportOptions();
  const chaotic_good_legacy::MessageChunker client_chunker =
      client_config.MakeMessageChunker();
  const chaotic_good_legacy::MessageChunker server_chunker =
      server_config.MakeMessageChunker();
  // Validate results
  EXPECT_EQ(client_options.encode_alignment, server_options.decode_alignment);
  EXPECT_EQ(client_options.decode_alignment, server_options.encode_alignment);
  EXPECT_EQ(client_chunker.alignment(), client_options.encode_alignment);
  EXPECT_EQ(server_chunker.alignment(), server_options.encode_alignment);
  EXPECT_GE(server_config.max_recv_chunk_size(),
            client_config.max_send_chunk_size());
  EXPECT_GE(client_config.max_recv_chunk_size(),
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
      a.has_value() && *a > 0) {
    EXPECT_EQ(client_options.inlined_payload_size_threshold, *a);
  }
  if (auto a = server_args.GetInt(
          GRPC_ARG_CHAOTIC_GOOD_INLINED_PAYLOAD_SIZE_THRESHOLD);
      a.has_value() && *a > 0) {
    EXPECT_EQ(server_options.inlined_payload_size_threshold, *a);
  }
}
FUZZ_TEST(MyTestSuite, ConfigTest);

}  // namespace
}  // namespace grpc_core
