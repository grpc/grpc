/* Decodes a double value into a float variable.
 * Used to read double values with AVR code, which doesn't support double directly.
 */

#include <stdio.h>
#include <pb_decode.h>
#include "double_conversion.h"
#include "doubleproto.pb.h"

int main()
{
    uint8_t buffer[32];
    size_t count = fread(buffer, 1, sizeof(buffer), stdin);
    pb_istream_t stream = pb_istream_from_buffer(buffer, count);
    
    AVRDoubleMessage message;
    pb_decode(&stream, AVRDoubleMessage_fields, &message);
    
    float v1 = double_to_float(message.field1);
    float v2 = double_to_float(message.field2);

    printf("Values: %f %f\n", v1, v2);
    
    if (v1 == 1234.5678f &&
        v2 == 0.00001f)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}
