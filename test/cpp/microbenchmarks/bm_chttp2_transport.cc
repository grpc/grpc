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
#include <sstream>
extern "C" {
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/iomgr/resource_quota.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/static_metadata.h"
}
#include "third_party/benchmark/include/benchmark/benchmark.h"

static struct Init {
  Init() {
    grpc_init();
    quota = grpc_resource_quota_create("test");
  }
  ~Init() {
    grpc_resource_quota_unref(quota);
    grpc_shutdown();
  }

  grpc_resource_quota *quota;
} g_init;

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
    ru_ = grpc_resource_user_create(g_init.quota, "dummy_endpoint");
  }

 private:
  grpc_resource_user *ru_;

  static void read(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                   grpc_slice_buffer *slices, grpc_closure *cb) {
    grpc_closure_sched(exec_ctx, cb, GRPC_ERROR_CANCELLED);
  }

  static void write(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                    grpc_slice_buffer *slices, grpc_closure *cb) {
    grpc_closure_sched(exec_ctx, cb, GRPC_ERROR_CANCELLED);
  }

  static grpc_workqueue *get_workqueue(grpc_endpoint *ep) { return NULL; }

  static void add_to_pollset(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                             grpc_pollset *pollset) {}

  static void add_to_pollset_set(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                                 grpc_pollset_set *pollset) {}

  static void shutdown(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep,
                       grpc_error *why) {}

  static void destroy(grpc_exec_ctx *exec_ctx, grpc_endpoint *ep) {
    grpc_resource_user_unref(exec_ctx, static_cast<DummyEndpoint *>(ep)->ru_);
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
    stream_ = gpr_malloc(grpc_transport_stream_size(f->transport()));
  }

  ~Stream() { gpr_free(stream_); }

  void Init() {
    grpc_transport_init_stream(f_->exec_ctx(), f_->transport(),
                               static_cast<grpc_stream *>(stream_), &refcount_,
                               NULL);
  }

  void Destroy() {
    grpc_transport_destroy_stream(f_->exec_ctx(), f_->transport(),
                                  static_cast<grpc_stream *>(stream_), NULL);
  }

  void Op(grpc_transport_stream_op *op) {
    grpc_transport_perform_stream_op(f_->exec_ctx(), f_->transport(),
                                     static_cast<grpc_stream *>(stream_), op);
  }

 private:
  Fixture *f_;
  grpc_stream_refcount refcount_;
  void *stream_;
};

////////////////////////////////////////////////////////////////////////////////
// Benchmarks
//

static void BM_StreamCreateDestroy(benchmark::State &state) {
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  while (state.KeepRunning()) {
    s.Init();
    s.Destroy();
    f.FlushExecCtx();
  }
}
BENCHMARK(BM_StreamCreateDestroy);

static void BM_TransportEmptyOp(benchmark::State &state) {
  Fixture f(grpc::ChannelArguments(), true);
  Stream s(&f);
  s.Init();
  grpc_closure c;
  while (state.KeepRunning()) {
    grpc_transport_stream_op op;
    memset(&op, 0, sizeof(op));
    op.on_complete =
        grpc_closure_init(&c, DoNothing, NULL, grpc_schedule_on_exec_ctx);
    s.Op(&op);
    f.FlushExecCtx();
  }
  s.Destroy();
  f.FlushExecCtx();
}
BENCHMARK(BM_TransportEmptyOp);

BENCHMARK_MAIN();
