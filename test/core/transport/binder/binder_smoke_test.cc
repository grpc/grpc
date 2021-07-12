
#include <gtest/gtest.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {
TEST(SmokeTest, Empty) { gpr_log(GPR_INFO, __func__); }
}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
