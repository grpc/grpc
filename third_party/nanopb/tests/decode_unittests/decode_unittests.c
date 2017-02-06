/* This includes the whole .c file to get access to static functions. */
#define PB_ENABLE_MALLOC
#include "pb_common.c"
#include "pb_decode.c"

#include <stdio.h>
#include <string.h>
#include "unittests.h"
#include "unittestproto.pb.h"

#define S(x) pb_istream_from_buffer((uint8_t*)x, sizeof(x) - 1)

bool stream_callback(pb_istream_t *stream, uint8_t *buf, size_t count)
{
    if (stream->state != NULL)
        return false; /* Simulate error */
    
    if (buf != NULL)
        memset(buf, 'x', count);
    return true;
}

/* Verifies that the stream passed to callback matches the byte array pointed to by arg. */
bool callback_check(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    int i;
    uint8_t byte;
    pb_bytes_array_t *ref = (pb_bytes_array_t*) *arg;
    
    for (i = 0; i < ref->size; i++)
    {
        if (!pb_read(stream, &byte, 1))
            return false;
        
        if (byte != ref->bytes[i])
            return false;
    }
    
    return true;
}

int main()
{
    int status = 0;
    
    {
        uint8_t buffer1[] = "foobartest1234";
        uint8_t buffer2[sizeof(buffer1)];
        pb_istream_t stream = pb_istream_from_buffer(buffer1, sizeof(buffer1));
        
        COMMENT("Test pb_read and pb_istream_t");
        TEST(pb_read(&stream, buffer2, 6))
        TEST(memcmp(buffer2, "foobar", 6) == 0)
        TEST(stream.bytes_left == sizeof(buffer1) - 6)
        TEST(pb_read(&stream, buffer2 + 6, stream.bytes_left))
        TEST(memcmp(buffer1, buffer2, sizeof(buffer1)) == 0)
        TEST(stream.bytes_left == 0)
        TEST(!pb_read(&stream, buffer2, 1))
    }
    
    {
        uint8_t buffer[20];
        pb_istream_t stream = {&stream_callback, NULL, 20};
        
        COMMENT("Test pb_read with custom callback");
        TEST(pb_read(&stream, buffer, 5))
        TEST(memcmp(buffer, "xxxxx", 5) == 0)
        TEST(!pb_read(&stream, buffer, 50))
        stream.state = (void*)1; /* Simulated error return from callback */
        TEST(!pb_read(&stream, buffer, 5))
        stream.state = NULL;
        TEST(pb_read(&stream, buffer, 15))
    }
    
    {
        pb_istream_t s;
        uint64_t u;
        int64_t i;
        
        COMMENT("Test pb_decode_varint");
        TEST((s = S("\x00"), pb_decode_varint(&s, &u) && u == 0));
        TEST((s = S("\x01"), pb_decode_varint(&s, &u) && u == 1));
        TEST((s = S("\xAC\x02"), pb_decode_varint(&s, &u) && u == 300));
        TEST((s = S("\xFF\xFF\xFF\xFF\x0F"), pb_decode_varint(&s, &u) && u == UINT32_MAX));
        TEST((s = S("\xFF\xFF\xFF\xFF\x0F"), pb_decode_varint(&s, (uint64_t*)&i) && i == UINT32_MAX));
        TEST((s = S("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01"),
              pb_decode_varint(&s, (uint64_t*)&i) && i == -1));
        TEST((s = S("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01"),
              pb_decode_varint(&s, &u) && u == UINT64_MAX));
        TEST((s = S("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01"),
              !pb_decode_varint(&s, &u)));
    }
    
    {
        pb_istream_t s;
        uint32_t u;
        
        COMMENT("Test pb_decode_varint32");
        TEST((s = S("\x00"), pb_decode_varint32(&s, &u) && u == 0));
        TEST((s = S("\x01"), pb_decode_varint32(&s, &u) && u == 1));
        TEST((s = S("\xAC\x02"), pb_decode_varint32(&s, &u) && u == 300));
        TEST((s = S("\xFF\xFF\xFF\xFF\x0F"), pb_decode_varint32(&s, &u) && u == UINT32_MAX));
        TEST((s = S("\xFF\xFF\xFF\xFF\xFF\x01"), !pb_decode_varint32(&s, &u)));
    }
    
    {
        pb_istream_t s;
        COMMENT("Test pb_skip_varint");
        TEST((s = S("\x00""foobar"), pb_skip_varint(&s) && s.bytes_left == 6))
        TEST((s = S("\xAC\x02""foobar"), pb_skip_varint(&s) && s.bytes_left == 6))
        TEST((s = S("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01""foobar"),
              pb_skip_varint(&s) && s.bytes_left == 6))
        TEST((s = S("\xFF"), !pb_skip_varint(&s)))
    }
    
    {
        pb_istream_t s;
        COMMENT("Test pb_skip_string")
        TEST((s = S("\x00""foobar"), pb_skip_string(&s) && s.bytes_left == 6))
        TEST((s = S("\x04""testfoobar"), pb_skip_string(&s) && s.bytes_left == 6))
        TEST((s = S("\x04"), !pb_skip_string(&s)))
        TEST((s = S("\xFF"), !pb_skip_string(&s)))
    }
    
    {
        pb_istream_t s = S("\x01\x00");
        pb_field_t f = {1, PB_LTYPE_VARINT, 0, 0, 4, 0, 0};
        uint32_t d;
        COMMENT("Test pb_dec_varint using uint32_t")
        TEST(pb_dec_varint(&s, &f, &d) && d == 1)
        
        /* Verify that no more than data_size is written. */
        d = 0xFFFFFFFF;
        f.data_size = 1;
        TEST(pb_dec_varint(&s, &f, &d) && (d == 0xFFFFFF00 || d == 0x00FFFFFF))
    }
    
    {
        pb_istream_t s;
        pb_field_t f = {1, PB_LTYPE_SVARINT, 0, 0, 4, 0, 0};
        int32_t d;
        
        COMMENT("Test pb_dec_svarint using int32_t")
        TEST((s = S("\x01"), pb_dec_svarint(&s, &f, &d) && d == -1))
        TEST((s = S("\x02"), pb_dec_svarint(&s, &f, &d) && d == 1))
        TEST((s = S("\xfe\xff\xff\xff\x0f"), pb_dec_svarint(&s, &f, &d) && d == INT32_MAX))
        TEST((s = S("\xff\xff\xff\xff\x0f"), pb_dec_svarint(&s, &f, &d) && d == INT32_MIN))
    }
    
    {
        pb_istream_t s;
        pb_field_t f = {1, PB_LTYPE_SVARINT, 0, 0, 8, 0, 0};
        uint64_t d;
        
        COMMENT("Test pb_dec_svarint using uint64_t")
        TEST((s = S("\x01"), pb_dec_svarint(&s, &f, &d) && d == -1))
        TEST((s = S("\x02"), pb_dec_svarint(&s, &f, &d) && d == 1))
        TEST((s = S("\xFE\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01"), pb_dec_svarint(&s, &f, &d) && d == INT64_MAX))
        TEST((s = S("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01"), pb_dec_svarint(&s, &f, &d) && d == INT64_MIN))
    }
    
    {
        pb_istream_t s;
        pb_field_t f = {1, PB_LTYPE_FIXED32, 0, 0, 4, 0, 0};
        float d;
        
        COMMENT("Test pb_dec_fixed32 using float (failures here may be caused by imperfect rounding)")
        TEST((s = S("\x00\x00\x00\x00"), pb_dec_fixed32(&s, &f, &d) && d == 0.0f))
        TEST((s = S("\x00\x00\xc6\x42"), pb_dec_fixed32(&s, &f, &d) && d == 99.0f))
        TEST((s = S("\x4e\x61\x3c\xcb"), pb_dec_fixed32(&s, &f, &d) && d == -12345678.0f))
        TEST((s = S("\x00"), !pb_dec_fixed32(&s, &f, &d) && d == -12345678.0f))
    }
    
    {
        pb_istream_t s;
        pb_field_t f = {1, PB_LTYPE_FIXED64, 0, 0, 8, 0, 0};
        double d;
        
        COMMENT("Test pb_dec_fixed64 using double (failures here may be caused by imperfect rounding)")
        TEST((s = S("\x00\x00\x00\x00\x00\x00\x00\x00"), pb_dec_fixed64(&s, &f, &d) && d == 0.0))
        TEST((s = S("\x00\x00\x00\x00\x00\xc0\x58\x40"), pb_dec_fixed64(&s, &f, &d) && d == 99.0))
        TEST((s = S("\x00\x00\x00\xc0\x29\x8c\x67\xc1"), pb_dec_fixed64(&s, &f, &d) && d == -12345678.0f))
    }
    
    {
        pb_istream_t s;
        struct { pb_size_t size; uint8_t bytes[5]; } d;
        pb_field_t f = {1, PB_LTYPE_BYTES, 0, 0, sizeof(d), 0, 0};
        
        COMMENT("Test pb_dec_bytes")
        TEST((s = S("\x00"), pb_dec_bytes(&s, &f, &d) && d.size == 0))
        TEST((s = S("\x01\xFF"), pb_dec_bytes(&s, &f, &d) && d.size == 1 && d.bytes[0] == 0xFF))
        TEST((s = S("\x05xxxxx"), pb_dec_bytes(&s, &f, &d) && d.size == 5))
        TEST((s = S("\x05xxxx"), !pb_dec_bytes(&s, &f, &d)))
        
        /* Note: the size limit on bytes-fields is not strictly obeyed, as
         * the compiler may add some padding to the struct. Using this padding
         * is not a very good thing to do, but it is difficult to avoid when
         * we use only a single uint8_t to store the size of the field.
         * Therefore this tests against a 10-byte string, while otherwise even
         * 6 bytes should error out.
         */
        TEST((s = S("\x10xxxxxxxxxx"), !pb_dec_bytes(&s, &f, &d)))
    }
    
    {
        pb_istream_t s;
        pb_field_t f = {1, PB_LTYPE_STRING, 0, 0, 5, 0, 0};
        char d[5];
        
        COMMENT("Test pb_dec_string")
        TEST((s = S("\x00"), pb_dec_string(&s, &f, &d) && d[0] == '\0'))
        TEST((s = S("\x04xyzz"), pb_dec_string(&s, &f, &d) && strcmp(d, "xyzz") == 0))
        TEST((s = S("\x05xyzzy"), !pb_dec_string(&s, &f, &d)))
    }
    
    {
        pb_istream_t s;
        IntegerArray dest;
        
        COMMENT("Testing pb_decode with repeated int32 field")
        TEST((s = S(""), pb_decode(&s, IntegerArray_fields, &dest) && dest.data_count == 0))
        TEST((s = S("\x08\x01\x08\x02"), pb_decode(&s, IntegerArray_fields, &dest)
             && dest.data_count == 2 && dest.data[0] == 1 && dest.data[1] == 2))
        s = S("\x08\x01\x08\x02\x08\x03\x08\x04\x08\x05\x08\x06\x08\x07\x08\x08\x08\x09\x08\x0A");
        TEST(pb_decode(&s, IntegerArray_fields, &dest) && dest.data_count == 10 && dest.data[9] == 10)
        s = S("\x08\x01\x08\x02\x08\x03\x08\x04\x08\x05\x08\x06\x08\x07\x08\x08\x08\x09\x08\x0A\x08\x0B");
        TEST(!pb_decode(&s, IntegerArray_fields, &dest))
    }
    
    {
        pb_istream_t s;
        IntegerArray dest;
        
        COMMENT("Testing pb_decode with packed int32 field")
        TEST((s = S("\x0A\x00"), pb_decode(&s, IntegerArray_fields, &dest)
            && dest.data_count == 0))
        TEST((s = S("\x0A\x01\x01"), pb_decode(&s, IntegerArray_fields, &dest)
            && dest.data_count == 1 && dest.data[0] == 1))
        TEST((s = S("\x0A\x0A\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A"), pb_decode(&s, IntegerArray_fields, &dest)
            && dest.data_count == 10 && dest.data[0] == 1 && dest.data[9] == 10))
        TEST((s = S("\x0A\x0B\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B"), !pb_decode(&s, IntegerArray_fields, &dest)))
        
        /* Test invalid wire data */
        TEST((s = S("\x0A\xFF"), !pb_decode(&s, IntegerArray_fields, &dest)))
        TEST((s = S("\x0A\x01"), !pb_decode(&s, IntegerArray_fields, &dest)))
    }
    
    {
        pb_istream_t s;
        IntegerArray dest;
        
        COMMENT("Testing pb_decode with unknown fields")
        TEST((s = S("\x18\x0F\x08\x01"), pb_decode(&s, IntegerArray_fields, &dest)
            && dest.data_count == 1 && dest.data[0] == 1))
        TEST((s = S("\x19\x00\x00\x00\x00\x00\x00\x00\x00\x08\x01"), pb_decode(&s, IntegerArray_fields, &dest)
            && dest.data_count == 1 && dest.data[0] == 1))
        TEST((s = S("\x1A\x00\x08\x01"), pb_decode(&s, IntegerArray_fields, &dest)
            && dest.data_count == 1 && dest.data[0] == 1))
        TEST((s = S("\x1B\x08\x01"), !pb_decode(&s, IntegerArray_fields, &dest)))
        TEST((s = S("\x1D\x00\x00\x00\x00\x08\x01"), pb_decode(&s, IntegerArray_fields, &dest)
            && dest.data_count == 1 && dest.data[0] == 1))
    }
    
    {
        pb_istream_t s;
        CallbackArray dest;
        struct { pb_size_t size; uint8_t bytes[10]; } ref;
        dest.data.funcs.decode = &callback_check;
        dest.data.arg = &ref;
        
        COMMENT("Testing pb_decode with callbacks")
        /* Single varint */
        ref.size = 1; ref.bytes[0] = 0x55;
        TEST((s = S("\x08\x55"), pb_decode(&s, CallbackArray_fields, &dest)))
        /* Packed varint */
        ref.size = 3; ref.bytes[0] = ref.bytes[1] = ref.bytes[2] = 0x55;
        TEST((s = S("\x0A\x03\x55\x55\x55"), pb_decode(&s, CallbackArray_fields, &dest)))
        /* Packed varint with loop */
        ref.size = 1; ref.bytes[0] = 0x55;
        TEST((s = S("\x0A\x03\x55\x55\x55"), pb_decode(&s, CallbackArray_fields, &dest)))
        /* Single fixed32 */
        ref.size = 4; ref.bytes[0] = ref.bytes[1] = ref.bytes[2] = ref.bytes[3] = 0xAA;
        TEST((s = S("\x0D\xAA\xAA\xAA\xAA"), pb_decode(&s, CallbackArray_fields, &dest)))
        /* Single fixed64 */
        ref.size = 8; memset(ref.bytes, 0xAA, 8);
        TEST((s = S("\x09\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"), pb_decode(&s, CallbackArray_fields, &dest)))
        /* Unsupported field type */
        TEST((s = S("\x0B\x00"), !pb_decode(&s, CallbackArray_fields, &dest)))
        
        /* Just make sure that our test function works */
        ref.size = 1; ref.bytes[0] = 0x56;
        TEST((s = S("\x08\x55"), !pb_decode(&s, CallbackArray_fields, &dest)))
    }
    
    {
        pb_istream_t s;
        IntegerArray dest;
        
        COMMENT("Testing pb_decode message termination")
        TEST((s = S(""), pb_decode(&s, IntegerArray_fields, &dest)))
        TEST((s = S("\x00"), pb_decode(&s, IntegerArray_fields, &dest)))
        TEST((s = S("\x08\x01"), pb_decode(&s, IntegerArray_fields, &dest)))
        TEST((s = S("\x08\x01\x00"), pb_decode(&s, IntegerArray_fields, &dest)))
        TEST((s = S("\x08"), !pb_decode(&s, IntegerArray_fields, &dest)))
    }
    
    {
        pb_istream_t s;
        IntegerContainer dest = {{0}};
        
        COMMENT("Testing pb_decode_delimited")
        TEST((s = S("\x09\x0A\x07\x0A\x05\x01\x02\x03\x04\x05"),
              pb_decode_delimited(&s, IntegerContainer_fields, &dest)) &&
              dest.submsg.data_count == 5)
    }
    
    {
        pb_istream_t s = {0};
        void *data = NULL;
        
        COMMENT("Testing allocate_field")
        TEST(allocate_field(&s, &data, 10, 10) && data != NULL);
        TEST(allocate_field(&s, &data, 10, 20) && data != NULL);
        
        {
            void *oldvalue = data;
            size_t very_big = (size_t)-1;
            size_t somewhat_big = very_big / 2 + 1;
            size_t not_so_big = (size_t)1 << (4 * sizeof(size_t));
        
            TEST(!allocate_field(&s, &data, very_big, 2) && data == oldvalue);
            TEST(!allocate_field(&s, &data, somewhat_big, 2) && data == oldvalue);
            TEST(!allocate_field(&s, &data, not_so_big, not_so_big) && data == oldvalue);
        }
        
        pb_free(data);
    }
    
    if (status != 0)
        fprintf(stdout, "\n\nSome tests FAILED!\n");
    
    return status;
}
