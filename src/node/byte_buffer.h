#ifndef NET_GRPC_NODE_BYTE_BUFFER_H_
#define NET_GRPC_NODE_BYTE_BUFFER_H_

#include <string.h>

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"

namespace grpc {
namespace node {

/* Convert a Node.js Buffer to grpc_byte_buffer. Requires that
   ::node::Buffer::HasInstance(buffer) */
grpc_byte_buffer *BufferToByteBuffer(v8::Handle<v8::Value> buffer);

/* Convert a grpc_byte_buffer to a Node.js Buffer */
v8::Handle<v8::Value> ByteBufferToBuffer(grpc_byte_buffer *buffer);

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_BYTE_BUFFER_H_
