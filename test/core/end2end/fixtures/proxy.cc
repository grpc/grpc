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

#include "test/core/end2end/fixtures/proxy.h"

#include <string.h>

#include <string>
#include <utility>

#include <grpc/byte_buffer.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/surface/call.h"
#include "test/core/util/port.h"

struct grpc_end2end_proxy {
  grpc_end2end_proxy()
      : cq(nullptr),
        server(nullptr),
        client(nullptr),
        shutdown(false),
        new_call(nullptr) {
    memset(&new_call_details, 0, sizeof(new_call_details));
    memset(&new_call_metadata, 0, sizeof(new_call_metadata));
  }
  grpc_core::Thread thd;
  std::string proxy_port;
  std::string server_port;
  grpc_completion_queue* cq;
  grpc_server* server;
  grpc_channel* client;

  int shutdown;

  // requested call
  grpc_call* new_call;
  grpc_call_details new_call_details;
  grpc_metadata_array new_call_metadata;
};

typedef struct {
  void (*func)(void* arg, int success);
  void* arg;
} closure;

typedef struct {
  gpr_refcount refs;
  grpc_end2end_proxy* proxy;

  grpc_call* c2p;
  grpc_call* p2s;

  grpc_metadata_array c2p_initial_metadata;
  grpc_metadata_array p2s_initial_metadata;

  grpc_byte_buffer* c2p_msg;
  grpc_byte_buffer* p2s_msg;

  grpc_metadata_array p2s_trailing_metadata;
  grpc_status_code p2s_status;
  grpc_slice p2s_status_details;

  int c2p_server_cancelled;
} proxy_call;

static void thread_main(void* arg);
static void request_call(grpc_end2end_proxy* proxy);

grpc_end2end_proxy* grpc_end2end_proxy_create(
    const grpc_end2end_proxy_def* def, const grpc_channel_args* client_args,
    const grpc_channel_args* server_args) {
  int proxy_port = grpc_pick_unused_port_or_die();
  int server_port = grpc_pick_unused_port_or_die();

  grpc_end2end_proxy* proxy = new grpc_end2end_proxy();

  proxy->proxy_port = grpc_core::JoinHostPort("localhost", proxy_port);
  proxy->server_port = grpc_core::JoinHostPort("localhost", server_port);

  gpr_log(GPR_DEBUG, "PROXY ADDR:%s BACKEND:%s", proxy->proxy_port.c_str(),
          proxy->server_port.c_str());

  proxy->cq = grpc_completion_queue_create_for_next(nullptr);
  proxy->server = def->create_server(proxy->proxy_port.c_str(), server_args);

  const char* arg_to_remove = GRPC_ARG_ENABLE_RETRIES;
  grpc_arg arg_to_add = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ENABLE_RETRIES), 0);
  const grpc_channel_args* proxy_client_args =
      grpc_channel_args_copy_and_add_and_remove(client_args, &arg_to_remove, 1,
                                                &arg_to_add, 1);
  proxy->client =
      def->create_client(proxy->server_port.c_str(), proxy_client_args);
  grpc_channel_args_destroy(proxy_client_args);

  grpc_server_register_completion_queue(proxy->server, proxy->cq, nullptr);
  grpc_server_start(proxy->server);

  grpc_call_details_init(&proxy->new_call_details);
  proxy->thd = grpc_core::Thread("grpc_end2end_proxy", thread_main, proxy);
  proxy->thd.Start();

  request_call(proxy);

  return proxy;
}

static closure* new_closure(void (*func)(void* arg, int success), void* arg) {
  closure* cl = static_cast<closure*>(gpr_malloc(sizeof(*cl)));
  cl->func = func;
  cl->arg = arg;
  return cl;
}

static void shutdown_complete(void* arg, int /*success*/) {
  grpc_end2end_proxy* proxy = static_cast<grpc_end2end_proxy*>(arg);
  proxy->shutdown = 1;
  grpc_completion_queue_shutdown(proxy->cq);
}

void grpc_end2end_proxy_destroy(grpc_end2end_proxy* proxy) {
  grpc_server_shutdown_and_notify(proxy->server, proxy->cq,
                                  new_closure(shutdown_complete, proxy));
  proxy->thd.Join();
  grpc_server_destroy(proxy->server);
  grpc_channel_destroy(proxy->client);
  grpc_completion_queue_destroy(proxy->cq);
  grpc_call_details_destroy(&proxy->new_call_details);
  delete proxy;
}

static void unrefpc(proxy_call* pc, const char* /*reason*/) {
  if (gpr_unref(&pc->refs)) {
    grpc_call_unref(pc->c2p);
    grpc_call_unref(pc->p2s);
    grpc_metadata_array_destroy(&pc->c2p_initial_metadata);
    grpc_metadata_array_destroy(&pc->p2s_initial_metadata);
    grpc_metadata_array_destroy(&pc->p2s_trailing_metadata);
    grpc_slice_unref(pc->p2s_status_details);
    gpr_free(pc);
  }
}

static void refpc(proxy_call* pc, const char* /*reason*/) {
  gpr_ref(&pc->refs);
}

static void on_c2p_sent_initial_metadata(void* arg, int /*success*/) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  unrefpc(pc, "on_c2p_sent_initial_metadata");
}

static void on_p2s_recv_initial_metadata(void* arg, int /*success*/) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  grpc_op op;
  grpc_call_error err;

  memset(&op, 0, sizeof(op));
  if (!pc->proxy->shutdown && !grpc_call_is_trailers_only(pc->p2s)) {
    op.op = GRPC_OP_SEND_INITIAL_METADATA;
    op.flags = 0;
    op.reserved = nullptr;
    op.data.send_initial_metadata.count = pc->p2s_initial_metadata.count;
    op.data.send_initial_metadata.metadata = pc->p2s_initial_metadata.metadata;
    refpc(pc, "on_c2p_sent_initial_metadata");
    err = grpc_call_start_batch(pc->c2p, &op, 1,
                                new_closure(on_c2p_sent_initial_metadata, pc),
                                nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);
  }

  unrefpc(pc, "on_p2s_recv_initial_metadata");
}

static void on_p2s_sent_initial_metadata(void* arg, int /*success*/) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  unrefpc(pc, "on_p2s_sent_initial_metadata");
}

static void on_c2p_recv_msg(void* arg, int success);

static void on_p2s_sent_message(void* arg, int success) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  grpc_op op;
  grpc_call_error err;

  grpc_byte_buffer_destroy(std::exchange(pc->c2p_msg, nullptr));
  if (!pc->proxy->shutdown && success) {
    op.op = GRPC_OP_RECV_MESSAGE;
    op.flags = 0;
    op.reserved = nullptr;
    op.data.recv_message.recv_message = &pc->c2p_msg;
    refpc(pc, "on_c2p_recv_msg");
    err = grpc_call_start_batch(pc->c2p, &op, 1,
                                new_closure(on_c2p_recv_msg, pc), nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);
  }

  unrefpc(pc, "on_p2s_sent_message");
}

static void on_p2s_sent_close(void* arg, int /*success*/) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  unrefpc(pc, "on_p2s_sent_close");
}

static void on_c2p_recv_msg(void* arg, int success) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  grpc_op op;
  grpc_call_error err;

  if (!pc->proxy->shutdown && success) {
    if (pc->c2p_msg != nullptr) {
      op.op = GRPC_OP_SEND_MESSAGE;
      op.flags = 0;
      op.reserved = nullptr;
      op.data.send_message.send_message = pc->c2p_msg;
      refpc(pc, "on_p2s_sent_message");
      err = grpc_call_start_batch(
          pc->p2s, &op, 1, new_closure(on_p2s_sent_message, pc), nullptr);
      GPR_ASSERT(err == GRPC_CALL_OK);
    } else {
      op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
      op.flags = 0;
      op.reserved = nullptr;
      refpc(pc, "on_p2s_sent_close");
      err = grpc_call_start_batch(pc->p2s, &op, 1,
                                  new_closure(on_p2s_sent_close, pc), nullptr);
      GPR_ASSERT(err == GRPC_CALL_OK);
    }
  } else {
    if (pc->c2p_msg != nullptr) {
      grpc_byte_buffer_destroy(pc->c2p_msg);
    }
  }

  unrefpc(pc, "on_c2p_recv_msg");
}

static void on_p2s_recv_msg(void* arg, int success);

static void on_c2p_sent_message(void* arg, int success) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  grpc_op op;
  grpc_call_error err;

  grpc_byte_buffer_destroy(pc->p2s_msg);
  if (!pc->proxy->shutdown && success) {
    op.op = GRPC_OP_RECV_MESSAGE;
    op.flags = 0;
    op.reserved = nullptr;
    op.data.recv_message.recv_message = &pc->p2s_msg;
    refpc(pc, "on_p2s_recv_msg");
    err = grpc_call_start_batch(pc->p2s, &op, 1,
                                new_closure(on_p2s_recv_msg, pc), nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);
  }

  unrefpc(pc, "on_c2p_sent_message");
}

static void on_p2s_recv_msg(void* arg, int success) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  grpc_op op;
  grpc_call_error err;

  if (!pc->proxy->shutdown && success && pc->p2s_msg) {
    op.op = GRPC_OP_SEND_MESSAGE;
    op.flags = 0;
    op.reserved = nullptr;
    op.data.send_message.send_message = pc->p2s_msg;
    refpc(pc, "on_c2p_sent_message");
    err = grpc_call_start_batch(pc->c2p, &op, 1,
                                new_closure(on_c2p_sent_message, pc), nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);
  } else {
    grpc_byte_buffer_destroy(pc->p2s_msg);
  }
  unrefpc(pc, "on_p2s_recv_msg");
}

static void on_c2p_sent_status(void* arg, int /*success*/) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  unrefpc(pc, "on_c2p_sent_status");
}

static void on_p2s_status(void* arg, int success) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  grpc_op op[2];  // Possibly send empty initial metadata also if trailers-only
  grpc_call_error err;

  memset(op, 0, sizeof(op));

  if (!pc->proxy->shutdown) {
    GPR_ASSERT(success);

    int op_count = 0;
    if (grpc_call_is_trailers_only(pc->p2s)) {
      op[op_count].op = GRPC_OP_SEND_INITIAL_METADATA;
      op_count++;
    }

    op[op_count].op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    op[op_count].flags = 0;
    op[op_count].reserved = nullptr;
    op[op_count].data.send_status_from_server.trailing_metadata_count =
        pc->p2s_trailing_metadata.count;
    op[op_count].data.send_status_from_server.trailing_metadata =
        pc->p2s_trailing_metadata.metadata;
    op[op_count].data.send_status_from_server.status = pc->p2s_status;
    op[op_count].data.send_status_from_server.status_details =
        &pc->p2s_status_details;
    op_count++;
    refpc(pc, "on_c2p_sent_status");
    err = grpc_call_start_batch(pc->c2p, op, op_count,
                                new_closure(on_c2p_sent_status, pc), nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);
  }

  unrefpc(pc, "on_p2s_status");
}

static void on_c2p_closed(void* arg, int /*success*/) {
  proxy_call* pc = static_cast<proxy_call*>(arg);
  unrefpc(pc, "on_c2p_closed");
}

static void on_new_call(void* arg, int success) {
  grpc_end2end_proxy* proxy = static_cast<grpc_end2end_proxy*>(arg);
  grpc_call_error err;

  if (success) {
    grpc_op op;
    memset(&op, 0, sizeof(op));
    proxy_call* pc = static_cast<proxy_call*>(gpr_malloc(sizeof(*pc)));
    memset(pc, 0, sizeof(*pc));
    pc->proxy = proxy;
    std::swap(pc->c2p_initial_metadata, proxy->new_call_metadata);
    pc->c2p = proxy->new_call;
    pc->p2s = grpc_channel_create_call(
        proxy->client, pc->c2p, GRPC_PROPAGATE_DEFAULTS, proxy->cq,
        proxy->new_call_details.method, &proxy->new_call_details.host,
        proxy->new_call_details.deadline, nullptr);
    gpr_ref_init(&pc->refs, 1);

    op.reserved = nullptr;

    op.op = GRPC_OP_RECV_INITIAL_METADATA;
    op.flags = 0;
    op.data.recv_initial_metadata.recv_initial_metadata =
        &pc->p2s_initial_metadata;
    refpc(pc, "on_p2s_recv_initial_metadata");
    err = grpc_call_start_batch(pc->p2s, &op, 1,
                                new_closure(on_p2s_recv_initial_metadata, pc),
                                nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);

    op.op = GRPC_OP_SEND_INITIAL_METADATA;
    op.flags = 0;
    op.data.send_initial_metadata.count = pc->c2p_initial_metadata.count;
    op.data.send_initial_metadata.metadata = pc->c2p_initial_metadata.metadata;
    refpc(pc, "on_p2s_sent_initial_metadata");
    err = grpc_call_start_batch(pc->p2s, &op, 1,
                                new_closure(on_p2s_sent_initial_metadata, pc),
                                nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);

    op.op = GRPC_OP_RECV_MESSAGE;
    op.flags = 0;
    op.data.recv_message.recv_message = &pc->c2p_msg;
    refpc(pc, "on_c2p_recv_msg");
    err = grpc_call_start_batch(pc->c2p, &op, 1,
                                new_closure(on_c2p_recv_msg, pc), nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);

    op.op = GRPC_OP_RECV_MESSAGE;
    op.flags = 0;
    op.data.recv_message.recv_message = &pc->p2s_msg;
    refpc(pc, "on_p2s_recv_msg");
    err = grpc_call_start_batch(pc->p2s, &op, 1,
                                new_closure(on_p2s_recv_msg, pc), nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);

    op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op.flags = 0;
    op.data.recv_status_on_client.trailing_metadata =
        &pc->p2s_trailing_metadata;
    op.data.recv_status_on_client.status = &pc->p2s_status;
    op.data.recv_status_on_client.status_details = &pc->p2s_status_details;
    refpc(pc, "on_p2s_status");
    err = grpc_call_start_batch(pc->p2s, &op, 1, new_closure(on_p2s_status, pc),
                                nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);

    op.op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    op.flags = 0;
    op.data.recv_close_on_server.cancelled = &pc->c2p_server_cancelled;
    refpc(pc, "on_c2p_closed");
    err = grpc_call_start_batch(pc->c2p, &op, 1, new_closure(on_c2p_closed, pc),
                                nullptr);
    GPR_ASSERT(err == GRPC_CALL_OK);

    request_call(proxy);

    grpc_call_details_destroy(&proxy->new_call_details);
    grpc_call_details_init(&proxy->new_call_details);

    unrefpc(pc, "init");
  } else {
    GPR_ASSERT(proxy->new_call == nullptr);
  }
}

static void request_call(grpc_end2end_proxy* proxy) {
  proxy->new_call = nullptr;
  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 proxy->server, &proxy->new_call,
                                 &proxy->new_call_details,
                                 &proxy->new_call_metadata, proxy->cq,
                                 proxy->cq, new_closure(on_new_call, proxy)));
}

static void thread_main(void* arg) {
  grpc_end2end_proxy* proxy = static_cast<grpc_end2end_proxy*>(arg);
  closure* cl;
  for (;;) {
    grpc_event ev = grpc_completion_queue_next(
        proxy->cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
    switch (ev.type) {
      case GRPC_QUEUE_TIMEOUT:
        grpc_core::Crash("Should never reach here");
      case GRPC_QUEUE_SHUTDOWN:
        return;
      case GRPC_OP_COMPLETE:
        cl = static_cast<closure*>(ev.tag);
        cl->func(cl->arg, ev.success);
        gpr_free(cl);
        break;
    }
  }
}

const char* grpc_end2end_proxy_get_client_target(grpc_end2end_proxy* proxy) {
  return proxy->proxy_port.c_str();
}

const char* grpc_end2end_proxy_get_server_port(grpc_end2end_proxy* proxy) {
  return proxy->server_port.c_str();
}
