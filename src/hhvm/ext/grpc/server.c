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

#include "call.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hhvm_grpc.h"

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>

#include "completion_queue.h"
#include "server.h"
#include "channel.h"
#include "server_credentials.h"
#include "timeval.h"

/**
 * Constructs a new instance of the Server class
 * @param array $args_array The arguments to pass to the server (optional)
 */
void HHVM_METHOD(Server, __construct,
  Array args_array) {
  ...
}

/**
 * Request a call on a server. Creates a single GRPC_SERVER_RPC_NEW event.
 * @return void
 */
void HHVM_METHOD(Server, requestCall) {
  ...
}

/**
 * Add a http2 over tcp listener.
 * @param string $addr The address to add
 * @return bool True on success, false on failure
 */
bool HHVM_METHOD(Server, addHttp2Port,
  const String& addr) {
  ...
}

/**
 * Add a secure http2 over tcp listener.
 * @param string $addr The address to add
 * @param ServerCredentials The ServerCredentials object
 * @return bool True on success, false on failure
 */
bool HHVM_METHOD(Server, addSecureHttp2Port,
  const String& addr,
  ServerCredentials& server_credentials) {
  ...
}

/**
 * Start a server - tells all listeners to start listening
 * @return void
 */
void HHVM_METHOD(Server, start) {
  ...
}

