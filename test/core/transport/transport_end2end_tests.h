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

#ifndef GRPC_TEST_CORE_TRANSPORT_TRANSPORT_END2END_TESTS_H
#define GRPC_TEST_CORE_TRANSPORT_TRANSPORT_END2END_TESTS_H

#include "src/core/transport/transport.h"

/* Defines a suite of tests that all GRPC transports should be able to pass */

/* A test configuration has a name and a factory method */
typedef struct grpc_transport_test_config {
  /* The name of this configuration */
  char *name;
  /* Create a transport
     Returns 0 on success

     Arguments:
       OUT: client           - the created client half of the transport
       IN:  client_callbacks - callback structure to be used by the client
                               transport
       IN:  client_user_data - user data pointer to be passed into each client
                               callback
       OUT: server           - the created server half of the transport
       IN:  server_callbacks - callback structure to be used by the server
                               transport
       IN:  server_user_data - user data pointer to be passed into each
                               server */
  int (*create_transport)(grpc_transport_setup_callback client_setup,
                          void *client_arg,
                          grpc_transport_setup_callback server_setup,
                          void *server_arg, grpc_mdctx *mdctx);
} grpc_transport_test_config;

/* Run the test suite on one configuration */
void grpc_transport_end2end_tests(grpc_transport_test_config *config);

#endif  /* GRPC_TEST_CORE_TRANSPORT_TRANSPORT_END2END_TESTS_H */
