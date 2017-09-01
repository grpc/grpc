/* Decode a message using oneof fields */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pb_decode.h>
#include "oneof.pb.h"
#include "test_helpers.h"
#include "unittests.h"

/* Test the 'AnonymousOneOfMessage' */
int test_oneof_1(pb_istream_t *stream, int option)
{
    AnonymousOneOfMessage msg;
    int status = 0;

    /* To better catch initialization errors */
    memset(&msg, 0xAA, sizeof(msg));

    if (!pb_decode(stream, AnonymousOneOfMessage_fields, &msg))
    {
        printf("Decoding failed: %s\n", PB_GET_ERROR(stream));
        return 1;
    }

    /* Check that the basic fields work normally */
    TEST(msg.prefix == 123);
    TEST(msg.suffix == 321);

    /* Check that we got the right oneof according to command line */
    if (option == 1)
    {
        TEST(msg.which_values == AnonymousOneOfMessage_first_tag);
        TEST(msg.first == 999);
    }
    else if (option == 2)
    {
        TEST(msg.which_values == AnonymousOneOfMessage_second_tag);
        TEST(strcmp(msg.second, "abcd") == 0);
    }
    else if (option == 3)
    {
        TEST(msg.which_values == AnonymousOneOfMessage_third_tag);
        TEST(msg.third.array[0] == 1);
        TEST(msg.third.array[1] == 2);
        TEST(msg.third.array[2] == 3);
        TEST(msg.third.array[3] == 4);
        TEST(msg.third.array[4] == 5);
    }

    return status;
}

int main(int argc, char **argv)
{
    uint8_t buffer[AnonymousOneOfMessage_size];
    size_t count;
    int option;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: decode_oneof [number]\n");
        return 1;
    }
    option = atoi(argv[1]);

    SET_BINARY_MODE(stdin);
    count = fread(buffer, 1, sizeof(buffer), stdin);

    if (!feof(stdin))
    {
        printf("Message does not fit in buffer\n");
        return 1;
    }

    {
        int status = 0;
        pb_istream_t stream;

        stream = pb_istream_from_buffer(buffer, count);
        status = test_oneof_1(&stream, option);

        if (status != 0)
            return status;
    }

    return 0;
}
