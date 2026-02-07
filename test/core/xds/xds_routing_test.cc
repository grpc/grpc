//
// Copyright 2026 gRPC authors.
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

#include "src/core/xds/grpc/xds_routing.h"

#include "src/core/filter/filter_chain.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_route_config.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {
namespace testing {
namespace {

class XdsPerRouteFilterChainBuilderTest : public ::testing::Test {
 protected:
  struct FakeFilterChain final : public FilterChain {
    std::vector<FilterAndConfig> filters;
  };

  class FakeFilterChainBuilder final : public FilterChainBuilder {
   public:
    absl::StatusOr<RefCountedPtr<FilterChain>> Build() override {
      return std::move(filter_chain_);
    }

   private:
    void AddFilter(const FilterHandle& filter_handle,
                   RefCountedPtr<const FilterConfig> config) override {
      if (filter_chain_ == nullptr) {
        filter_chain_ = MakeRefCounted<FakeFilterChain>();
      }
      filter_handle.AddToBuilder(&filter_chain_->filters, std::move(config));
    }

    RefCountedPtr<FakeFilterChain> filter_chain_;
  };

};

TEST_F(XdsPerRouteFilterChainBuilderTest, RouteWithoutTypedPerFilterConfig) {
}

}  // namespace
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
