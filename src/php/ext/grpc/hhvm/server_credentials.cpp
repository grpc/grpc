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

#include "server_credentials.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

namespace HPHP {

Class* ServerCredentialsData::s_class = nullptr;
const StaticString ServerCredentialsData::s_className("Grpc\\ServerCredentials");

IMPLEMENT_GET_CLASS(ServerCredentialsData);

ServerCredentialsData::ServerCredentialsData() {}
ServerCredentialsData::~ServerCredentialsData() { sweep(); }

void ServerCredentialsData::init(grpc_server_credentials* server_credentials) {
  wrapped = server_credentials;
}

void ServerCredentialsData::sweep() {
  if (wrapped) {
    grpc_server_credentials_release(wrapped);
    wrapped = nullptr;
  }
}

grpc_server_credentials* ServerCredentialsData::getWrapped() {
  return wrapped;
}

Object HHVM_STATIC_METHOD(ServerCredentials, createSsl,
  const String& pem_root_certs,
  const String& pem_private_key,
  const String& pem_cert_chain) {
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;

  pem_key_cert_pair.private_key = pem_private_key.c_str();
  pem_key_cert_pair.cert_chain = pem_cert_chain.c_str();

  auto newServerCredentialsObj = Object{ServerCredentialsData::getClass()};
  auto serverCredentialsData = Native::data<ServerCredentialsData>(newServerCredentialsObj);

  /* TODO: add a client_certificate_request field in ServerCredentials and pass
   * it as the last parameter. */
  serverCredentialsData->init(grpc_ssl_server_credentials_create_ex(
    pem_root_certs.c_str(),
    &pem_key_cert_pair,
    1,
    GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
    nullptr
  ));

  return newServerCredentialsObj;
}

} // namespace HPHP
