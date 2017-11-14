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

#include <grpc++/support/channel_arguments.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <string.h>
#include <memory>
#include <queue>
#include <sstream>
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/static_metadata.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

auto& force_library_initialization = Library::get();

////////////////////////////////////////////////////////////////////////////////
// Helper classes
//

class DummyEndpoint : public grpc_endpoint {
 public:
  DummyEndpoint() {
    static const grpc_endpoint_vtable my_vtable = {read,
                                                   write,
                                                   add_to_pollset,
                                                   add_to_pollset_set,
                                                   delete_from_pollset_set,
                                                   shutdown,
                                                   destroy,
                                                   get_resource_user,
                                                   get_peer,
                                                   get_fd};
    grpc_endpoint::vtable = &my_vtable;
    ru_ = grpc_resource_user_create(Library::get().rq(), "dummy_endpoint");
  }

  void PushInput(grpc_exec_ctx* exec_ctx, grpc_slice slice) {
    if (read_cb_ == nullptr) {
      GPR_ASSERT(!have_slice_);
      buffered_slice_ = slice;
      have_slice_ = true;
      return;
    }
    grpc_slice_buffer_add(slices_, slice);
    GRPC_CLOSURE_SCHED(exec_ctx, read_cb_, GRPC_ERROR_NONE);
    read_cb_ = nullptr;
  }

 private:
  grpc_resource_user* ru_;
  grpc_closure* read_cb_ = nullptr;
  grpc_slice_buffer* slices_ = nullptr;
  bool have_slice_ = false;
  grpc_slice buffered_slice_;

  void QueueRead(grpc_exec_ctx* exec_ctx, grpc_slice_buffer* slices,
                 grpc_closure* cb) {
    GPR_ASSERT(read_cb_ == nullptr);
    if (have_slice_) {
      have_slice_ = false;
      grpc_slice_buffer_add(slices, buffered_slice_);
      GRPC_CLOSURE_SCHED(exec_ctx, cb, GRPC_ERROR_NONE);
      return;
    }
    read_cb_ = cb;
    slices_ = slices;
  }

  static void read(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep,
                   grpc_slice_buffer* slices, grpc_closure* cb) {
    static_cast<DummyEndpoint*>(ep)->QueueRead(exec_ctx, slices, cb);
  }

  static void write(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep,
                    grpc_slice_buffer* slices, grpc_closure* cb) {
    GRPC_CLOSURE_SCHED(exec_ctx, cb, GRPC_ERROR_NONE);
  }

  static grpc_workqueue* get_workqueue(grpc_endpoint* ep) { return nullptr; }

  static void add_to_pollset(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep,
                             grpc_pollset* pollset) {}

  static void add_to_pollset_set(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep,
                                 grpc_pollset_set* pollset) {}

  static void delete_from_pollset_set(grpc_exec_ctx* exec_ctx,
                                      grpc_endpoint* ep,
                                      grpc_pollset_set* pollset) {}

  static void shutdown(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep,
                       grpc_error* why) {
    grpc_resource_user_shutdown(exec_ctx, static_cast<DummyEndpoint*>(ep)->ru_);
    GRPC_CLOSURE_SCHED(exec_ctx, static_cast<DummyEndpoint*>(ep)->read_cb_,
                       why);
  }

  static void destroy(grpc_exec_ctx* exec_ctx, grpc_endpoint* ep) {
    grpc_resource_user_unref(exec_ctx, static_cast<DummyEndpoint*>(ep)->ru_);
    delete static_cast<DummyEndpoint*>(ep);
  }

  static grpc_resource_user* get_resource_user(grpc_endpoint* ep) {
    return static_cast<DummyEndpoint*>(ep)->ru_;
  }
  static char* get_peer(grpc_endpoint* ep) { return gpr_strdup("test"); }
  static int get_fd(grpc_endpoint* ep) { return 0; }
};

class Fixture {
 public:
  Fixture(const grpc::ChannelArguments& args, bool client) {
    grpc_channel_args c_args = args.c_channel_args();
    ep_ = new DummyEndpoint;
    t_ = grpc_create_chttp2_transport(exec_ctx(), &c_args, ep_, client);
    grpc_chttp2_transport_start_reading(exec_ctx(), t_, nullptr);
    FlushExecCtx();
  }

  void FlushExecCtx() { grpc_exec_ctx_flush(&exec_ctx_); }

  ~Fixture() {
    grpc_transport_destroy(&exec_ctx_, t_);
    grpc_exec_ctx_finish(&exec_ctx_);
  }

  grpc_chttp2_transport* chttp2_transport() {
    return reinterpret_cast<grpc_chttp2_transport*>(t_);
  }
  grpc_transport* transport() { return t_; }
  grpc_exec_ctx* exec_ctx() { return &exec_ctx_; }

  void PushInput(grpc_slice slice) { ep_->PushInput(exec_ctx(), slice); }

 private:
  DummyEndpoint* ep_;
  grpc_exec_ctx exec_ctx_ = GRPC_EXEC_CTX_INIT;
  grpc_transport* t_;
};

class Closure : public grpc_closure {
 public:
  virtual ~Closure() {}
};

template <class F>
std::unique_ptr<Closure> MakeClosure(
    F f, grpc_closure_scheduler* sched = grpc_schedule_on_exec_ctx) {
  struct C : public Closure {
    C(const F& f, grpc_closure_scheduler* sched) : f_(f) {
      GRPC_CLOSURE_INIT(this, Execute, this, sched);
    }
    F f_;
    static void Execute(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
      static_cast<C*>(arg)->f_(exec_ctx, error);
    }
  };
  return std::unique_ptr<Closure>(new C(f, sched));
}

template <class F>
grpc_closure* MakeOnceClosure(
    F f, grpc_closure_scheduler* sched = grpc_schedule_on_exec_ctx) {
  struct C : public grpc_closure {
    C(const F& f) : f_(f) {}
    F f_;
    static void Execute(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
      static_cast<C*>(arg)->f_(exec_ctx, error);
      delete static_cast<C*>(arg);
    }
  };
  auto* c = new C{f};
  return GRPC_CLOSURE_INIT(c, C::Execute, c, sched);
}

class Stream {
 public:
  Stream(Fixture* f) : f_(f) {
    stream_size_ = grpc_transport_stream_size(f->transport());
    stream_ = gpr_malloc(stream_size_);
    arena_ = gpr_arena_create(4096);
  }

  ~Stream() {
    gpr_event_wait(&done_, gpr_inf_future(GPR_CLOCK_REALTIME));
    gpr_free(stream_);
    gpr_arena_destroy(arena_);
  }

  void Init(benchmark::State& state) {
    GRPC_STREAM_REF_INIT(&refcount_, 1, &Stream::FinishDestroy, this,
                         "test_stream");
    gpr_event_init(&done_);
    memset(stream_, 0, stream_size_);
    if ((state.iterations() & 0xffff) == 0) {
      gpr_arena_destroy(arena_);
      arena_ = gpr_arena_create(4096);
    }
    grpc_transport_init_stream(f_->exec_ctx(), f_->transport(),
                               static_cast<grpc_stream*>(stream_), &refcount_,
                               nullptr, arena_);
  }

  void DestroyThen(grpc_exec_ctx* exec_ctx, grpc_closure* closure) {
    destroy_closure_ = closure;
#ifndef NDEBUG
    grpc_stream_unref(exec_ctx, &refcount_, "DestroyThen");
#else
    grpc_stream_unref(exec_ctx, &refcount_);
#endif
  }

  void Op(grpc_exec_ctx* exec_ctx, grpc_transport_stream_op_batch* op) {
    grpc_transport_perform_stream_op(exec_ctx, f_->transport(),
                                     static_cast<grpc_stream*>(stream_), op);
  }

  grpc_chttp2_stream* chttp2_stream() {
    return static_cast<grpc_chttp2_stream*>(stream_);
  }

 private:
  static void FinishDestroy(grpc_exec_ctx* exec_ctx, void* arg,
                            grpc_error* error) {
    auto stream = static_cast<Stream*>(arg);
    grpc_transport_destroy_stream(exec_ctx, stream->f_->transport(),
                                  static_cast<grpc_stream*>(stream->stream_),
                                  stream->destroy_closure_);
    gpr_event_set(&stream->done_, (void*)1);
  }

  Fixture* f_;
  grpc_stream_refcount refcount_;
  gpr_arena* arena_;
  size_t stream_size_;
  void* stream_;
  grpc_closure* destroy_closure_ = nullptr;
  gpr_event done_;
};

////////////////////////////////////////////////////////////////////////////////
// Benchmarks
//

static void BM_StreamCreateDestroy(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload;
  memset(&op, 0, sizeof(op));
  op.cancel_stream = true;
  op.payload = &op_payload;
  op_payload.cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
  std::unique_ptr<Closure> next =
      MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
        if (!state.KeepRunning()) return;
        s.Init(state);
        s.Op(exec_ctx, &op);
        s.DestroyThen(exec_ctx, next.get());
      });
  GRPC_CLOSURE_RUN(f.exec_ctx(), next.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  track_counters.Finish(state);
}
BENCHMARK(BM_StreamCreateDestroy);

class RepresentativeClientInitialMetadata {
 public:
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx* exec_ctx) {
    return {
        GRPC_MDELEM_SCHEME_HTTP,
        GRPC_MDELEM_METHOD_POST,
        grpc_mdelem_from_slices(
            exec_ctx, GRPC_MDSTR_PATH,
            grpc_slice_intern(grpc_slice_from_static_string("/foo/bar"))),
        grpc_mdelem_from_slices(exec_ctx, GRPC_MDSTR_AUTHORITY,
                                grpc_slice_intern(grpc_slice_from_static_string(
                                    "foo.test.google.fr:1234"))),
        GRPC_MDELEM_GRPC_ACCEPT_ENCODING_IDENTITY_COMMA_DEFLATE_COMMA_GZIP,
        GRPC_MDELEM_TE_TRAILERS,
        GRPC_MDELEM_CONTENT_TYPE_APPLICATION_SLASH_GRPC,
        grpc_mdelem_from_slices(
            exec_ctx, GRPC_MDSTR_USER_AGENT,
            grpc_slice_intern(grpc_slice_from_static_string(
                "grpc-c/3.0.0-dev (linux; chttp2; green)")))};
  }
};

template <class Metadata>
static void BM_StreamCreateSendInitialMetadataDestroy(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload;
  memset(&op_payload, 0, sizeof(op_payload));
  std::unique_ptr<Closure> start;
  std::unique_ptr<Closure> done;

  auto reset_op = [&]() {
    memset(&op, 0, sizeof(op));
    op.payload = &op_payload;
  };

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  b.deadline = GRPC_MILLIS_INF_FUTURE;
  std::vector<grpc_mdelem> elems = Metadata::GetElems(f.exec_ctx());
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd",
        grpc_metadata_batch_add_tail(f.exec_ctx(), &b, &storage[i], elems[i])));
  }

  f.FlushExecCtx();
  start = MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
    if (!state.KeepRunning()) return;
    s.Init(state);
    reset_op();
    op.on_complete = done.get();
    op.send_initial_metadata = true;
    op.payload->send_initial_metadata.send_initial_metadata = &b;
    s.Op(exec_ctx, &op);
  });
  done = MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
    reset_op();
    op.cancel_stream = true;
    op.payload->cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
    s.Op(exec_ctx, &op);
    s.DestroyThen(exec_ctx, start.get());
  });
  GRPC_CLOSURE_SCHED(f.exec_ctx(), start.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  grpc_metadata_batch_destroy(f.exec_ctx(), &b);
  track_counters.Finish(state);
}
BENCHMARK_TEMPLATE(BM_StreamCreateSendInitialMetadataDestroy,
                   RepresentativeClientInitialMetadata);

static void BM_TransportEmptyOp(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  s.Init(state);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload;
  memset(&op_payload, 0, sizeof(op_payload));
  auto reset_op = [&]() {
    memset(&op, 0, sizeof(op));
    op.payload = &op_payload;
  };
  std::unique_ptr<Closure> c =
      MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
        if (!state.KeepRunning()) return;
        reset_op();
        op.on_complete = c.get();
        s.Op(exec_ctx, &op);
      });
  GRPC_CLOSURE_SCHED(f.exec_ctx(), c.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  reset_op();
  op.cancel_stream = true;
  op_payload.cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
  s.Op(f.exec_ctx(), &op);
  s.DestroyThen(f.exec_ctx(), MakeOnceClosure([](grpc_exec_ctx* exec_ctx,
                                                 grpc_error* error) {}));
  f.FlushExecCtx();
  track_counters.Finish(state);
}
BENCHMARK(BM_TransportEmptyOp);

std::vector<std::unique_ptr<gpr_event>> done_events;

static void BM_TransportStreamSend(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  auto s = std::unique_ptr<Stream>(new Stream(&f));
  s->Init(state);
  grpc_transport_stream_op_batch op;
  grpc_transport_stream_op_batch_payload op_payload;
  memset(&op_payload, 0, sizeof(op_payload));
  auto reset_op = [&]() {
    memset(&op, 0, sizeof(op));
    op.payload = &op_payload;
  };
  grpc_slice_buffer_stream send_stream;
  grpc_slice_buffer send_buffer;
  grpc_slice_buffer_init(&send_buffer);
  grpc_slice_buffer_add(&send_buffer, gpr_slice_malloc(state.range(0)));
  memset(GRPC_SLICE_START_PTR(send_buffer.slices[0]), 0,
         GRPC_SLICE_LENGTH(send_buffer.slices[0]));

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  b.deadline = GRPC_MILLIS_INF_FUTURE;
  std::vector<grpc_mdelem> elems =
      RepresentativeClientInitialMetadata::GetElems(f.exec_ctx());
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd",
        grpc_metadata_batch_add_tail(f.exec_ctx(), &b, &storage[i], elems[i])));
  }

  gpr_event* bm_done = new gpr_event;
  gpr_event_init(bm_done);

  std::unique_ptr<Closure> c =
      MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
        if (!state.KeepRunning()) {
          gpr_event_set(bm_done, (void*)1);
          return;
        }
        // force outgoing window to be yuge
        s->chttp2_stream()->flow_control->TestOnlyForceHugeWindow();
        f.chttp2_transport()->flow_control->TestOnlyForceHugeWindow();
        grpc_slice_buffer_stream_init(&send_stream, &send_buffer, 0);
        reset_op();
        op.on_complete = c.get();
        op.send_message = true;
        op.payload->send_message.send_message = &send_stream.base;
        s->Op(exec_ctx, &op);
      });

  reset_op();
  op.send_initial_metadata = true;
  op.payload->send_initial_metadata.send_initial_metadata = &b;
  op.on_complete = c.get();
  s->Op(f.exec_ctx(), &op);

  f.FlushExecCtx();
  gpr_event_wait(bm_done, gpr_inf_future(GPR_CLOCK_REALTIME));
  done_events.emplace_back(bm_done);

  reset_op();
  op.cancel_stream = true;
  op.payload->cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
  s->Op(f.exec_ctx(), &op);
  s->DestroyThen(f.exec_ctx(), MakeOnceClosure([](grpc_exec_ctx* exec_ctx,
                                                  grpc_error* error) {}));
  f.FlushExecCtx();
  s.reset();
  track_counters.Finish(state);
  grpc_metadata_batch_destroy(f.exec_ctx(), &b);
  grpc_slice_buffer_destroy(&send_buffer);
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
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  s.Init(state);
  grpc_transport_stream_op_batch_payload op_payload;
  memset(&op_payload, 0, sizeof(op_payload));
  grpc_transport_stream_op_batch op;
  grpc_byte_stream* recv_stream;
  grpc_slice incoming_data = CreateIncomingDataSlice(state.range(0), 16384);

  auto reset_op = [&]() {
    memset(&op, 0, sizeof(op));
    op.payload = &op_payload;
  };

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  grpc_metadata_batch b_recv;
  grpc_metadata_batch_init(&b_recv);
  b.deadline = GRPC_MILLIS_INF_FUTURE;
  std::vector<grpc_mdelem> elems =
      RepresentativeClientInitialMetadata::GetElems(f.exec_ctx());
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd",
        grpc_metadata_batch_add_tail(f.exec_ctx(), &b, &storage[i], elems[i])));
  }

  std::unique_ptr<Closure> do_nothing =
      MakeClosure([](grpc_exec_ctx* exec_ctx, grpc_error* error) {});

  uint32_t received;

  std::unique_ptr<Closure> drain_start;
  std::unique_ptr<Closure> drain;
  std::unique_ptr<Closure> drain_continue;
  grpc_slice recv_slice;

  std::unique_ptr<Closure> c =
      MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
        if (!state.KeepRunning()) return;
        // force outgoing window to be yuge
        s.chttp2_stream()->flow_control->TestOnlyForceHugeWindow();
        f.chttp2_transport()->flow_control->TestOnlyForceHugeWindow();
        received = 0;
        reset_op();
        op.on_complete = do_nothing.get();
        op.recv_message = true;
        op.payload->recv_message.recv_message = &recv_stream;
        op.payload->recv_message.recv_message_ready = drain_start.get();
        s.Op(exec_ctx, &op);
        f.PushInput(grpc_slice_ref(incoming_data));
      });

  drain_start = MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
    if (recv_stream == nullptr) {
      GPR_ASSERT(!state.KeepRunning());
      return;
    }
    GRPC_CLOSURE_RUN(exec_ctx, drain.get(), GRPC_ERROR_NONE);
  });

  drain = MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
    do {
      if (received == recv_stream->length) {
        grpc_byte_stream_destroy(exec_ctx, recv_stream);
        GRPC_CLOSURE_SCHED(exec_ctx, c.get(), GRPC_ERROR_NONE);
        return;
      }
    } while (grpc_byte_stream_next(exec_ctx, recv_stream,
                                   recv_stream->length - received,
                                   drain_continue.get()) &&
             GRPC_ERROR_NONE ==
                 grpc_byte_stream_pull(exec_ctx, recv_stream, &recv_slice) &&
             (received += GRPC_SLICE_LENGTH(recv_slice),
              grpc_slice_unref_internal(exec_ctx, recv_slice), true));
  });

  drain_continue = MakeClosure([&](grpc_exec_ctx* exec_ctx, grpc_error* error) {
    grpc_byte_stream_pull(exec_ctx, recv_stream, &recv_slice);
    received += GRPC_SLICE_LENGTH(recv_slice);
    grpc_slice_unref_internal(exec_ctx, recv_slice);
    GRPC_CLOSURE_RUN(exec_ctx, drain.get(), GRPC_ERROR_NONE);
  });

  reset_op();
  op.send_initial_metadata = true;
  op.payload->send_initial_metadata.send_initial_metadata = &b;
  op.recv_initial_metadata = true;
  op.payload->recv_initial_metadata.recv_initial_metadata = &b_recv;
  op.payload->recv_initial_metadata.recv_initial_metadata_ready =
      do_nothing.get();
  op.on_complete = c.get();
  s.Op(f.exec_ctx(), &op);
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
  s.Op(f.exec_ctx(), &op);
  s.DestroyThen(f.exec_ctx(), MakeOnceClosure([](grpc_exec_ctx* exec_ctx,
                                                 grpc_error* error) {}));
  f.FlushExecCtx();
  track_counters.Finish(state);
  grpc_metadata_batch_destroy(f.exec_ctx(), &b);
  grpc_metadata_batch_destroy(f.exec_ctx(), &b_recv);
  grpc_slice_unref(incoming_data);
}
BENCHMARK(BM_TransportStreamRecv)->Range(0, 128 * 1024 * 1024);

BENCHMARK_MAIN();
