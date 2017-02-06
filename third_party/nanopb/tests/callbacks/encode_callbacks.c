/* Encoding testcase for callback fields */

#include <stdio.h>
#include <string.h>
#include <pb_encode.h>
#include "callbacks.pb.h"
#include "test_helpers.h"

bool encode_string(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    char *str = "Hello world!";
    
    if (!pb_encode_tag_for_field(stream, field))
        return false;
    
    return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

bool encode_int32(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    if (!pb_encode_tag_for_field(stream, field))
        return false;
    
    return pb_encode_varint(stream, 42);
}

bool encode_fixed32(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    uint32_t value = 42;

    if (!pb_encode_tag_for_field(stream, field))
        return false;
    
    return pb_encode_fixed32(stream, &value);
}

bool encode_fixed64(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    uint64_t value = 42;

    if (!pb_encode_tag_for_field(stream, field))
        return false;
    
    return pb_encode_fixed64(stream, &value);
}

bool encode_repeatedstring(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    char *str[4] = {"Hello world!", "", "Test", "Test2"};
    int i;
    
    for (i = 0; i < 4; i++)
    {
        if (!pb_encode_tag_for_field(stream, field))
            return false;
        
        if (!pb_encode_string(stream, (uint8_t*)str[i], strlen(str[i])))
            return false;
    }
    return true;
}

int main()
{
    uint8_t buffer[1024];
    pb_ostream_t stream;
    TestMessage testmessage = {{{NULL}}};
    
    stream = pb_ostream_from_buffer(buffer, 1024);
    
    testmessage.stringvalue.funcs.encode = &encode_string;
    testmessage.int32value.funcs.encode = &encode_int32;
    testmessage.fixed32value.funcs.encode = &encode_fixed32;
    testmessage.fixed64value.funcs.encode = &encode_fixed64;
    
    testmessage.has_submsg = true;
    testmessage.submsg.stringvalue.funcs.encode = &encode_string;
    testmessage.submsg.int32value.funcs.encode = &encode_int32;
    testmessage.submsg.fixed32value.funcs.encode = &encode_fixed32;
    testmessage.submsg.fixed64value.funcs.encode = &encode_fixed64;

    testmessage.repeatedstring.funcs.encode = &encode_repeatedstring;
    
    if (!pb_encode(&stream, TestMessage_fields, &testmessage))
        return 1;
    
    SET_BINARY_MODE(stdout);
    if (fwrite(buffer, stream.bytes_written, 1, stdout) != 1)
        return 2;
    
    return 0;
}
