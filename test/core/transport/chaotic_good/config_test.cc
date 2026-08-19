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

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
#include "src/core/lib/experiments/config.h"
#include "gtest/gtest.h"

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
    : public chaotic_good::ClientConnectionFactory {
 public:
  chaotic_good::PendingConnection Connect(absl::string_view id) override {
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
  chaotic_good::Config client_config(client_args, /*is_server=*/false);
  chaotic_good::Config server_config(server_args, /*is_server=*/true);
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
  const chaotic_good::TcpFrameTransport::Options client_options =
      client_config.MakeTcpFrameTransportOptions();
  const chaotic_good::TcpFrameTransport::Options server_options =
      server_config.MakeTcpFrameTransportOptions();
  const chaotic_good::MessageChunker client_chunker =
      client_config.MakeMessageChunker();
  const chaotic_good::MessageChunker server_chunker =
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

void SetExperiments(absl::string_view experiments) {
  ConfigVars::Overrides overrides;
  overrides.experiments = experiments;
  ConfigVars::SetOverrides(overrides);
  TestOnlyReloadExperimentsFromConfigVariables();
}

TEST(ConfigTest, ChunkingDisabledByDefault) {
  // Ensure the experiment is disabled (default).
  SetExperiments("-chaotic_good_send_supported_features");

  ChannelArgs args;
  chaotic_good::Config client_config(args, /*is_server=*/false);
  chaotic_good::Config server_config(args, /*is_server=*/true);

  chaotic_good_frame::Settings client_settings;
  client_config.PrepareClientOutgoingSettings(client_settings);

  // Verify that supported features do NOT include CHUNKING (current behavior).
  bool has_chunking = false;
  for (auto feature : client_settings.supported_features()) {
    if (feature == chaotic_good_frame::Settings::CHUNKING) {
      has_chunking = true;
      break;
    }
  }
  EXPECT_FALSE(has_chunking);

  EXPECT_TRUE(
      server_config.ReceiveClientIncomingSettings(client_settings).ok());

  chaotic_good_frame::Settings server_settings;
  server_config.PrepareServerOutgoingSettings(server_settings);

  FakeClientConnectionFactory fake_factory;
  EXPECT_TRUE(
      client_config.ReceiveServerIncomingSettings(server_settings, fake_factory)
          .ok());

  // Verify that max chunk size is 0 because chunking was not negotiated.
  EXPECT_EQ(client_config.max_recv_chunk_size(), 0u);
  EXPECT_EQ(client_config.max_send_chunk_size(), 0u);
  EXPECT_EQ(server_config.max_recv_chunk_size(), 0u);
  EXPECT_EQ(server_config.max_send_chunk_size(), 0u);
}

TEST(ConfigTest, ChunkingEnabledWithExperiment) {
  // Ensure the experiment is enabled.
  SetExperiments("chaotic_good_send_supported_features");

  ChannelArgs args;
  chaotic_good::Config client_config(args, /*is_server=*/false);
  chaotic_good::Config server_config(args, /*is_server=*/true);

  chaotic_good_frame::Settings client_settings;
  client_config.PrepareClientOutgoingSettings(client_settings);

  // Verify that supported features INCLUDE CHUNKING.
  bool has_chunking = false;
  for (auto feature : client_settings.supported_features()) {
    if (feature == chaotic_good_frame::Settings::CHUNKING) {
      has_chunking = true;
      break;
    }
  }
  EXPECT_TRUE(has_chunking);

  EXPECT_TRUE(
      server_config.ReceiveClientIncomingSettings(client_settings).ok());

  chaotic_good_frame::Settings server_settings;
  server_config.PrepareServerOutgoingSettings(server_settings);

  FakeClientConnectionFactory fake_factory;
  EXPECT_TRUE(
      client_config.ReceiveServerIncomingSettings(server_settings, fake_factory)
          .ok());

  // Verify that max chunk size is NOT 0 because chunking was negotiated.
  EXPECT_NE(client_config.max_recv_chunk_size(), 0u);
  EXPECT_NE(client_config.max_send_chunk_size(), 0u);
  EXPECT_NE(server_config.max_recv_chunk_size(), 0u);
  EXPECT_NE(server_config.max_send_chunk_size(), 0u);
}

// Helper function to simulate client-server dynamic handshaking negotiation.
// Returns the settled Resolved data connections count settled on the server,
// and symmetrically asserts that the prepared outgoing Server settings packages
// the exact same settled value.
int32_t NegotiateConnectionLimit(std::optional<int> server_cap,
                                 std::optional<int> client_limit) {
  ChannelArgs server_args;
  if (server_cap.has_value()) {
    server_args =
        server_args.Set(GRPC_ARG_CHAOTIC_GOOD_DATA_CONNECTIONS, *server_cap);
  }
  chaotic_good::Config server_config(server_args, /*is_server=*/true);

  chaotic_good_frame::Settings client_settings;
  if (client_limit.has_value()) {
    client_settings.set_num_data_connections(*client_limit);
  }

  EXPECT_TRUE(
      server_config.ReceiveClientIncomingSettings(client_settings).ok());

  // Symmetrical validation: Verify that the prepared server settings frame
  // response contains the actual finalized dynamic negotiated count!
  chaotic_good_frame::Settings server_settings;
  server_config.PrepareServerOutgoingSettings(server_settings);
  EXPECT_TRUE(server_settings.has_num_data_connections());
  EXPECT_EQ(server_settings.num_data_connections(),
            server_config.num_data_connections());

  return server_config.num_data_connections();
}

struct ConstructorTestCase {
  std::optional<int> options_val;
  bool is_server;
  int32_t expected_settled_connections;
};

class ConfigConstructorModeTest
    : public ::testing::TestWithParam<ConstructorTestCase> {};

TEST_P(ConfigConstructorModeTest, OptionExtractionAsserts) {
  const auto& test_case = GetParam();
  ChannelArgs args;
  if (test_case.options_val.has_value()) {
    args = args.Set(GRPC_ARG_CHAOTIC_GOOD_DATA_CONNECTIONS,
                    *test_case.options_val);
  }
  chaotic_good::Config config(args, test_case.is_server);
  EXPECT_EQ(config.num_data_connections(),
            test_case.expected_settled_connections);
}

INSTANTIATE_TEST_SUITE_P(ConfigConstructorModeTestInstantiation,
                         ConfigConstructorModeTest,
                         ::testing::ValuesIn(std::vector<ConstructorTestCase>{
                             // 1. Client Mode (is_server = false)
                             {5, /*is_server=*/false, 5},
                             {0, /*is_server=*/false, 0},
                             {-5, /*is_server=*/false, 0},
                             {std::nullopt, /*is_server=*/false,
                              std::numeric_limits<int32_t>::max()},
                             // 2. Server Mode (is_server = true)
                             {4, /*is_server=*/true, 4},
                             {-3, /*is_server=*/true, 0},
                             {std::nullopt, /*is_server=*/true, 1},
                         }));

TEST(ConfigTest, SerializedSettingsPacksNumDataConnections) {
  ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_CHAOTIC_GOOD_DATA_CONNECTIONS, 8);
  chaotic_good::Config config(args, /*is_server=*/false);
  chaotic_good_frame::Settings settings;
  config.PrepareClientOutgoingSettings(settings);

  EXPECT_TRUE(settings.has_num_data_connections());
  EXPECT_EQ(settings.num_data_connections(), 8);
}

struct NegotiationTestCase {
  std::optional<int> server_cap;
  std::optional<int> client_limit;
  int32_t expected_negotiated_resolved;
};

class ServerNegotiationLimitTest
    : public ::testing::TestWithParam<NegotiationTestCase> {};

TEST_P(ServerNegotiationLimitTest, NegotiationPermutationsAsserts) {
  const auto& test_case = GetParam();
  EXPECT_EQ(
      NegotiateConnectionLimit(test_case.server_cap, test_case.client_limit),
      test_case.expected_negotiated_resolved);
}

INSTANTIATE_TEST_SUITE_P(ServerNegotiationLimitTestInstantiation,
                         ServerNegotiationLimitTest,
                         ::testing::ValuesIn(std::vector<NegotiationTestCase>{
                             {4, 2, 2},
                             {3, 3, 3},
                             {2, 5, 2},
                             {4, 0, 0},
                             {4, std::nullopt, 4},
                             {std::nullopt, 2, 1},
                             {-1, std::nullopt, 0},
                             {-5, 3, 0},
                             {4, -3, 0},
                         }));

}  // namespace
}  // namespace grpc_core
