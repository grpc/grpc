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

#ifndef GRPC_INTERNAL_CORE_CHANNEL_CLIENT_SETUP_H
#define GRPC_INTERNAL_CORE_CHANNEL_CLIENT_SETUP_H

#include "src/core/channel/client_channel.h"
#include "src/core/transport/metadata.h"
#include <grpc/support/time.h>

/* Convenience API's to simplify transport setup */

typedef struct grpc_client_setup grpc_client_setup;
typedef struct grpc_client_setup_request grpc_client_setup_request;

void grpc_client_setup_create_and_attach(
    grpc_channel_stack *newly_minted_channel, const grpc_channel_args *args,
    grpc_mdctx *mdctx,
    void (*initiate)(void *user_data, grpc_client_setup_request *request),
    void (*done)(void *user_data), void *user_data);

/* Check that r is the active request: needs to be performed at each callback.
   If this races, we'll have two connection attempts running at once and the
   old one will get cleaned up in due course, which is fine. */
int grpc_client_setup_request_should_continue(grpc_client_setup_request *r);
void grpc_client_setup_request_finish(grpc_client_setup_request *r,
                                      int was_successful);
const grpc_channel_args *grpc_client_setup_get_channel_args(
    grpc_client_setup_request *r);

/* Call before calling back into the setup listener, and call only if
   this function returns 1. If it returns 1, also promise to call
   grpc_client_setup_cb_end */
int grpc_client_setup_cb_begin(grpc_client_setup_request *r);
void grpc_client_setup_cb_end(grpc_client_setup_request *r);

/* Get the deadline for a request passed in to initiate. Implementations should
   make a best effort to honor this deadline. */
gpr_timespec grpc_client_setup_request_deadline(grpc_client_setup_request *r);

grpc_mdctx *grpc_client_setup_get_mdctx(grpc_client_setup_request *r);

#endif  /* GRPC_INTERNAL_CORE_CHANNEL_CLIENT_SETUP_H */
