/* Fuzz testing for the nanopb core.
 * Attempts to verify all the properties defined in the security model document.
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

static uint64_t random_seed;

/* Uses xorshift64 here instead of rand() for both speed and
 * reproducibility across platforms. */
static uint32_t rand_word()
{
    random_seed ^= random_seed >> 12;
    random_seed ^= random_seed << 25;
    random_seed ^= random_seed >> 27;
    return random_seed * 2685821657736338717ULL;
}

/* Get a random integer in range, with approximately flat distribution. */
static int rand_int(int min, int max)
{
    return rand_word() % (max + 1 - min) + min;
}

static bool rand_bool()
{
    return rand_word() & 1;
}

/* Get a random byte, with skewed distribution.
 * Important corner cases like 0xFF, 0x00 and 0xFE occur more
 * often than other values. */
static uint8_t rand_byte()
{
    uint32_t w = rand_word();
    uint8_t b = w & 0xFF;
    if (w & 0x100000)
        b >>= (w >> 8) & 7;
    if (w & 0x200000)
        b <<= (w >> 12) & 7;
    if (w & 0x400000)
        b ^= 0xFF;
    return b;
}

/* Get a random length, with skewed distribution.
 * Favors the shorter lengths, but always atleast 1. */
static size_t rand_len(size_t max)
{
    uint32_t w = rand_word();
    size_t s;
    if (w & 0x800000)
        w &= 3;
    else if (w & 0x400000)
        w &= 15;
    else if (w & 0x200000)
        w &= 255;

    s = (w % max);
    if (s == 0)
        s = 1;
    
    return s;
}

/* Fills a buffer with random data with skewed distribution. */
static void rand_fill(uint8_t *buf, size_t count)
{
    while (count--)
        *buf++ = rand_byte();
}

/* Fill with random protobuf-like data */
static size_t rand_fill_protobuf(uint8_t *buf, size_t min_bytes, size_t max_bytes, int min_tag)
{
    pb_ostream_t stream = pb_ostream_from_buffer(buf, max_bytes);

    while(stream.bytes_written < min_bytes)
    {
        pb_wire_type_t wt = rand_int(0, 3);
        if (wt == 3) wt = 5; /* Gap in values */
        
        if (!pb_encode_tag(&stream, wt, rand_int(min_tag, min_tag + 512)))
            break;
    
        if (wt == PB_WT_VARINT)
        {
            uint64_t value;
            rand_fill((uint8_t*)&value, sizeof(value));
            pb_encode_varint(&stream, value);
        }
        else if (wt == PB_WT_64BIT)
        {
            uint64_t value;
            rand_fill((uint8_t*)&value, sizeof(value));
            pb_encode_fixed64(&stream, &value);
        }
        else if (wt == PB_WT_32BIT)
        {
            uint32_t value;
            rand_fill((uint8_t*)&value, sizeof(value));
            pb_encode_fixed32(&stream, &value);
        }
        else if (wt == PB_WT_STRING)
        {
            size_t len;
            uint8_t *buf;
            
            if (min_bytes > stream.bytes_written)
                len = rand_len(min_bytes - stream.bytes_written);
            else
                len = 0;
            
            buf = malloc(len);
            pb_encode_varint(&stream, len);
            rand_fill(buf, len);
            pb_write(&stream, buf, len);
            free(buf);
        }
    }
    
    return stream.bytes_written;
}

/* Given a buffer of data, mess it up a bit */
static void rand_mess(uint8_t *buf, size_t count)
{
    int m = rand_int(0, 3);
    
    if (m == 0)
    {
        /* Replace random substring */
        int s = rand_int(0, count - 1);
        int l = rand_len(count - s);
        rand_fill(buf + s, l);
    }
    else if (m == 1)
    {
        /* Swap random bytes */
        int a = rand_int(0, count - 1);
        int b = rand_int(0, count - 1);
        int x = buf[a];
        buf[a] = buf[b];
        buf[b] = x;
    }
    else if (m == 2)
    {
        /* Duplicate substring */
        int s = rand_int(0, count - 2);
        int l = rand_len((count - s) / 2);
        memcpy(buf + s + l, buf + s, l);
    }
    else if (m == 3)
    {
        /* Add random protobuf noise */
        int s = rand_int(0, count - 1);
        int l = rand_len(count - s);
        rand_fill_protobuf(buf + s, l, count - s, 1);
    }
}

/* Some default data to put in the message */
static const alltypes_static_AllTypes initval = alltypes_static_AllTypes_init_default;

#define BUFSIZE 4096

static bool do_static_encode(uint8_t *buffer, size_t *msglen)
{
    pb_ostream_t stream;
    bool status;

    /* Allocate a message and fill it with defaults */
    alltypes_static_AllTypes *msg = malloc_with_check(sizeof(alltypes_static_AllTypes));
    memcpy(msg, &initval, sizeof(initval));

    /* Apply randomness to the data before encoding */
    while (rand_int(0, 7))
        rand_mess((uint8_t*)msg, sizeof(alltypes_static_AllTypes));

    stream = pb_ostream_from_buffer(buffer, BUFSIZE);
    status = pb_encode(&stream, alltypes_static_AllTypes_fields, msg);
    assert(stream.bytes_written <= BUFSIZE);
    assert(stream.bytes_written <= alltypes_static_AllTypes_size);
    
    *msglen = stream.bytes_written;
    pb_release(alltypes_static_AllTypes_fields, msg);
    free_with_check(msg);
    
    return status;
}

/* Append or prepend protobuf noise */
static void do_protobuf_noise(uint8_t *buffer, size_t *msglen)
{
    int m = rand_int(0, 2);
    size_t max_size = BUFSIZE - 32 - *msglen;
    if (m == 1)
    {
        /* Prepend */
        uint8_t *tmp = malloc_with_check(BUFSIZE);
        size_t s = rand_fill_protobuf(tmp, rand_len(max_size), BUFSIZE - *msglen, 512);
        memmove(buffer + s, buffer, *msglen);
        memcpy(buffer, tmp, s);
        free_with_check(tmp);
        *msglen += s;
    }
    else if (m == 2)
    {
        /* Append */
        size_t s = rand_fill_protobuf(buffer + *msglen, rand_len(max_size), BUFSIZE - *msglen, 512);
        *msglen += s;
    }
}

static bool do_static_decode(uint8_t *buffer, size_t msglen, bool assert_success)
{
    pb_istream_t stream;
    bool status;
    
    alltypes_static_AllTypes *msg = malloc_with_check(sizeof(alltypes_static_AllTypes));
    rand_fill((uint8_t*)msg, sizeof(alltypes_static_AllTypes));
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
    
    rand_fill(buffer, BUFSIZE);

    if (do_static_encode(buffer, &msglen))
    {
        do_protobuf_noise(buffer, &msglen);
    
        status = do_static_decode(buffer, msglen, true);
        
        if (status)
            do_static_roundtrip(buffer, msglen);
        
        status = do_pointer_decode(buffer, msglen, true);
        
        if (status)
            do_pointer_roundtrip(buffer, msglen);
        
        /* Apply randomness to the encoded data */
        while (rand_bool())
            rand_mess(buffer, BUFSIZE);
        
        /* Apply randomness to encoded data length */
        if (rand_bool())
            msglen = rand_int(0, BUFSIZE);
        
        status = do_static_decode(buffer, msglen, false);
        do_pointer_decode(buffer, msglen, status);
        
        if (status)
        {
            do_static_roundtrip(buffer, msglen);
            do_pointer_roundtrip(buffer, msglen);
        }
    }
    
    free_with_check(buffer);
}

int main(int argc, char **argv)
{
    int i;
    if (argc > 1)
    {
        random_seed = atol(argv[1]);
    }
    else
    {
        random_seed = time(NULL);
    }
    
    fprintf(stderr, "Random seed: %llu\n", (long long unsigned)random_seed);
    
    for (i = 0; i < 10000; i++)
    {
        run_iteration();
    }
    
    return 0;
}

