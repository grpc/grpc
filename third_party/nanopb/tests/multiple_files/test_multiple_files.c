/*
 * Tests if this still compiles when multiple .proto files are involved.
 */

#include <stdio.h>
#include <pb_encode.h>
#include "unittests.h"
#include "multifile2.pb.h"

int main()
{
    int status = 0;
    
    /* Test that included file options are properly loaded */
    TEST(OneofMessage_size == 27);
    
    /* Check that enum signedness is detected properly */
    TEST(PB_LTYPE(Enums_fields[0].type) == PB_LTYPE_VARINT);
    TEST(PB_LTYPE(Enums_fields[1].type) == PB_LTYPE_UVARINT);
    
    return status;
}
