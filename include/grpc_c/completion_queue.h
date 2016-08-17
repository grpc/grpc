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

#ifndef GRPC_C_COMPLETION_QUEUE_H
#define GRPC_C_COMPLETION_QUEUE_H

#include <grpc_c/grpc_c.h>
#include <stdbool.h>

typedef struct gpr_timespec GRPC_timespec;

/** Tri-state return for GRPC_completion_queue_next */
typedef enum GRPC_completion_queue_next_status {
  /**
   * The completion queue has been shutdown.
   * It is guaranteed no more events will be posted.
   * The listening thread may exit.
   */
  GRPC_COMPLETION_QUEUE_SHUTDOWN,

  /* Got a new event; \a tag will be filled in with its */
  /* associated value; \a ok indicating its success.    */
  GRPC_COMPLETION_QUEUE_GOT_EVENT,

  /* Deadline was reached. */
  GRPC_COMPLETION_QUEUE_TIMEOUT
} GRPC_completion_queue_operation_status;

/**
 * Creates a completion queue. You can listen for new events on it.
 */
GRPC_completion_queue *GRPC_completion_queue_create();

/**
 * Shuts down the completion queue. Call GRPC_completion_queue_shutdown_wait
 * to drain all pending events before destroying this queue.
 */
void GRPC_completion_queue_shutdown(GRPC_completion_queue *cq);

/**
 * Destroys the completion queue and frees resources. The queue must be fully
 * shutdown before this call.
 */
void GRPC_completion_queue_destroy(GRPC_completion_queue *cq);

/**
 * Swallows events and blocks until it sees the shutdown event.
 */
void GRPC_completion_queue_shutdown_wait(GRPC_completion_queue *cq);

/**
 * Wait for a new event on this completion queue. The event may represent
 * completion of a read or write operation, or an incoming call (applicable to
 * server) etc.
 * \a ok indicates if the operation is successful.
 */
GRPC_completion_queue_operation_status GRPC_completion_queue_next(
    GRPC_completion_queue *cq, void **tag, bool *ok);

/**
 * Same as GRPC_completion_queue_next, but lets you specify an execution
 * deadline.
 */
GRPC_completion_queue_operation_status GRPC_completion_queue_next_deadline(
    GRPC_completion_queue *cq, GRPC_timespec deadline, void **tag, bool *ok);

#endif /* GRPC_C_COMPLETION_QUEUE_H */
