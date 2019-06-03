/*
 * COPYRIGHT 2019, Cerebras Systems, Inc.
 */

// This code will be emitted by Cerebras protoc plugin.

#include <stdlib.h>
#include "helloworld.pb.h"
#include "helloworld-cb.pb.h"

struct helloworld_CHelloRequest {
    helloworld::HelloRequest *msg;
};

struct helloworld_CHelloReply {
    helloworld::HelloReply *msg;
};

/*
 * helloworld_CHelloRequest_new - constructs wrapped HelloRequest object.
 *
 * @return - Wrapped HelloRequest message.
 */
helloworld_CHelloRequest_t *
helloworld_CHelloRequest_new()
{
    helloworld_CHelloRequest_t *wrapper;

    wrapper = (helloworld_CHelloRequest_t *) malloc(sizeof(helloworld_CHelloRequest));
    wrapper->msg = new helloworld::HelloRequest();

    return wrapper;
}

/*
 * helloworld_CHelloRequest_delete - deletes wrapped HelloRequest object.
 *
 * @wrapper - Wrapped HelloRequest message. 
 */
void 
helloworld_CHelloRequest_delete(helloworld_CHelloRequest_t *wrapper)
{
    delete wrapper->msg;
    free(wrapper);
}

/*
 * helloworld_CHelloRequest_set_name - Sets name field.
 *
 * @wrapper - Wrapped HelloRequest message.
 * @name - Name field value. 
 */
void 
helloworld_CHelloRequest_set_name(helloworld_CHelloRequest_t *wrapper, 
                                  const char *name)
{
    wrapper->msg->set_name(name);
}

/*
 * helloworld_CHelloRequest_set_name - Gets name field.
 *
 * @wrapper - Wrapped HelloRequest message.
 * @return - Name field value.
 */
const char * 
helloworld_CHelloRequest_get_name(helloworld_CHelloRequest_t *wrapper)
{
    return wrapper->msg->name().c_str();
}

/*
 * helloworld_CHelloRequest_new - constructs wrapped HelloReply object.
 *
 * @return - Wrapped HelloReply message.
 */
helloworld_CHelloReply_t * 
helloworld_CHelloReply_new()
{
    helloworld_CHelloReply_t *wrapper;

    wrapper = (helloworld_CHelloReply_t *) malloc(sizeof(helloworld_CHelloReply));
    wrapper->msg = new helloworld::HelloReply();

    return wrapper;
}

/*
 * helloworld_CHelloReply_delete - Deletes wrapped HelloReply object.
 *
 * @return - Wrapped HelloReply message.
 */
void 
helloworld_CHelloReply_delete(helloworld_CHelloReply_t *wrapped)
{
    delete wrapped->msg;
    free(wrapped);
}

/*
 * helloworld_CHelloReply_set_message - Set message field.
 *
 * @wrapped - Wrapped HelloReply message.
 * @message - message field value.
 */
void 
helloworld_CHelloReply_set_message(helloworld_CHelloReply_t *wrapped, 
                                   const char *message)
{
    wrapped->msg->set_message(message);
}

/*
 * helloworld_CHelloReply_get_message - Gets message field.
 *
 * @wrapped - Wrapped HelloReply object.
 * @return - message field.
 */
const char * 
helloworld_CHelloReply_get_message(helloworld_CHelloReply_t *wrapped)
{
    return wrapped->msg->message().c_str();
}