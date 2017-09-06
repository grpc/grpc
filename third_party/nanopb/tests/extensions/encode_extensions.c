/* Tests extension fields.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pb_encode.h>
#include "alltypes.pb.h"
#include "extensions.pb.h"
#include "test_helpers.h"

int main(int argc, char **argv)
{
    uint8_t buffer[1024];
    pb_ostream_t stream;

    AllTypes alltypes = {0};
    int32_t extensionfield1 = 12345;
    pb_extension_t ext1;
    ExtensionMessage extensionfield2 = {"test", 54321};
    pb_extension_t ext2;

    /* Set up the extensions */
    alltypes.extensions = &ext1;

    ext1.type = &AllTypes_extensionfield1;
    ext1.dest = &extensionfield1;
    ext1.next = &ext2;
    
    ext2.type = &ExtensionMessage_AllTypes_extensionfield2;
    ext2.dest = &extensionfield2;
    ext2.next = NULL;

    /* Set up the output stream */
    stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    
    /* Now encode the message and check if we succeeded. */
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
    
    /* Check that the field tags are properly generated */
    (void)AllTypes_extensionfield1_tag;
    (void)ExtensionMessage_AllTypes_extensionfield2_tag;
}

