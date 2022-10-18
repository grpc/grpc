//
// Copyright 2022 gRPC authors.
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

#include "src/core/ext/xds/xds_http_filters.h"

#include <algorithm>
#include <string>
#include <vector>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/wrappers.pb.h>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "upb/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/proto/grpc/testing/xds/v3/fault.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/config_grpc_cli.h"

namespace grpc_core {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::fault::v3::HTTPFault;
using ::envoy::extensions::filters::http::rbac::v3::RBAC;
using ::envoy::extensions::filters::http::router::v3::Router;

class XdsHttpFilterTest : public ::testing::Test {
 protected:
  XdsHttpFilterTest() { XdsHttpFilterRegistry::Init(); }
  ~XdsHttpFilterTest() { XdsHttpFilterRegistry::Shutdown(); }

  XdsExtension MakeXdsExtension(const google::protobuf::Message& message) {
    auto any = std::make_unique<google::protobuf::Any>();
    any->PackFrom(message);
    ValidationErrors::ScopedField field(
        &errors_,
        absl::StrCat(
            "http_filter.value[",
            absl::StripPrefix(any->type_url(), "types.googleapis.com/"), "]"));
    XdsExtension extension;
    extension.type = any->type_url();
    extension.value = absl::string_view(any->value());
    extension.validation_fields.push_back(std::move(field));
    any_storage_.push_back(std::move(any));
    return extension;
  }

  ValidationErrors errors_;
  upb::Arena arena_;
  std::vector<std::unique_ptr<google::protobuf::Any>> any_storage_;
};

TEST_F(XdsHttpFilterTest, Registry) {
  EXPECT_EQ(XdsHttpFilterRegistry::GetFilterForType("does_not_exist"), nullptr);
  auto* router = XdsHttpFilterRegistry::GetFilterForType(
      XdsHttpRouterFilter::StaticConfigName());
  ASSERT_NE(router, nullptr);
}

TEST_F(XdsHttpFilterTest, Router) {
  auto* router_filter = XdsHttpFilterRegistry::GetFilterForType(
      XdsHttpRouterFilter::StaticConfigName());
  ASSERT_NE(router_filter, nullptr);
  Router router;
  XdsExtension extension = MakeXdsExtension(router);
  auto config = router_filter->GenerateFilterConfig(std::move(extension),
                                                    arena_.ptr(), &errors_);
  ASSERT_TRUE(errors_.ok()) << errors_.status("unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, router_filter->ConfigProtoName());
  EXPECT_EQ(config->config, Json()) << config->config.Dump();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
