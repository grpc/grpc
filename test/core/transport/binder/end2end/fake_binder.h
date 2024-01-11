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

// A collection of fake objects that offers in-memory simulation of data
// transmission from one binder to another.
//
// Once the implementation of Binder is changed from BinderAndroid to
// FakeBinder, we'll be able to test and fuzz our end-to-end binder transport in
// a non-Android environment.
//
// The following diagram shows the high-level overview of how the in-memory
// simulation works (FakeReceiver means FakeTransactionReceiver).
//
//                                        thread boundary
//                                                |
//                                                |
// ----------------           ----------------    |  receive
// |  FakeBinder  |           | FakeReceiver | <--|----------------
// ----------------           ----------------    |               |
//        |                           ^           |   ------------------------
//        | endpoint            owner |           |   | TransactionProcessor |
//        |                           |           |   ------------------------
//        v                           |           |               ^
// ----------------           ----------------    |               |
// | FakeEndpoint | --------> | FakeEndpoint | ---|----------------
// ---------------- other_end ----------------    |  enqueue
//       | ^                         ^ |          |
//       | |           recv_endpoint | |          |
//       | |                         | |
//       | | send_endpoint           | |
//       v |                         | v
// -------------------------------------------
// |             FakeBinderTunnel            |
// -------------------------------------------

#ifndef GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_FAKE_BINDER_H
#define GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_FAKE_BINDER_H

#include <atomic>
#include <forward_list>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/variant.h"

#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"

namespace grpc_binder {
namespace end2end_testing {

using FakeData = std::vector<
    absl::variant<int32_t, int64_t, void*, std::string, std::vector<int8_t>>>;

// A fake writable parcel.
//
// It simulates the functionalities of a real writable parcel and stores all
// written data in memory. The data can then be transferred by calling
// MoveData().
class FakeWritableParcel final : public WritableParcel {
 public:
  int32_t GetDataSize() const override;
  absl::Status WriteInt32(int32_t data) override;
  absl::Status WriteInt64(int64_t data) override;
  absl::Status WriteBinder(HasRawBinder* binder) override;
  absl::Status WriteString(absl::string_view s) override;
  absl::Status WriteByteArray(const int8_t* buffer, int32_t length) override;

  FakeData MoveData() { return std::move(data_); }

 private:
  FakeData data_;
  int32_t data_size_ = 0;
};

// A fake readable parcel.
//
// It takes in the data transferred from a FakeWritableParcel and provides
// methods to retrieve those data in the receiving end.
class FakeReadableParcel final : public ReadableParcel {
 public:
  explicit FakeReadableParcel(FakeData data) : data_(std::move(data)) {
    for (auto& d : data_) {
      if (absl::holds_alternative<int32_t>(d)) {
        data_size_ += sizeof(int32_t);
      } else if (absl::holds_alternative<int64_t>(d)) {
        data_size_ += sizeof(int64_t);
      } else if (absl::holds_alternative<void*>(d)) {
        data_size_ += sizeof(void*);
      } else if (absl::holds_alternative<std::string>(d)) {
        data_size_ += absl::get<std::string>(d).size();
      } else {
        data_size_ += absl::get<std::vector<int8_t>>(d).size();
      }
    }
  }

  int32_t GetDataSize() const override;
  absl::Status ReadInt32(int32_t* data) override;
  absl::Status ReadInt64(int64_t* data) override;
  absl::Status ReadBinder(std::unique_ptr<Binder>* data) override;
  absl::Status ReadByteArray(std::string* data) override;
  absl::Status ReadString(std::string* str) override;

 private:
  const FakeData data_;
  size_t data_position_ = 0;
  int32_t data_size_ = 0;
};

class FakeBinder;
class FakeBinderTunnel;

// FakeEndpoint is a simple struct that holds the pointer to the other end, a
// pointer to the tunnel and a pointer to its owner. This tells the owner where
// the data should be sent.
struct FakeEndpoint {
  explicit FakeEndpoint(FakeBinderTunnel* tunnel) : tunnel(tunnel) {}

  FakeEndpoint* other_end;
  FakeBinderTunnel* tunnel;
  // The owner is either a FakeBinder (the sending part) or a
  // FakeTransactionReceiver (the receiving part). Both parts hold an endpoint
  // with |owner| pointing back to them and |other_end| pointing to each other.
  void* owner;
};

class PersistentFakeTransactionReceiver;

// A fake transaction receiver.
//
// This is the receiving part of a pair of binders. When constructed, a binder
// tunnle is created, and the sending part can be retrieved by calling
// GetSender().
//
// It also provides a Receive() function to simulate the on-transaction
// callback of a real Android binder.
class FakeTransactionReceiver : public TransactionReceiver {
 public:
  FakeTransactionReceiver(grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
                          TransactionReceiver::OnTransactCb cb);

  void* GetRawBinder() override;

  std::unique_ptr<Binder> GetSender() const;

 private:
  PersistentFakeTransactionReceiver* persistent_tx_receiver_;
};

// A "persistent" version of the FakeTransactionReceiver. That is, its lifetime
// is managed by the processor and it outlives the wire reader and
// grpc_binder_transport, so we can safely dereference a pointer to it in
// ProcessLoop().
class PersistentFakeTransactionReceiver {
 public:
  PersistentFakeTransactionReceiver(
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      TransactionReceiver::OnTransactCb cb,
      std::unique_ptr<FakeBinderTunnel> tunnel);

  absl::Status Receive(BinderTransportTxCode tx_code, ReadableParcel* parcel) {
    return callback_(static_cast<transaction_code_t>(tx_code), parcel,
                     /*uid=*/0);
  }

 private:
  grpc_core::RefCountedPtr<WireReader> wire_reader_ref_;
  TransactionReceiver::OnTransactCb callback_;
  std::unique_ptr<FakeBinderTunnel> tunnel_;

  friend class FakeTransactionReceiver;
};

// The sending part of a binders pair. It provides a FakeWritableParcel to the
// user, and when Transact() is called, it transfers the written data to the
// other end of the tunnel by following the information in its endpoint.
class FakeBinder final : public Binder {
 public:
  explicit FakeBinder(FakeEndpoint* endpoint) : endpoint_(endpoint) {}

  void Initialize() override {}
  absl::Status PrepareTransaction() override {
    input_ = std::make_unique<FakeWritableParcel>();
    return absl::OkStatus();
  }

  absl::Status Transact(BinderTransportTxCode tx_code) override;

  WritableParcel* GetWritableParcel() const override { return input_.get(); }

  std::unique_ptr<TransactionReceiver> ConstructTxReceiver(
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      TransactionReceiver::OnTransactCb transact_cb) const override;

  void* GetRawBinder() override { return endpoint_->other_end; }

 private:
  FakeEndpoint* endpoint_;
  std::unique_ptr<FakeWritableParcel> input_;
};

// A transaction processor.
//
// Once constructed, it'll create a another thread that deliver in-coming
// transactions to their destinations.
class TransactionProcessor {
 public:
  explicit TransactionProcessor(absl::Duration delay = absl::ZeroDuration());
  ~TransactionProcessor() { Terminate(); }

  void SetDelay(absl::Duration delay);

  void Terminate();
  void ProcessLoop();
  void Flush();

  // Issue a transaction with |target| pointing to the target endpoint. The
  // transactions will be delivered in the same order they're issued, possibly
  // with random delay to simulate real-world situation.
  void EnQueueTransaction(FakeEndpoint* target, BinderTransportTxCode tx_code,
                          FakeData data);

  PersistentFakeTransactionReceiver& NewPersistentTxReceiver(
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      TransactionReceiver::OnTransactCb cb,
      std::unique_ptr<FakeBinderTunnel> tunnel) {
    grpc_core::MutexLock lock(&tx_receiver_mu_);
    storage_.emplace_front(wire_reader_ref, cb, std::move(tunnel));
    return storage_.front();
  }

 private:
  absl::Duration GetRandomDelay();
  void WaitForNextTransaction() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  grpc_core::Mutex mu_;
  std::queue<std::tuple<FakeEndpoint*, BinderTransportTxCode, FakeData>>
      tx_queue_ ABSL_GUARDED_BY(mu_);
  absl::Time deliver_time_ ABSL_GUARDED_BY(mu_);
  int64_t delay_nsec_;
  absl::BitGen bit_gen_;
  grpc_core::Thread tx_thread_;
  std::atomic<bool> terminated_;

  grpc_core::Mutex tx_receiver_mu_;
  // Use forward_list to avoid invalid pointers resulted by reallocation in
  // containers such as std::vector.
  std::forward_list<PersistentFakeTransactionReceiver> storage_
      ABSL_GUARDED_BY(tx_receiver_mu_);
};

// The global (shared) processor. Test suite should be responsible of
// creating/deleting it.
extern TransactionProcessor* g_transaction_processor;

// A binder tunnel.
//
// It is a simple helper that creates and links two endpoints.
class FakeBinderTunnel {
 public:
  FakeBinderTunnel();

  void EnQueueTransaction(FakeEndpoint* target, BinderTransportTxCode tx_code,
                          FakeData data) {
    g_transaction_processor->EnQueueTransaction(target, tx_code,
                                                std::move(data));
  }

  FakeEndpoint* GetSendEndpoint() const { return send_endpoint_.get(); }
  FakeEndpoint* GetRecvEndpoint() const { return recv_endpoint_.get(); }

 private:
  std::unique_ptr<FakeEndpoint> send_endpoint_;
  std::unique_ptr<FakeEndpoint> recv_endpoint_;
};

// A helper function for constructing a pair of connected binders.
std::pair<std::unique_ptr<Binder>, std::unique_ptr<TransactionReceiver>>
NewBinderPair(TransactionReceiver::OnTransactCb transact_cb);

}  // namespace end2end_testing
}  // namespace grpc_binder

#endif  // GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_FAKE_BINDER_H
