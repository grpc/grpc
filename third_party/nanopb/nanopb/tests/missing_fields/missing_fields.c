/* Checks that missing required fields are detected properly */

#include <stdio.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "missing_fields.pb.h"

int main()
{
    uint8_t buffer[512];
    size_t size;
    
    /* Create a message with one missing field */
    {
        MissingField msg = {0};
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        
        if (!pb_encode(&stream, MissingField_fields, &msg))
        {
            printf("Encode failed.\n");
            return 1;
        }

        size = stream.bytes_written;
    }

    /* Test that it decodes properly if we don't require that field */
    {
        MissingField msg = {0};
        pb_istream_t stream = pb_istream_from_buffer(buffer, size);
        
        if (!pb_decode(&stream, MissingField_fields, &msg))
        {
            printf("Decode failed: %s\n", PB_GET_ERROR(&stream));
            return 2;
        }
    }
    
    /* Test that it does *not* decode properly if we require the field */
    {
        AllFields msg = {0};
        pb_istream_t stream = pb_istream_from_buffer(buffer, size);
        
        if (pb_decode(&stream, AllFields_fields, &msg))
        {
            printf("Decode didn't detect missing field.\n");
            return 3;
        }
    }
    
    return 0; /* All ok */
}

