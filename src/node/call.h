#ifndef NET_GRPC_NODE_CALL_H_
#define NET_GRPC_NODE_CALL_H_

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"

#include "channel.h"

namespace grpc {
namespace node {

/* Wrapper class for grpc_call structs. */
class Call : public ::node::ObjectWrap {
 public:
  static void Init(v8::Handle<v8::Object> exports);
  static bool HasInstance(v8::Handle<v8::Value> val);
  /* Wrap a grpc_call struct in a javascript object */
  static v8::Handle<v8::Value> WrapStruct(grpc_call *call);

 private:
  explicit Call(grpc_call *call);
  ~Call();

  // Prevent copying
  Call(const Call&);
  Call& operator=(const Call&);

  static NAN_METHOD(New);
  static NAN_METHOD(AddMetadata);
  static NAN_METHOD(StartInvoke);
  static NAN_METHOD(ServerAccept);
  static NAN_METHOD(ServerEndInitialMetadata);
  static NAN_METHOD(Cancel);
  static NAN_METHOD(StartWrite);
  static NAN_METHOD(StartWriteStatus);
  static NAN_METHOD(WritesDone);
  static NAN_METHOD(StartRead);
  static v8::Persistent<v8::Function> constructor;
  // Used for typechecking instances of this javascript class
  static v8::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_call *wrapped_call;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_CALL_H_
