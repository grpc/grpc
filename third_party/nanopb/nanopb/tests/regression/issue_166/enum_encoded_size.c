#include <stdio.h>
#include <string.h>
#include <pb_encode.h>
#include "unittests.h"
#include "enums.pb.h"

int main()
{
    int status = 0;
    
    uint8_t buf[256];
    SignedMsg msg1;
    UnsignedMsg msg2;
    pb_ostream_t s;
    
    {
        COMMENT("Test negative value of signed enum");
        /* Negative value should take up the maximum size */
        msg1.value = SignedEnum_SE_MIN;
        s = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST(pb_encode(&s, SignedMsg_fields, &msg1));
        TEST(s.bytes_written == SignedMsg_size);
        
        COMMENT("Test positive value of signed enum");
        /* Positive value should be smaller */
        msg1.value = SignedEnum_SE_MAX;
        s = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST(pb_encode(&s, SignedMsg_fields, &msg1));
        TEST(s.bytes_written < SignedMsg_size);
    }
    
    {
        COMMENT("Test positive value of unsigned enum");
        /* This should take up the maximum size */
        msg2.value = UnsignedEnum_UE_MAX;
        s = pb_ostream_from_buffer(buf, sizeof(buf));
        TEST(pb_encode(&s, UnsignedMsg_fields, &msg2));
        TEST(s.bytes_written == UnsignedMsg_size);
    }
    
    return status;
}

