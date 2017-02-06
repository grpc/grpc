/* Attempts to test all the datatypes supported by ProtoBuf when used as callback fields.
 * Note that normally there would be no reason to use callback fields for this,
 * because each encoder defined here only gives a single field.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pb_decode.h>
#include "alltypes.pb.h"
#include "test_helpers.h"

#define TEST(x) if (!(x)) { \
    printf("Test " #x " failed (in field %d).\n", field->tag); \
    return false; \
    }

static bool read_varint(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    uint64_t value;
    if (!pb_decode_varint(stream, &value))
        return false;
    
    TEST((int64_t)value == (long)*arg);
    return true;
}

static bool read_svarint(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    int64_t value;
    if (!pb_decode_svarint(stream, &value))
        return false;
    
    TEST(value == (long)*arg);
    return true;
}

static bool read_fixed32(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    uint32_t value;
    if (!pb_decode_fixed32(stream, &value))
        return false;
    
    TEST(value == *(uint32_t*)*arg);
    return true;
}

static bool read_fixed64(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    uint64_t value;
    if (!pb_decode_fixed64(stream, &value))
        return false;
    
    TEST(value == *(uint64_t*)*arg);
    return true;
}

static bool read_string(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    uint8_t buf[16] = {0};
    size_t len = stream->bytes_left;
    
    if (len > sizeof(buf) - 1 || !pb_read(stream, buf, len))
        return false;
    
    TEST(strcmp((char*)buf, *arg) == 0);
    return true;
}

static bool read_submsg(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    SubMessage submsg = {""};
    
    if (!pb_decode(stream, SubMessage_fields, &submsg))
        return false;
    
    TEST(memcmp(&submsg, *arg, sizeof(submsg)));
    return true;
}

static bool read_emptymsg(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    EmptyMessage emptymsg = {0};
    return pb_decode(stream, EmptyMessage_fields, &emptymsg);
}

static bool read_repeated_varint(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    int32_t** expected = (int32_t**)arg;
    uint64_t value;
    if (!pb_decode_varint(stream, &value))
        return false;

    TEST(*(*expected)++ == value);
    return true;
}

static bool read_repeated_svarint(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    int32_t** expected = (int32_t**)arg;
    int64_t value;
    if (!pb_decode_svarint(stream, &value))
        return false;

    TEST(*(*expected)++ == value);
    return true;
}

static bool read_repeated_fixed32(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    uint32_t** expected = (uint32_t**)arg;
    uint32_t value;
    if (!pb_decode_fixed32(stream, &value))
        return false;

    TEST(*(*expected)++ == value);
    return true;
}

static bool read_repeated_fixed64(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    uint64_t** expected = (uint64_t**)arg;
    uint64_t value;
    if (!pb_decode_fixed64(stream, &value))
        return false;

    TEST(*(*expected)++ == value);
    return true;
}

static bool read_repeated_string(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    uint8_t*** expected = (uint8_t***)arg;
    uint8_t buf[16] = {0};
    size_t len = stream->bytes_left;
    
    if (len > sizeof(buf) - 1 || !pb_read(stream, buf, len))
        return false;
    
    TEST(strcmp((char*)*(*expected)++, (char*)buf) == 0);
    return true;
}

static bool read_repeated_submsg(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    SubMessage** expected = (SubMessage**)arg;
    SubMessage decoded = {""};
    if (!pb_decode(stream, SubMessage_fields, &decoded))
        return false;

    TEST(memcmp((*expected)++, &decoded, sizeof(decoded)) == 0);
    return true;
}

static bool read_limits(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    Limits decoded = {0};
    if (!pb_decode(stream, Limits_fields, &decoded))
        return false;

    TEST(decoded.int32_min  == INT32_MIN);
    TEST(decoded.int32_max  == INT32_MAX);
    TEST(decoded.uint32_min == 0);
    TEST(decoded.uint32_max == UINT32_MAX);
    TEST(decoded.int64_min  == INT64_MIN);
    TEST(decoded.int64_max  == INT64_MAX);
    TEST(decoded.uint64_min == 0);
    TEST(decoded.uint64_max == UINT64_MAX);
    TEST(decoded.enum_min   == HugeEnum_Negative);
    TEST(decoded.enum_max   == HugeEnum_Positive);
    
    return true;
}

/* This function is called once from main(), it handles
   the decoding and checks the fields. */
bool check_alltypes(pb_istream_t *stream, int mode)
{
    /* Values for use from callbacks through pointers. */
    uint32_t    req_fixed32     = 1008;
    int32_t     req_sfixed32    = -1009;
    float       req_float       = 1010.0f;
    uint64_t    req_fixed64     = 1011;
    int64_t     req_sfixed64    = -1012;
    double      req_double      = 1013.0;
    SubMessage  req_submsg      = {"1016", 1016};
    
    int32_t     rep_int32[5]    = {0, 0, 0, 0, -2001};
    int32_t     rep_int64[5]    = {0, 0, 0, 0, -2002};
    int32_t     rep_uint32[5]   = {0, 0, 0, 0,  2003};
    int32_t     rep_uint64[5]   = {0, 0, 0, 0,  2004};
    int32_t     rep_sint32[5]   = {0, 0, 0, 0, -2005};
    int32_t     rep_sint64[5]   = {0, 0, 0, 0, -2006};
    int32_t     rep_bool[5]     = {false, false, false, false, true};
    uint32_t    rep_fixed32[5]  = {0, 0, 0, 0,  2008};
    int32_t     rep_sfixed32[5] = {0, 0, 0, 0, -2009};
    float       rep_float[5]    = {0, 0, 0, 0,  2010.0f};
    uint64_t    rep_fixed64[5]  = {0, 0, 0, 0,  2011};
    int64_t     rep_sfixed64[5] = {0, 0, 0, 0, -2012};
    double      rep_double[5]   = {0, 0, 0, 0,  2013.0};
    char*       rep_string[5]   = {"", "", "", "", "2014"};
    char*       rep_bytes[5]    = {"", "", "", "", "2015"};
    SubMessage  rep_submsg[5]   = {{"", 0, 0, 3},
                                   {"", 0, 0, 3},
                                   {"", 0, 0, 3},
                                   {"", 0, 0, 3},
                                   {"2016", 2016, true, 2016}};
    int32_t     rep_enum[5]     = {0, 0, 0, 0, MyEnum_Truth};
    
    uint32_t    opt_fixed32     = 3048;
    int32_t     opt_sfixed32    = 3049;
    float       opt_float       = 3050.0f;
    uint64_t    opt_fixed64     = 3051;
    int64_t     opt_sfixed64    = 3052;
    double      opt_double      = 3053.0f;
    SubMessage  opt_submsg      = {"3056", 3056};

    SubMessage  oneof_msg1      = {"4059", 4059};
    
    /* Bind callbacks for required fields */
    AllTypes alltypes;
    
    /* Fill with garbage to better detect initialization errors */
    memset(&alltypes, 0xAA, sizeof(alltypes));
    alltypes.extensions = 0;
    
    alltypes.req_int32.funcs.decode = &read_varint;
    alltypes.req_int32.arg = (void*)-1001;
    
    alltypes.req_int64.funcs.decode = &read_varint;
    alltypes.req_int64.arg = (void*)-1002;
    
    alltypes.req_uint32.funcs.decode = &read_varint;
    alltypes.req_uint32.arg = (void*)1003;

    alltypes.req_uint32.funcs.decode = &read_varint;
    alltypes.req_uint32.arg = (void*)1003;    
    
    alltypes.req_uint64.funcs.decode = &read_varint;
    alltypes.req_uint64.arg = (void*)1004;
    
    alltypes.req_sint32.funcs.decode = &read_svarint;
    alltypes.req_sint32.arg = (void*)-1005;   
    
    alltypes.req_sint64.funcs.decode = &read_svarint;
    alltypes.req_sint64.arg = (void*)-1006;   
    
    alltypes.req_bool.funcs.decode = &read_varint;
    alltypes.req_bool.arg = (void*)true;   
    
    alltypes.req_fixed32.funcs.decode = &read_fixed32;
    alltypes.req_fixed32.arg = &req_fixed32;
    
    alltypes.req_sfixed32.funcs.decode = &read_fixed32;
    alltypes.req_sfixed32.arg = &req_sfixed32;
    
    alltypes.req_float.funcs.decode = &read_fixed32;
    alltypes.req_float.arg = &req_float;
    
    alltypes.req_fixed64.funcs.decode = &read_fixed64;
    alltypes.req_fixed64.arg = &req_fixed64;
    
    alltypes.req_sfixed64.funcs.decode = &read_fixed64;
    alltypes.req_sfixed64.arg = &req_sfixed64;
    
    alltypes.req_double.funcs.decode = &read_fixed64;
    alltypes.req_double.arg = &req_double;
    
    alltypes.req_string.funcs.decode = &read_string;
    alltypes.req_string.arg = "1014";
    
    alltypes.req_bytes.funcs.decode = &read_string;
    alltypes.req_bytes.arg = "1015";
    
    alltypes.req_submsg.funcs.decode = &read_submsg;
    alltypes.req_submsg.arg = &req_submsg;
    
    alltypes.req_enum.funcs.decode = &read_varint;
    alltypes.req_enum.arg = (void*)MyEnum_Truth;
    
    alltypes.req_emptymsg.funcs.decode = &read_emptymsg;
    
    /* Bind callbacks for repeated fields */
    alltypes.rep_int32.funcs.decode = &read_repeated_varint;
    alltypes.rep_int32.arg = rep_int32;
    
    alltypes.rep_int64.funcs.decode = &read_repeated_varint;
    alltypes.rep_int64.arg = rep_int64;
    
    alltypes.rep_uint32.funcs.decode = &read_repeated_varint;
    alltypes.rep_uint32.arg = rep_uint32;
    
    alltypes.rep_uint64.funcs.decode = &read_repeated_varint;
    alltypes.rep_uint64.arg = rep_uint64;
    
    alltypes.rep_sint32.funcs.decode = &read_repeated_svarint;
    alltypes.rep_sint32.arg = rep_sint32;
    
    alltypes.rep_sint64.funcs.decode = &read_repeated_svarint;
    alltypes.rep_sint64.arg = rep_sint64;
    
    alltypes.rep_bool.funcs.decode = &read_repeated_varint;
    alltypes.rep_bool.arg = rep_bool;
    
    alltypes.rep_fixed32.funcs.decode = &read_repeated_fixed32;
    alltypes.rep_fixed32.arg = rep_fixed32;
    
    alltypes.rep_sfixed32.funcs.decode = &read_repeated_fixed32;
    alltypes.rep_sfixed32.arg = rep_sfixed32;
    
    alltypes.rep_float.funcs.decode = &read_repeated_fixed32;
    alltypes.rep_float.arg = rep_float;
    
    alltypes.rep_fixed64.funcs.decode = &read_repeated_fixed64;
    alltypes.rep_fixed64.arg = rep_fixed64;
    
    alltypes.rep_sfixed64.funcs.decode = &read_repeated_fixed64;
    alltypes.rep_sfixed64.arg = rep_sfixed64;
    
    alltypes.rep_double.funcs.decode = &read_repeated_fixed64;
    alltypes.rep_double.arg = rep_double;
    
    alltypes.rep_string.funcs.decode = &read_repeated_string;
    alltypes.rep_string.arg = rep_string;
    
    alltypes.rep_bytes.funcs.decode = &read_repeated_string;
    alltypes.rep_bytes.arg = rep_bytes;
    
    alltypes.rep_submsg.funcs.decode = &read_repeated_submsg;
    alltypes.rep_submsg.arg = rep_submsg;
    
    alltypes.rep_enum.funcs.decode = &read_repeated_varint;
    alltypes.rep_enum.arg = rep_enum;
    
    alltypes.rep_emptymsg.funcs.decode = &read_emptymsg;
    
    alltypes.req_limits.funcs.decode = &read_limits;
    
    alltypes.end.funcs.decode = &read_varint;
    alltypes.end.arg = (void*)1099;
    
    /* Bind callbacks for optional fields */
    if (mode == 1)
    {
        alltypes.opt_int32.funcs.decode = &read_varint;
        alltypes.opt_int32.arg = (void*)3041;
        
        alltypes.opt_int64.funcs.decode = &read_varint;
        alltypes.opt_int64.arg = (void*)3042;
        
        alltypes.opt_uint32.funcs.decode = &read_varint;
        alltypes.opt_uint32.arg = (void*)3043;
        
        alltypes.opt_uint64.funcs.decode = &read_varint;
        alltypes.opt_uint64.arg = (void*)3044;
        
        alltypes.opt_sint32.funcs.decode = &read_svarint;
        alltypes.opt_sint32.arg = (void*)3045;
        
        alltypes.opt_sint64.funcs.decode = &read_svarint;
        alltypes.opt_sint64.arg = (void*)3046;
        
        alltypes.opt_bool.funcs.decode = &read_varint;
        alltypes.opt_bool.arg = (void*)true;

        alltypes.opt_fixed32.funcs.decode = &read_fixed32;
        alltypes.opt_fixed32.arg = &opt_fixed32;
        
        alltypes.opt_sfixed32.funcs.decode = &read_fixed32;
        alltypes.opt_sfixed32.arg = &opt_sfixed32;
        
        alltypes.opt_float.funcs.decode = &read_fixed32;
        alltypes.opt_float.arg = &opt_float;
        
        alltypes.opt_fixed64.funcs.decode = &read_fixed64;
        alltypes.opt_fixed64.arg = &opt_fixed64;
        
        alltypes.opt_sfixed64.funcs.decode = &read_fixed64;
        alltypes.opt_sfixed64.arg = &opt_sfixed64;
        
        alltypes.opt_double.funcs.decode = &read_fixed64;
        alltypes.opt_double.arg = &opt_double;
        
        alltypes.opt_string.funcs.decode = &read_string;
        alltypes.opt_string.arg = "3054";
        
        alltypes.opt_bytes.funcs.decode = &read_string;
        alltypes.opt_bytes.arg = "3055";
        
        alltypes.opt_submsg.funcs.decode = &read_submsg;
        alltypes.opt_submsg.arg = &opt_submsg;
        
        alltypes.opt_enum.funcs.decode = &read_varint;
        alltypes.opt_enum.arg = (void*)MyEnum_Truth;
        
        alltypes.opt_emptymsg.funcs.decode = &read_emptymsg;

        alltypes.oneof_msg1.funcs.decode = &read_submsg;
        alltypes.oneof_msg1.arg = &oneof_msg1;
    }
    
    return pb_decode(stream, AllTypes_fields, &alltypes);
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
