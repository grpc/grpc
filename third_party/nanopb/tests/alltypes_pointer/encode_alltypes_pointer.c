/* Attempts to test all the datatypes supported by ProtoBuf.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pb_encode.h>
#include "alltypes.pb.h"
#include "test_helpers.h"

int main(int argc, char **argv)
{
    int mode = (argc > 1) ? atoi(argv[1]) : 0;
    
    /* Values for required fields */
    int32_t     req_int32         = -1001;
    int64_t     req_int64         = -1002;
    uint32_t    req_uint32        = 1003;
    uint64_t    req_uint64        = 1004;
    int32_t     req_sint32        = -1005;
    int64_t     req_sint64        = -1006;
    bool        req_bool          = true;
    uint32_t    req_fixed32       = 1008;
    int32_t     req_sfixed32      = -1009;
    float       req_float         = 1010.0f;
    uint64_t    req_fixed64       = 1011;
    int64_t     req_sfixed64      = -1012;
    double      req_double        = 1013.0;
    char*       req_string        = "1014";
    PB_BYTES_ARRAY_T(4) req_bytes = {4, {'1', '0', '1', '5'}};
    static int32_t req_substuff   = 1016;
    SubMessage  req_submsg        = {"1016", &req_substuff};
    MyEnum      req_enum          = MyEnum_Truth;
    EmptyMessage req_emptymsg     = {0};
    
    int32_t     end               = 1099;

    /* Values for repeated fields */
    int32_t     rep_int32[5]      = {0, 0, 0, 0, -2001};
    int64_t     rep_int64[5]      = {0, 0, 0, 0, -2002};
    uint32_t    rep_uint32[5]     = {0, 0, 0, 0, 2003};
    uint64_t    rep_uint64[5]     = {0, 0, 0, 0, 2004};
    int32_t     rep_sint32[5]     = {0, 0, 0, 0, -2005};
    int64_t     rep_sint64[5]     = {0, 0, 0, 0, -2006};
    bool        rep_bool[5]       = {false, false, false, false, true};
    uint32_t    rep_fixed32[5]    = {0, 0, 0, 0, 2008};
    int32_t     rep_sfixed32[5]   = {0, 0, 0, 0, -2009};
    float       rep_float[5]      = {0, 0, 0, 0, 2010.0f};
    uint64_t    rep_fixed64[5]    = {0, 0, 0, 0, 2011};
    int64_t     rep_sfixed64[5]   = {0, 0, 0, 0, -2012};
    double      rep_double[5]     = {0, 0, 0, 0, 2013.0f};
    char*       rep_string[5]     = {"", "", "", "", "2014"};
    static PB_BYTES_ARRAY_T(4) rep_bytes_4 = {4, {'2', '0', '1', '5'}};
    pb_bytes_array_t *rep_bytes[5]= {NULL, NULL, NULL, NULL, (pb_bytes_array_t*)&rep_bytes_4};
    static int32_t rep_sub2zero   = 0;
    static int32_t rep_substuff2  = 2016;
    static uint32_t rep_substuff3 = 2016;
    SubMessage  rep_submsg[5]     = {{"", &rep_sub2zero},
                                     {"", &rep_sub2zero},
                                     {"", &rep_sub2zero},
                                     {"", &rep_sub2zero},
                                     {"2016", &rep_substuff2, &rep_substuff3}};
    MyEnum      rep_enum[5]       = {0, 0, 0, 0, MyEnum_Truth};
    EmptyMessage rep_emptymsg[5]  = {{0}, {0}, {0}, {0}, {0}};

    /* Values for optional fields */
    int32_t     opt_int32         = 3041;
    int64_t     opt_int64         = 3042;
    uint32_t    opt_uint32        = 3043;
    uint64_t    opt_uint64        = 3044;
    int32_t     opt_sint32        = 3045;
    int64_t     opt_sint64        = 3046;
    bool        opt_bool          = true;
    uint32_t    opt_fixed32       = 3048;
    int32_t     opt_sfixed32      = 3049;
    float       opt_float         = 3050.0f;
    uint64_t    opt_fixed64       = 3051;
    int64_t     opt_sfixed64      = 3052;
    double      opt_double        = 3053.0;
    char*       opt_string        = "3054";
    PB_BYTES_ARRAY_T(4) opt_bytes = {4, {'3', '0', '5', '5'}};
    static int32_t opt_substuff   = 3056;
    SubMessage  opt_submsg        = {"3056", &opt_substuff};
    MyEnum      opt_enum          = MyEnum_Truth;
    EmptyMessage opt_emptymsg     = {0};

    static int32_t oneof_substuff = 4059;
    SubMessage  oneof_msg1        = {"4059", &oneof_substuff};

    /* Values for the Limits message. */
    static int32_t  int32_min  = INT32_MIN;
    static int32_t  int32_max  = INT32_MAX;
    static uint32_t uint32_min = 0;
    static uint32_t uint32_max = UINT32_MAX;
    static int64_t  int64_min  = INT64_MIN;
    static int64_t  int64_max  = INT64_MAX;
    static uint64_t uint64_min = 0;
    static uint64_t uint64_max = UINT64_MAX;
    static HugeEnum enum_min   = HugeEnum_Negative;
    static HugeEnum enum_max   = HugeEnum_Positive;
    Limits req_limits = {&int32_min,    &int32_max,
                         &uint32_min,   &uint32_max,
                         &int64_min,    &int64_max,
                         &uint64_min,   &uint64_max,
                         &enum_min,     &enum_max};

    /* Initialize the message struct with pointers to the fields. */
    AllTypes alltypes = {0};

    alltypes.req_int32         = &req_int32;
    alltypes.req_int64         = &req_int64;
    alltypes.req_uint32        = &req_uint32;
    alltypes.req_uint64        = &req_uint64;
    alltypes.req_sint32        = &req_sint32;
    alltypes.req_sint64        = &req_sint64;
    alltypes.req_bool          = &req_bool;
    alltypes.req_fixed32       = &req_fixed32;
    alltypes.req_sfixed32      = &req_sfixed32;
    alltypes.req_float         = &req_float;
    alltypes.req_fixed64       = &req_fixed64;
    alltypes.req_sfixed64      = &req_sfixed64;
    alltypes.req_double        = &req_double;
    alltypes.req_string        = req_string;
    alltypes.req_bytes         = (pb_bytes_array_t*)&req_bytes;
    alltypes.req_submsg        = &req_submsg;
    alltypes.req_enum          = &req_enum;
    alltypes.req_emptymsg      = &req_emptymsg;
    alltypes.req_limits        = &req_limits;
    
    alltypes.rep_int32_count    = 5; alltypes.rep_int32     = rep_int32;
    alltypes.rep_int64_count    = 5; alltypes.rep_int64     = rep_int64;
    alltypes.rep_uint32_count   = 5; alltypes.rep_uint32    = rep_uint32;
    alltypes.rep_uint64_count   = 5; alltypes.rep_uint64    = rep_uint64;
    alltypes.rep_sint32_count   = 5; alltypes.rep_sint32    = rep_sint32;
    alltypes.rep_sint64_count   = 5; alltypes.rep_sint64    = rep_sint64;
    alltypes.rep_bool_count     = 5; alltypes.rep_bool      = rep_bool;
    alltypes.rep_fixed32_count  = 5; alltypes.rep_fixed32   = rep_fixed32;
    alltypes.rep_sfixed32_count = 5; alltypes.rep_sfixed32  = rep_sfixed32;
    alltypes.rep_float_count    = 5; alltypes.rep_float     = rep_float;
    alltypes.rep_fixed64_count  = 5; alltypes.rep_fixed64   = rep_fixed64;
    alltypes.rep_sfixed64_count = 5; alltypes.rep_sfixed64  = rep_sfixed64;
    alltypes.rep_double_count   = 5; alltypes.rep_double    = rep_double;
    alltypes.rep_string_count   = 5; alltypes.rep_string    = rep_string;
    alltypes.rep_bytes_count    = 5; alltypes.rep_bytes     = rep_bytes;
    alltypes.rep_submsg_count   = 5; alltypes.rep_submsg    = rep_submsg;
    alltypes.rep_enum_count     = 5; alltypes.rep_enum      = rep_enum;
    alltypes.rep_emptymsg_count = 5; alltypes.rep_emptymsg  = rep_emptymsg;
    
    if (mode != 0)
    {
        /* Fill in values for optional fields */
        alltypes.opt_int32         = &opt_int32;
        alltypes.opt_int64         = &opt_int64;
        alltypes.opt_uint32        = &opt_uint32;
        alltypes.opt_uint64        = &opt_uint64;
        alltypes.opt_sint32        = &opt_sint32;
        alltypes.opt_sint64        = &opt_sint64;
        alltypes.opt_bool          = &opt_bool;
        alltypes.opt_fixed32       = &opt_fixed32;
        alltypes.opt_sfixed32      = &opt_sfixed32;
        alltypes.opt_float         = &opt_float;
        alltypes.opt_fixed64       = &opt_fixed64;
        alltypes.opt_sfixed64      = &opt_sfixed64;
        alltypes.opt_double        = &opt_double;
        alltypes.opt_string        = opt_string;
        alltypes.opt_bytes         = (pb_bytes_array_t*)&opt_bytes;
        alltypes.opt_submsg        = &opt_submsg;
        alltypes.opt_enum          = &opt_enum;
        alltypes.opt_emptymsg      = &opt_emptymsg;

        alltypes.which_oneof = AllTypes_oneof_msg1_tag;
        alltypes.oneof.oneof_msg1 = &oneof_msg1;
    }
    
    alltypes.end = &end;
    
    {
        uint8_t buffer[4096];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        
        /* Now encode it and check if we succeeded. */
        if (pb_encode(&stream, AllTypes_fields, &alltypes))
        {
            SET_BINARY_MODE(stdout);
            fwrite(buffer, 1, stream.bytes_written, stdout);
            return 0; /* Success */
        }
        else
        {
            fprintf(stderr, "Encoding failed: %s\n", PB_GET_ERROR(&stream));
            return 1; /* Failure */
        }
    }
}
