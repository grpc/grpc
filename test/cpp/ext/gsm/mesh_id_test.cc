//
//
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
//
//

#include "gtest/gtest.h"

#include <grpcpp/grpcpp.h>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/env.h"
#include "src/cpp/ext/gsm/metadata_exchange.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

class TestScenario {
 public:
  enum class XdsBootstrapSource : std::uint8_t { kFromFile, kFromConfig };

  explicit TestScenario(XdsBootstrapSource bootstrap_source)
      : bootstrap_source_(bootstrap_source) {}

  static std::string Name(const ::testing::TestParamInfo<TestScenario>& info) {
    switch (info.param.bootstrap_source_) {
      case TestScenario::XdsBootstrapSource::kFromFile:
        return "BootstrapFromFile";
      case TestScenario::XdsBootstrapSource::kFromConfig:
        return "BootstrapFromConfig";
    }
  }

  XdsBootstrapSource bootstrap_source() const { return bootstrap_source_; }

 private:
  XdsBootstrapSource bootstrap_source_;
};

class MeshIdTest : public ::testing::TestWithParam<TestScenario> {
 protected:
  void SetBootstrap(const char* bootstrap) {
    switch (GetParam().bootstrap_source()) {
      case TestScenario::XdsBootstrapSource::kFromFile: {
        ASSERT_EQ(bootstrap_file_name_, nullptr);
        FILE* bootstrap_file =
            gpr_tmpfile("gcp_observability_config", &bootstrap_file_name_);
        fputs(bootstrap, bootstrap_file);
        fclose(bootstrap_file);
        grpc_core::SetEnv("GRPC_XDS_BOOTSTRAP", bootstrap_file_name_);
        break;
      }
      case TestScenario::XdsBootstrapSource::kFromConfig:
        grpc_core::SetEnv("GRPC_XDS_BOOTSTRAP_CONFIG", bootstrap);
        break;
    }
  }

  ~MeshIdTest() override {
    grpc_core::UnsetEnv("GRPC_GCP_OBSERVABILITY_CONFIG");
    grpc_core::UnsetEnv("GRPC_XDS_BOOTSTRAP");
    if (bootstrap_file_name_ != nullptr) {
      remove(bootstrap_file_name_);
      gpr_free(bootstrap_file_name_);
    }
  }

  char* bootstrap_file_name_ = nullptr;
};

TEST_P(MeshIdTest, Empty) {
  SetBootstrap("");
  EXPECT_EQ(grpc::internal::GetMeshId(), "unknown");
}

TEST_P(MeshIdTest, NothingSet) {
  EXPECT_EQ(grpc::internal::GetMeshId(), "unknown");
}

TEST_P(MeshIdTest, BadJson) {
  SetBootstrap("{");
  EXPECT_EQ(grpc::internal::GetMeshId(), "unknown");
}

TEST_P(MeshIdTest, UnexpectedMeshIdFormatType1) {
  SetBootstrap(
      "{\"node\": {\"id\": "
      "\"abcdef\"}}");
  EXPECT_EQ(grpc::internal::GetMeshId(), "unknown");
}

TEST_P(MeshIdTest, UnexpectedMeshIdFormatType2) {
  SetBootstrap(
      "{\"node\": {\"id\": "
      "\"projects/1234567890/networks/mesh-id/nodes/"
      "01234567-89ab-4def-8123-456789abcdef\"}}");
  EXPECT_EQ(grpc::internal::GetMeshId(), "unknown");
}

TEST_P(MeshIdTest, Basic) {
  SetBootstrap(
      "{\"node\": {\"id\": "
      "\"projects/1234567890/networks/mesh:mesh-id/nodes/"
      "01234567-89ab-4def-8123-456789abcdef\"}}");
  EXPECT_EQ(grpc::internal::GetMeshId(), "mesh-id");
}

INSTANTIATE_TEST_SUITE_P(
    MeshId, MeshIdTest,
    ::testing::Values(
        TestScenario(TestScenario::XdsBootstrapSource::kFromFile),
        TestScenario(TestScenario::XdsBootstrapSource::kFromConfig)),
    &TestScenario::Name);

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
