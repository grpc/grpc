#ifndef NET_GRPC_NODE_CREDENTIALS_H_
#define NET_GRPC_NODE_CREDENTIALS_H_

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace grpc {
namespace node {

/* Wrapper class for grpc_credentials structs */
class Credentials : public ::node::ObjectWrap {
 public:
  static void Init(v8::Handle<v8::Object> exports);
  static bool HasInstance(v8::Handle<v8::Value> val);
  /* Wrap a grpc_credentials struct in a javascript object */
  static v8::Handle<v8::Value> WrapStruct(grpc_credentials *credentials);

  /* Returns the grpc_credentials struct that this object wraps */
  grpc_credentials *GetWrappedCredentials();

 private:
  explicit Credentials(grpc_credentials *credentials);
  ~Credentials();

  // Prevent copying
  Credentials(const Credentials&);
  Credentials& operator=(const Credentials&);

  static NAN_METHOD(New);
  static NAN_METHOD(CreateDefault);
  static NAN_METHOD(CreateSsl);
  static NAN_METHOD(CreateComposite);
  static NAN_METHOD(CreateGce);
  static NAN_METHOD(CreateFake);
  static NAN_METHOD(CreateIam);
  static v8::Persistent<v8::Function> constructor;
  // Used for typechecking instances of this javascript class
  static v8::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_credentials *wrapped_credentials;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_CREDENTIALS_H_
