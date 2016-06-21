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


#ifndef TEST_GRPC_C_COMPLETION_QUEUE_PUBLIC_H
#define TEST_GRPC_C_COMPLETION_QUEUE_PUBLIC_H

#include <stdbool.h>

typedef grpc_completion_queue GRPC_completion_queue;

/// Tri-state return for GRPC_completion_queue_next
enum GRPC_completion_queue_next_status {
  GRPC_COMPLETION_QUEUE_SHUTDOWN,   ///< The completion queue has been shutdown.
  GRPC_COMPLETION_QUEUE_GOT_EVENT,  ///< Got a new event; \a tag will be filled in with its
                                    ///< associated value; \a ok indicating its success.
  GRPC_COMPLETION_QUEUE_TIMEOUT     ///< deadline was reached.
};

/// \return true if read a regular event, false if the queue is shutting down.
bool GRPC_completion_queue_next(GRPC_completion_queue *cq, void** tag, bool* ok);

#endif //TEST_GRPC_C_COMPLETION_QUEUE_PUBLIC_H
