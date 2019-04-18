/* Encode a message using oneof fields */

#include <stdio.h>
#include <stdlib.h>
#include <pb_encode.h>
#include "oneof.pb.h"
#include "test_helpers.h"

int main(int argc, char **argv)
{
    uint8_t buffer[OneOfMessage_size];
    OneOfMessage msg = OneOfMessage_init_zero;
    pb_ostream_t stream;
    int option;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: encode_oneof [number]\n");
        return 1;
    }
    option = atoi(argv[1]);

    /* Prefix and suffix are used to test that the union does not disturb
     * other fields in the same message. */
    msg.prefix = 123;

    /* We encode one of the 'values' fields based on command line argument */
    if (option == 1)
    {
        msg.which_values = OneOfMessage_first_tag;
        msg.values.first = 999;
    }
    else if (option == 2)
    {
        msg.which_values = OneOfMessage_second_tag;
        strcpy(msg.values.second, "abcd");
    }
    else if (option == 3)
    {
        msg.which_values = OneOfMessage_third_tag;
        msg.values.third.array_count = 5;
        msg.values.third.array[0] = 1;
        msg.values.third.array[1] = 2;
        msg.values.third.array[2] = 3;
        msg.values.third.array[3] = 4;
        msg.values.third.array[4] = 5;
    }

    msg.suffix = 321;

    stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (pb_encode(&stream, OneOfMessage_fields, &msg))
    {
        SET_BINARY_MODE(stdout);
        fwrite(buffer, 1, stream.bytes_written, stdout);
        return 0;
    }
    else
    {
        fprintf(stderr, "Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return 1;
    }
}
