/* Same as test_decode1 but reads from stdin directly.
 */

#include <stdio.h>
#include <pb_decode.h>
#include "person.pb.h"
#include "test_helpers.h"

/* This function is called once from main(), it handles
   the decoding and printing.
   Ugly copy-paste from test_decode1.c. */
bool print_person(pb_istream_t *stream)
{
    int i;
    Person person = Person_init_zero;
    
    if (!pb_decode(stream, Person_fields, &person))
        return false;
    
    /* Now the decoding is done, rest is just to print stuff out. */

    printf("name: \"%s\"\n", person.name);
    printf("id: %ld\n", (long)person.id);
    
    if (person.has_email)
        printf("email: \"%s\"\n", person.email);
    
    for (i = 0; i < person.phone_count; i++)
    {
        Person_PhoneNumber *phone = &person.phone[i];
        printf("phone {\n");
        printf("  number: \"%s\"\n", phone->number);
        
        if (phone->has_type)
        {
            switch (phone->type)
            {
                case Person_PhoneType_WORK:
                    printf("  type: WORK\n");
                    break;
                
                case Person_PhoneType_HOME:
                    printf("  type: HOME\n");
                    break;
                
                case Person_PhoneType_MOBILE:
                    printf("  type: MOBILE\n");
                    break;
            }
        }
        printf("}\n");
    }
    
    return true;
}

/* This binds the pb_istream_t to stdin */
bool callback(pb_istream_t *stream, uint8_t *buf, size_t count)
{
    FILE *file = (FILE*)stream->state;
    bool status;
    
    status = (fread(buf, 1, count, file) == count);
    
    if (feof(file))
        stream->bytes_left = 0;
    
    return status;
}

int main()
{
    pb_istream_t stream = {&callback, NULL, SIZE_MAX};
    stream.state = stdin;
    SET_BINARY_MODE(stdin);

    if (!print_person(&stream))
    {
        printf("Parsing failed: %s\n", PB_GET_ERROR(&stream));
        return 1;
    } else {
        return 0;
    }
}
