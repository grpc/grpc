/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

ReadWriteMutex s_default_pem_root_certs_mutex;

static char *default_pem_root_certs = NULL;

static grpc_ssl_roots_override_result get_ssl_roots_override(char **pem_root_certs) {
  ReadLock l(s_default_pem_root_certs_mutex);
  *pem_root_certs = default_pem_root_certs;
  if (*pem_root_certs == NULL) {
    return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  }
  return GRPC_SSL_ROOTS_OVERRIDE_OK;
}

Class* ChannelCredentialsData::s_class = nullptr;
const StaticString ChannelCredentialsData::s_className("Grpc\\ChannelCredentials");

IMPLEMENT_GET_CLASS(ChannelCredentialsData);

ChannelCredentialsData::ChannelCredentialsData() {}
ChannelCredentialsData::~ChannelCredentialsData() { sweep(); }

void ChannelCredentialsData::init(grpc_channel_credentials* channel_credentials) {
  wrapped = channel_credentials;
}

void ChannelCredentialsData::sweep() {
  if (wrapped) {
    grpc_channel_credentials_release(wrapped);
    wrapped = nullptr;
  }
}

grpc_channel_credentials* ChannelCredentialsData::getWrapped() {
  return wrapped;
}

void HHVM_STATIC_METHOD(ChannelCredentials, setDefaultRootsPem,
  const String& pem_roots) {
  WriteLock l(s_default_pem_root_certs_mutex);
  if (default_pem_root_certs != NULL) {
    gpr_free((void *)default_pem_root_certs);
  }
  default_pem_root_certs = (char *) gpr_malloc((pem_roots.length() + 1) * sizeof(char));
  memcpy(default_pem_root_certs, pem_roots.c_str(), pem_roots.length() + 1);
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createDefault) {
  auto newChannelCredentialsObj = Object{ChannelCredentialsData::getClass()};
  auto channelCredentialsData = Native::data<ChannelCredentialsData>(newChannelCredentialsObj);
  grpc_channel_credentials *channel_credentials = grpc_google_default_credentials_create();
  channelCredentialsData->init(channel_credentials);

  return newChannelCredentialsObj;
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createSsl,
  const Variant& pem_root_certs /*=null*/,
  const Variant& pem_key_cert_pair__private_key /*= null*/,
  const Variant& pem_key_cert_pair__cert_chain /*=null*/
  ) {
  const char *pem_root_certs_ = NULL;

  if (pem_root_certs.isString()) {
    pem_root_certs_ = pem_root_certs.toString().c_str();
  }

  auto newChannelCredentialsObj = Object{ChannelCredentialsData::getClass()};
  auto channelCredentialsData = Native::data<ChannelCredentialsData>(newChannelCredentialsObj);

  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  pem_key_cert_pair.private_key = pem_key_cert_pair.cert_chain = NULL;

  if (pem_key_cert_pair__private_key.isString()) {
    pem_key_cert_pair.private_key = pem_key_cert_pair__private_key.toString().c_str();
  }

  if (pem_key_cert_pair__cert_chain.isString()) {
    pem_key_cert_pair.cert_chain = pem_key_cert_pair__cert_chain.toString().c_str();
  }

  channelCredentialsData->init(grpc_ssl_credentials_create(
    pem_root_certs_,
    pem_key_cert_pair.private_key == NULL ? NULL : &pem_key_cert_pair, NULL
  ));

  return newChannelCredentialsObj;
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj) {
  auto channelCredentialsData = Native::data<ChannelCredentialsData>(cred1_obj);
  auto callCredentialsData = Native::data<CallCredentialsData>(cred2_obj);

  grpc_channel_credentials* channel_credentials = grpc_composite_channel_credentials_create(
    channelCredentialsData->getWrapped(),
    callCredentialsData->getWrapped(),
    NULL
  );

  auto newChannelCredentialsObj = Object{ChannelCredentialsData::getClass()};
  auto newChannelCredentialsData = Native::data<ChannelCredentialsData>(newChannelCredentialsObj);
  newChannelCredentialsData->init(channel_credentials);

  return newChannelCredentialsObj;
}

Variant HHVM_STATIC_METHOD(ChannelCredentials, createInsecure) {
  return Variant();
}

void grpc_hhvm_init_channel_credentials() {
  grpc_set_ssl_roots_override_callback(get_ssl_roots_override);
}

} // namespace HPHP
