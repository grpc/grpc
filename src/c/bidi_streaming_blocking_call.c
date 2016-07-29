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


#include <grpc_c/grpc_c.h>
#include <grpc_c/status.h>
#include <grpc_c/completion_queue.h>
#include <grpc/support/log.h>
#include <grpc/support/alloc.h>
#include "src/c/bidi_streaming_blocking_call.h"
#include "src/c/alloc.h"
#include "src/c/completion_queue.h"

GRPC_client_reader_writer *GRPC_bidi_streaming_blocking_call(const GRPC_method rpc_method,
                                                             GRPC_client_context *const context) {
  grpc_completion_queue *cq = GRPC_completion_queue_create();
  grpc_call *call = grpc_channel_create_call(context->channel,
                                             NULL,
                                             GRPC_PROPAGATE_DEFAULTS,
                                             cq,
                                             rpc_method.name,
                                             "",
                                             context->deadline,
                                             NULL);
  context->call = call;
  context->rpc_method = rpc_method;

  grpc_call_op_set set = {
    {
      grpc_op_send_metadata
    },
    .context = context,
    .user_tag = &set
  };

  grpc_client_reader_writer *reader_writer = GRPC_ALLOC_STRUCT(grpc_client_reader_writer, {
    .context = context,
    .call = call,
    .cq = cq,
  });

  grpc_start_batch_from_op_set(reader_writer->call, &set, reader_writer->context, (GRPC_message) {0, 0}, NULL);
  bool ok = GRPC_completion_queue_pluck_internal(cq, &set);
  if (!ok) {
    GRPC_client_reader_writer_terminate(reader_writer);
    return NULL;
  } else {
    return reader_writer;
  }
}

bool GRPC_bidi_streaming_blocking_read(GRPC_client_reader_writer *reader_writer, void *response) {
  grpc_call_op_set set_meta = {
    {
      grpc_op_recv_metadata,
      grpc_op_recv_object
    },
    .context = reader_writer->context,
    .user_tag = &set_meta
  };
  grpc_call_op_set set_no_meta = {
    {
      grpc_op_recv_object
    },
    .context = reader_writer->context,
    .user_tag = &set_no_meta
  };
  grpc_call_op_set *pSet = NULL;
  if (reader_writer->context->initial_metadata_received == false) {
    pSet = &set_meta;
  } else {
    pSet = &set_no_meta;
  }

  grpc_start_batch_from_op_set(reader_writer->call, pSet, reader_writer->context, (GRPC_message) {0, 0}, response);
  bool ok = GRPC_completion_queue_pluck_internal(reader_writer->cq, pSet);
  reader_writer->context->status.ok &= ok;
  return ok && pSet->message_received;
}

bool GRPC_bidi_streaming_blocking_write(GRPC_client_reader_writer *reader_writer, const GRPC_message request) {
  grpc_call_op_set set = {
    {
      grpc_op_send_object
    },
    .context = reader_writer->context,
    .user_tag = &set
  };

  grpc_start_batch_from_op_set(reader_writer->call, &set, reader_writer->context, request, NULL);
  bool ok = GRPC_completion_queue_pluck_internal(reader_writer->cq, &set);
  reader_writer->context->status.ok &= ok;
  return ok;
}

bool GRPC_bidi_streaming_blocking_writes_done(GRPC_client_reader_writer *reader_writer) {
  grpc_call_op_set set = {
    {
      grpc_op_send_close
    },
    .context = reader_writer->context,
    .user_tag = &set
  };

  grpc_start_batch_from_op_set(reader_writer->call, &set, reader_writer->context, (GRPC_message) {0, 0}, NULL);
  bool ok = GRPC_completion_queue_pluck_internal(reader_writer->cq, (&set));
  reader_writer->context->status.ok &= ok;
  return ok;
}

GRPC_status GRPC_client_reader_writer_terminate(GRPC_client_reader_writer *reader_writer) {
  grpc_call_op_set set = {
    {
      grpc_op_recv_status
    },
    .context = reader_writer->context,
    .user_tag = &set
  };
  grpc_start_batch_from_op_set(reader_writer->call, &set, reader_writer->context, (GRPC_message) {0, 0}, NULL);
  bool ok = GRPC_completion_queue_pluck_internal(reader_writer->cq, &set);
  GRPC_completion_queue_shutdown(reader_writer->cq);
  GRPC_completion_queue_shutdown_wait(reader_writer->cq);
  GRPC_completion_queue_destroy(reader_writer->cq);
  grpc_call_destroy(reader_writer->call);
  reader_writer->context->call = NULL;
  grpc_client_context *context = reader_writer->context;
  gpr_free(reader_writer);
  context->status.ok &= ok;
  return context->status;
}
