#include "src/core/ext/transport/chttp2/transport/frame_window_update.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/grpc_check.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace chttp2 {
namespace {

class WindowOverflowTest : public ::testing::Test {
 protected:
  void SetUp() override {
    memory_owhttp2-window-update-overflowner_ = std::make_unique<MemoryOwner>(
        ResourceQuota::Default()->memory_quota()->CreateMemoryOwner());
  }
  std::unique_ptr<MemoryOwner> memory_owner_;
};

TEST_F(WindowOverflowTest, TransportWindowOverflow) {
  ExecCtx exec_ctx;
  // Initialize transport with default window (65535)
  TransportFlowControl tfc("test", true, memory_owner_.get());
  grpc_chttp2_transport t; 
  // We only need flow_control and a few other fields for the parser to work
  // but initializing the whole struct is safer.
  // Actually, for a unit test, we can mock the parts we need.
  
  // Create a fake transport manually to avoid full initialization complexity
  memset(&t, 0, sizeof(t));
  new (&t.flow_control) TransportFlowControl("test", true, memory_owner_.get());
  
  grpc_chttp2_window_update_parser parser;
  grpc_chttp2_window_update_parser_begin_frame(&parser, 4, 0, t.settings.mutable_peer());
  
  // 2^31 - 1 update = 0x7fffffff
  uint8_t data[] = {0x7f, 0xff, 0xff, 0xff};
  grpc_slice slice = grpc_slice_from_static_buffer(data, 4);
  
  // Current window is 65535. Adding 2^31-1 will overflow.
  auto err = grpc_chttp2_window_update_parser_parse(&parser, &t, nullptr, slice, 1);
  
  EXPECT_FALSE(err.ok());
  EXPECT_EQ(err.message(), "FLOW_CONTROL_ERROR: window overflow");
  
  t.flow_control.~TransportFlowControl();
}

}  // namespace
}  // namespace chttp2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
