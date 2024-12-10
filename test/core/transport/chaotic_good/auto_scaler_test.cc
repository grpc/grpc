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

#include "src/core/ext/transport/chaotic_good/auto_scaler.h"

#include <grpc/grpc.h>

#include <memory>

#include "absl/random/random.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/race.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

class AutoScalerTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  class RunLoop {
   public:
    explicit RunLoop(AutoScalerTest* test) : test_(test) {
      test_->SpawnTestSeqWithoutContext("control_loop", [this, test = test_]() {
        return Race(std::exchange(test->auto_scaler_, nullptr)->ControlLoop(),
                    [this]() -> Poll<Empty> {
                      if (!done_) {
                        waker_ = GetContext<Activity>()->MakeNonOwningWaker();
                        return Pending{};
                      }
                      return Empty{};
                    });
      });
    }
    ~RunLoop() {
      done_ = true;
      waker_.WakeupAsync();
      test_->WaitForAllPendingWork();
    }

    void ExpectAddConnection(SourceLocation whence = {}) {
      LOG(INFO) << whence << " ExpectAddConnection";
      test_->TickUntilDone(test_->subject_->Expect<ExpectedAddConnection>());
    }

    void ExpectRemoveConnection(SourceLocation whence = {}) {
      LOG(INFO) << whence << " ExpectRemoveConnection";
      test_->TickUntilDone(test_->subject_->Expect<ExpectedRemoveConnection>());
    }

    void ExpectMeasureOverallLatency(TDigest client, TDigest server,
                                     SourceLocation whence = {}) {
      LOG(INFO) << whence << " ExpectMeasureOverallLatency:\nclient: "
                << client.Quantile(0.5) << "\nserver: " << server.Quantile(0.5);
      test_->TickUntilDone(
          test_->subject_->Expect<ExpectedMeasureOverallLatency>(
              std::move(client), std::move(server)));
    }

    void ExpectMeasurePerConnectionLatency(
        absl::flat_hash_map<uint32_t, chaotic_good::AutoScaler::Metrics>
            metrics,
        SourceLocation whence = {}) {
      LOG(INFO) << whence << " ExpectMeasurePerConnectionLatency";
      test_->TickUntilDone(
          test_->subject_->Expect<ExpectedMeasurePerConnectionLatency>(
              std::move(metrics)));
    }

    void ExpectParkConnection(uint32_t id, SourceLocation whence = {}) {
      LOG(INFO) << whence << " ExpectParkConnection " << id;
      test_->TickUntilDone(test_->subject_->Expect<ExpectedParkConnection>(id));
    }

    void ExpectUnparkConnection(uint32_t id, SourceLocation whence = {}) {
      LOG(INFO) << whence << " ExpectUnparkConnection " << id;
      test_->TickUntilDone(
          test_->subject_->Expect<ExpectedUnparkConnection>(id));
    }

    std::vector<uint32_t> ListActiveConnections() {
      return test_->subject_->ListActiveConnections();
    }

   private:
    AutoScalerTest* const test_;
    bool done_ = false;
    Waker waker_;
  };

  static TDigest RandomDigest(double median, double stddev,
                              size_t samples = 1000) {
    absl::BitGen gen;
    TDigest digest(chaotic_good::AutoScaler::Metrics::compression());
    for (size_t i = 0; i < samples; i++) {
      digest.Add(absl::Gaussian<double>(gen, median, stddev));
    }
    return digest;
  }

 private:
  class Notifier {
   public:
    void Done() { *done_ = true; }
    bool IsDone() const { return *done_; }

   private:
    std::shared_ptr<bool> done_ = std::make_shared<bool>(false);
  };

  class ExpectedOp {
   public:
    virtual ~ExpectedOp() { notifier_.Done(); }

    virtual void AddConnection() { Crash("unexpected AddConnection"); }
    virtual void RemoveConnection() { Crash("unexpected RemoveConnection"); }
    virtual void ParkConnection(uint32_t) {
      Crash("unexpected ParkConnection");
    }
    virtual void UnparkConnection(uint32_t) {
      Crash("unexpected UnparkConnection");
    }
    virtual chaotic_good::AutoScaler::Metrics MeasureOverallLatency() {
      Crash("unexpected MeasureOverallLatency");
    }
    virtual absl::flat_hash_map<uint32_t, chaotic_good::AutoScaler::Metrics>
    MeasurePerConnectionLatency() {
      Crash("unexpected MeasurePerConnectionLatency");
    }

    Notifier notifier() { return notifier_; }

   private:
    Notifier notifier_;
  };

  class ExpectedMeasureOverallLatency final : public ExpectedOp {
   public:
    ExpectedMeasureOverallLatency(TDigest client, TDigest server)
        : client_(std::move(client)), server_(std::move(server)) {}

    chaotic_good::AutoScaler::Metrics MeasureOverallLatency() override {
      return chaotic_good::AutoScaler::Metrics{std::move(client_),
                                               std::move(server_)};
    }

   private:
    TDigest client_;
    TDigest server_;
  };

  class ExpectedMeasurePerConnectionLatency final : public ExpectedOp {
   public:
    explicit ExpectedMeasurePerConnectionLatency(
        absl::flat_hash_map<uint32_t, chaotic_good::AutoScaler::Metrics>
            metrics)
        : per_connection_metrics_(std::move(metrics)) {}

    absl::flat_hash_map<uint32_t, chaotic_good::AutoScaler::Metrics>
    MeasurePerConnectionLatency() override {
      return std::move(per_connection_metrics_);
    }

   private:
    absl::flat_hash_map<uint32_t, chaotic_good::AutoScaler::Metrics>
        per_connection_metrics_;
  };

  class ExpectedAddConnection final : public ExpectedOp {
   public:
    void AddConnection() override {}
  };

  class ExpectedRemoveConnection final : public ExpectedOp {
   public:
    void RemoveConnection() override {}
  };

  class ExpectedParkConnection final : public ExpectedOp {
   public:
    explicit ExpectedParkConnection(uint32_t id) : id_(id) {}
    void ParkConnection(uint32_t id) override { CHECK_EQ(id, id_); }

   private:
    uint32_t id_;
  };

  class ExpectedUnparkConnection final : public ExpectedOp {
   public:
    explicit ExpectedUnparkConnection(uint32_t id) : id_(id) {}
    void UnparkConnection(uint32_t id) override { CHECK_EQ(id, id_); }

   private:
    uint32_t id_;
  };

  class FakeSubject final : public chaotic_good::AutoScaler::SubjectInterface {
   public:
    explicit FakeSubject(AutoScalerTest* test) : test_(test) {}

    Promise<uint32_t> AddConnection() override {
      return Seq(WaitExpected(), [this](std::unique_ptr<ExpectedOp> op) {
        op->AddConnection();
        uint32_t id = connections_.size();
        connections_.emplace_back();
        return id;
      });
    }
    Promise<Empty> RemoveConnection(uint32_t id) override {
      return Seq(WaitExpected(), [this, id](std::unique_ptr<ExpectedOp> op) {
        op->RemoveConnection();
        CHECK_EQ(connections_[id].state, ConnectionState::kActive);
        connections_[id].state = ConnectionState::kRemoved;
        return Empty{};
      });
    }
    Promise<Empty> ParkConnection(uint32_t id) override {
      return Seq(WaitExpected(), [this, id](std::unique_ptr<ExpectedOp> op) {
        op->ParkConnection(id);
        CHECK_EQ(connections_[id].state, ConnectionState::kActive);
        connections_[id].state = ConnectionState::kParked;
        return Empty{};
      });
    }
    Promise<Empty> UnparkConnection(uint32_t id) override {
      return Seq(WaitExpected(), [this, id](std::unique_ptr<ExpectedOp> op) {
        op->UnparkConnection(id);
        CHECK_EQ(connections_[id].state, ConnectionState::kParked);
        connections_[id].state = ConnectionState::kActive;
        return Empty{};
      });
    }
    Promise<chaotic_good::AutoScaler::Metrics> MeasureOverallLatency()
        override {
      return Seq(WaitExpected(), [](std::unique_ptr<ExpectedOp> op) {
        return op->MeasureOverallLatency();
      });
    }
    Promise<absl::flat_hash_map<uint32_t, chaotic_good::AutoScaler::Metrics>>
    MeasurePerConnectionLatency() override {
      return Seq(WaitExpected(), [](std::unique_ptr<ExpectedOp> op) {
        return op->MeasurePerConnectionLatency();
      });
    }
    size_t GetNumConnections() override {
      size_t count = 0;
      for (const auto& con : connections_) {
        if (con.state != ConnectionState::kRemoved) ++count;
      }
      return count;
    }
    std::vector<uint32_t> ListActiveConnections() {
      std::vector<uint32_t> ids;
      for (size_t i = 0; i < connections_.size(); i++) {
        if (connections_[i].state == ConnectionState::kActive) {
          ids.push_back(i);
        }
      }
      return ids;
    }

    template <typename T, typename... Args>
    Notifier Expect(Args&&... args) {
      auto op = std::make_unique<T>(std::forward<Args>(args)...);
      auto notifier = op->notifier();
      test_->event_engine()->Run([op = std::move(op), this]() mutable {
        CHECK(expected_op_ == nullptr);
        expected_op_ = std::move(op);
        expected_op_waker_.Wakeup();
      });
      return notifier;
    }

   private:
    Promise<std::unique_ptr<ExpectedOp>> WaitExpected() {
      return [this]() mutable -> Poll<std::unique_ptr<ExpectedOp>> {
        if (expected_op_ == nullptr) {
          expected_op_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
          return Pending();
        }
        return std::move(expected_op_);
      };
    }

    enum class ConnectionState {
      kActive,
      kRemoved,
      kParked,
    };
    friend std::ostream& operator<<(std::ostream& out, ConnectionState st) {
      switch (st) {
        case ConnectionState::kActive:
          return out << "Active";
        case ConnectionState::kRemoved:
          return out << "Removed";
        case ConnectionState::kParked:
          return out << "Parked";
      }
      return out;
    }
    struct Connection {
      ConnectionState state = ConnectionState::kActive;
    };

    AutoScalerTest* const test_;
    std::unique_ptr<ExpectedOp> expected_op_;
    Waker expected_op_waker_;
    std::vector<Connection> connections_;
  };

  void TickUntilDone(Notifier n) {
    TickUntil<Empty>([n]() -> Poll<Empty> {
      return n.IsDone() ? Poll<Empty>{Empty{}} : Pending{};
    });
  }

  FakeSubject* subject_ = new FakeSubject(this);
  RefCountedPtr<chaotic_good::AutoScaler> auto_scaler_ =
      MakeRefCounted<chaotic_good::AutoScaler>(
          std::unique_ptr<chaotic_good::AutoScaler::SubjectInterface>(subject_),
          chaotic_good::AutoScaler::Options());
};

#define AUTO_SCALER_TEST(name) YODEL_TEST(AutoScalerTest, name)

AUTO_SCALER_TEST(NoOp) { RunLoop run_loop(this); }

AUTO_SCALER_TEST(Run) {
  auto very_low = []() { return RandomDigest(100, 10, 100000); };
  auto low = []() { return RandomDigest(300, 10, 100000); };
  auto medium = []() { return RandomDigest(500, 10, 100000); };
  auto high = []() { return RandomDigest(700, 10, 100000); };

  RunLoop run_loop(this);
  run_loop.ExpectMeasureOverallLatency(medium(), medium());
  run_loop.ExpectAddConnection();
  run_loop.ExpectMeasureOverallLatency(low(), medium());
  run_loop.ExpectMeasureOverallLatency(low(), medium());
  run_loop.ExpectAddConnection();
  run_loop.ExpectMeasureOverallLatency(low(), medium());
  run_loop.ExpectRemoveConnection();
  run_loop.ExpectMeasureOverallLatency(low(), medium());
  run_loop.ExpectAddConnection();
  run_loop.ExpectMeasureOverallLatency(very_low(), medium());
  run_loop.ExpectMeasureOverallLatency(low(), medium());
  run_loop.ExpectAddConnection();
  run_loop.ExpectMeasureOverallLatency(high(), high());
  run_loop.ExpectRemoveConnection();
  run_loop.ExpectMeasureOverallLatency(medium(), high());
  auto connections = run_loop.ListActiveConnections();
  EXPECT_EQ(connections.size(), 2);
  absl::flat_hash_map<uint32_t, chaotic_good::AutoScaler::Metrics> metrics;
  metrics.emplace(connections[0],
                  chaotic_good::AutoScaler::Metrics{low(), low()});
  metrics.emplace(connections[1],
                  chaotic_good::AutoScaler::Metrics{high(), high()});
  run_loop.ExpectMeasurePerConnectionLatency(std::move(metrics));
  run_loop.ExpectParkConnection(connections[1]);
  run_loop.ExpectMeasureOverallLatency(medium(), high());
  run_loop.ExpectUnparkConnection(connections[1]);
}

}  // namespace grpc_core
