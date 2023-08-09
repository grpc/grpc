// Copyright 2021 gRPC authors.
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

#include "test/core/transport/binder/end2end/fake_binder.h"

#include <algorithm>
#include <random>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include "test/core/util/test_config.h"

namespace grpc_binder {
namespace end2end_testing {
namespace {

class FakeBinderTest : public ::testing::TestWithParam<absl::Duration> {
 public:
  FakeBinderTest() {
    g_transaction_processor = new TransactionProcessor(GetParam());
  }
  ~FakeBinderTest() override { delete g_transaction_processor; }
};

}  // namespace

TEST_P(FakeBinderTest, SendInt32) {
  constexpr int kValue = 0x1234;
  constexpr int kTxCode = 0x4321;
  int called = 0;
  std::unique_ptr<Binder> sender;
  std::unique_ptr<TransactionReceiver> tx_receiver;
  std::tie(sender, tx_receiver) = NewBinderPair(
      [&](transaction_code_t tx_code, ReadableParcel* parcel, int /*uid*/) {
        EXPECT_EQ(tx_code, kTxCode);
        int value = 0;
        EXPECT_TRUE(parcel->ReadInt32(&value).ok());
        EXPECT_EQ(value, kValue);
        called++;
        return absl::OkStatus();
      });

  EXPECT_TRUE(sender->PrepareTransaction().ok());
  WritableParcel* parcel = sender->GetWritableParcel();
  EXPECT_TRUE(parcel->WriteInt32(kValue).ok());
  EXPECT_TRUE(sender->Transact(BinderTransportTxCode(kTxCode)).ok());

  g_transaction_processor->Terminate();
  EXPECT_EQ(called, 1);
}

TEST_P(FakeBinderTest, SendString) {
  constexpr char kValue[] = "example-string";
  constexpr int kTxCode = 0x4321;
  int called = 0;
  std::unique_ptr<Binder> sender;
  std::unique_ptr<TransactionReceiver> tx_receiver;
  std::tie(sender, tx_receiver) = NewBinderPair(
      [&](transaction_code_t tx_code, ReadableParcel* parcel, int /*uid*/) {
        EXPECT_EQ(tx_code, kTxCode);
        std::string value;
        EXPECT_TRUE(parcel->ReadString(&value).ok());
        EXPECT_STREQ(value.c_str(), kValue);
        called++;
        return absl::OkStatus();
      });

  EXPECT_TRUE(sender->PrepareTransaction().ok());
  WritableParcel* parcel = sender->GetWritableParcel();
  EXPECT_TRUE(parcel->WriteString(kValue).ok());
  EXPECT_TRUE(sender->Transact(BinderTransportTxCode(kTxCode)).ok());

  g_transaction_processor->Terminate();
  EXPECT_EQ(called, 1);
}

TEST_P(FakeBinderTest, SendByteArray) {
  constexpr char kValue[] = "example-byte-array";
  constexpr int kTxCode = 0x4321;
  int called = 0;
  std::unique_ptr<Binder> sender;
  std::unique_ptr<TransactionReceiver> tx_receiver;
  std::tie(sender, tx_receiver) = NewBinderPair(
      [&](transaction_code_t tx_code, ReadableParcel* parcel, int /*uid*/) {
        EXPECT_EQ(tx_code, kTxCode);
        std::string value;
        EXPECT_TRUE(parcel->ReadByteArray(&value).ok());
        EXPECT_EQ(value, kValue);
        called++;
        return absl::OkStatus();
      });

  EXPECT_TRUE(sender->PrepareTransaction().ok());
  WritableParcel* parcel = sender->GetWritableParcel();
  EXPECT_TRUE(parcel
                  ->WriteByteArray(reinterpret_cast<const int8_t*>(kValue),
                                   strlen(kValue))
                  .ok());
  EXPECT_TRUE(sender->Transact(BinderTransportTxCode(kTxCode)).ok());

  g_transaction_processor->Terminate();
  EXPECT_EQ(called, 1);
}

TEST_P(FakeBinderTest, SendMultipleItems) {
  constexpr char kByteArray[] = "example-byte-array";
  constexpr char kString[] = "example-string";
  constexpr int kValue = 0x1234;
  constexpr int kTxCode = 0x4321;
  int called = 0;
  std::unique_ptr<Binder> sender;
  std::unique_ptr<TransactionReceiver> tx_receiver;
  std::tie(sender, tx_receiver) = NewBinderPair(
      [&](transaction_code_t tx_code, ReadableParcel* parcel, int /*uid*/) {
        int value_result;
        EXPECT_EQ(tx_code, kTxCode);
        EXPECT_TRUE(parcel->ReadInt32(&value_result).ok());
        EXPECT_EQ(value_result, kValue);
        std::string byte_array_result;
        EXPECT_TRUE(parcel->ReadByteArray(&byte_array_result).ok());
        EXPECT_EQ(byte_array_result, kByteArray);
        std::string string_result;
        EXPECT_TRUE(parcel->ReadString(&string_result).ok());
        EXPECT_STREQ(string_result.c_str(), kString);
        called++;
        return absl::OkStatus();
      });

  EXPECT_TRUE(sender->PrepareTransaction().ok());
  WritableParcel* parcel = sender->GetWritableParcel();
  EXPECT_TRUE(parcel->WriteInt32(kValue).ok());
  EXPECT_TRUE(parcel
                  ->WriteByteArray(reinterpret_cast<const int8_t*>(kByteArray),
                                   strlen(kByteArray))
                  .ok());
  EXPECT_TRUE(parcel->WriteString(kString).ok());
  EXPECT_TRUE(sender->Transact(BinderTransportTxCode(kTxCode)).ok());

  g_transaction_processor->Terminate();
  EXPECT_EQ(called, 1);
}

TEST_P(FakeBinderTest, SendBinder) {
  constexpr int kValue = 0x1234;
  constexpr int kTxCode = 0x4321;
  int called = 0;
  std::unique_ptr<Binder> sender;
  std::unique_ptr<TransactionReceiver> tx_receiver;
  std::tie(sender, tx_receiver) = NewBinderPair(
      [&](transaction_code_t tx_code, ReadableParcel* parcel, int /*uid*/) {
        EXPECT_EQ(tx_code, kTxCode);
        std::unique_ptr<Binder> binder;
        EXPECT_TRUE(parcel->ReadBinder(&binder).ok());
        EXPECT_TRUE(binder->PrepareTransaction().ok());
        WritableParcel* writable_parcel = binder->GetWritableParcel();
        EXPECT_TRUE(writable_parcel->WriteInt32(kValue).ok());
        EXPECT_TRUE(binder->Transact(BinderTransportTxCode(kTxCode + 1)).ok());
        called++;
        return absl::OkStatus();
      });

  int called2 = 0;
  std::unique_ptr<TransactionReceiver> tx_receiver2 =
      std::make_unique<FakeTransactionReceiver>(
          nullptr,
          [&](transaction_code_t tx_code, ReadableParcel* parcel, int /*uid*/) {
            int value;
            EXPECT_TRUE(parcel->ReadInt32(&value).ok());
            EXPECT_EQ(value, kValue);
            EXPECT_EQ(tx_code, kTxCode + 1);
            called2++;
            return absl::OkStatus();
          });
  EXPECT_TRUE(sender->PrepareTransaction().ok());
  WritableParcel* parcel = sender->GetWritableParcel();
  EXPECT_TRUE(parcel->WriteBinder(tx_receiver2.get()).ok());
  EXPECT_TRUE(sender->Transact(BinderTransportTxCode(kTxCode)).ok());

  g_transaction_processor->Terminate();
  EXPECT_EQ(called, 1);
  EXPECT_EQ(called2, 1);
}

TEST_P(FakeBinderTest, SendTransactionAfterDestruction) {
  constexpr int kValue = 0x1234;
  constexpr int kTxCode = 0x4321;
  std::unique_ptr<Binder> sender;
  int called = 0;
  {
    std::unique_ptr<TransactionReceiver> tx_receiver;
    std::tie(sender, tx_receiver) = NewBinderPair(
        [&](transaction_code_t tx_code, ReadableParcel* parcel, int /*uid*/) {
          EXPECT_EQ(tx_code, kTxCode);
          int value;
          EXPECT_TRUE(parcel->ReadInt32(&value).ok());
          EXPECT_EQ(value, kValue + called);
          called++;
          return absl::OkStatus();
        });
    EXPECT_TRUE(sender->PrepareTransaction().ok());
    WritableParcel* parcel = sender->GetWritableParcel();
    EXPECT_TRUE(parcel->WriteInt32(kValue).ok());
    EXPECT_TRUE(sender->Transact(BinderTransportTxCode(kTxCode)).ok());
  }
  // tx_receiver gets destructed here. This additional transaction should
  // *still* be received.
  EXPECT_TRUE(sender->PrepareTransaction().ok());
  WritableParcel* parcel = sender->GetWritableParcel();
  EXPECT_TRUE(parcel->WriteInt32(kValue + 1).ok());
  EXPECT_TRUE(sender->Transact(BinderTransportTxCode(kTxCode)).ok());

  g_transaction_processor->Terminate();
  EXPECT_EQ(called, 2);
}

namespace {

struct ThreadArgument {
  int tid;
  std::vector<std::vector<std::pair<std::unique_ptr<Binder>,
                                    std::unique_ptr<TransactionReceiver>>>>*
      global_binder_pairs;
  std::vector<std::vector<int>>* global_cnts;
  int tx_code;
  int num_pairs_per_thread;
  int num_transactions_per_pair;
  grpc_core::Mutex* mu;
};

}  // namespace

// Verify that this system works correctly in a concurrent environment.
//
// In end-to-end tests, there will be at least two threads, one from client to
// server and vice versa. Thus, it's important for us to make sure that the
// simulation is correct in such setup.
TEST_P(FakeBinderTest, StressTest) {
  constexpr int kTxCode = 0x4321;
  constexpr int kNumThreads = 16;
  constexpr int kNumPairsPerThread = 128;
  constexpr int kNumTransactionsPerPair = 128;
  std::vector<ThreadArgument> args(kNumThreads);

  grpc_core::Mutex mu;
  std::vector<std::vector<
      std::pair<std::unique_ptr<Binder>, std::unique_ptr<TransactionReceiver>>>>
      global_binder_pairs(kNumThreads);
  std::vector<std::vector<int>> global_cnts(
      kNumThreads, std::vector<int>(kNumPairsPerThread, 0));

  auto th_function = [](void* arg) {
    ThreadArgument* th_arg = static_cast<ThreadArgument*>(arg);
    int tid = th_arg->tid;
    std::vector<std::pair<std::unique_ptr<Binder>,
                          std::unique_ptr<TransactionReceiver>>>
        binder_pairs;
    for (int p = 0; p < th_arg->num_pairs_per_thread; ++p) {
      std::unique_ptr<Binder> binder;
      std::unique_ptr<TransactionReceiver> tx_receiver;
      int expected_tx_code = th_arg->tx_code;
      std::vector<std::vector<int>>* cnt = th_arg->global_cnts;
      std::tie(binder, tx_receiver) =
          NewBinderPair([tid, p, cnt, expected_tx_code](
                            transaction_code_t tx_code, ReadableParcel* parcel,
                            int /*uid*/) mutable {
            EXPECT_EQ(tx_code, expected_tx_code);
            int value;
            EXPECT_TRUE(parcel->ReadInt32(&value).ok());
            EXPECT_EQ(tid, value);
            EXPECT_TRUE(parcel->ReadInt32(&value).ok());
            EXPECT_EQ(p, value);
            EXPECT_TRUE(parcel->ReadInt32(&value).ok());
            EXPECT_EQ((*cnt)[tid][p], value);
            (*cnt)[tid][p]++;
            return absl::OkStatus();
          });
      binder_pairs.emplace_back(std::move(binder), std::move(tx_receiver));
    }
    std::vector<int> order;
    for (int i = 0; i < th_arg->num_pairs_per_thread; ++i) {
      for (int j = 0; j < th_arg->num_transactions_per_pair; ++j) {
        order.emplace_back(i);
      }
    }
    std::mt19937 rng(tid);
    std::shuffle(order.begin(), order.end(), rng);
    std::vector<int> tx_cnt(th_arg->num_pairs_per_thread);
    for (int p : order) {
      EXPECT_TRUE(binder_pairs[p].first->PrepareTransaction().ok());
      WritableParcel* parcel = binder_pairs[p].first->GetWritableParcel();
      EXPECT_TRUE(parcel->WriteInt32(th_arg->tid).ok());
      EXPECT_TRUE(parcel->WriteInt32(p).ok());
      EXPECT_TRUE(parcel->WriteInt32(tx_cnt[p]++).ok());
      EXPECT_TRUE(binder_pairs[p]
                      .first->Transact(BinderTransportTxCode(th_arg->tx_code))
                      .ok());
    }
    th_arg->mu->Lock();
    (*th_arg->global_binder_pairs)[tid] = std::move(binder_pairs);
    th_arg->mu->Unlock();
  };

  std::vector<grpc_core::Thread> thrs(kNumThreads);
  std::vector<std::string> thr_names(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    args[i].tid = i;
    args[i].global_binder_pairs = &global_binder_pairs;
    args[i].global_cnts = &global_cnts;
    args[i].tx_code = kTxCode;
    args[i].num_pairs_per_thread = kNumPairsPerThread;
    args[i].num_transactions_per_pair = kNumTransactionsPerPair;
    args[i].mu = &mu;
    thr_names[i] = absl::StrFormat("thread-%d", i);
    thrs[i] = grpc_core::Thread(thr_names[i].c_str(), th_function, &args[i]);
  }
  for (auto& th : thrs) th.Start();
  for (auto& th : thrs) th.Join();
  g_transaction_processor->Terminate();
}

INSTANTIATE_TEST_SUITE_P(FakeBinderTestWithDifferentDelayTimes, FakeBinderTest,
                         testing::Values(absl::ZeroDuration(),
                                         absl::Nanoseconds(10),
                                         absl::Microseconds(10)));

}  // namespace end2end_testing
}  // namespace grpc_binder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
