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

#include <benchmark/benchmark.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpcpp/support/channel_arguments.h>
#include <string.h>
#include <memory>
#include <queue>
#include <sstream>
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/static_metadata.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

////////////////////////////////////////////////////////////////////////////////
// Helper classes
//

namespace grpc_binder {

class PhonyReadableParcel : public ReadableParcel {
 public:
  PhonyReadableParcel() : ptr_(0) {}

  template <class T>
  void PushInput(T value) {
    storage_.push_back(std::move(value));
  }

  absl::Status ReadInt32(int32_t* data) const override {
    *data = absl::get<int32_t>(storage_[ptr_++]);
    return absl::OkStatus();
  }
  absl::Status ReadBinder(std::unique_ptr<Binder>* data) const override;
  absl::Status ReadByteArray(std::string* data) const override {
    const auto& byte_array = absl::get<std::vector<int8_t>>(storage_[ptr_++]);
    data->resize(byte_array.size());
    for (size_t i = 0; i < data->size(); ++i) {
      (*data)[i] = byte_array[i];
    }
    return absl::OkStatus();
  }
  absl::Status ReadString(char data[111]) const override {
    const std::string& str = absl::get<std::string>(storage_[ptr_++]);
    for (size_t i = 0; i < str.size(); ++i) {
      data[i] = str[i];
    }
    return absl::OkStatus();
  }

 private:
  std::vector<absl::variant<int, void*, std::vector<int8_t>, std::string>>
      storage_;
  mutable size_t ptr_;
};

class PhonyTransactionReceiver : public TransactionReceiver {
 public:
  PhonyTransactionReceiver(
      grpc_core::RefCountedPtr<WireReader> /*wire_reader_ref*/,
      TransactionReceiver::OnTransactCb transact_cb) {
    auto parcel = absl::make_unique<PhonyReadableParcel>();
    parcel->PushInput(77);       // version
    parcel->PushInput(nullptr);  // binder
    transact_cb(
        static_cast<transaction_code_t>(BinderTransportTxCode::SETUP_TRANSPORT),
        parcel.get())
        .IgnoreError();
  }
  void* GetRawBinder() override { return nullptr; }
};

class BMWritableParcel : public WritableParcel {
 public:
  int32_t GetDataPosition() const override { return 0; }
  absl::Status SetDataPosition(int32_t pos) override {
    benchmark::DoNotOptimize(pos);
    return absl::OkStatus();
  }
  absl::Status WriteInt32(int32_t data) override {
    benchmark::DoNotOptimize(data);
    return absl::OkStatus();
  }
  absl::Status WriteBinder(HasRawBinder* binder) override {
    benchmark::DoNotOptimize(binder);
    return absl::OkStatus();
  }
  absl::Status WriteString(absl::string_view s) override {
    benchmark::DoNotOptimize(s);
    return absl::OkStatus();
  }
  absl::Status WriteByteArray(const int8_t* buffer, int32_t length) override {
    benchmark::DoNotOptimize(buffer);
    benchmark::DoNotOptimize(length);
    return absl::OkStatus();
  }
};

class PhonyBinder : public Binder {
 public:
  PhonyBinder() : parcel_(absl::make_unique<BMWritableParcel>()) {}
  void Initialize() override {}
  absl::Status PrepareTransaction() override { return absl::OkStatus(); }
  absl::Status Transact(BinderTransportTxCode /*tx_code*/) override {
    return absl::OkStatus();
  }
  WritableParcel* GetWritableParcel() const override { return parcel_.get(); }
  ReadableParcel* GetReadableParcel() const override { return nullptr; }

  std::unique_ptr<TransactionReceiver> ConstructTxReceiver(
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      TransactionReceiver::OnTransactCb transact_cb) const override {
    return absl::make_unique<PhonyTransactionReceiver>(
        std::move(wire_reader_ref), std::move(transact_cb));
  }

  void* GetRawBinder() override { return nullptr; }

 private:
  std::unique_ptr<WritableParcel> parcel_;
};

absl::Status PhonyReadableParcel::ReadBinder(
    std::unique_ptr<Binder>* data) const {
  *data = absl::make_unique<PhonyBinder>();
  ptr_++;  // don't care what the value is.
  return absl::OkStatus();
}

}  // namespace grpc_binder

class Fixture {
 public:
  Fixture(const grpc::ChannelArguments& /*args*/, bool client) {
    t_ = client ? grpc_create_binder_transport_client(
                      absl::make_unique<grpc_binder::PhonyBinder>())
                : grpc_create_binder_transport_server(
                      absl::make_unique<grpc_binder::PhonyBinder>());
    FlushExecCtx();
  }

  ~Fixture() { grpc_transport_destroy(t_); }

  grpc_transport* transport() { return t_; }

  void FlushExecCtx() { grpc_core::ExecCtx::Get()->Flush(); }

 private:
  grpc_transport* t_;
};

class TestClosure : public grpc_closure {
 public:
  virtual ~TestClosure() {}
};

template <class F>
std::unique_ptr<TestClosure> MakeTestClosure(F f) {
  struct C : public TestClosure {
    explicit C(const F& f) : f_(f) {
      GRPC_CLOSURE_INIT(this, Execute, this, nullptr);
    }
    F f_;
    static void Execute(void* arg, grpc_error_handle error) {
      static_cast<C*>(arg)->f_(error);
    }
  };
  return std::unique_ptr<TestClosure>(new C(f));
}

template <class F>
grpc_closure* MakeOnceClosure(F f) {
  struct C : public grpc_closure {
    explicit C(const F& f) : f_(f) {}
    F f_;
    static void Execute(void* arg, grpc_error_handle error) {
      static_cast<C*>(arg)->f_(error);
      delete static_cast<C*>(arg);
    }
  };
  auto* c = new C{f};
  return GRPC_CLOSURE_INIT(c, C::Execute, c, nullptr);
}

class Stream {
 public:
  explicit Stream(Fixture* f) : f_(f) {
    stream_size_ = grpc_transport_stream_size(f->transport());
    stream_ = gpr_malloc(stream_size_);
    arena_ = grpc_core::Arena::Create(4096);
  }

  ~Stream() {
    gpr_event_wait(&done_, gpr_inf_future(GPR_CLOCK_REALTIME));
    gpr_free(stream_);
    arena_->Destroy();
  }

  void Init(benchmark::State& state) {
    GRPC_STREAM_REF_INIT(&refcount_, 1, &Stream::FinishDestroy, this,
                         "test_stream");
    gpr_event_init(&done_);
    memset(stream_, 0, stream_size_);
    if ((state.iterations() & 0xffff) == 0) {
      arena_->Destroy();
      arena_ = grpc_core::Arena::Create(4096);
    }
    grpc_transport_init_stream(f_->transport(),
                               static_cast<grpc_stream*>(stream_), &refcount_,
                               nullptr, arena_);
  }

  void DestroyThen(grpc_closure* closure) {
    destroy_closure_ = closure;
#ifndef NDEBUG
    grpc_stream_unref(&refcount_, "DestroyThen");
#else
    grpc_stream_unref(&refcount_);
#endif
  }

  void Op(grpc_transport_stream_op_batch* op) {
    grpc_transport_perform_stream_op(f_->transport(),
                                     static_cast<grpc_stream*>(stream_), op);
  }

 private:
  static void FinishDestroy(void* arg, grpc_error_handle /*error*/) {
    auto stream = static_cast<Stream*>(arg);
    grpc_transport_destroy_stream(stream->f_->transport(),
                                  static_cast<grpc_stream*>(stream->stream_),
                                  stream->destroy_closure_);
    gpr_event_set(&stream->done_, reinterpret_cast<void*>(1));
  }

  Fixture* f_;
  grpc_stream_refcount refcount_;
  grpc_core::Arena* arena_;
  size_t stream_size_;
  void* stream_;
  grpc_closure* destroy_closure_ = nullptr;
  gpr_event done_;
};

////////////////////////////////////////////////////////////////////////////////
// Benchmarks
//
std::vector<std::unique_ptr<gpr_event>> done_events;

static void BM_StreamCreateDestroy(benchmark::State& state) {
  grpc_core::ExecCtx exec_ctx;
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  auto* s = new Stream(&f);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload(nullptr);
  op = {};
  op.cancel_stream = true;
  op.payload = &op_payload;
  op_payload.cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
  std::unique_ptr<TestClosure> next =
      MakeTestClosure([&, s](grpc_error_handle /*error*/) {
        if (!state.KeepRunning()) {
          delete s;
          return;
        }
        s->Init(state);
        s->Op(&op);
        s->DestroyThen(next.get());
      });
  grpc_core::Closure::Run(DEBUG_LOCATION, next.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  track_counters.Finish(state);
}
BENCHMARK(BM_StreamCreateDestroy);

class RepresentativeClientInitialMetadata {
 public:
  static std::vector<grpc_mdelem> GetElems() {
    return {
        GRPC_MDELEM_SCHEME_HTTP,
        GRPC_MDELEM_METHOD_POST,
        grpc_mdelem_from_slices(GRPC_MDSTR_PATH,
                                grpc_slice_intern(grpc_slice_from_static_string(
                                    "/foo/bar/bm_binder_transport"))),
        grpc_mdelem_from_slices(GRPC_MDSTR_AUTHORITY,
                                grpc_slice_intern(grpc_slice_from_static_string(
                                    "foo.test.google.fr:1234"))),
        GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP,
        GRPC_MDELEM_TE_TRAILERS,
        GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
        grpc_mdelem_from_slices(
            GRPC_MDSTR_USER_AGENT,
            grpc_slice_intern(grpc_slice_from_static_string(
                "grpc-c/3.0.0-dev (linux; binder; green)")))};
  }
};

template <class Metadata>
static void BM_StreamCreateSendInitialMetadataDestroy(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  Fixture f(grpc::ChannelArguments(), true);
  auto* s = new Stream(&f);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload(nullptr);
  std::unique_ptr<TestClosure> start;
  std::unique_ptr<TestClosure> done;

  auto reset_op = [&]() {
    op = {};
    op.payload = &op_payload;
  };

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  b.deadline = GRPC_MILLIS_INF_FUTURE;
  std::vector<grpc_mdelem> elems = Metadata::GetElems();
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd", grpc_metadata_batch_add_tail(&b, &storage[i], elems[i])));
  }

  f.FlushExecCtx();
  gpr_event bm_done;
  gpr_event_init(&bm_done);
  start = MakeTestClosure([&, s](grpc_error_handle /*error*/) {
    if (!state.KeepRunning()) {
      delete s;
      gpr_event_set(&bm_done, (void*)1);
      return;
    }
    s->Init(state);
    reset_op();
    op.on_complete = done.get();
    op.send_initial_metadata = true;
    op.payload->send_initial_metadata.send_initial_metadata = &b;
    s->Op(&op);
  });
  done = MakeTestClosure([&](grpc_error_handle /*error*/) {
    reset_op();
    op.cancel_stream = true;
    op.payload->cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
    s->Op(&op);
    s->DestroyThen(start.get());
  });
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, start.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  gpr_event_wait(&bm_done, gpr_inf_future(GPR_CLOCK_REALTIME));
  grpc_metadata_batch_destroy(&b);
  track_counters.Finish(state);
}
BENCHMARK_TEMPLATE(BM_StreamCreateSendInitialMetadataDestroy,
                   RepresentativeClientInitialMetadata);

static void BM_TransportEmptyOp(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  Fixture f(grpc::ChannelArguments(), true);
  auto* s = new Stream(&f);
  s->Init(state);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload(nullptr);
  auto reset_op = [&]() {
    op = {};
    op.payload = &op_payload;
  };
  std::unique_ptr<TestClosure> c =
      MakeTestClosure([&](grpc_error_handle /*error*/) {
        if (!state.KeepRunning()) return;
        reset_op();
        op.on_complete = c.get();
        s->Op(&op);
      });
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, c.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  reset_op();
  op.cancel_stream = true;
  op_payload.cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
  gpr_event* stream_cancel_done = new gpr_event;
  gpr_event_init(stream_cancel_done);
  std::unique_ptr<TestClosure> stream_cancel_closure =
      MakeTestClosure([&](grpc_error_handle error) {
        GPR_ASSERT(error == GRPC_ERROR_NONE);
        gpr_event_set(stream_cancel_done, reinterpret_cast<void*>(1));
      });
  op.on_complete = stream_cancel_closure.get();
  s->Op(&op);
  f.FlushExecCtx();
  gpr_event_wait(stream_cancel_done, gpr_inf_future(GPR_CLOCK_REALTIME));
  done_events.emplace_back(stream_cancel_done);
  s->DestroyThen(
      MakeOnceClosure([s](grpc_error_handle /*error*/) { delete s; }));
  f.FlushExecCtx();
  track_counters.Finish(state);
}
BENCHMARK(BM_TransportEmptyOp);

static void BM_TransportStreamSend(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  Fixture f(grpc::ChannelArguments(), true);
  auto* s = new Stream(&f);
  s->Init(state);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload(nullptr);
  auto reset_op = [&]() {
    op = {};
    op.payload = &op_payload;
  };
  // Create the send_message payload slice.
  // Note: We use grpc_slice_malloc_large() instead of grpc_slice_malloc()
  // to force the slice to be refcounted, so that it remains alive when it
  // is unreffed after each send_message op.
  grpc_slice send_slice = grpc_slice_malloc_large(state.range(0));
  memset(GRPC_SLICE_START_PTR(send_slice), 0, GRPC_SLICE_LENGTH(send_slice));
  grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream> send_stream;
  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  b.deadline = GRPC_MILLIS_INF_FUTURE;
  std::vector<grpc_mdelem> elems =
      RepresentativeClientInitialMetadata::GetElems();
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd", grpc_metadata_batch_add_tail(&b, &storage[i], elems[i])));
  }

  gpr_event* bm_done = new gpr_event;
  gpr_event_init(bm_done);

  std::unique_ptr<TestClosure> c =
      MakeTestClosure([&](grpc_error_handle /*error*/) {
        if (!state.KeepRunning()) {
          gpr_event_set(bm_done, reinterpret_cast<void*>(1));
          return;
        }
        grpc_slice_buffer send_buffer;
        grpc_slice_buffer_init(&send_buffer);
        grpc_slice_buffer_add(&send_buffer, grpc_slice_ref(send_slice));
        send_stream.Init(&send_buffer, 0);
        grpc_slice_buffer_destroy(&send_buffer);
        reset_op();
        op.on_complete = c.get();
        op.send_message = true;
        op.payload->send_message.send_message.reset(send_stream.get());
        s->Op(&op);
      });

  reset_op();
  op.send_initial_metadata = true;
  op.payload->send_initial_metadata.send_initial_metadata = &b;
  op.on_complete = c.get();
  s->Op(&op);

  f.FlushExecCtx();
  gpr_event_wait(bm_done, gpr_inf_future(GPR_CLOCK_REALTIME));
  done_events.emplace_back(bm_done);

  reset_op();
  op.cancel_stream = true;
  op.payload->cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
  gpr_event* stream_cancel_done = new gpr_event;
  gpr_event_init(stream_cancel_done);
  std::unique_ptr<TestClosure> stream_cancel_closure =
      MakeTestClosure([&](grpc_error_handle error) {
        GPR_ASSERT(error == GRPC_ERROR_NONE);
        gpr_event_set(stream_cancel_done, reinterpret_cast<void*>(1));
      });
  op.on_complete = stream_cancel_closure.get();
  s->Op(&op);
  f.FlushExecCtx();
  gpr_event_wait(stream_cancel_done, gpr_inf_future(GPR_CLOCK_REALTIME));
  done_events.emplace_back(stream_cancel_done);
  s->DestroyThen(
      MakeOnceClosure([s](grpc_error_handle /*error*/) { delete s; }));
  f.FlushExecCtx();
  track_counters.Finish(state);
  grpc_metadata_batch_destroy(&b);
  grpc_slice_unref(send_slice);
}
BENCHMARK(BM_TransportStreamSend)->Range(0, 128 * 1024 * 1024);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
