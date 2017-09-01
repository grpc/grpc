/* Simulate IO errors after each byte in a stream.
 * Verifies proper error propagation.
 */

#include <stdio.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include "alltypes.pb.h"
#include "test_helpers.h"

typedef struct
{
    uint8_t *buffer;
    size_t fail_after;
} faulty_stream_t;

bool read_callback(pb_istream_t *stream, uint8_t *buf, size_t count)
{
    faulty_stream_t *state = stream->state;
    
    while (count--)
    {
        if (state->fail_after == 0)
            PB_RETURN_ERROR(stream, "simulated");
        state->fail_after--;
        *buf++ = *state->buffer++;
    }
    
    return true;
}
bool write_callback(pb_ostream_t *stream, const uint8_t *buf, size_t count)
{
    faulty_stream_t *state = stream->state;
    
    while (count--)
    {
        if (state->fail_after == 0)
            PB_RETURN_ERROR(stream, "simulated");
        state->fail_after--;
        *state->buffer++ = *buf++;
    }
    
    return true;
}

int main()
{
    uint8_t buffer[2048];
    size_t msglen;
    AllTypes msg = AllTypes_init_zero;
    
    /* Get some base data to run the tests with */
    SET_BINARY_MODE(stdin);
    msglen = fread(buffer, 1, sizeof(buffer), stdin);
    
    /* Test IO errors on decoding */
    {
        bool status;
        pb_istream_t stream = {&read_callback, NULL, SIZE_MAX};
        faulty_stream_t fs;
        size_t i;
        
        for (i = 0; i < msglen; i++)
        {
            stream.bytes_left = msglen;
            stream.state = &fs;
            fs.buffer = buffer;
            fs.fail_after = i;

            status = pb_decode(&stream, AllTypes_fields, &msg);
            if (status != false)
            {
                fprintf(stderr, "Unexpected success in decode\n");
                return 2;
            }
            else if (strcmp(stream.errmsg, "simulated") != 0)
            {
                fprintf(stderr, "Wrong error in decode: %s\n", stream.errmsg);
                return 3;
            }
        }
        
        stream.bytes_left = msglen;
        stream.state = &fs;
        fs.buffer = buffer;
        fs.fail_after = msglen;
        status = pb_decode(&stream, AllTypes_fields, &msg);
        
        if (!status)
        {
            fprintf(stderr, "Decoding failed: %s\n", stream.errmsg);
            return 4;
        }
    }
    
    /* Test IO errors on encoding */
    {
        bool status;
        pb_ostream_t stream = {&write_callback, NULL, SIZE_MAX, 0};
        faulty_stream_t fs;
        size_t i;
        
        for (i = 0; i < msglen; i++)
        {
            stream.max_size = msglen;
            stream.bytes_written = 0;
            stream.state = &fs;
            fs.buffer = buffer;
            fs.fail_after = i;

            status = pb_encode(&stream, AllTypes_fields, &msg);
            if (status != false)
            {
                fprintf(stderr, "Unexpected success in encode\n");
                return 5;
            }
            else if (strcmp(stream.errmsg, "simulated") != 0)
            {
                fprintf(stderr, "Wrong error in encode: %s\n", stream.errmsg);
                return 6;
            }
        }
        
        stream.max_size = msglen;
        stream.bytes_written = 0;
        stream.state = &fs;
        fs.buffer = buffer;
        fs.fail_after = msglen;
        status = pb_encode(&stream, AllTypes_fields, &msg);
        
        if (!status)
        {
            fprintf(stderr, "Encoding failed: %s\n", stream.errmsg);
            return 7;
        }
    }

    return 0;   
}

