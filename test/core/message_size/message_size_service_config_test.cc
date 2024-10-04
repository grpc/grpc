//
// Copyright 2019 gRPC authors.
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
//

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <stddef.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/service_config/service_config_parser.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {

class MessageSizeParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    parser_index_ =
        CoreConfiguration::Get().service_config_parser().GetParserIndex(
            "message_size");
  }

  size_t parser_index_;
};

TEST_F(MessageSizeParserTest, Valid) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"maxRequestMessageBytes\": 1024,\n"
      "    \"maxResponseMessageBytes\": 1024\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = static_cast<MessageSizeParsedConfig*>(
      ((*vector_ptr)[parser_index_]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_send_size(), 1024U);
  EXPECT_EQ(parsed_config->max_recv_size(), 1024U);
}

TEST_F(MessageSizeParserTest, InvalidMaxRequestMessageBytes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"maxRequestMessageBytes\": -1024\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].maxRequestMessageBytes "
            "error:failed to parse non-negative number]")
      << service_config.status();
}

TEST_F(MessageSizeParserTest, InvalidMaxResponseMessageBytes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"maxResponseMessageBytes\": {}\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].maxResponseMessageBytes "
            "error:is not a number]")
      << service_config.status();
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
