#ifndef NET_GRPC_NODE_SERVER_CREDENTIALS_H_
#define NET_GRPC_NODE_SERVER_CREDENTIALS_H_

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace grpc {
namespace node {

/* Wrapper class for grpc_server_credentials structs */
class ServerCredentials : public ::node::ObjectWrap {
 public:
  static void Init(v8::Handle<v8::Object> exports);
  static bool HasInstance(v8::Handle<v8::Value> val);
  /* Wrap a grpc_server_credentials struct in a javascript object */
  static v8::Handle<v8::Value> WrapStruct(grpc_server_credentials *credentials);

  /* Returns the grpc_server_credentials struct that this object wraps */
  grpc_server_credentials *GetWrappedServerCredentials();

 private:
  explicit ServerCredentials(grpc_server_credentials *credentials);
  ~ServerCredentials();

  // Prevent copying
  ServerCredentials(const ServerCredentials&);
  ServerCredentials& operator=(const ServerCredentials&);

  static NAN_METHOD(New);
  static NAN_METHOD(CreateSsl);
  static NAN_METHOD(CreateFake);
  static v8::Persistent<v8::Function> constructor;
  // Used for typechecking instances of this javascript class
  static v8::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_server_credentials *wrapped_credentials;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_SERVER_CREDENTIALS_H_
