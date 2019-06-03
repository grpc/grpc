/*
 * COPYRIGHT 2019, Cerebras Systems, Inc.
 */

// This code will be emitted by Cerebras protoc plugin.

#ifndef _HELLOWORLD_CB_PB_H
#define _HELLOWORLD_CB_PB_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 1. "helloworld" prefix is from the package scope in helloworld.proto.
 * 2. Message type name is prefixed by "C" for C binding.
 */
struct helloworld_CHelloRequest;
typedef struct helloworld_CHelloRequest helloworld_CHelloRequest_t;

struct helloworld_CHelloReply;
typedef struct helloworld_CHelloReply helloworld_CHelloReply_t;


/**
 * setter/getters function names have "<package>_<type>_" as its prefix.
 */

// HelloRequest 
helloworld_CHelloRequest_t * helloworld_CHelloRequest_new();
void helloworld_CHelloRequest_delete(helloworld_CHelloRequest_t *);


void helloworld_CHelloRequest_set_name(helloworld_CHelloRequest_t *, const char *);
const char * helloworld_CHelloRequest_get_name(helloworld_CHelloRequest_t *);

// HelloReply
helloworld_CHelloReply_t * helloworld_CHelloReply_new();
void helloworld_CHelloReply_delete(helloworld_CHelloReply_t *);

void helloworld_CHelloReply_set_message(helloworld_CHelloReply_t *, const char *);
const char * helloworld_CHelloReply_get_message(helloworld_CHelloReply_t *);

#ifdef __cplusplus
}
#endif

#endif // _HELLOWORLD_CB_PB_H