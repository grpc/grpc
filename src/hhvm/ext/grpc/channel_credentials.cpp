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

#include "channel_credentials.h"
#include "call_credentials.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hhvm_grpc.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/vm/native-data.h"

#include <grpc/support/alloc.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

namespace HPHP {

static char *default_pem_root_certs = NULL;

const StaticString s_ChannelCredentials("ChannelCredentialsWrapper");

class ChannelCredentialsWrapper {
  private:
    grpc_channel_credentials* wrapped{nullptr};
  public:
    ChannelCredentialsWrapper() {}
    ~ChannelCredentialsWrapper() { sweep(); }

    void new(grpc_channel_credentials* channel_credentials) {
      memcpy(wrapped, channel_credentials, sizeof(grpc_channel_credentials));
    }

    void sweep() {
      if (wrapped) {
        grpc_channel_credentials_release(wrapped);
        req::free(wrapped);
        wrapped = nullptr;
      }
    }

    grpc_channel_credentials* getWrapped() {
      return wrapped;
    }
}

void HHVM_METHOD(ChannelCredentials, setDefaultRootsPem,
  const String& pem_roots) {
  default_pem_root_certs = gpr_malloc((pem_roots.length() + 1) * sizeof(char));
  memcpy(default_pem_root_certs, pem_roots.toCppString(), pem_roots.length() + 1);
}

Object HHVM_METHOD(ChannelCredentials, createDefault) {
  auto channelCredentialsWrapper = Native::data<ChannelCredentialsWrapper>(this_);
  grpc_channel_credentials *channel_credentials = grpc_google_default_credentials_create();
  channelCredentialsWrapper->new(channel_credentials);

  return Object(std::move(channelCredentialsWrapper));
}

Object HHVM_METHOD(ChannelCredentials, createSsl,
  const String& pem_root_certs,
  const Variant& pem_key_cert_pair__private_key /*= null*/,
  const Variant& pem_key_cert_pair__cert_chain /*=null*/
  ) {

  auto channelCredentialsWrapper = Native::data<ChannelCredentialsWrapper>(this_);

  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  pem_key_cert_pair.private_key = pem_key_cert_pair.cert_chain = NULL;

  if (pem_key_cert_pair__private_key.isString()) {
    pem_key_cert_pair.private_key = pem_key_cert_pair__private_key.toString().toCppString();
  }

  if (pem_key_cert_pair__cert_chain.isString()) {
    pem_key_cert_pair.cert_chain = pem_key_cert_pair__cert_chain.toString().toCppString();
  }

  grpc_channel_credentials *channel_credentials = grpc_ssl_credentials_create(
        pem_root_certs.toCppString(),
        pem_key_cert_pair.private_key == NULL ? NULL : &pem_key_cert_pair, NULL);

  channelCredentialsWrapper->new(channel_credentials);

  return Object(std::move(channelCredentialsWrapper));
}

Object HHVM_METHOD(ChannelCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj) {
  auto channelCredentialsWrapper1 = Native::data<ChannelCredentialsWrapper>(cred1_obj);
  auto callCredentialsWrapper2 = Native::data<CallCredentialsWrapper>(cred2_obj);

  grpc_channel_credentials* channel_credentials = grpc_composite_channel_credentials_create(
    channelCredentialsWrapper1->getWrapped(),
    calllCredentialsWrapper2->getWrapped(),
    NULL
  );

  auto newChannelCredentialsWrapper = req::make<ChannelCredentialsWrapper>();
  newChannelCredentialsWrapper->new(channel_credentials);

  return newChannelCredentialsWrapper;
}

void HHVM_METHOD(ChannelCredentials, createInsecure) {
  return;
}

} // namespace HPHP
