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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/service_config/service_config_parser.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

// A regular expression to enter referenced or child errors.
#define CHILD_ERROR_TAG ".*children.*"

class MessageSizeParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    builder_ = std::make_unique<CoreConfiguration::WithSubstituteBuilder>(
        [](CoreConfiguration::Builder* builder) {
          builder->service_config_parser()->RegisterParser(
              absl::make_unique<MessageSizeParser>());
        });
    EXPECT_EQ(CoreConfiguration::Get().service_config_parser().GetParserIndex(
                  "message_size"),
              0);
  }

 private:
  std::unique_ptr<CoreConfiguration::WithSubstituteBuilder> builder_;
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
  auto parsed_config =
      static_cast<MessageSizeParsedConfig*>(((*vector_ptr)[0]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->limits().max_send_size, 1024);
  EXPECT_EQ(parsed_config->limits().max_recv_size, 1024);
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
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing message size method parameters:.*"
                  "Message size parser" CHILD_ERROR_TAG
                  "field:maxRequestMessageBytes error:should be non-negative"));
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
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing message size method parameters:.*"
                  "Message size parser" CHILD_ERROR_TAG
                  "field:maxResponseMessageBytes error:should be of type "
                  "number"));
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
