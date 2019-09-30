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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/connectivity_state.h"

/* These routines are here to facilitate debugging - they produce string
   representations of various transport data structures */

static void put_metadata(gpr_strvec* b, grpc_mdelem md) {
  gpr_strvec_add(b, gpr_strdup("key="));
  gpr_strvec_add(
      b, grpc_dump_slice(GRPC_MDKEY(md), GPR_DUMP_HEX | GPR_DUMP_ASCII));

  gpr_strvec_add(b, gpr_strdup(" value="));
  gpr_strvec_add(
      b, grpc_dump_slice(GRPC_MDVALUE(md), GPR_DUMP_HEX | GPR_DUMP_ASCII));
}

static void put_metadata_list(gpr_strvec* b, grpc_metadata_batch md) {
  grpc_linked_mdelem* m;
  for (m = md.list.head; m != nullptr; m = m->next) {
    if (m != md.list.head) gpr_strvec_add(b, gpr_strdup(", "));
    put_metadata(b, m->md);
  }
  if (md.deadline != GRPC_MILLIS_INF_FUTURE) {
    char* tmp;
    gpr_asprintf(&tmp, " deadline=%" PRId64, md.deadline);
    gpr_strvec_add(b, tmp);
  }
}

char* grpc_transport_stream_op_batch_string(
    grpc_transport_stream_op_batch* op) {
  char* tmp;
  char* out;

  gpr_strvec b;
  gpr_strvec_init(&b);

  if (op->send_initial_metadata) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("SEND_INITIAL_METADATA{"));
    put_metadata_list(
        &b, *op->payload->send_initial_metadata.send_initial_metadata);
    gpr_strvec_add(&b, gpr_strdup("}"));
  }

  if (op->send_message) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    if (op->payload->send_message.send_message != nullptr) {
      gpr_asprintf(&tmp, "SEND_MESSAGE:flags=0x%08x:len=%d",
                   op->payload->send_message.send_message->flags(),
                   op->payload->send_message.send_message->length());
    } else {
      // This can happen when we check a batch after the transport has
      // processed and cleared the send_message op.
      tmp =
          gpr_strdup("SEND_MESSAGE(flag and length unknown, already orphaned)");
    }
    gpr_strvec_add(&b, tmp);
  }

  if (op->send_trailing_metadata) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("SEND_TRAILING_METADATA{"));
    put_metadata_list(
        &b, *op->payload->send_trailing_metadata.send_trailing_metadata);
    gpr_strvec_add(&b, gpr_strdup("}"));
  }

  if (op->recv_initial_metadata) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("RECV_INITIAL_METADATA"));
  }

  if (op->recv_message) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("RECV_MESSAGE"));
  }

  if (op->recv_trailing_metadata) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("RECV_TRAILING_METADATA"));
  }

  if (op->cancel_stream) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    const char* msg =
        grpc_error_string(op->payload->cancel_stream.cancel_error);
    gpr_asprintf(&tmp, "CANCEL:%s", msg);

    gpr_strvec_add(&b, tmp);
  }

  out = gpr_strvec_flatten(&b, nullptr);
  gpr_strvec_destroy(&b);

  return out;
}

char* grpc_transport_op_string(grpc_transport_op* op) {
  char* tmp;
  char* out;
  bool first = true;

  gpr_strvec b;
  gpr_strvec_init(&b);

  if (op->start_connectivity_watch != nullptr) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    gpr_asprintf(
        &tmp, "START_CONNECTIVITY_WATCH:watcher=%p:from=%s",
        op->start_connectivity_watch.get(),
        grpc_core::ConnectivityStateName(op->start_connectivity_watch_state));
    gpr_strvec_add(&b, tmp);
  }

  if (op->stop_connectivity_watch != nullptr) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    gpr_asprintf(&tmp, "STOP_CONNECTIVITY_WATCH:watcher=%p",
                 op->stop_connectivity_watch);
    gpr_strvec_add(&b, tmp);
  }

  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    const char* err = grpc_error_string(op->disconnect_with_error);
    gpr_asprintf(&tmp, "DISCONNECT:%s", err);
    gpr_strvec_add(&b, tmp);
  }

  if (op->goaway_error) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    const char* msg = grpc_error_string(op->goaway_error);
    gpr_asprintf(&tmp, "SEND_GOAWAY:%s", msg);

    gpr_strvec_add(&b, tmp);
  }

  if (op->set_accept_stream) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    gpr_asprintf(&tmp, "SET_ACCEPT_STREAM:%p(%p,...)", op->set_accept_stream_fn,
                 op->set_accept_stream_user_data);
    gpr_strvec_add(&b, tmp);
  }

  if (op->bind_pollset != nullptr) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    gpr_strvec_add(&b, gpr_strdup("BIND_POLLSET"));
  }

  if (op->bind_pollset_set != nullptr) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    gpr_strvec_add(&b, gpr_strdup("BIND_POLLSET_SET"));
  }

  if (op->send_ping.on_initiate != nullptr || op->send_ping.on_ack != nullptr) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    // first = false;
    gpr_strvec_add(&b, gpr_strdup("SEND_PING"));
  }

  out = gpr_strvec_flatten(&b, nullptr);
  gpr_strvec_destroy(&b);

  return out;
}

void grpc_call_log_op(const char* file, int line, gpr_log_severity severity,
                      grpc_call_element* elem,
                      grpc_transport_stream_op_batch* op) {
  char* str = grpc_transport_stream_op_batch_string(op);
  gpr_log(file, line, severity, "OP[%s:%p]: %s", elem->filter->name, elem, str);
  gpr_free(str);
}
