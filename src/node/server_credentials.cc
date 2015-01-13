#include <node.h>

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/log.h"
#include "server_credentials.h"

namespace grpc {
namespace node {

using ::node::Buffer;
using v8::Arguments;
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

Persistent<Function> ServerCredentials::constructor;
Persistent<FunctionTemplate> ServerCredentials::fun_tpl;

ServerCredentials::ServerCredentials(grpc_server_credentials *credentials)
    : wrapped_credentials(credentials) {
}

ServerCredentials::~ServerCredentials() {
  gpr_log(GPR_DEBUG, "Destroying server credentials object");
  grpc_server_credentials_release(wrapped_credentials);
}

void ServerCredentials::Init(Handle<Object> exports) {
  NanScope();
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->SetClassName(NanNew("ServerCredentials"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NanAssignPersistent(fun_tpl, tpl);
  NanAssignPersistent(constructor, tpl->GetFunction());
  constructor->Set(NanNew("createSsl"),
                   FunctionTemplate::New(CreateSsl)->GetFunction());
  constructor->Set(NanNew("createFake"),
                   FunctionTemplate::New(CreateFake)->GetFunction());
  exports->Set(NanNew("ServerCredentials"), constructor);
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
    External::New(reinterpret_cast<void*>(credentials)) };
  return NanEscapeScope(constructor->NewInstance(argc, argv));
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
    grpc_server_credentials *creds_value =
        reinterpret_cast<grpc_server_credentials*>(External::Unwrap(args[0]));
    ServerCredentials *credentials = new ServerCredentials(creds_value);
    credentials->Wrap(args.This());
    NanReturnValue(args.This());
  } else {
    const int argc = 1;
    Local<Value> argv[argc] = { args[0] };
    NanReturnValue(constructor->NewInstance(argc, argv));
  }
}

NAN_METHOD(ServerCredentials::CreateSsl) {
  NanScope();
  char *root_certs = NULL;
  char *private_key;
  char *cert_chain;
  int root_certs_length = 0, private_key_length, cert_chain_length;
  if (Buffer::HasInstance(args[0])) {
    root_certs = Buffer::Data(args[0]);
    root_certs_length = Buffer::Length(args[0]);
  } else if (!(args[0]->IsNull() || args[0]->IsUndefined())) {
    return NanThrowTypeError(
        "createSSl's first argument must be a Buffer if provided");
  }
  if (!Buffer::HasInstance(args[1])) {
    return NanThrowTypeError(
        "createSsl's second argument must be a Buffer");
  }
  private_key = Buffer::Data(args[1]);
  private_key_length = Buffer::Length(args[1]);
  if (!Buffer::HasInstance(args[2])) {
    return NanThrowTypeError(
        "createSsl's third argument must be a Buffer");
  }
  cert_chain = Buffer::Data(args[2]);
  cert_chain_length = Buffer::Length(args[2]);
  NanReturnValue(WrapStruct(grpc_ssl_server_credentials_create(
      reinterpret_cast<unsigned char*>(root_certs), root_certs_length,
      reinterpret_cast<unsigned char*>(private_key), private_key_length,
      reinterpret_cast<unsigned char*>(cert_chain), cert_chain_length)));
}

NAN_METHOD(ServerCredentials::CreateFake) {
  NanScope();
  NanReturnValue(WrapStruct(
      grpc_fake_transport_security_server_credentials_create()));
}

}  // namespace node
}  // namespace grpc
