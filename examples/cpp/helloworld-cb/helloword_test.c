/*
 * COPYRIGHT 2019, Cerebras Systems, Inc.
 */

#include <assert.h>
#include <string.h>
#include "helloworld-cb.pb.h"

#define TEST_NAME  "Test name"
#define TEST_MSG   "Test message"

/*
 * Demonstrates C interfaces to C++ protobuf messages.
 */
int 
main(int argc, char **argv)
{
    helloworld_CHelloRequest_t *request = helloworld_CHelloRequest_new();
    helloworld_CHelloRequest_set_name(request, TEST_NAME);
    assert(strcmp(helloworld_CHelloRequest_get_name(request), TEST_NAME) == 0);

    helloworld_CHelloReply_t *reply = helloworld_CHelloReply_new();
    helloworld_CHelloReply_set_message(reply, TEST_MSG);
    assert(strcmp(helloworld_CHelloReply_get_message(reply), TEST_MSG) == 0);

    return 0;
}