#ifndef NET_GRPC_NODE_TAG_H_
#define NET_GRPC_NODE_TAG_H_

#include <node.h>

namespace grpc {
namespace node {

/* Create a void* tag that can be passed to various grpc_call functions from
   a javascript value and the javascript wrapper for the call. The call can be
   null. */
void *CreateTag(v8::Handle<v8::Value> tag, v8::Handle<v8::Value> call);
/* Return the javascript value stored in the tag */
v8::Handle<v8::Value> GetTagHandle(void *tag);
/* Returns true if the call was set (non-null) when the tag was created */
bool TagHasCall(void *tag);
/* Returns the javascript wrapper for the call associated with this tag */
v8::Handle<v8::Value> TagGetCall(void *call);
/* Destroy the tag and all resources it is holding. It is illegal to call any
   of these other functions on a tag after it has been destroyed. */
void DestroyTag(void *tag);

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_TAG_H_
