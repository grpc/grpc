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

struct DefaultPemRootCerts {
  DefaultPemRootCerts();
  char * getCerts();
  void setCerts(const String& pem_roots);

  char * default_pem_root_certs{NULL};

  static DECLARE_THREAD_LOCAL(DefaultPemRootCerts, tl_obj);
};

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

void HHVM_STATIC_METHOD(ChannelCredentials, setDefaultRootsPem,
  const String& pem_roots);

Object HHVM_STATIC_METHOD(ChannelCredentials, createDefault);

Object HHVM_STATIC_METHOD(ChannelCredentials, createSsl,
  const Variant& pem_root_certs,
  const Variant& pem_key_cert_pair__private_key /*= null*/,
  const Variant& pem_key_cert_pair__cert_chain /*=null*/
  );

Object HHVM_STATIC_METHOD(ChannelCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj);

Variant HHVM_STATIC_METHOD(ChannelCredentials, createInsecure);

void grpc_hhvm_init_channel_credentials();

}

#endif /* NET_GRPC_HHVM_GRPC_CHANNEL_CREDENTIALS_H_ */
