/* A very simple decoding test case, using person.proto.
 * Produces output compatible with protoc --decode.
 * Reads the encoded data from stdin and prints the values
 * to stdout as text.
 *
 * Run e.g. ./test_encode1 | ./test_decode1
 */

#include <stdio.h>
#include <pb_decode.h>
#include "person.pb.h"
#include "test_helpers.h"

/* This function is called once from main(), it handles
   the decoding and printing. */
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

int main()
{
    uint8_t buffer[Person_size];
    pb_istream_t stream;
    size_t count;
    
    /* Read the data into buffer */
    SET_BINARY_MODE(stdin);
    count = fread(buffer, 1, sizeof(buffer), stdin);
    
    if (!feof(stdin))
    {
    	printf("Message does not fit in buffer\n");
    	return 1;
    }
    
    /* Construct a pb_istream_t for reading from the buffer */
    stream = pb_istream_from_buffer(buffer, count);
    
    /* Decode and print out the stuff */
    if (!print_person(&stream))
    {
        printf("Parsing failed: %s\n", PB_GET_ERROR(&stream));
        return 1;
    } else {
        return 0;
    }
}
