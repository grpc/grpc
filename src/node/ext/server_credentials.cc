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

#include <node.h>

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/log.h"
#include "server_credentials.h"

namespace grpc {
namespace node {

using v8::Exception;
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::ObjectTemplate;
using v8::Persistent;
using v8::Value;

NanCallback *ServerCredentials::constructor;
Persistent<FunctionTemplate> ServerCredentials::fun_tpl;

ServerCredentials::ServerCredentials(grpc_server_credentials *credentials)
    : wrapped_credentials(credentials) {}

ServerCredentials::~ServerCredentials() {
  grpc_server_credentials_release(wrapped_credentials);
}

void ServerCredentials::Init(Handle<Object> exports) {
  NanScope();
  Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
  tpl->SetClassName(NanNew("ServerCredentials"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NanAssignPersistent(fun_tpl, tpl);
  Handle<Function> ctr = tpl->GetFunction();
  ctr->Set(NanNew("createSsl"),
           NanNew<FunctionTemplate>(CreateSsl)->GetFunction());
  constructor = new NanCallback(ctr);
  exports->Set(NanNew("ServerCredentials"), ctr);
}

bool ServerCredentials::HasInstance(Handle<Value> val) {
  NanScope();
  return NanHasInstance(fun_tpl, val);
}

Handle<Value> ServerCredentials::WrapStruct(
    grpc_server_credentials *credentials) {
  NanEscapableScope();
  if (credentials == NULL) {
    return NanEscapeScope(NanNull());
  }
  const int argc = 1;
  Handle<Value> argv[argc] = {
    NanNew<External>(reinterpret_cast<void *>(credentials))};
  return NanEscapeScope(constructor->GetFunction()->NewInstance(argc, argv));
}

grpc_server_credentials *ServerCredentials::GetWrappedServerCredentials() {
  return wrapped_credentials;
}

NAN_METHOD(ServerCredentials::New) {
  NanScope();

  if (args.IsConstructCall()) {
    if (!args[0]->IsExternal()) {
      return NanThrowTypeError(
          "ServerCredentials can only be created with the provide functions");
    }
    Handle<External> ext = args[0].As<External>();
    grpc_server_credentials *creds_value =
        reinterpret_cast<grpc_server_credentials *>(ext->Value());
    ServerCredentials *credentials = new ServerCredentials(creds_value);
    credentials->Wrap(args.This());
    NanReturnValue(args.This());
  } else {
    const int argc = 1;
    Local<Value> argv[argc] = {args[0]};
    NanReturnValue(constructor->GetFunction()->NewInstance(argc, argv));
  }
}

NAN_METHOD(ServerCredentials::CreateSsl) {
  // TODO: have the node API support multiple key/cert pairs.
  NanScope();
  char *root_certs = NULL;
  grpc_ssl_pem_key_cert_pair key_cert_pair;
  if (::node::Buffer::HasInstance(args[0])) {
    root_certs = ::node::Buffer::Data(args[0]);
  } else if (!(args[0]->IsNull() || args[0]->IsUndefined())) {
    return NanThrowTypeError(
        "createSSl's first argument must be a Buffer if provided");
  }
  if (!::node::Buffer::HasInstance(args[1])) {
    return NanThrowTypeError("createSsl's second argument must be a Buffer");
  }
  key_cert_pair.private_key = ::node::Buffer::Data(args[1]);
  if (!::node::Buffer::HasInstance(args[2])) {
    return NanThrowTypeError("createSsl's third argument must be a Buffer");
  }
  key_cert_pair.cert_chain = ::node::Buffer::Data(args[2]);
  // TODO Add a force_client_auth parameter and pass it as the last parameter
  // here.
  NanReturnValue(WrapStruct(
      grpc_ssl_server_credentials_create(root_certs, &key_cert_pair, 1, 0)));
}

}  // namespace node
}  // namespace grpc
