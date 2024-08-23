// Copyright 2024 gRPC authors.
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

#include "src/core/ext/filters/gcp_authentication/gcp_authentication_filter.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/useful.h"
#include "test/core/filters/filter_test.h"

namespace grpc_core {
namespace {

class GcpAuthenticationFilterTest : public FilterTest<GcpAuthenticationFilter> {
 protected:
  static RefCountedPtr<ServiceConfig> MakeServiceConfig(
      absl::string_view service_config_json) {
    auto service_config = ServiceConfigImpl::Create(
        ChannelArgs()
            .Set(GRPC_ARG_PARSE_GCP_AUTHENTICATION_METHOD_CONFIG, true),
        service_config_json);
    CHECK(service_config.ok()) << service_config.status();
    return *service_config;
  }

  static RefCountedPtr<XdsConfig> MakeXdsConfig(
      absl::string_view cluster, absl::string_view instance_name,
      // FIXME: how to pass the contents of the metadata map?
      ) {
  }

  ChannelArgs MakeChannelArgs(absl::string_view service_config_json) {
    return ChannelArgs()
               // FIXME: add XdsConfig
               .SetObject(MakeServiceConfig(service_config_json));
  }

  Channel MakeChannelWithServiceConfig(absl::string_view service_config_json) {
    return MakeChannel(MakeChannelArgs(service_config_json)).value();
  }
};

TEST_F(GcpAuthenticationFilterTest, CreateFailsWithoutRequiredChannelArgs) {
  EXPECT_FALSE(
      GcpAuthenticationFilter::Create(ChannelArgs(), ChannelFilter::Args())
          .ok());
}

TEST_F(GcpAuthenticationFilterTest, CreateSucceeds) {
  auto filter = MakeChannel(MakeChannelArgs(absl::OkStatus()));
  EXPECT_TRUE(filter.ok()) << filter.status();
}

TEST_F(GcpAuthenticationFilterTest, CallCredsFails) {
  Call call(MakeChannelWithCallCredsResult(
      absl::UnauthenticatedError("access denied")));
  call.Start(call.NewClientMetadata({{":authority", target()}}));
  EXPECT_EVENT(Finished(
      &call, HasMetadataResult(absl::UnauthenticatedError("access denied"))));
  Step();
}

TEST_F(GcpAuthenticationFilterTest, RewritesInvalidStatusFromCallCreds) {
  Call call(MakeChannelWithCallCredsResult(absl::AbortedError("nope")));
  call.Start(call.NewClientMetadata({{":authority", target()}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::InternalError(
                                   "Illegal status code from call credentials; "
                                   "original status: ABORTED: nope"))));
  Step();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
