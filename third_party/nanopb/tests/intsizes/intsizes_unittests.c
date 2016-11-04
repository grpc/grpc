#include <stdio.h>
#include <string.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include "unittests.h"
#include "intsizes.pb.h"

#define S(x) pb_istream_from_buffer((uint8_t*)x, sizeof(x) - 1)

/* This is a macro instead of function in order to get the actual values
 * into the TEST() lines in output */
#define TEST_ROUNDTRIP(int8, uint8, sint8, \
                       int16, uint16, sint16, \
                       int32, uint32, sint32, \
                       int64, uint64, sint64, expected_result) \
{                                                                           \
    uint8_t buffer1[128], buffer2[128];                                     \
    size_t msgsize;                                                         \
    DefaultSizes msg1 = DefaultSizes_init_zero;                             \
    IntSizes msg2 = IntSizes_init_zero;                                     \
                                                                            \
    msg1.req_int8   = int8;                                                 \
    msg1.req_uint8  = uint8;                                                \
    msg1.req_sint8  = sint8;                                                \
    msg1.req_int16  = int16;                                                \
    msg1.req_uint16 = uint16;                                               \
    msg1.req_sint16 = sint16;                                               \
    msg1.req_int32  = int32;                                                \
    msg1.req_uint32 = uint32;                                               \
    msg1.req_sint32 = sint32;                                               \
    msg1.req_int64  = int64;                                                \
    msg1.req_uint64 = uint64;                                               \
    msg1.req_sint64 = sint64;                                               \
                                                                            \
    {                                                                       \
        pb_ostream_t s = pb_ostream_from_buffer(buffer1, sizeof(buffer1));  \
        TEST(pb_encode(&s, DefaultSizes_fields, &msg1));                    \
        msgsize = s.bytes_written;                                          \
    }                                                                       \
                                                                            \
    {                                                                       \
        pb_istream_t s = pb_istream_from_buffer(buffer1, msgsize);          \
        TEST(pb_decode(&s, IntSizes_fields, &msg2) == expected_result);     \
        if (expected_result)                                                \
        {                                                                   \
            TEST( (int64_t)msg2.req_int8   == int8);                        \
            TEST((uint64_t)msg2.req_uint8  == uint8);                       \
            TEST( (int64_t)msg2.req_sint8  == sint8);                       \
            TEST( (int64_t)msg2.req_int16  == int16);                       \
            TEST((uint64_t)msg2.req_uint16 == uint16);                      \
            TEST( (int64_t)msg2.req_sint16 == sint16);                      \
            TEST( (int64_t)msg2.req_int32  == int32);                       \
            TEST((uint64_t)msg2.req_uint32 == uint32);                      \
            TEST( (int64_t)msg2.req_sint32 == sint32);                      \
            TEST( (int64_t)msg2.req_int64  == int64);                       \
            TEST((uint64_t)msg2.req_uint64 == uint64);                      \
            TEST( (int64_t)msg2.req_sint64 == sint64);                      \
        }                                                                   \
    }                                                                       \
                                                                            \
    if (expected_result)                                                    \
    {                                                                       \
        pb_ostream_t s = pb_ostream_from_buffer(buffer2, sizeof(buffer2));  \
        TEST(pb_encode(&s, IntSizes_fields, &msg2));                        \
        TEST(s.bytes_written == msgsize);                                   \
        TEST(memcmp(buffer1, buffer2, msgsize) == 0);                       \
    }                                                                       \
}

int main()
{
    int status = 0;

    {
        IntSizes msg = IntSizes_init_zero;

        COMMENT("Test field sizes");
        TEST(sizeof(msg.req_int8) == 1);
        TEST(sizeof(msg.req_uint8) == 1);
        TEST(sizeof(msg.req_sint8) == 1);
        TEST(sizeof(msg.req_int16) == 2);
        TEST(sizeof(msg.req_uint16) == 2);
        TEST(sizeof(msg.req_sint16) == 2);
        TEST(sizeof(msg.req_int32) == 4);
        TEST(sizeof(msg.req_uint32) == 4);
        TEST(sizeof(msg.req_sint32) == 4);
        TEST(sizeof(msg.req_int64) == 8);
        TEST(sizeof(msg.req_uint64) == 8);
        TEST(sizeof(msg.req_sint64) == 8);
    }

    COMMENT("Test roundtrip at maximum value");
    TEST_ROUNDTRIP(127,     255,    127,
                   32767, 65535,  32767,
                   INT32_MAX, UINT32_MAX, INT32_MAX,
                   INT64_MAX, UINT64_MAX, INT64_MAX, true);

    COMMENT("Test roundtrip at minimum value");
    TEST_ROUNDTRIP(-128,      0,   -128,
                   -32768,    0, -32768,
                   INT32_MIN, 0, INT32_MIN,
                   INT64_MIN, 0, INT64_MIN, true);

    COMMENT("Test overflow detection");
    TEST_ROUNDTRIP(-129,      0,   -128,
                   -32768,    0, -32768,
                   INT32_MIN, 0, INT32_MIN,
                   INT64_MIN, 0, INT64_MIN, false);
    TEST_ROUNDTRIP(127,     256,    127,
                   32767, 65535,  32767,
                   INT32_MAX, UINT32_MAX, INT32_MAX,
                   INT64_MAX, UINT64_MAX, INT64_MAX, false);
    TEST_ROUNDTRIP(-128,      0,   -128,
                   -32768,    0, -32769,
                   INT32_MIN, 0, INT32_MIN,
                   INT64_MIN, 0, INT64_MIN, false);

    if (status != 0)
        fprintf(stdout, "\n\nSome tests FAILED!\n");

    return status;
}