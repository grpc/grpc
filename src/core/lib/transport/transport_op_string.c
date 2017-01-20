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

#include "src/core/lib/channel/channel_stack.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/connectivity_state.h"

/* These routines are here to facilitate debugging - they produce string
   representations of various transport data structures */

static void put_metadata(gpr_strvec *b, grpc_mdelem md) {
  gpr_strvec_add(b, gpr_strdup("key="));
  gpr_strvec_add(
      b, grpc_dump_slice(GRPC_MDKEY(md), GPR_DUMP_HEX | GPR_DUMP_ASCII));

  gpr_strvec_add(b, gpr_strdup(" value="));
  gpr_strvec_add(
      b, grpc_dump_slice(GRPC_MDVALUE(md), GPR_DUMP_HEX | GPR_DUMP_ASCII));
}

static void put_metadata_list(gpr_strvec *b, grpc_metadata_batch md) {
  grpc_linked_mdelem *m;
  for (m = md.list.head; m != NULL; m = m->next) {
    if (m != md.list.head) gpr_strvec_add(b, gpr_strdup(", "));
    put_metadata(b, m->md);
  }
  if (gpr_time_cmp(md.deadline, gpr_inf_future(md.deadline.clock_type)) != 0) {
    char *tmp;
    gpr_asprintf(&tmp, " deadline=%" PRId64 ".%09d", md.deadline.tv_sec,
                 md.deadline.tv_nsec);
    gpr_strvec_add(b, tmp);
  }
}

char *grpc_transport_stream_op_string(grpc_transport_stream_op *op) {
  char *tmp;
  char *out;

  gpr_strvec b;
  gpr_strvec_init(&b);

  gpr_strvec_add(
      &b, gpr_strdup(op->covered_by_poller ? "[COVERED]" : "[UNCOVERED]"));

  if (op->send_initial_metadata != NULL) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("SEND_INITIAL_METADATA{"));
    put_metadata_list(&b, *op->send_initial_metadata);
    gpr_strvec_add(&b, gpr_strdup("}"));
  }

  if (op->send_message != NULL) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_asprintf(&tmp, "SEND_MESSAGE:flags=0x%08x:len=%d",
                 op->send_message->flags, op->send_message->length);
    gpr_strvec_add(&b, tmp);
  }

  if (op->send_trailing_metadata != NULL) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("SEND_TRAILING_METADATA{"));
    put_metadata_list(&b, *op->send_trailing_metadata);
    gpr_strvec_add(&b, gpr_strdup("}"));
  }

  if (op->recv_initial_metadata != NULL) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("RECV_INITIAL_METADATA"));
  }

  if (op->recv_message != NULL) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("RECV_MESSAGE"));
  }

  if (op->recv_trailing_metadata != NULL) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    gpr_strvec_add(&b, gpr_strdup("RECV_TRAILING_METADATA"));
  }

  if (op->cancel_error != GRPC_ERROR_NONE) {
    gpr_strvec_add(&b, gpr_strdup(" "));
    const char *msg = grpc_error_string(op->cancel_error);
    gpr_asprintf(&tmp, "CANCEL:%s", msg);

    gpr_strvec_add(&b, tmp);
  }

  out = gpr_strvec_flatten(&b, NULL);
  gpr_strvec_destroy(&b);

  return out;
}

char *grpc_transport_op_string(grpc_transport_op *op) {
  char *tmp;
  char *out;
  bool first = true;

  gpr_strvec b;
  gpr_strvec_init(&b);

  if (op->on_connectivity_state_change != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    if (op->connectivity_state != NULL) {
      gpr_asprintf(&tmp, "ON_CONNECTIVITY_STATE_CHANGE:p=%p:from=%s",
                   op->on_connectivity_state_change,
                   grpc_connectivity_state_name(*op->connectivity_state));
      gpr_strvec_add(&b, tmp);
    } else {
      gpr_asprintf(&tmp, "ON_CONNECTIVITY_STATE_CHANGE:p=%p:unsubscribe",
                   op->on_connectivity_state_change);
      gpr_strvec_add(&b, tmp);
    }
  }

  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    const char *err = grpc_error_string(op->disconnect_with_error);
    gpr_asprintf(&tmp, "DISCONNECT:%s", err);
    gpr_strvec_add(&b, tmp);
  }

  if (op->goaway_error) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    const char *msg = grpc_error_string(op->goaway_error);
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

  if (op->bind_pollset != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    gpr_strvec_add(&b, gpr_strdup("BIND_POLLSET"));
  }

  if (op->bind_pollset_set != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    gpr_strvec_add(&b, gpr_strdup("BIND_POLLSET_SET"));
  }

  if (op->send_ping != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = false;
    gpr_strvec_add(&b, gpr_strdup("SEND_PING"));
  }

  out = gpr_strvec_flatten(&b, NULL);
  gpr_strvec_destroy(&b);

  return out;
}

void grpc_call_log_op(char *file, int line, gpr_log_severity severity,
                      grpc_call_element *elem, grpc_transport_stream_op *op) {
  char *str = grpc_transport_stream_op_string(op);
  gpr_log(file, line, severity, "OP[%s:%p]: %s", elem->filter->name, elem, str);
  gpr_free(str);
}
