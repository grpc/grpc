#include "src/core/lib/event_engine/iomgr_engine.h"

#include <grpc/grpc.h>

#include "test/core/event_engine/test_suite/event_engine_test.h"
#include "test/core/util/test_config.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  SetEventEngineFactory([]() {
    return absl::make_unique<
        grpc_event_engine::experimental::IomgrEventEngine>();
  });
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
