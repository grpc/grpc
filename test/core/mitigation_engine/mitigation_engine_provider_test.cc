#include <grpc/grpc.h>

#include <optional>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/mitigation_engine/mitigation_engine.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

class TestMitigationEngine : public MitigationEngine {
 public:
  explicit TestMitigationEngine(bool* destroyed = nullptr)
      : destroyed_(destroyed) {}
  ~TestMitigationEngine() override {
    if (destroyed_ != nullptr) *destroyed_ = true;
  }
  std::optional<Action> EvaluateIncomingConnection(
      absl::string_view /*peer_address*/) override {
    return std::nullopt;
  }
  std::optional<Action> EvaluateIncomingMetadata(
      absl::string_view key, absl::string_view value) override {
    return std::nullopt;
  }
  std::optional<Action> EvaluateAllIncomingMetadata(
      const grpc_metadata_batch& metadata) override {
    return std::nullopt;
  }

 private:
  bool* destroyed_;
};

class TestProvider : public MitigationEngineProvider {
 public:
  void SetEngine(RefCountedPtr<MitigationEngine> engine) {
    engine_ = std::move(engine);
  }
  RefCountedPtr<MitigationEngine> GetEngine() override { return engine_; }

 private:
  RefCountedPtr<MitigationEngine> engine_;
};

TEST(MitigationEngineProviderTest, DefaultReturnsNull) {
  class DefaultProvider : public MitigationEngineProvider {
   public:
    RefCountedPtr<MitigationEngine> GetEngine() override { return nullptr; }
  };
  auto provider = MakeRefCounted<DefaultProvider>();
  EXPECT_EQ(provider->GetEngine(), nullptr);
}

TEST(MitigationEngineProviderTest, MitigationEngineUpdate) {
  // Ensure that the old engine is not destroyed until the last reference is
  // dropped.
  bool destroyed1 = false;
  bool destroyed2 = false;
  auto engine1 = MakeRefCounted<TestMitigationEngine>(&destroyed1);
  auto engine2 = MakeRefCounted<TestMitigationEngine>(&destroyed2);
  void* ptr1 = engine1.get();
  void* ptr2 = engine2.get();

  auto provider = MakeRefCounted<TestProvider>();

  provider->SetEngine(std::move(engine1));
  auto ref_to_engine1 = provider->GetEngine();
  EXPECT_EQ(ref_to_engine1.get(), ptr1);

  // Set a new engine.
  provider->SetEngine(std::move(engine2));
  auto ref_to_engine2 = provider->GetEngine();
  EXPECT_EQ(ref_to_engine2.get(), ptr2);

  // engine1 should still be alive.
  EXPECT_FALSE(destroyed1);
  EXPECT_FALSE(destroyed2);

  // Drop our local ref to engine1, and make sure engine1 is destroyed.
  ref_to_engine1.reset();
  EXPECT_TRUE(destroyed1);
  EXPECT_FALSE(destroyed2);
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
