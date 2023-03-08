//
//
// Copyright 2015 gRPC authors.
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
//
//

// Microbenchmarks around CHTTP2 transport operations

#include <string.h>

#include <memory>
#include <queue>
#include <sstream>

#include <benchmark/benchmark.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

////////////////////////////////////////////////////////////////////////////////
// Helper classes
//

class PhonyEndpoint : public grpc_endpoint {
 public:
  PhonyEndpoint() {
    static const grpc_endpoint_vtable my_vtable = {read,
                                                   write,
                                                   add_to_pollset,
                                                   add_to_pollset_set,
                                                   delete_from_pollset_set,
                                                   shutdown,
                                                   destroy,
                                                   get_peer,
                                                   get_local_address,
                                                   get_fd,
                                                   can_track_err};
    grpc_endpoint::vtable = &my_vtable;
  }

  void PushInput(grpc_slice slice) {
    if (read_cb_ == nullptr) {
      GPR_ASSERT(!have_slice_);
      buffered_slice_ = slice;
      have_slice_ = true;
      return;
    }
    grpc_slice_buffer_add(slices_, slice);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, read_cb_, absl::OkStatus());
    read_cb_ = nullptr;
  }

 private:
  grpc_closure* read_cb_ = nullptr;
  grpc_slice_buffer* slices_ = nullptr;
  bool have_slice_ = false;
  grpc_slice buffered_slice_;

  void QueueRead(grpc_slice_buffer* slices, grpc_closure* cb) {
    GPR_ASSERT(read_cb_ == nullptr);
    if (have_slice_) {
      have_slice_ = false;
      grpc_slice_buffer_add(slices, buffered_slice_);
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::OkStatus());
      return;
    }
    read_cb_ = cb;
    slices_ = slices;
  }

  static void read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, bool /*urgent*/,
                   int /*min_progress_size*/) {
    static_cast<PhonyEndpoint*>(ep)->QueueRead(slices, cb);
  }

  static void write(grpc_endpoint* /*ep*/, grpc_slice_buffer* /*slices*/,
                    grpc_closure* cb, void* /*arg*/, int /*max_frame_size*/) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, absl::OkStatus());
  }

  static void add_to_pollset(grpc_endpoint* /*ep*/, grpc_pollset* /*pollset*/) {
  }

  static void add_to_pollset_set(grpc_endpoint* /*ep*/,
                                 grpc_pollset_set* /*pollset*/) {}

  static void delete_from_pollset_set(grpc_endpoint* /*ep*/,
                                      grpc_pollset_set* /*pollset*/) {}

  static void shutdown(grpc_endpoint* ep, grpc_error_handle why) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                            static_cast<PhonyEndpoint*>(ep)->read_cb_, why);
  }

  static void destroy(grpc_endpoint* ep) {
    delete static_cast<PhonyEndpoint*>(ep);
  }

  static absl::string_view get_peer(grpc_endpoint* /*ep*/) { return "test"; }
  static absl::string_view get_local_address(grpc_endpoint* /*ep*/) {
    return "test";
  }
  static int get_fd(grpc_endpoint* /*ep*/) { return 0; }
  static bool can_track_err(grpc_endpoint* /*ep*/) { return false; }
};

class Fixture {
 public:
  Fixture(const grpc::ChannelArguments& args, bool client) {
    grpc_channel_args c_args = args.c_channel_args();
    ep_ = new PhonyEndpoint;
    auto final_args = grpc_core::CoreConfiguration::Get()
                          .channel_args_preconditioning()
                          .PreconditionChannelArgs(&c_args);
    t_ = grpc_create_chttp2_transport(final_args, ep_, client);
    grpc_chttp2_transport_start_reading(t_, nullptr, nullptr, nullptr);
    FlushExecCtx();
  }

  void FlushExecCtx() { grpc_core::ExecCtx::Get()->Flush(); }

  ~Fixture() { grpc_transport_destroy(t_); }

  grpc_chttp2_transport* chttp2_transport() {
    return reinterpret_cast<grpc_chttp2_transport*>(t_);
  }
  grpc_transport* transport() { return t_; }

  void PushInput(grpc_slice slice) { ep_->PushInput(slice); }

 private:
  PhonyEndpoint* ep_;
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
    arena_ = grpc_core::Arena::Create(4096, &memory_allocator_);
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
      arena_ = grpc_core::Arena::Create(4096, &memory_allocator_);
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

  grpc_chttp2_stream* chttp2_stream() {
    return static_cast<grpc_chttp2_stream*>(stream_);
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
  grpc_core::MemoryAllocator memory_allocator_ =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
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
  Fixture f(grpc::ChannelArguments(), true);
  auto* s = new Stream(&f);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload(nullptr);
  op = {};
  op.cancel_stream = true;
  op.payload = &op_payload;
  op_payload.cancel_stream.cancel_error = absl::CancelledError();
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
  grpc_core::Closure::Run(DEBUG_LOCATION, next.get(), absl::OkStatus());
  f.FlushExecCtx();
}
BENCHMARK(BM_StreamCreateDestroy);

class RepresentativeClientInitialMetadata {
 public:
  static void Prepare(grpc_metadata_batch* b) {
    b->Set(grpc_core::HttpSchemeMetadata(),
           grpc_core::HttpSchemeMetadata::kHttp);
    b->Set(grpc_core::HttpMethodMetadata(),
           grpc_core::HttpMethodMetadata::kPost);
    b->Set(grpc_core::HttpPathMetadata(),
           grpc_core::Slice(grpc_core::StaticSlice::FromStaticString(
               "/foo/bar/bm_chttp2_transport")));
    b->Set(grpc_core::HttpAuthorityMetadata(),
           grpc_core::Slice(grpc_core::StaticSlice::FromStaticString(
               "foo.test.google.fr:1234")));
    b->Set(
        grpc_core::GrpcAcceptEncodingMetadata(),
        grpc_core::CompressionAlgorithmSet(
            {GRPC_COMPRESS_NONE, GRPC_COMPRESS_DEFLATE, GRPC_COMPRESS_GZIP}));
    b->Set(grpc_core::TeMetadata(), grpc_core::TeMetadata::kTrailers);
    b->Set(grpc_core::ContentTypeMetadata(),
           grpc_core::ContentTypeMetadata::kApplicationGrpc);
    b->Set(grpc_core::UserAgentMetadata(),
           grpc_core::Slice(grpc_core::StaticSlice::FromStaticString(
               "grpc-c/3.0.0-dev (linux; chttp2; green)")));
  }
};

template <class Metadata>
static void BM_StreamCreateSendInitialMetadataDestroy(benchmark::State& state) {
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

  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  auto arena = grpc_core::MakeScopedArena(1024, &memory_allocator);
  grpc_metadata_batch b(arena.get());
  Metadata::Prepare(&b);

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
    op.payload->cancel_stream.cancel_error = absl::CancelledError();
    s->Op(&op);
    s->DestroyThen(start.get());
  });
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, start.get(), absl::OkStatus());
  f.FlushExecCtx();
  gpr_event_wait(&bm_done, gpr_inf_future(GPR_CLOCK_REALTIME));
}
BENCHMARK_TEMPLATE(BM_StreamCreateSendInitialMetadataDestroy,
                   RepresentativeClientInitialMetadata);

static void BM_TransportEmptyOp(benchmark::State& state) {
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
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, c.get(), absl::OkStatus());
  f.FlushExecCtx();
  reset_op();
  op.cancel_stream = true;
  op_payload.cancel_stream.cancel_error = absl::CancelledError();
  gpr_event* stream_cancel_done = new gpr_event;
  gpr_event_init(stream_cancel_done);
  std::unique_ptr<TestClosure> stream_cancel_closure =
      MakeTestClosure([&](grpc_error_handle error) {
        GPR_ASSERT(error.ok());
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
}
BENCHMARK(BM_TransportEmptyOp);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
