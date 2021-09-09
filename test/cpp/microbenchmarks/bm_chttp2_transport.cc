/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/* Microbenchmarks around CHTTP2 transport operations */

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
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/static_metadata.h"
#include "test/core/util/resource_user_util.h"
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
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, read_cb_, GRPC_ERROR_NONE);
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
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
      return;
    }
    read_cb_ = cb;
    slices_ = slices;
  }

  static void read(grpc_endpoint* ep, grpc_slice_buffer* slices,
                   grpc_closure* cb, bool /*urgent*/) {
    static_cast<PhonyEndpoint*>(ep)->QueueRead(slices, cb);
  }

  static void write(grpc_endpoint* /*ep*/, grpc_slice_buffer* /*slices*/,
                    grpc_closure* cb, void* /*arg*/) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, cb, GRPC_ERROR_NONE);
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
    t_ = grpc_create_chttp2_transport(&c_args, ep_, client,
                                      grpc_resource_user_create_unlimited());
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
                                    "/foo/bar/bm_chttp2_transport"))),
        grpc_mdelem_from_slices(GRPC_MDSTR_AUTHORITY,
                                grpc_slice_intern(grpc_slice_from_static_string(
                                    "foo.test.google.fr:1234"))),
        GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP,
        GRPC_MDELEM_TE_TRAILERS,
        GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
        grpc_mdelem_from_slices(
            GRPC_MDSTR_USER_AGENT,
            grpc_slice_intern(grpc_slice_from_static_string(
                "grpc-c/3.0.0-dev (linux; chttp2; green)")))};
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
        // force outgoing window to be yuge
        s->chttp2_stream()->flow_control->TestOnlyForceHugeWindow();
        f.chttp2_transport()->flow_control->TestOnlyForceHugeWindow();
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

#define SLICE_FROM_BUFFER(s) grpc_slice_from_static_buffer(s, sizeof(s) - 1)

static grpc_slice CreateIncomingDataSlice(size_t length, size_t frame_size) {
  std::queue<char> unframed;

  unframed.push(static_cast<uint8_t>(0));
  unframed.push(static_cast<uint8_t>(length >> 24));
  unframed.push(static_cast<uint8_t>(length >> 16));
  unframed.push(static_cast<uint8_t>(length >> 8));
  unframed.push(static_cast<uint8_t>(length));
  for (size_t i = 0; i < length; i++) {
    unframed.push('a');
  }

  std::vector<char> framed;
  while (unframed.size() > frame_size) {
    // frame size
    framed.push_back(static_cast<uint8_t>(frame_size >> 16));
    framed.push_back(static_cast<uint8_t>(frame_size >> 8));
    framed.push_back(static_cast<uint8_t>(frame_size));
    // data frame
    framed.push_back(0);
    // no flags
    framed.push_back(0);
    // stream id
    framed.push_back(0);
    framed.push_back(0);
    framed.push_back(0);
    framed.push_back(1);
    // frame data
    for (size_t i = 0; i < frame_size; i++) {
      framed.push_back(unframed.front());
      unframed.pop();
    }
  }

  // frame size
  framed.push_back(static_cast<uint8_t>(unframed.size() >> 16));
  framed.push_back(static_cast<uint8_t>(unframed.size() >> 8));
  framed.push_back(static_cast<uint8_t>(unframed.size()));
  // data frame
  framed.push_back(0);
  // no flags
  framed.push_back(0);
  // stream id
  framed.push_back(0);
  framed.push_back(0);
  framed.push_back(0);
  framed.push_back(1);
  while (!unframed.empty()) {
    framed.push_back(unframed.front());
    unframed.pop();
  }

  return grpc_slice_from_copied_buffer(framed.data(), framed.size());
}

static void BM_TransportStreamRecv(benchmark::State& state) {
  TrackCounters track_counters;
  grpc_core::ExecCtx exec_ctx;
  Fixture f(grpc::ChannelArguments(), true);
  auto* s = new Stream(&f);
  s->Init(state);
  grpc_transport_stream_op_batch_payload op_payload(nullptr);
  grpc_transport_stream_op_batch op;
  grpc_core::OrphanablePtr<grpc_core::ByteStream> recv_stream;
  grpc_slice incoming_data = CreateIncomingDataSlice(state.range(0), 16384);

  auto reset_op = [&]() {
    op = {};
    op.payload = &op_payload;
  };

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  grpc_metadata_batch b_recv;
  grpc_metadata_batch_init(&b_recv);
  b.deadline = GRPC_MILLIS_INF_FUTURE;
  std::vector<grpc_mdelem> elems =
      RepresentativeClientInitialMetadata::GetElems();
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd", grpc_metadata_batch_add_tail(&b, &storage[i], elems[i])));
  }

  std::unique_ptr<TestClosure> do_nothing =
      MakeTestClosure([](grpc_error_handle /*error*/) {});

  uint32_t received;

  std::unique_ptr<TestClosure> drain_start;
  std::unique_ptr<TestClosure> drain;
  std::unique_ptr<TestClosure> drain_continue;
  grpc_slice recv_slice;

  std::unique_ptr<TestClosure> c =
      MakeTestClosure([&](grpc_error_handle /*error*/) {
        if (!state.KeepRunning()) return;
        // force outgoing window to be yuge
        s->chttp2_stream()->flow_control->TestOnlyForceHugeWindow();
        f.chttp2_transport()->flow_control->TestOnlyForceHugeWindow();
        received = 0;
        reset_op();
        op.on_complete = do_nothing.get();
        op.recv_message = true;
        op.payload->recv_message.recv_message = &recv_stream;
        op.payload->recv_message.call_failed_before_recv_message = nullptr;
        op.payload->recv_message.recv_message_ready = drain_start.get();
        s->Op(&op);
        f.PushInput(grpc_slice_ref(incoming_data));
      });

  drain_start = MakeTestClosure([&](grpc_error_handle /*error*/) {
    if (recv_stream == nullptr) {
      GPR_ASSERT(!state.KeepRunning());
      return;
    }
    grpc_core::Closure::Run(DEBUG_LOCATION, drain.get(), GRPC_ERROR_NONE);
  });

  drain = MakeTestClosure([&](grpc_error_handle /*error*/) {
    do {
      if (received == recv_stream->length()) {
        recv_stream.reset();
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c.get(), GRPC_ERROR_NONE);
        return;
      }
    } while (recv_stream->Next(recv_stream->length() - received,
                               drain_continue.get()) &&
             GRPC_ERROR_NONE == recv_stream->Pull(&recv_slice) &&
             (received += GRPC_SLICE_LENGTH(recv_slice),
              grpc_slice_unref_internal(recv_slice), true));
  });

  drain_continue = MakeTestClosure([&](grpc_error_handle /*error*/) {
    recv_stream->Pull(&recv_slice);
    received += GRPC_SLICE_LENGTH(recv_slice);
    grpc_slice_unref_internal(recv_slice);
    grpc_core::Closure::Run(DEBUG_LOCATION, drain.get(), GRPC_ERROR_NONE);
  });

  reset_op();
  op.send_initial_metadata = true;
  op.payload->send_initial_metadata.send_initial_metadata = &b;
  op.recv_initial_metadata = true;
  op.payload->recv_initial_metadata.recv_initial_metadata = &b_recv;
  op.payload->recv_initial_metadata.recv_initial_metadata_ready =
      do_nothing.get();
  op.on_complete = c.get();
  s->Op(&op);
  f.PushInput(SLICE_FROM_BUFFER(
      "\x00\x00\x00\x04\x00\x00\x00\x00\x00"
      // Generated using:
      // tools/codegen/core/gen_header_frame.py <
      // test/cpp/microbenchmarks/representative_server_initial_metadata.headers
      "\x00\x00X\x01\x04\x00\x00\x00\x01"
      "\x10\x07:status\x03"
      "200"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"));

  f.FlushExecCtx();
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
  grpc_metadata_batch_destroy(&b);
  grpc_metadata_batch_destroy(&b_recv);
  f.FlushExecCtx();
  track_counters.Finish(state);
  grpc_slice_unref(incoming_data);
}
BENCHMARK(BM_TransportStreamRecv)->Range(0, 128 * 1024 * 1024);

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
