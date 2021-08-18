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

#include <fuzzer/FuzzedDataProvider.h>

#include <thread>
#include <utility>

#include "absl/memory/memory.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/surface/channel.h"

bool squelch = true;
bool leak_check = true;

namespace {

// A WritableParcel implementation that simply does nothing. Don't use
// MockWritableParcel here since capturing calls is expensive.
class NoOpWritableParcel : public grpc_binder::WritableParcel {
 public:
  int32_t GetDataPosition() const override { return 0; }
  absl::Status SetDataPosition(int32_t /*pos*/) override {
    return absl::OkStatus();
  }
  absl::Status WriteInt32(int32_t /*data*/) override {
    return absl::OkStatus();
  }
  absl::Status WriteBinder(grpc_binder::HasRawBinder* /*binder*/) override {
    return absl::OkStatus();
  }
  absl::Status WriteString(absl::string_view /*s*/) override {
    return absl::OkStatus();
  }
  absl::Status WriteByteArray(const int8_t* /*buffer*/,
                              int32_t /*length*/) override {
    return absl::OkStatus();
  }
};

// Binder implementation used in fuzzing.
//
// Most of its the functionalities are no-op, except ConstructTxReceiver now
// returns a FuzzedTransactionReceiver.
class FuzzedBinder : public grpc_binder::Binder {
 public:
  FuzzedBinder() : input_(absl::make_unique<NoOpWritableParcel>()) {}

  FuzzedBinder(const uint8_t* data, size_t size)
      : data_(data),
        size_(size),
        input_(absl::make_unique<NoOpWritableParcel>()) {}

  void Initialize() override {}
  absl::Status PrepareTransaction() override { return absl::OkStatus(); }

  absl::Status Transact(
      grpc_binder::BinderTransportTxCode /*tx_code*/) override {
    return absl::OkStatus();
  }

  std::unique_ptr<grpc_binder::TransactionReceiver> ConstructTxReceiver(
      grpc_core::RefCountedPtr<grpc_binder::WireReader> wire_reader_ref,
      grpc_binder::TransactionReceiver::OnTransactCb cb) const override;

  grpc_binder::ReadableParcel* GetReadableParcel() const override {
    return nullptr;
  }
  grpc_binder::WritableParcel* GetWritableParcel() const override {
    return input_.get();
  }
  void* GetRawBinder() override { return nullptr; }

 private:
  const uint8_t* data_;
  size_t size_;
  std::unique_ptr<grpc_binder::WritableParcel> input_;
};

// ReadableParcel implementation used in fuzzing.
//
// It consumes a FuzzedDataProvider, and returns fuzzed data upon user's
// requests.
class FuzzedReadableParcel : public grpc_binder::ReadableParcel {
 public:
  explicit FuzzedReadableParcel(FuzzedDataProvider* data_provider)
      : data_provider_(data_provider) {}

  absl::Status ReadInt32(int32_t* data) const override {
    *data = data_provider_->ConsumeIntegral<int32_t>();
    return absl::OkStatus();
  }
  absl::Status ReadBinder(
      std::unique_ptr<grpc_binder::Binder>* binder) const override {
    *binder = absl::make_unique<FuzzedBinder>();
    return absl::OkStatus();
  }
  absl::Status ReadByteArray(std::string* data) const override {
    *data = data_provider_->ConsumeRandomLengthString(100);
    return absl::OkStatus();
  }
  absl::Status ReadString(char data[111]) const override {
    std::string s = data_provider_->ConsumeRandomLengthString(100);
    std::memcpy(data, s.data(), s.size());
    return absl::OkStatus();
  }

 private:
  FuzzedDataProvider* data_provider_;
};

std::vector<std::thread>* thread_pool = nullptr;

void FuzzingLoop(
    const uint8_t* data, size_t size,
    grpc_core::RefCountedPtr<grpc_binder::WireReader> wire_reader_ref,
    grpc_binder::TransactionReceiver::OnTransactCb callback) {
  FuzzedDataProvider data_provider(data, size);
  std::unique_ptr<grpc_binder::ReadableParcel> parcel =
      absl::make_unique<FuzzedReadableParcel>(&data_provider);
  callback(static_cast<transaction_code_t>(
               grpc_binder::BinderTransportTxCode::SETUP_TRANSPORT),
           parcel.get())
      .IgnoreError();
  while (data_provider.remaining_bytes() > 0) {
    gpr_log(GPR_INFO, "Fuzzing");
    bool streaming_call = data_provider.ConsumeBool();
    transaction_code_t tx_code =
        streaming_call
            ? data_provider.ConsumeIntegralInRange<transaction_code_t>(
                  0, static_cast<transaction_code_t>(
                         grpc_binder::BinderTransportTxCode::PING_RESPONSE))
            : data_provider.ConsumeIntegralInRange<transaction_code_t>(
                  0, LAST_CALL_TRANSACTION);
    std::unique_ptr<grpc_binder::ReadableParcel> parcel =
        absl::make_unique<FuzzedReadableParcel>(&data_provider);
    callback(tx_code, parcel.get()).IgnoreError();
    // absl::SleepFor(absl::Milliseconds(10));
  }
  wire_reader_ref = nullptr;
}

// TransactionReceiver implementation used in fuzzing.
//
// When constructed, start sending fuzzed requests to the client. When all the
// bytes are consumed, the reference to WireReader will be released.
class FuzzedTransactionReceiver : public grpc_binder::TransactionReceiver {
 public:
  FuzzedTransactionReceiver(
      const uint8_t* data, size_t size,
      grpc_core::RefCountedPtr<grpc_binder::WireReader> wire_reader_ref,
      grpc_binder::TransactionReceiver::OnTransactCb cb) {
    gpr_log(GPR_INFO, "Construct FuzzedTransactionReceiver");
    thread_pool->emplace_back(FuzzingLoop, data, size, wire_reader_ref, cb);
  }

  void* GetRawBinder() override { return nullptr; }
};

std::unique_ptr<grpc_binder::TransactionReceiver>
FuzzedBinder::ConstructTxReceiver(
    grpc_core::RefCountedPtr<grpc_binder::WireReader> wire_reader_ref,
    grpc_binder::TransactionReceiver::OnTransactCb cb) const {
  return absl::make_unique<FuzzedTransactionReceiver>(data_, size_,
                                                      wire_reader_ref, cb);
}

void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }
void dont_log(gpr_log_func_args*) {}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  thread_pool = new std::vector<std::thread>();
  grpc_test_only_set_slice_hash_seed(0);
  if (squelch) gpr_set_log_function(dont_log);
  grpc_init();
  {
    // Copied and modified from grpc/test/core/end2end/fuzzers/client_fuzzer.cc
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);

    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_transport* client_transport = grpc_create_binder_transport_client(
        absl::make_unique<FuzzedBinder>(data, size));
    grpc_arg authority_arg = grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
        const_cast<char*>("test-authority"));
    grpc_channel_args* args =
        grpc_channel_args_copy_and_add(nullptr, &authority_arg, 1);
    grpc_channel* channel =
        grpc_channel_create("test-target", args, GRPC_CLIENT_DIRECT_CHANNEL,
                            client_transport, nullptr, 0, nullptr);
    grpc_channel_args_destroy(args);
    grpc_slice host = grpc_slice_from_static_string("localhost");
    grpc_call* call = grpc_channel_create_call(
        channel, nullptr, 0, cq, grpc_slice_from_static_string("/foo"), &host,
        gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    grpc_metadata_array initial_metadata_recv;
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_byte_buffer* response_payload_recv = nullptr;
    grpc_metadata_array trailing_metadata_recv;
    grpc_metadata_array_init(&trailing_metadata_recv);
    grpc_status_code status;
    grpc_slice details = grpc_empty_slice();

    grpc_op ops[6];
    memset(ops, 0, sizeof(ops));
    grpc_op* op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    grpc_call_error error =
        grpc_call_start_batch(call, ops, (size_t)(op - ops), tag(1), nullptr);
    int requested_calls = 1;
    GPR_ASSERT(GRPC_CALL_OK == error);
    grpc_event ev;
    while (true) {
      grpc_core::ExecCtx::Get()->Flush();
      ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      switch (ev.type) {
        case GRPC_QUEUE_TIMEOUT:
          goto done;
        case GRPC_QUEUE_SHUTDOWN:
          break;
        case GRPC_OP_COMPLETE:
          requested_calls--;
          break;
      }
    }

  done:
    if (requested_calls) {
      grpc_call_cancel(call, nullptr);
    }
    for (auto& thr : *thread_pool) {
      thr.join();
    }
    for (int i = 0; i < requested_calls; i++) {
      ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    }
    grpc_completion_queue_shutdown(cq);
    for (int i = 0; i < requested_calls; i++) {
      ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
    }
    grpc_call_unref(call);
    grpc_completion_queue_destroy(cq);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
    grpc_slice_unref(details);
    grpc_channel_destroy(channel);
    if (response_payload_recv != nullptr) {
      grpc_byte_buffer_destroy(response_payload_recv);
    }
  }
  delete thread_pool;
  grpc_shutdown();
  return 0;
}
