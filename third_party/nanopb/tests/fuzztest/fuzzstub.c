/* Fuzz testing for the nanopb core.
 * This can be used with external fuzzers, e.g. radamsa.
 * It performs most of the same checks as fuzztest, but does not feature data generation.
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <malloc_wrappers.h>
#include "alltypes_static.pb.h"
#include "alltypes_pointer.pb.h"

#define BUFSIZE 4096

static bool do_static_decode(uint8_t *buffer, size_t msglen, bool assert_success)
{
    pb_istream_t stream;
    bool status;
    
    alltypes_static_AllTypes *msg = malloc_with_check(sizeof(alltypes_static_AllTypes));
    stream = pb_istream_from_buffer(buffer, msglen);
    status = pb_decode(&stream, alltypes_static_AllTypes_fields, msg);
    
    if (!status && assert_success)
    {
        /* Anything that was successfully encoded, should be decodeable.
         * One exception: strings without null terminator are encoded up
         * to end of buffer, but refused on decode because the terminator
         * would not fit. */
        if (strcmp(stream.errmsg, "string overflow") != 0)
            assert(status);
    }
    
    free_with_check(msg);
    return status;
}

static bool do_pointer_decode(uint8_t *buffer, size_t msglen, bool assert_success)
{
    pb_istream_t stream;
    bool status;
    alltypes_pointer_AllTypes *msg;
    
    msg = malloc_with_check(sizeof(alltypes_pointer_AllTypes));
    memset(msg, 0, sizeof(alltypes_pointer_AllTypes));
    stream = pb_istream_from_buffer(buffer, msglen);

    assert(get_alloc_count() == 0);
    status = pb_decode(&stream, alltypes_pointer_AllTypes_fields, msg);
    
    if (assert_success)
        assert(status);
    
    pb_release(alltypes_pointer_AllTypes_fields, msg);    
    assert(get_alloc_count() == 0);
    
    free_with_check(msg);

    return status;
}

/* Do a decode -> encode -> decode -> encode roundtrip */
static void do_static_roundtrip(uint8_t *buffer, size_t msglen)
{
    bool status;
    uint8_t *buf2 = malloc_with_check(BUFSIZE);
    uint8_t *buf3 = malloc_with_check(BUFSIZE);
    size_t msglen2, msglen3;
    alltypes_static_AllTypes *msg1 = malloc_with_check(sizeof(alltypes_static_AllTypes));
    alltypes_static_AllTypes *msg2 = malloc_with_check(sizeof(alltypes_static_AllTypes));
    memset(msg1, 0, sizeof(alltypes_static_AllTypes));
    memset(msg2, 0, sizeof(alltypes_static_AllTypes));
    
    {
        pb_istream_t stream = pb_istream_from_buffer(buffer, msglen);
        status = pb_decode(&stream, alltypes_static_AllTypes_fields, msg1);
        assert(status);
    }
    
    {
        pb_ostream_t stream = pb_ostream_from_buffer(buf2, BUFSIZE);
        status = pb_encode(&stream, alltypes_static_AllTypes_fields, msg1);
        assert(status);
        msglen2 = stream.bytes_written;
    }
    
    {
        pb_istream_t stream = pb_istream_from_buffer(buf2, msglen2);
        status = pb_decode(&stream, alltypes_static_AllTypes_fields, msg2);
        assert(status);
    }
    
    {
        pb_ostream_t stream = pb_ostream_from_buffer(buf3, BUFSIZE);
        status = pb_encode(&stream, alltypes_static_AllTypes_fields, msg2);
        assert(status);
        msglen3 = stream.bytes_written;
    }
    
    assert(msglen2 == msglen3);
    assert(memcmp(buf2, buf3, msglen2) == 0);
    
    free_with_check(msg1);
    free_with_check(msg2);
    free_with_check(buf2);
    free_with_check(buf3);
}

/* Do decode -> encode -> decode -> encode roundtrip */
static void do_pointer_roundtrip(uint8_t *buffer, size_t msglen)
{
    bool status;
    uint8_t *buf2 = malloc_with_check(BUFSIZE);
    uint8_t *buf3 = malloc_with_check(BUFSIZE);
    size_t msglen2, msglen3;
    alltypes_pointer_AllTypes *msg1 = malloc_with_check(sizeof(alltypes_pointer_AllTypes));
    alltypes_pointer_AllTypes *msg2 = malloc_with_check(sizeof(alltypes_pointer_AllTypes));
    memset(msg1, 0, sizeof(alltypes_pointer_AllTypes));
    memset(msg2, 0, sizeof(alltypes_pointer_AllTypes));
    
    {
        pb_istream_t stream = pb_istream_from_buffer(buffer, msglen);
        status = pb_decode(&stream, alltypes_pointer_AllTypes_fields, msg1);
        assert(status);
    }
    
    {
        pb_ostream_t stream = pb_ostream_from_buffer(buf2, BUFSIZE);
        status = pb_encode(&stream, alltypes_pointer_AllTypes_fields, msg1);
        assert(status);
        msglen2 = stream.bytes_written;
    }
    
    {
        pb_istream_t stream = pb_istream_from_buffer(buf2, msglen2);
        status = pb_decode(&stream, alltypes_pointer_AllTypes_fields, msg2);
        assert(status);
    }
    
    {
        pb_ostream_t stream = pb_ostream_from_buffer(buf3, BUFSIZE);
        status = pb_encode(&stream, alltypes_pointer_AllTypes_fields, msg2);
        assert(status);
        msglen3 = stream.bytes_written;
    }
    
    assert(msglen2 == msglen3);
    assert(memcmp(buf2, buf3, msglen2) == 0);
    
    pb_release(alltypes_pointer_AllTypes_fields, msg1);
    pb_release(alltypes_pointer_AllTypes_fields, msg2);
    free_with_check(msg1);
    free_with_check(msg2);
    free_with_check(buf2);
    free_with_check(buf3);
}

static void run_iteration()
{
    uint8_t *buffer = malloc_with_check(BUFSIZE);
    size_t msglen;
    bool status;
    
    msglen = fread(buffer, 1, BUFSIZE, stdin);

    status = do_static_decode(buffer, msglen, false);
    
    if (status)
        do_static_roundtrip(buffer, msglen);
    
    status = do_pointer_decode(buffer, msglen, false);
    
    if (status)
        do_pointer_roundtrip(buffer, msglen);
    
    free_with_check(buffer);
}

int main(int argc, char **argv)
{
    run_iteration();
    
    return 0;
}

