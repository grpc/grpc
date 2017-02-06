/* Encodes a float value into a double on the wire.
 * Used to emit doubles from AVR code, which doesn't support double directly.
 */

#include <stdio.h>
#include <pb_encode.h>
#include "double_conversion.h"
#include "doubleproto.pb.h"

int main()
{
    AVRDoubleMessage message = {
        float_to_double(1234.5678f),
        float_to_double(0.00001f)
    };
    
    uint8_t buffer[32];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    
    pb_encode(&stream, AVRDoubleMessage_fields, &message);
    fwrite(buffer, 1, stream.bytes_written, stdout);

    return 0;
}

