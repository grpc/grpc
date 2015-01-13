#ifndef NET_GRPC_NODE_SERVER_H_
#define NET_GRPC_NODE_SERVER_H_

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"

namespace grpc {
namespace node {

/* Wraps grpc_server as a JavaScript object. Provides a constructor
   and wrapper methods for grpc_server_create, grpc_server_request_call,
   grpc_server_add_http2_port, and grpc_server_start. */
class Server : public ::node::ObjectWrap {
 public:
  /* Initializes the Server class and exposes the constructor and
     wrapper methods to JavaScript */
  static void Init(v8::Handle<v8::Object> exports);
  /* Tests whether the given value was constructed by this class's
     JavaScript constructor */
  static bool HasInstance(v8::Handle<v8::Value> val);

 private:
  explicit Server(grpc_server *server);
  ~Server();

  // Prevent copying
  Server(const Server&);
  Server& operator=(const Server&);

  static NAN_METHOD(New);
  static NAN_METHOD(RequestCall);
  static NAN_METHOD(AddHttp2Port);
  static NAN_METHOD(AddSecureHttp2Port);
  static NAN_METHOD(Start);
  static NAN_METHOD(Shutdown);
  static v8::Persistent<v8::Function> constructor;
  static v8::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_server *wrapped_server;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_SERVER_H_
