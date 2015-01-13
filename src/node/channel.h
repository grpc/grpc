#ifndef NET_GRPC_NODE_CHANNEL_H_
#define NET_GRPC_NODE_CHANNEL_H_

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"

namespace grpc {
namespace node {

/* Wrapper class for grpc_channel structs */
class Channel : public ::node::ObjectWrap {
 public:
  static void Init(v8::Handle<v8::Object> exports);
  static bool HasInstance(v8::Handle<v8::Value> val);
  /* This is used to typecheck javascript objects before converting them to
     this type */
  static v8::Persistent<v8::Value> prototype;

  /* Returns the grpc_channel struct that this object wraps */
  grpc_channel *GetWrappedChannel();

  /* Return the hostname that this channel connects to */
  char *GetHost();

 private:
  explicit Channel(grpc_channel *channel, NanUtf8String *host);
  ~Channel();

  // Prevent copying
  Channel(const Channel&);
  Channel& operator=(const Channel&);

  static NAN_METHOD(New);
  static NAN_METHOD(Close);
  static v8::Persistent<v8::Function> constructor;
  static v8::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_channel *wrapped_channel;
  NanUtf8String *host;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_CHANNEL_H_
