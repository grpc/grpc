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

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"

#include <grpc/support/alloc.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

namespace HPHP {

static char *default_pem_root_certs = NULL;

ChannelCredentials::ChannelCredentials() {}
ChannelCredentials::~ChannelCredentials() { sweep(); }

void ChannelCredentials::init(grpc_channel_credentials* channel_credentials) {
  wrapped = channel_credentials;
}

void ChannelCredentials::sweep() {
  if (wrapped) {
    grpc_channel_credentials_release(wrapped);
    wrapped = nullptr;
  }
}

grpc_channel_credentials* ChannelCredentials::getWrapped() {
  return wrapped;
}

void HHVM_METHOD(ChannelCredentials, setDefaultRootsPem,
  const String& pem_roots) {
  default_pem_root_certs = (char *) gpr_malloc((pem_roots.length() + 1) * sizeof(char));
  memcpy(default_pem_root_certs, pem_roots.c_str(), pem_roots.length() + 1);
}

Object HHVM_METHOD(ChannelCredentials, createDefault) {
  auto newChannelCredentialsObj = create_object("ChannelCredentials", Array());
  auto channelCredentials = Native::data<ChannelCredentials>(newChannelCredentialsObj);
  grpc_channel_credentials *channel_credentials = grpc_google_default_credentials_create();
  channelCredentials->init(channel_credentials);

  return newChannelCredentialsObj;
}

Object HHVM_METHOD(ChannelCredentials, createSsl,
  const String& pem_root_certs,
  const Variant& pem_key_cert_pair__private_key /*= null*/,
  const Variant& pem_key_cert_pair__cert_chain /*=null*/
  ) {

  auto newChannelCredentialsObj = create_object("ChannelCredentials", Array());
  auto channelCredentials = Native::data<ChannelCredentials>(newChannelCredentialsObj);

  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  pem_key_cert_pair.private_key = pem_key_cert_pair.cert_chain = NULL;

  if (pem_key_cert_pair__private_key.isString()) {
    pem_key_cert_pair.private_key = pem_key_cert_pair__private_key.toString().c_str();
  }

  if (pem_key_cert_pair__cert_chain.isString()) {
    pem_key_cert_pair.cert_chain = pem_key_cert_pair__cert_chain.toString().c_str();
  }

  channelCredentials->init(grpc_ssl_credentials_create(
    pem_root_certs.c_str(),
    pem_key_cert_pair.private_key == NULL ? NULL : &pem_key_cert_pair, NULL)
  );

  return newChannelCredentialsObj;
}

Object HHVM_METHOD(ChannelCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj) {
  auto channelCredentials1 = Native::data<ChannelCredentials>(cred1_obj);
  auto callCredentials2 = Native::data<CallCredentials>(cred2_obj);

  grpc_channel_credentials* channel_credentials = grpc_composite_channel_credentials_create(
    channelCredentials1->getWrapped(),
    callCredentials2->getWrapped(),
    NULL
  );

  auto newChannelCredentialsObj = create_object("ChannelCredentials", Array());
  auto newChannelCredentials = Native::data<ChannelCredentials>(newChannelCredentialsObj);
  newChannelCredentials->init(channel_credentials);

  return newChannelCredentialsObj;
}

void HHVM_METHOD(ChannelCredentials, createInsecure) {
  return;
}

} // namespace HPHP
