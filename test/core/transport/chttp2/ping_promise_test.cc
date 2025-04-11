#include "src/core/ext/transport/chttp2/transport/ping_promise.h"

#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/util/notification.h"
#include "src/core/util/time.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

namespace {
using ::grpc_core::http2::KeepAliveSystem;
using ::grpc_core::http2::KeepAliveSystemInterface;

using ::testing::MockFunction;
using ::testing::StrictMock;
using ::testing::WithArgs;
}  // namespace

class MockKeepAliveSystemInterface : public KeepAliveSystemInterface {
 public:
  MOCK_METHOD(Promise<absl::Status>, SendPing, (), (override));
  MOCK_METHOD(Promise<absl::Status>, KeepAliveTimeout, (), (override));
  void ExpectSendPing(int& end_after) {
    EXPECT_CALL(*this, SendPing())
        .Times(end_after)
        .WillRepeatedly([&end_after] {
          if (--end_after == 0) {
            LOG(INFO) << "SendPing returning: " << absl::CancelledError();
            return Immediate(absl::CancelledError());
          }
          LOG(INFO) << "SendPing returning: " << absl::OkStatus();
          return Immediate(absl::OkStatus());
        });
  }
  void ExpectSendPing(Duration duration) {
    EXPECT_CALL(*this, SendPing()).WillOnce([duration] {
      return TrySeq(Sleep(duration),
                    [] { return Immediate(absl::OkStatus()); });
    });
  }

  void ExpectKeepAliveTimeout() {
    EXPECT_CALL(*this, KeepAliveTimeout()).WillOnce([] {
      return (Immediate(absl::OkStatus()));
    });
  }
};

class KeepAliveSystemTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  Party* GetParty() { return party_.get(); }
  void InitParty() {
    auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
    general_party_arena
        ->SetContext<grpc_event_engine::experimental::EventEngine>(
            event_engine().get());
    party_ = Party::Make(std::move(general_party_arena));
  }

 private:
  void InitCoreConfiguration() override {}
  void Shutdown() override { party_.reset(); }

  RefCountedPtr<Party> party_;
};

YODEL_TEST(KeepAliveSystemTest, TestKeepAlive) {
  InitParty();
  int end_after = 2;
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  keep_alive_interface->ExpectSendPing(end_after);

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    Duration::Infinity());
  auto party = GetParty();
  keep_alive_system.Spawn(party, Duration::Seconds(1));

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(KeepAliveSystemTest, TestKeepAliveTimeout) {
  InitParty();
  // int end_after = 1;
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  keep_alive_interface->ExpectKeepAliveTimeout();
  keep_alive_interface->ExpectSendPing(Duration::Hours(1));

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    Duration::Seconds(1));
  auto party = GetParty();
  keep_alive_system.Spawn(party, Duration::Seconds(1));

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

YODEL_TEST(KeepAliveSystemTest, TestGotData) {
  InitParty();
  std::string execution_order;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  StrictMock<MockFunction<void(absl::Status)>> on_done2;
  std::unique_ptr<StrictMock<MockKeepAliveSystemInterface>>
      keep_alive_interface =
          std::make_unique<StrictMock<MockKeepAliveSystemInterface>>();
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(on_done2, Call(absl::OkStatus()));

  KeepAliveSystem keep_alive_system(std::move(keep_alive_interface),
                                    Duration::Hours(1));
  auto party = GetParty();
  Latch<void> latch;
  Latch<void> latch2;

  party->Spawn("WaitForData",
               TrySeq(keep_alive_system.TestOnlyWaitForData(),
                      [&on_done, &execution_order, &latch]() {
                        execution_order.append("3");
                        on_done.Call(absl::OkStatus());
                        latch.Set();
                        return absl::OkStatus();
                      }),
               [](auto) { LOG(INFO) << "Reached KeepAlive end"; });
  party->Spawn("ReadDataAndWaitForData",
               TrySeq(
                   [&keep_alive_system]() {
                     keep_alive_system.GotData();
                     keep_alive_system.GotData();
                     return absl::OkStatus();
                   },
                   [&latch]() { return latch.Wait(); },
                   [&keep_alive_system, &latch2] {
                     keep_alive_system.TestOnlyResetDataReceived();
                     latch2.Set();
                     return keep_alive_system.TestOnlyWaitForData();
                   },
                   [&on_done2] {
                     on_done2.Call(absl::OkStatus());
                     return absl::OkStatus();
                   }),
               [](auto) { LOG(INFO) << "ReadData end"; });

  party->Spawn("ReadData2",
               TrySeq(latch2.Wait(),
                      [&keep_alive_system] {
                        keep_alive_system.GotData();
                        return absl::OkStatus();
                      }),
               [](auto) { LOG(INFO) << "ReadData2 end"; });

  WaitForAllPendingWork();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace grpc_core
