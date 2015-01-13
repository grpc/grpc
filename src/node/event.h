#ifndef NET_GRPC_NODE_EVENT_H_
#define NET_GRPC_NODE_EVENT_H_

#include <node.h>
#include "grpc/grpc.h"

namespace grpc {
namespace node {

v8::Handle<v8::Value> CreateEventObject(grpc_event *event);

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_EVENT_H_
