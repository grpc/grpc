/* This program reads a message from stdin, detects its type and decodes it.
 */
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pb_decode.h>
#include "unionproto.pb.h"

/* This function reads manually the first tag from the stream and finds the
 * corresponding message type. It doesn't yet decode the actual message.
 *
 * Returns a pointer to the MsgType_fields array, as an identifier for the
 * message type. Returns null if the tag is of unknown type or an error occurs.
 */
const pb_field_t* decode_unionmessage_type(pb_istream_t *stream)
{
    pb_wire_type_t wire_type;
    uint32_t tag;
    bool eof;

    while (pb_decode_tag(stream, &wire_type, &tag, &eof))
    {
        if (wire_type == PB_WT_STRING)
        {
            const pb_field_t *field;
            for (field = UnionMessage_fields; field->tag != 0; field++)
            {
                if (field->tag == tag && (field->type & PB_LTYPE_SUBMESSAGE))
                {
                    /* Found our field. */
                    return field->ptr;
                }
            }
        }
        
        /* Wasn't our field.. */
        pb_skip_field(stream, wire_type);
    }
    
    return NULL;
}

bool decode_unionmessage_contents(pb_istream_t *stream, const pb_field_t fields[], void *dest_struct)
{
    pb_istream_t substream;
    bool status;
    if (!pb_make_string_substream(stream, &substream))
        return false;
    
    status = pb_decode(&substream, fields, dest_struct);
    pb_close_string_substream(stream, &substream);
    return status;
}

int main()
{
    /* Read the data into buffer */
    uint8_t buffer[512];
    size_t count = fread(buffer, 1, sizeof(buffer), stdin);
    pb_istream_t stream = pb_istream_from_buffer(buffer, count);
    
    const pb_field_t *type = decode_unionmessage_type(&stream);
    bool status = false;
    
    if (type == MsgType1_fields)
    {
        MsgType1 msg = {};
        status = decode_unionmessage_contents(&stream, MsgType1_fields, &msg);
        printf("Got MsgType1: %d\n", msg.value);
    }
    else if (type == MsgType2_fields)
    {
        MsgType2 msg = {};
        status = decode_unionmessage_contents(&stream, MsgType2_fields, &msg);
        printf("Got MsgType2: %s\n", msg.value ? "true" : "false");
    }
    else if (type == MsgType3_fields)
    {
        MsgType3 msg = {};
        status = decode_unionmessage_contents(&stream, MsgType3_fields, &msg);
        printf("Got MsgType3: %d %d\n", msg.value1, msg.value2);    
    }
    
    if (!status)
    {
        printf("Decode failed: %s\n", PB_GET_ERROR(&stream));
        return 1;
    }
    
    return 0;
}



