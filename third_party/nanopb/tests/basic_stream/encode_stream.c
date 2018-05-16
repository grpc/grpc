/* Same as test_encode1.c, except writes directly to stdout.
 */

#include <stdio.h>
#include <pb_encode.h>
#include "person.pb.h"
#include "test_helpers.h"

/* This binds the pb_ostream_t into the stdout stream */
bool streamcallback(pb_ostream_t *stream, const uint8_t *buf, size_t count)
{
    FILE *file = (FILE*) stream->state;
    return fwrite(buf, 1, count, file) == count;
}

int main()
{
    /* Initialize the structure with constants */
    Person person = {"Test Person 99", 99, true, "test@person.com",
        3, {{"555-12345678", true, Person_PhoneType_MOBILE},
            {"99-2342", false, 0},
            {"1234-5678", true, Person_PhoneType_WORK},
        }};
    
    /* Prepare the stream, output goes directly to stdout */
    pb_ostream_t stream = {&streamcallback, NULL, SIZE_MAX, 0};
    stream.state = stdout;
    SET_BINARY_MODE(stdout);
    
    /* Now encode it and check if we succeeded. */
    if (pb_encode(&stream, Person_fields, &person))
    {
        return 0; /* Success */
    }
    else
    {
        fprintf(stderr, "Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return 1; /* Failure */
    }
}
