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

#ifndef GRPC_INTERNAL_CORE_SURFACE_COMPLETION_QUEUE_H
#define GRPC_INTERNAL_CORE_SURFACE_COMPLETION_QUEUE_H

/* Internal API for completion channels */

#include "src/core/iomgr/pollset.h"
#include <grpc/grpc.h>

/* A finish func is executed whenever the event consumer calls
   grpc_event_finish */
typedef void (*grpc_event_finish_func)(void *user_data, grpc_op_error error);

/* Flag that an operation is beginning: the completion channel will not finish
   shutdown until a corrensponding grpc_cq_end_* call is made */
void grpc_cq_begin_op(grpc_completion_queue *cc, grpc_call *call,
                      grpc_completion_type type);

/* grpc_cq_end_* functions pair with a grpc_cq_begin_op

   grpc_cq_end_* common arguments:
   cc        - the completion channel to queue on
   tag       - the user supplied operation tag
   on_finish - grpc_event_finish_func that is called during grpc_event_finish
               can be NULL to not get a callback
   user_data - user_data parameter to be passed to on_finish

   Other parameters match the data member of grpc_event */

/* Queue a GRPC_READ operation */
void grpc_cq_end_read(grpc_completion_queue *cc, void *tag, grpc_call *call,
                      grpc_event_finish_func on_finish, void *user_data,
                      grpc_byte_buffer *read);
/* Queue a GRPC_INVOKE_ACCEPTED operation */
void grpc_cq_end_invoke_accepted(grpc_completion_queue *cc, void *tag,
                                 grpc_call *call,
                                 grpc_event_finish_func on_finish,
                                 void *user_data, grpc_op_error error);
/* Queue a GRPC_WRITE_ACCEPTED operation */
void grpc_cq_end_write_accepted(grpc_completion_queue *cc, void *tag,
                                grpc_call *call,
                                grpc_event_finish_func on_finish,
                                void *user_data, grpc_op_error error);
/* Queue a GRPC_FINISH_ACCEPTED operation */
void grpc_cq_end_finish_accepted(grpc_completion_queue *cc, void *tag,
                                 grpc_call *call,
                                 grpc_event_finish_func on_finish,
                                 void *user_data, grpc_op_error error);
/* Queue a GRPC_OP_COMPLETED operation */
void grpc_cq_end_op_complete(grpc_completion_queue *cc, void *tag,
                             grpc_call *call, grpc_event_finish_func on_finish,
                             void *user_data, grpc_op_error error);
/* Queue a GRPC_CLIENT_METADATA_READ operation */
void grpc_cq_end_client_metadata_read(grpc_completion_queue *cc, void *tag,
                                      grpc_call *call,
                                      grpc_event_finish_func on_finish,
                                      void *user_data, size_t count,
                                      grpc_metadata *elements);

void grpc_cq_end_finished(grpc_completion_queue *cc, void *tag, grpc_call *call,
                          grpc_event_finish_func on_finish, void *user_data,
                          grpc_status_code status, const char *details,
                          grpc_metadata *metadata_elements,
                          size_t metadata_count);

void grpc_cq_end_new_rpc(grpc_completion_queue *cc, void *tag, grpc_call *call,
                         grpc_event_finish_func on_finish, void *user_data,
                         const char *method, const char *host,
                         gpr_timespec deadline, size_t metadata_count,
                         grpc_metadata *metadata_elements);

void grpc_cq_end_op(grpc_completion_queue *cc, void *tag, grpc_call *call,
                    grpc_event_finish_func on_finish, void *user_data,
                    grpc_op_error error);

void grpc_cq_end_server_shutdown(grpc_completion_queue *cc, void *tag);

/* disable polling for some tests */
void grpc_completion_queue_dont_poll_test_only(grpc_completion_queue *cc);

void grpc_cq_dump_pending_ops(grpc_completion_queue *cc);

grpc_pollset *grpc_cq_pollset(grpc_completion_queue *cc);

#endif  /* GRPC_INTERNAL_CORE_SURFACE_COMPLETION_QUEUE_H */
