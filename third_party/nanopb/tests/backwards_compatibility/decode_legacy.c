/* Tests the decoding of all types.
 * This is a backwards-compatibility test, using alltypes_legacy.h.
 * It is similar to decode_alltypes, but duplicated in order to allow
 * decode_alltypes to test any new features introduced later.
 *
 * Run e.g. ./encode_legacy | ./decode_legacy
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pb_decode.h>
#include "alltypes_legacy.h"
#include "test_helpers.h"

#define TEST(x) if (!(x)) { \
    printf("Test " #x " failed.\n"); \
    return false; \
    }

/* This function is called once from main(), it handles
   the decoding and checks the fields. */
bool check_alltypes(pb_istream_t *stream, int mode)
{
    AllTypes alltypes = {0};
    
    if (!pb_decode(stream, AllTypes_fields, &alltypes))
        return false;
    
    TEST(alltypes.req_int32         == -1001);
    TEST(alltypes.req_int64         == -1002);
    TEST(alltypes.req_uint32        == 1003);
    TEST(alltypes.req_uint64        == 1004);
    TEST(alltypes.req_sint32        == -1005);
    TEST(alltypes.req_sint64        == -1006);
    TEST(alltypes.req_bool          == true);
    
    TEST(alltypes.req_fixed32       == 1008);
    TEST(alltypes.req_sfixed32      == -1009);
    TEST(alltypes.req_float         == 1010.0f);
    
    TEST(alltypes.req_fixed64       == 1011);
    TEST(alltypes.req_sfixed64      == -1012);
    TEST(alltypes.req_double        == 1013.0f);
    
    TEST(strcmp(alltypes.req_string, "1014") == 0);
    TEST(alltypes.req_bytes.size == 4);
    TEST(memcmp(alltypes.req_bytes.bytes, "1015", 4) == 0);
    TEST(strcmp(alltypes.req_submsg.substuff1, "1016") == 0);
    TEST(alltypes.req_submsg.substuff2 == 1016);
    TEST(alltypes.req_submsg.substuff3 == 3);
    TEST(alltypes.req_enum == MyEnum_Truth);
    
    TEST(alltypes.rep_int32_count == 5 && alltypes.rep_int32[4] == -2001 && alltypes.rep_int32[0] == 0);
    TEST(alltypes.rep_int64_count == 5 && alltypes.rep_int64[4] == -2002 && alltypes.rep_int64[0] == 0);
    TEST(alltypes.rep_uint32_count == 5 && alltypes.rep_uint32[4] == 2003 && alltypes.rep_uint32[0] == 0);
    TEST(alltypes.rep_uint64_count == 5 && alltypes.rep_uint64[4] == 2004 && alltypes.rep_uint64[0] == 0);
    TEST(alltypes.rep_sint32_count == 5 && alltypes.rep_sint32[4] == -2005 && alltypes.rep_sint32[0] == 0);
    TEST(alltypes.rep_sint64_count == 5 && alltypes.rep_sint64[4] == -2006 && alltypes.rep_sint64[0] == 0);
    TEST(alltypes.rep_bool_count == 5 && alltypes.rep_bool[4] == true && alltypes.rep_bool[0] == false);
    
    TEST(alltypes.rep_fixed32_count == 5 && alltypes.rep_fixed32[4] == 2008 && alltypes.rep_fixed32[0] == 0);
    TEST(alltypes.rep_sfixed32_count == 5 && alltypes.rep_sfixed32[4] == -2009 && alltypes.rep_sfixed32[0] == 0);
    TEST(alltypes.rep_float_count == 5 && alltypes.rep_float[4] == 2010.0f && alltypes.rep_float[0] == 0.0f);
    
    TEST(alltypes.rep_fixed64_count == 5 && alltypes.rep_fixed64[4] == 2011 && alltypes.rep_fixed64[0] == 0);
    TEST(alltypes.rep_sfixed64_count == 5 && alltypes.rep_sfixed64[4] == -2012 && alltypes.rep_sfixed64[0] == 0);
    TEST(alltypes.rep_double_count == 5 && alltypes.rep_double[4] == 2013.0 && alltypes.rep_double[0] == 0.0);
    
    TEST(alltypes.rep_string_count == 5 && strcmp(alltypes.rep_string[4], "2014") == 0 && alltypes.rep_string[0][0] == '\0');
    TEST(alltypes.rep_bytes_count == 5 && alltypes.rep_bytes[4].size == 4 && alltypes.rep_bytes[0].size == 0);
    TEST(memcmp(alltypes.rep_bytes[4].bytes, "2015", 4) == 0);

    TEST(alltypes.rep_submsg_count == 5);
    TEST(strcmp(alltypes.rep_submsg[4].substuff1, "2016") == 0 && alltypes.rep_submsg[0].substuff1[0] == '\0');
    TEST(alltypes.rep_submsg[4].substuff2 == 2016 && alltypes.rep_submsg[0].substuff2 == 0);
    TEST(alltypes.rep_submsg[4].substuff3 == 2016 && alltypes.rep_submsg[0].substuff3 == 3);
    
    TEST(alltypes.rep_enum_count == 5 && alltypes.rep_enum[4] == MyEnum_Truth && alltypes.rep_enum[0] == MyEnum_Zero);
    
    if (mode == 0)
    {
        /* Expect default values */
        TEST(alltypes.has_opt_int32     == false);
        TEST(alltypes.opt_int32         == 4041);
        TEST(alltypes.has_opt_int64     == false);
        TEST(alltypes.opt_int64         == 4042);
        TEST(alltypes.has_opt_uint32    == false);
        TEST(alltypes.opt_uint32        == 4043);
        TEST(alltypes.has_opt_uint64    == false);
        TEST(alltypes.opt_uint64        == 4044);
        TEST(alltypes.has_opt_sint32    == false);
        TEST(alltypes.opt_sint32        == 4045);
        TEST(alltypes.has_opt_sint64    == false);
        TEST(alltypes.opt_sint64        == 4046);
        TEST(alltypes.has_opt_bool      == false);
        TEST(alltypes.opt_bool          == false);
        
        TEST(alltypes.has_opt_fixed32   == false);
        TEST(alltypes.opt_fixed32       == 4048);
        TEST(alltypes.has_opt_sfixed32  == false);
        TEST(alltypes.opt_sfixed32      == 4049);
        TEST(alltypes.has_opt_float     == false);
        TEST(alltypes.opt_float         == 4050.0f);
        
        TEST(alltypes.has_opt_fixed64   == false);
        TEST(alltypes.opt_fixed64       == 4051);
        TEST(alltypes.has_opt_sfixed64  == false);
        TEST(alltypes.opt_sfixed64      == 4052);
        TEST(alltypes.has_opt_double    == false);
        TEST(alltypes.opt_double        == 4053.0);
        
        TEST(alltypes.has_opt_string    == false);
        TEST(strcmp(alltypes.opt_string, "4054") == 0);
        TEST(alltypes.has_opt_bytes     == false);
        TEST(alltypes.opt_bytes.size == 4);
        TEST(memcmp(alltypes.opt_bytes.bytes, "4055", 4) == 0);
        TEST(alltypes.has_opt_submsg    == false);
        TEST(strcmp(alltypes.opt_submsg.substuff1, "1") == 0);
        TEST(alltypes.opt_submsg.substuff2 == 2);
        TEST(alltypes.opt_submsg.substuff3 == 3);
        TEST(alltypes.has_opt_enum     == false);
        TEST(alltypes.opt_enum == MyEnum_Second);
    }
    else
    {
        /* Expect filled-in values */
        TEST(alltypes.has_opt_int32     == true);
        TEST(alltypes.opt_int32         == 3041);
        TEST(alltypes.has_opt_int64     == true);
        TEST(alltypes.opt_int64         == 3042);
        TEST(alltypes.has_opt_uint32    == true);
        TEST(alltypes.opt_uint32        == 3043);
        TEST(alltypes.has_opt_uint64    == true);
        TEST(alltypes.opt_uint64        == 3044);
        TEST(alltypes.has_opt_sint32    == true);
        TEST(alltypes.opt_sint32        == 3045);
        TEST(alltypes.has_opt_sint64    == true);
        TEST(alltypes.opt_sint64        == 3046);
        TEST(alltypes.has_opt_bool      == true);
        TEST(alltypes.opt_bool          == true);
        
        TEST(alltypes.has_opt_fixed32   == true);
        TEST(alltypes.opt_fixed32       == 3048);
        TEST(alltypes.has_opt_sfixed32  == true);
        TEST(alltypes.opt_sfixed32      == 3049);
        TEST(alltypes.has_opt_float     == true);
        TEST(alltypes.opt_float         == 3050.0f);
        
        TEST(alltypes.has_opt_fixed64   == true);
        TEST(alltypes.opt_fixed64       == 3051);
        TEST(alltypes.has_opt_sfixed64  == true);
        TEST(alltypes.opt_sfixed64      == 3052);
        TEST(alltypes.has_opt_double    == true);
        TEST(alltypes.opt_double        == 3053.0);
        
        TEST(alltypes.has_opt_string    == true);
        TEST(strcmp(alltypes.opt_string, "3054") == 0);
        TEST(alltypes.has_opt_bytes     == true);
        TEST(alltypes.opt_bytes.size == 4);
        TEST(memcmp(alltypes.opt_bytes.bytes, "3055", 4) == 0);
        TEST(alltypes.has_opt_submsg    == true);
        TEST(strcmp(alltypes.opt_submsg.substuff1, "3056") == 0);
        TEST(alltypes.opt_submsg.substuff2 == 3056);
        TEST(alltypes.opt_submsg.substuff3 == 3);
        TEST(alltypes.has_opt_enum      == true);
        TEST(alltypes.opt_enum == MyEnum_Truth);
    }
    
    TEST(alltypes.end == 1099);
    
    return true;
}

int main(int argc, char **argv)
{
    uint8_t buffer[1024];
    size_t count;
    pb_istream_t stream;

    /* Whether to expect the optional values or the default values. */
    int mode = (argc > 1) ? atoi(argv[1]) : 0;
    
    /* Read the data into buffer */
    SET_BINARY_MODE(stdin);
    count = fread(buffer, 1, sizeof(buffer), stdin);
    
    /* Construct a pb_istream_t for reading from the buffer */
    stream = pb_istream_from_buffer(buffer, count);
    
    /* Decode and print out the stuff */
    if (!check_alltypes(&stream, mode))
    {
        printf("Parsing failed: %s\n", PB_GET_ERROR(&stream));
        return 1;
    } else {
        return 0;
    }
}
