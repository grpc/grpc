/*
 *
 * Copyright 2014, Google Inc.
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

#include <grpc/support/port_platform.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>

#include <string.h>

#ifdef GPR_WIN32
#define GPR_EXPORT __declspec(dllexport)
#define GPR_CALLTYPE __stdcall
#endif

#ifndef GPR_EXPORT
#define GPR_EXPORT
#endif

#ifndef GPR_CALLTYPE
#define GPR_CALLTYPE
#endif

grpc_byte_buffer *string_to_byte_buffer(const char *buffer, size_t len) {
  gpr_slice slice = gpr_slice_from_copied_buffer(buffer, len);
  grpc_byte_buffer *bb = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);
  return bb;
}

/* Init & shutdown */

GPR_EXPORT void GPR_CALLTYPE grpcsharp_init(void) { grpc_init(); }

GPR_EXPORT void GPR_CALLTYPE grpcsharp_shutdown(void) { grpc_shutdown(); }

/* Completion queue */

GPR_EXPORT grpc_completion_queue *GPR_CALLTYPE
grpcsharp_completion_queue_create(void) {
  return grpc_completion_queue_create();
}

GPR_EXPORT grpc_event *GPR_CALLTYPE
grpcsharp_completion_queue_next(grpc_completion_queue *cq,
                                gpr_timespec deadline) {
  return grpc_completion_queue_next(cq, deadline);
}

GPR_EXPORT grpc_event *GPR_CALLTYPE
grpcsharp_completion_queue_pluck(grpc_completion_queue *cq, void *tag,
                                 gpr_timespec deadline) {
  return grpc_completion_queue_pluck(cq, tag, deadline);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_completion_queue_shutdown(grpc_completion_queue *cq) {
  grpc_completion_queue_shutdown(cq);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_completion_queue_destroy(grpc_completion_queue *cq) {
  grpc_completion_queue_destroy(cq);
}

GPR_EXPORT grpc_completion_type GPR_CALLTYPE
grpcsharp_completion_queue_next_with_callback(grpc_completion_queue *cq) {
  grpc_event *ev;
  grpc_completion_type t;
  void(GPR_CALLTYPE * callback)(grpc_event *);

  ev = grpc_completion_queue_next(cq, gpr_inf_future);
  t = ev->type;
  if (ev->tag) {
    /* call the callback in ev->tag */
    /* C forbids to cast object pointers to function pointers, so
     * we cast to intptr first.
     */
    callback = (void(GPR_CALLTYPE *)(grpc_event *))(gpr_intptr)ev->tag;
    (*callback)(ev);
  }
  grpc_event_finish(ev);

  /* return completion type to allow some handling for events that have no
   * tag - such as GRPC_QUEUE_SHUTDOWN
   */
  return t;
}

/* Channel */

GPR_EXPORT grpc_channel *GPR_CALLTYPE
grpcsharp_channel_create(const char *target, const grpc_channel_args *args) {
  return grpc_channel_create(target, args);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_channel_destroy(grpc_channel *channel) {
  grpc_channel_destroy(channel);
}

GPR_EXPORT grpc_call *GPR_CALLTYPE
grpcsharp_channel_create_call_old(grpc_channel *channel, const char *method,
                                  const char *host, gpr_timespec deadline) {
  return grpc_channel_create_call_old(channel, method, host, deadline);
}

/* Event */

GPR_EXPORT void GPR_CALLTYPE grpcsharp_event_finish(grpc_event *event) {
  grpc_event_finish(event);
}

GPR_EXPORT grpc_completion_type GPR_CALLTYPE
grpcsharp_event_type(const grpc_event *event) {
  return event->type;
}

GPR_EXPORT grpc_op_error GPR_CALLTYPE
grpcsharp_event_write_accepted(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_WRITE_ACCEPTED);
  return event->data.invoke_accepted;
}

GPR_EXPORT grpc_op_error GPR_CALLTYPE
grpcsharp_event_finish_accepted(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_FINISH_ACCEPTED);
  return event->data.finish_accepted;
}

GPR_EXPORT grpc_status_code GPR_CALLTYPE
grpcsharp_event_finished_status(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_FINISHED);
  return event->data.finished.status;
}

GPR_EXPORT const char *GPR_CALLTYPE
grpcsharp_event_finished_details(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_FINISHED);
  return event->data.finished.details;
}

GPR_EXPORT gpr_intptr GPR_CALLTYPE
grpcsharp_event_read_length(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_READ);
  if (!event->data.read) {
    return -1;
  }
  return grpc_byte_buffer_length(event->data.read);
}

/*
 * Copies data from read event to a buffer. Fatal error occurs if
 * buffer is too small.
 */
GPR_EXPORT void GPR_CALLTYPE
grpcsharp_event_read_copy_to_buffer(const grpc_event *event, char *buffer,
                                    size_t buffer_len) {
  grpc_byte_buffer_reader *reader;
  gpr_slice slice;
  size_t offset = 0;

  GPR_ASSERT(event->type == GRPC_READ);
  reader = grpc_byte_buffer_reader_create(event->data.read);

  GPR_ASSERT(event->data.read);
  while (grpc_byte_buffer_reader_next(reader, &slice)) {
    size_t len = GPR_SLICE_LENGTH(slice);
    GPR_ASSERT(offset + len <= buffer_len);
    memcpy(buffer + offset, GPR_SLICE_START_PTR(slice),
           GPR_SLICE_LENGTH(slice));
    offset += len;
    gpr_slice_unref(slice);
  }
  grpc_byte_buffer_reader_destroy(reader);
}

GPR_EXPORT grpc_call *GPR_CALLTYPE
grpcsharp_event_call(const grpc_event *event) {
  /* we only allow this for newly incoming server calls. */
  GPR_ASSERT(event->type == GRPC_SERVER_RPC_NEW);
  return event->call;
}

GPR_EXPORT const char *GPR_CALLTYPE
grpcsharp_event_server_rpc_new_method(const grpc_event *event) {
  GPR_ASSERT(event->type == GRPC_SERVER_RPC_NEW);
  return event->data.server_rpc_new.method;
}

/* Timespec */

GPR_EXPORT gpr_timespec GPR_CALLTYPE gprsharp_now(void) { return gpr_now(); }

GPR_EXPORT gpr_timespec GPR_CALLTYPE gprsharp_inf_future(void) {
  return gpr_inf_future;
}

GPR_EXPORT gpr_int32 GPR_CALLTYPE gprsharp_sizeof_timespec(void) {
  return sizeof(gpr_timespec);
}

/* Call */

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_add_metadata_old(grpc_call *call, grpc_metadata *metadata,
                                gpr_uint32 flags) {
  return grpc_call_add_metadata_old(call, metadata, flags);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_invoke_old(grpc_call *call, grpc_completion_queue *cq,
                          void *metadata_read_tag, void *finished_tag,
                          gpr_uint32 flags) {
  return grpc_call_invoke_old(call, cq, metadata_read_tag, finished_tag, flags);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_server_accept_old(grpc_call *call, grpc_completion_queue *cq,
                                 void *finished_tag) {
  return grpc_call_server_accept_old(call, cq, finished_tag);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_server_end_initial_metadata_old(grpc_call *call,
                                               gpr_uint32 flags) {
  return grpc_call_server_end_initial_metadata_old(call, flags);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE grpcsharp_call_cancel(grpc_call *call) {
  return grpc_call_cancel(call);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_cancel_with_status(grpc_call *call, grpc_status_code status,
                                  const char *description) {
  return grpc_call_cancel_with_status(call, status, description);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_start_write_old(grpc_call *call, grpc_byte_buffer *byte_buffer,
                               void *tag, gpr_uint32 flags) {
  return grpc_call_start_write_old(call, byte_buffer, tag, flags);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_start_write_status_old(grpc_call *call,
                                      grpc_status_code status_code,
                                      const char *status_message, void *tag) {
  return grpc_call_start_write_status_old(call, status_code, status_message,
                                          tag);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_writes_done_old(grpc_call *call, void *tag) {
  return grpc_call_writes_done_old(call, tag);
}

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_call_start_read_old(grpc_call *call, void *tag) {
  return grpc_call_start_read_old(call, tag);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_call_destroy(grpc_call *call) {
  grpc_call_destroy(call);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_call_start_write_from_copied_buffer(grpc_call *call,
                                              const char *buffer, size_t len,
                                              void *tag, gpr_uint32 flags) {
  grpc_byte_buffer *byte_buffer = string_to_byte_buffer(buffer, len);
  GPR_ASSERT(grpc_call_start_write_old(call, byte_buffer, tag, flags) ==
             GRPC_CALL_OK);
  grpc_byte_buffer_destroy(byte_buffer);
}

/* Server */

GPR_EXPORT grpc_call_error GPR_CALLTYPE
grpcsharp_server_request_call_old(grpc_server *server, void *tag_new) {
  return grpc_server_request_call_old(server, tag_new);
}

GPR_EXPORT grpc_server *GPR_CALLTYPE
grpcsharp_server_create(grpc_completion_queue *cq,
                        const grpc_channel_args *args) {
  return grpc_server_create(cq, args);
}

GPR_EXPORT int GPR_CALLTYPE
grpcsharp_server_add_http2_port(grpc_server *server, const char *addr) {
  return grpc_server_add_http2_port(server, addr);
}

GPR_EXPORT int GPR_CALLTYPE
grpcsharp_server_add_secure_http2_port(grpc_server *server, const char *addr) {
  return grpc_server_add_secure_http2_port(server, addr);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_start(grpc_server *server) {
  grpc_server_start(server);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_shutdown(grpc_server *server) {
  grpc_server_shutdown(server);
}

GPR_EXPORT void GPR_CALLTYPE
grpcsharp_server_shutdown_and_notify(grpc_server *server, void *tag) {
  grpc_server_shutdown_and_notify(server, tag);
}

GPR_EXPORT void GPR_CALLTYPE grpcsharp_server_destroy(grpc_server *server) {
  grpc_server_destroy(server);
}
