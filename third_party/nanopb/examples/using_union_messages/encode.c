/* This program takes a command line argument and encodes a message in
 * one of MsgType1, MsgType2 or MsgType3.
 */
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pb_encode.h>
#include "unionproto.pb.h"

/* This function is the core of the union encoding process. It handles
 * the top-level pb_field_t array manually, in order to encode a correct
 * field tag before the message. The pointer to MsgType_fields array is
 * used as an unique identifier for the message type.
 */
bool encode_unionmessage(pb_ostream_t *stream, const pb_field_t messagetype[], const void *message)
{
    const pb_field_t *field;
    for (field = UnionMessage_fields; field->tag != 0; field++)
    {
        if (field->ptr == messagetype)
        {
            /* This is our field, encode the message using it. */
            if (!pb_encode_tag_for_field(stream, field))
                return false;
            
            return pb_encode_submessage(stream, messagetype, message);
        }
    }
    
    /* Didn't find the field for messagetype */
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s (1|2|3)\n", argv[0]);
        return 1;
    }
    
    uint8_t buffer[512];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    
    bool status = false;
    int msgtype = atoi(argv[1]);
    if (msgtype == 1)
    {
        /* Send message of type 1 */
        MsgType1 msg = {42};
        status = encode_unionmessage(&stream, MsgType1_fields, &msg);
    }
    else if (msgtype == 2)
    {
        /* Send message of type 2 */
        MsgType2 msg = {true};
        status = encode_unionmessage(&stream, MsgType2_fields, &msg);
    }
    else if (msgtype == 3)
    {
        /* Send message of type 3 */
        MsgType3 msg = {3, 1415};
        status = encode_unionmessage(&stream, MsgType3_fields, &msg);
    }
    else
    {
        fprintf(stderr, "Unknown message type: %d\n", msgtype);
        return 2;
    }
    
    if (!status)
    {
        fprintf(stderr, "Encoding failed!\n");
        return 3;
    }
    else
    {
        fwrite(buffer, 1, stream.bytes_written, stdout);
        return 0; /* Success */
    }
}


