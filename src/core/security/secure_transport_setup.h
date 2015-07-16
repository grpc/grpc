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

#ifndef GRPC_INTERNAL_CORE_SECURITY_SECURE_TRANSPORT_SETUP_H
#define GRPC_INTERNAL_CORE_SECURITY_SECURE_TRANSPORT_SETUP_H

#include "src/core/iomgr/endpoint.h"
#include "src/core/security/security_connector.h"

/* --- Secure transport setup --- */

/* Ownership of the secure_endpoint is transfered. */
typedef void (*grpc_secure_transport_setup_done_cb)(
    void *user_data, grpc_security_status status,
    grpc_endpoint *wrapped_endpoint, grpc_endpoint *secure_endpoint);

/* Calls the callback upon completion. */
void grpc_setup_secure_transport(grpc_security_connector *connector,
                                 grpc_endpoint *nonsecure_endpoint,
                                 grpc_secure_transport_setup_done_cb cb,
                                 void *user_data);

#endif  /* GRPC_INTERNAL_CORE_SECURITY_SECURE_TRANSPORT_SETUP_H */
