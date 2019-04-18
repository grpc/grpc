/* Attempts to test all the datatypes supported by ProtoBuf.
 * This is a backwards-compatibility test, using alltypes_legacy.h.
 * It is similar to encode_alltypes, but duplicated in order to allow
 * encode_alltypes to test any new features introduced later.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pb_encode.h>
#include "alltypes_legacy.h"
#include "test_helpers.h"

int main(int argc, char **argv)
{
    int mode = (argc > 1) ? atoi(argv[1]) : 0;
    
    /* Initialize the structure with constants */
    AllTypes alltypes = {0};
    
    alltypes.req_int32         = -1001;
    alltypes.req_int64         = -1002;
    alltypes.req_uint32        = 1003;
    alltypes.req_uint64        = 1004;
    alltypes.req_sint32        = -1005;
    alltypes.req_sint64        = -1006;
    alltypes.req_bool          = true;
    
    alltypes.req_fixed32       = 1008;
    alltypes.req_sfixed32      = -1009;
    alltypes.req_float         = 1010.0f;
    
    alltypes.req_fixed64       = 1011;
    alltypes.req_sfixed64      = -1012;
    alltypes.req_double        = 1013.0;
    
    strcpy(alltypes.req_string, "1014");
    alltypes.req_bytes.size = 4;
    memcpy(alltypes.req_bytes.bytes, "1015", 4);
    strcpy(alltypes.req_submsg.substuff1, "1016");
    alltypes.req_submsg.substuff2 = 1016;
    alltypes.req_enum = MyEnum_Truth;
    
    alltypes.rep_int32_count = 5; alltypes.rep_int32[4] = -2001;
    alltypes.rep_int64_count = 5; alltypes.rep_int64[4] = -2002;
    alltypes.rep_uint32_count = 5; alltypes.rep_uint32[4] = 2003;
    alltypes.rep_uint64_count = 5; alltypes.rep_uint64[4] = 2004;
    alltypes.rep_sint32_count = 5; alltypes.rep_sint32[4] = -2005;
    alltypes.rep_sint64_count = 5; alltypes.rep_sint64[4] = -2006;
    alltypes.rep_bool_count = 5; alltypes.rep_bool[4] = true;
    
    alltypes.rep_fixed32_count = 5; alltypes.rep_fixed32[4] = 2008;
    alltypes.rep_sfixed32_count = 5; alltypes.rep_sfixed32[4] = -2009;
    alltypes.rep_float_count = 5; alltypes.rep_float[4] = 2010.0f;
    
    alltypes.rep_fixed64_count = 5; alltypes.rep_fixed64[4] = 2011;
    alltypes.rep_sfixed64_count = 5; alltypes.rep_sfixed64[4] = -2012;
    alltypes.rep_double_count = 5; alltypes.rep_double[4] = 2013.0;
    
    alltypes.rep_string_count = 5; strcpy(alltypes.rep_string[4], "2014");
    alltypes.rep_bytes_count = 5; alltypes.rep_bytes[4].size = 4;
    memcpy(alltypes.rep_bytes[4].bytes, "2015", 4);

    alltypes.rep_submsg_count = 5;
    strcpy(alltypes.rep_submsg[4].substuff1, "2016");
    alltypes.rep_submsg[4].substuff2 = 2016;
    alltypes.rep_submsg[4].has_substuff3 = true;
    alltypes.rep_submsg[4].substuff3 = 2016;
    
    alltypes.rep_enum_count = 5; alltypes.rep_enum[4] = MyEnum_Truth;
    
    if (mode != 0)
    {
        /* Fill in values for optional fields */
        alltypes.has_opt_int32 = true;
        alltypes.opt_int32         = 3041;
        alltypes.has_opt_int64 = true;
        alltypes.opt_int64         = 3042;
        alltypes.has_opt_uint32 = true;
        alltypes.opt_uint32        = 3043;
        alltypes.has_opt_uint64 = true;
        alltypes.opt_uint64        = 3044;
        alltypes.has_opt_sint32 = true;
        alltypes.opt_sint32        = 3045;
        alltypes.has_opt_sint64 = true;
        alltypes.opt_sint64        = 3046;
        alltypes.has_opt_bool = true;
        alltypes.opt_bool          = true;
        
        alltypes.has_opt_fixed32 = true;
        alltypes.opt_fixed32       = 3048;
        alltypes.has_opt_sfixed32 = true;
        alltypes.opt_sfixed32      = 3049;
        alltypes.has_opt_float = true;
        alltypes.opt_float         = 3050.0f;
        
        alltypes.has_opt_fixed64 = true;
        alltypes.opt_fixed64       = 3051;
        alltypes.has_opt_sfixed64 = true;
        alltypes.opt_sfixed64      = 3052;
        alltypes.has_opt_double = true;
        alltypes.opt_double        = 3053.0;
        
        alltypes.has_opt_string = true;
        strcpy(alltypes.opt_string, "3054");
        alltypes.has_opt_bytes = true;
        alltypes.opt_bytes.size = 4;
        memcpy(alltypes.opt_bytes.bytes, "3055", 4);
        alltypes.has_opt_submsg = true;
        strcpy(alltypes.opt_submsg.substuff1, "3056");
        alltypes.opt_submsg.substuff2 = 3056;
        alltypes.has_opt_enum = true;
        alltypes.opt_enum = MyEnum_Truth;
    }
    
    alltypes.end = 1099;

    {    
        uint8_t buffer[1024];
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
            fprintf(stderr, "Encoding failed!\n");
            return 1; /* Failure */
        }
    }
}
