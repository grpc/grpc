/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Microbenchmarks around CHTTP2 transport operations */

#include <grpc++/support/channel_arguments.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <string.h>
#include <memory>
#include <sstream>
extern "C" {
  #include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
  #include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/static_metadata.h"
}
#include "test/cpp/microbenchmarks/helpers.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

auto &force_library_initialization = Library::get();

////////////////////////////////////////////////////////////////////////////////
// Helper classes
//

class DummyEndpoint : public grpc_endpoint {
 public:
  DummyEndpoint() {
    static const grpc_endpoint_vtable my_vtable = {read,
                                                   write,
                                                   get_workqueue,
                                                   add_to_pollset,
                                                   add_to_pollset_set,
                                                   shutdown,
                                                   destroy,
                                                   get_resource_user,
                                                   get_peer,
                                                   get_fd};
    grpc_endpoint::vtable = &my_vtable;
    ru_ = grpc_resource_user_create(Library::get().rq(), "dummy_endpoint");
  }

 private:
  grpc_resource_user *ru_;
  grpc_closure *read_cb_ = nullptr;

  static void read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                   grpc_slice_buffer *slices, grpc_closure *cb) {
    static_cast<DummyEndpoint *>(ep)->read_cb_ = cb;
  }

  static void write(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                    grpc_slice_buffer *slices, grpc_closure *cb) {
    grpc_closure_sched(exec_ctx, cb, GRPC_ERROR_NONE);
  }

  static grpc_workqueue *get_workqueue(grpc_endpoint *ep) { return NULL; }

  static void add_to_pollset(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                             grpc_pollset *pollset) {}

  static void add_to_pollset_set(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                 grpc_pollset_set *pollset) {}

  static void shutdown(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                       grpc_error *why) {
    grpc_resource_user_shutdown(exec_ctx,
                                static_cast<DummyEndpoint *>(ep)->ru_);
    grpc_closure_sched(exec_ctx, static_cast<DummyEndpoint *>(ep)->read_cb_,
                       why);
  }

  static void destroy(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
    grpc_resource_user_unref(exec_ctx, static_cast<DummyEndpoint *>(ep)->ru_);
    delete static_cast<DummyEndpoint *>(ep);
  }

  static grpc_resource_user *get_resource_user(grpc_endpoint *ep) {
    return static_cast<DummyEndpoint *>(ep)->ru_;
  }
  static char *get_peer(grpc_endpoint *ep) { return gpr_strdup("test"); }
  static int get_fd(grpc_endpoint *ep) { return 0; }
};

class Fixture {
 public:
  Fixture(const grpc::ChannelArguments &args, bool client) {
    grpc_channel_args c_args = args.c_channel_args();
    t_ = grpc_create_chttp2_transport(&exec_ctx_, &c_args, new DummyEndpoint,
                                      client);
  }

  void FlushExecCtx() { grpc_exec_ctx_flush(&exec_ctx_); }

  ~Fixture() {
    grpc_transport_destroy(&exec_ctx_, t_);
    grpc_exec_ctx_finish(&exec_ctx_);
  }

grpc_chttp2_transport *chttp2_transport() { return reinterpret_cast<grpc_chttp2_transport*>(t_);}
  grpc_transport *transport() { return t_; }
  grpc_exec_ctx *exec_ctx() { return &exec_ctx_; }

 private:
  grpc_exec_ctx exec_ctx_ = GRPC_EXEC_CTX_INIT;
  grpc_transport *t_;
};

static void DoNothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}

class Stream {
 public:
  Stream(Fixture *f) : f_(f) {
    GRPC_STREAM_REF_INIT(&refcount_, 1, DoNothing, nullptr, "test_stream");
    stream_size_ = grpc_transport_stream_size(f->transport());
    stream_ = gpr_malloc(stream_size_);
    arena_ = gpr_arena_create(4096);
  }

  ~Stream() {
    gpr_free(stream_);
    gpr_arena_destroy(arena_);
  }

  void Init(benchmark::State &state) {
    memset(stream_, 0, stream_size_);
    if ((state.iterations() & 0xffff) == 0) {
      gpr_arena_destroy(arena_);
      arena_ = gpr_arena_create(4096);
    }
    grpc_transport_init_stream(f_->exec_ctx(), f_->transport(),
                               static_cast<grpc_stream *>(stream_), &refcount_,
                               NULL, arena_);
  }

  void DestroyThen(grpc_closure *closure) {
    grpc_transport_destroy_stream(f_->exec_ctx(), f_->transport(),
                                  static_cast<grpc_stream *>(stream_), closure);
  }

  void Op(grpc_transport_stream_op *op) {
    grpc_transport_perform_stream_op(f_->exec_ctx(), f_->transport(),
                                     static_cast<grpc_stream *>(stream_), op);
  }

  grpc_chttp2_stream *chttp2_stream() {
    return static_cast<grpc_chttp2_stream*>(stream_);
  }

 private:
  Fixture *f_;
  grpc_stream_refcount refcount_;
  gpr_arena *arena_;
  size_t stream_size_;
  void *stream_;
};

class Closure : public grpc_closure {
 public:
  virtual ~Closure() {}
};

template <class F>
std::unique_ptr<Closure> MakeClosure(
    F f, grpc_closure_scheduler *sched = grpc_schedule_on_exec_ctx) {
  struct C : public Closure {
    C(const F &f, grpc_closure_scheduler *sched) : f_(f) {
      grpc_closure_init(this, Execute, this, sched);
    }
    F f_;
    static void Execute(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
      static_cast<C *>(arg)->f_(exec_ctx, error);
    }
  };
  return std::unique_ptr<Closure>(new C(f, sched));
}

template <class F>
grpc_closure *MakeOnceClosure(
    F f, grpc_closure_scheduler *sched = grpc_schedule_on_exec_ctx) {
  struct C : public grpc_closure {
    C(const F &f) : f_(f) {}
    F f_;
    static void Execute(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
      static_cast<C *>(arg)->f_(exec_ctx, error);
      delete static_cast<C *>(arg);
    }
  };
  auto *c = new C{f};
  return grpc_closure_init(c, C::Execute, c, sched);
}

////////////////////////////////////////////////////////////////////////////////
// Benchmarks
//

static void BM_StreamCreateDestroy(benchmark::State &state) {
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  std::unique_ptr<Closure> next =
      MakeClosure([&](grpc_exec_ctx *exec_ctx, grpc_error *error) {
        if (!state.KeepRunning()) return;
        s.Init(state);
        s.DestroyThen(next.get());
      });
  grpc_closure_run(f.exec_ctx(), next.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  track_counters.Finish(state);
}
BENCHMARK(BM_StreamCreateDestroy);

class RepresentativeClientInitialMetadata {
 public:
  static std::vector<grpc_mdelem> GetElems(grpc_exec_ctx *exec_ctx) {
    return {
        GRPC_MDELEM_SCHEME_HTTP, GRPC_MDELEM_METHOD_POST,
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
static void BM_StreamCreateSendInitialMetadataDestroy(benchmark::State &state) {
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  grpc_transport_stream_op op;
  std::unique_ptr<Closure> start;
  std::unique_ptr<Closure> done;

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  b.deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  std::vector<grpc_mdelem> elems = Metadata::GetElems(f.exec_ctx());
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd",
        grpc_metadata_batch_add_tail(f.exec_ctx(), &b, &storage[i], elems[i])));
  }

  f.FlushExecCtx();
  start = MakeClosure([&](grpc_exec_ctx *exec_ctx, grpc_error *error) {
    if (!state.KeepRunning()) return;
    s.Init(state);
    memset(&op, 0, sizeof(op));
    op.on_complete = done.get();
    op.send_initial_metadata = &b;
    s.Op(&op);
  });
  done = MakeClosure([&](grpc_exec_ctx *exec_ctx, grpc_error *error) {
    memset(&op, 0, sizeof(op));
    op.cancel_error = GRPC_ERROR_CANCELLED;
    s.Op(&op);
    s.DestroyThen(start.get());
  });
  grpc_closure_sched(f.exec_ctx(), start.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  grpc_metadata_batch_destroy(f.exec_ctx(), &b);
  track_counters.Finish(state);
}
BENCHMARK_TEMPLATE(BM_StreamCreateSendInitialMetadataDestroy,
                   RepresentativeClientInitialMetadata);

static void BM_TransportEmptyOp(benchmark::State &state) {
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  s.Init(state);
  grpc_transport_stream_op op;
  std::unique_ptr<Closure> c =
      MakeClosure([&](grpc_exec_ctx *exec_ctx, grpc_error *error) {
        if (!state.KeepRunning()) return;
        memset(&op, 0, sizeof(op));
        op.on_complete = c.get();
        s.Op(&op);
      });
  grpc_closure_sched(f.exec_ctx(), c.get(), GRPC_ERROR_NONE);
  f.FlushExecCtx();
  s.DestroyThen(
      MakeOnceClosure([](grpc_exec_ctx *exec_ctx, grpc_error *error) {}));
  f.FlushExecCtx();
  track_counters.Finish(state);
}
BENCHMARK(BM_TransportEmptyOp);

static void BM_TransportStreamSend(benchmark::State &state) {
  TrackCounters track_counters;
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  s.Init(state);
  grpc_transport_stream_op op;
  grpc_slice_buffer_stream send_stream;
  grpc_slice_buffer send_buffer;
  grpc_slice_buffer_init(&send_buffer);
  grpc_slice_buffer_add(&send_buffer, gpr_slice_malloc(state.range(0)));
  memset(GRPC_SLICE_START_PTR(send_buffer.slices[0]), 0, GRPC_SLICE_LENGTH(send_buffer.slices[0]));

  grpc_metadata_batch b;
  grpc_metadata_batch_init(&b);
  b.deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  std::vector<grpc_mdelem> elems = RepresentativeClientInitialMetadata::GetElems(f.exec_ctx());
  std::vector<grpc_linked_mdelem> storage(elems.size());
  for (size_t i = 0; i < elems.size(); i++) {
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "addmd",
        grpc_metadata_batch_add_tail(f.exec_ctx(), &b, &storage[i], elems[i])));
  }

  std::unique_ptr<Closure> c =
      MakeClosure([&](grpc_exec_ctx *exec_ctx, grpc_error *error) {
        if (!state.KeepRunning()) return;
        // force outgoing window to be yuge
        s.chttp2_stream()->outgoing_window_delta = 1024*1024*1024;
        f.chttp2_transport()->outgoing_window = 1024*1024*1024;
        grpc_slice_buffer_stream_init(&send_stream, &send_buffer, 0);
        memset(&op, 0, sizeof(op));
        op.on_complete = c.get();
        op.send_message = &send_stream.base;
        s.Op(&op);
      });

  memset(&op,0,sizeof(op));
  op.send_initial_metadata = &b;
  op.on_complete = MakeOnceClosure([&](grpc_exec_ctx *exec_ctx, grpc_error *error) {
    grpc_closure_sched(f.exec_ctx(), c.get(), GRPC_ERROR_NONE);
  });
  s.Op(&op);

  f.FlushExecCtx();
  memset(&op, 0, sizeof(op));
  op.cancel_error = GRPC_ERROR_CANCELLED;
  s.Op(&op);
  s.DestroyThen(
      MakeOnceClosure([](grpc_exec_ctx *exec_ctx, grpc_error *error) {}));
  f.FlushExecCtx();
  track_counters.Finish(state);
}
BENCHMARK(BM_TransportStreamSend)->Range(0,100*1024*1024);

BENCHMARK_MAIN();
