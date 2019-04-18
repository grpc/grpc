#include <stdio.h>
#include <string.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include "unittests.h"
#include "enumsizes.pb.h"

int main()
{
    int status = 0;

    UnpackedEnums msg1 = {
        UU8_MIN,  UU8_MAX,
        UI8_MIN,  UI8_MAX,
        UU16_MIN, UU16_MAX,
        UI16_MIN, UI16_MAX,
    };
    
    PackedEnums msg2;
    UnpackedEnums msg3;
    uint8_t buf[256];
    size_t msgsize;
    
    COMMENT("Step 1: unpacked enums -> protobuf");
    {
        pb_ostream_t s = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST(pb_encode(&s, UnpackedEnums_fields, &msg1));
        msgsize = s.bytes_written;
    }
    
    COMMENT("Step 2: protobuf -> packed enums");
    {
        pb_istream_t s = pb_istream_from_buffer(buf, msgsize);
        TEST(pb_decode(&s, PackedEnums_fields, &msg2));
        
        TEST(msg1.u8_min  == (int)msg2.u8_min);
        TEST(msg1.u8_max  == (int)msg2.u8_max);
        TEST(msg1.i8_min  == (int)msg2.i8_min);
        TEST(msg1.i8_max  == (int)msg2.i8_max);
        TEST(msg1.u16_min == (int)msg2.u16_min);
        TEST(msg1.u16_max == (int)msg2.u16_max);
        TEST(msg1.i16_min == (int)msg2.i16_min);
        TEST(msg1.i16_max == (int)msg2.i16_max);
    }
    
    COMMENT("Step 3: packed enums -> protobuf");
    {
        pb_ostream_t s = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST(pb_encode(&s, PackedEnums_fields, &msg2));
        msgsize = s.bytes_written;
    }
    
    COMMENT("Step 4: protobuf -> unpacked enums");
    {
        pb_istream_t s = pb_istream_from_buffer(buf, msgsize);
        TEST(pb_decode(&s, UnpackedEnums_fields, &msg3));

        TEST(msg1.u8_min  == (int)msg3.u8_min);
        TEST(msg1.u8_max  == (int)msg3.u8_max);
        TEST(msg1.i8_min  == (int)msg3.i8_min);
        TEST(msg1.i8_max  == (int)msg3.i8_max);
        TEST(msg1.u16_min == (int)msg2.u16_min);
        TEST(msg1.u16_max == (int)msg2.u16_max);
        TEST(msg1.i16_min == (int)msg2.i16_min);
        TEST(msg1.i16_max == (int)msg2.i16_max);
    }

    if (status != 0)
        fprintf(stdout, "\n\nSome tests FAILED!\n");

    return status;
}
