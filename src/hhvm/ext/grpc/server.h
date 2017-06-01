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

#ifndef NET_GRPC_HHVM_GRPC_SERVER_H_
#define NET_GRPC_HHVM_GRPC_SERVER_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hhvm_grpc.h"

#include <grpc/grpc.h>

class ServerWrapper {
  private:
    grpc_server* wrapped;
  public:
    ServerWrapper();
    ~ServerWrapper();

    void new(grpc_server* server);
    void sweep();
    grpc_server* getWrapped();
}

void HHVM_METHOD(Server, __construct,
  const Variant& args_array_or_null /* = null */);

void HHVM_METHOD(Server, requestCall);

bool HHVM_METHOD(Server, addHttp2Port,
  const String& addr);

bool HHVM_METHOD(Server, addSecureHttp2Port,
  const String& addr,
  const Object& server_credentials);

void HHVM_METHOD(Server, start);

#endif /* NET_GRPC_HHVM_GRPC_SERVER_H_ */
