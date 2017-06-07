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

#ifndef NET_GRPC_HHVM_GRPC_CHANNEL_CREDENTIALS_H_
#define NET_GRPC_HHVM_GRPC_CHANNEL_CREDENTIALS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "common.h"

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace HPHP {

class ChannelCredentialsData {
  private:
    grpc_channel_credentials* wrapped{nullptr};
  public:
    static Class* s_class;
    static const StaticString s_className;

    static Class* getClass();

    ChannelCredentialsData();
    ~ChannelCredentialsData();

    void init(grpc_channel_credentials* channel_credentials);
    void sweep();
    grpc_channel_credentials* getWrapped();
};

void HHVM_METHOD(ChannelCredentials, setDefaultRootsPem,
  const String& pem_roots);

Object HHVM_METHOD(ChannelCredentials, createDefault);

Object HHVM_METHOD(ChannelCredentials, createSsl,
  const String& pem_root_certs,
  const Variant& pem_key_cert_pair__private_key /*= null*/,
  const Variant& pem_key_cert_pair__cert_chain /*=null*/
  );

Object HHVM_METHOD(ChannelCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj);

void HHVM_METHOD(ChannelCredentials, createInsecure);

}

#endif /* NET_GRPC_HHVM_GRPC_CHANNEL_CREDENTIALS_H_ */
