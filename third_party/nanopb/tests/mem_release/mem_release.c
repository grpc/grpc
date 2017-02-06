/* Make sure that all fields are freed in various scenarios. */

#include <pb_decode.h>
#include <pb_encode.h>
#include <malloc_wrappers.h>
#include <stdio.h>
#include <test_helpers.h>
#include "mem_release.pb.h"

#define TEST(x) if (!(x)) { \
    fprintf(stderr, "Test " #x " on line %d failed.\n", __LINE__); \
    return false; \
    }

static char *test_str_arr[] = {"1", "2", ""};
static SubMessage test_msg_arr[] = {SubMessage_init_zero, SubMessage_init_zero};
static pb_extension_t ext1, ext2;

static void fill_TestMessage(TestMessage *msg)
{
    msg->static_req_submsg.dynamic_str = "12345";
    msg->static_req_submsg.dynamic_str_arr_count = 3;
    msg->static_req_submsg.dynamic_str_arr = test_str_arr;
    msg->static_req_submsg.dynamic_submsg_count = 2;
    msg->static_req_submsg.dynamic_submsg = test_msg_arr;
    msg->static_req_submsg.dynamic_submsg[1].dynamic_str = "abc";
    msg->static_opt_submsg.dynamic_str = "abc";
    msg->static_rep_submsg_count = 2;
    msg->static_rep_submsg[1].dynamic_str = "abc";
    msg->has_static_opt_submsg = true;
    msg->dynamic_submsg = &msg->static_req_submsg;

    msg->extensions = &ext1;
    ext1.type = &dynamic_ext;
    ext1.dest = &msg->static_req_submsg;
    ext1.next = &ext2;
    ext2.type = &static_ext;
    ext2.dest = &msg->static_req_submsg;
    ext2.next = NULL;
}

/* Basic fields, nested submessages, extensions */
static bool test_TestMessage()
{
    uint8_t buffer[256];
    size_t msgsize;
    
    /* Construct a message with various fields filled in */
    {
        TestMessage msg = TestMessage_init_zero;
        pb_ostream_t stream;

        fill_TestMessage(&msg);
        
        stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        if (!pb_encode(&stream, TestMessage_fields, &msg))
        {
            fprintf(stderr, "Encode failed: %s\n", PB_GET_ERROR(&stream));
            return false;
        }
        msgsize = stream.bytes_written;
    }
    
    /* Output encoded message for debug */
    SET_BINARY_MODE(stdout);
    fwrite(buffer, 1, msgsize, stdout);
    
    /* Decode memory using dynamic allocation */
    {
        TestMessage msg = TestMessage_init_zero;
        pb_istream_t stream;
        SubMessage ext2_dest;

        msg.extensions = &ext1;
        ext1.type = &dynamic_ext;
        ext1.dest = NULL;
        ext1.next = &ext2;
        ext2.type = &static_ext;
        ext2.dest = &ext2_dest;
        ext2.next = NULL;
        
        stream = pb_istream_from_buffer(buffer, msgsize);
        if (!pb_decode(&stream, TestMessage_fields, &msg))
        {
            fprintf(stderr, "Decode failed: %s\n", PB_GET_ERROR(&stream));
            return false;
        }
        
        /* Make sure it encodes back to same data */
        {
            uint8_t buffer2[256];
            pb_ostream_t ostream = pb_ostream_from_buffer(buffer2, sizeof(buffer2));
            TEST(pb_encode(&ostream, TestMessage_fields, &msg));
            TEST(ostream.bytes_written == msgsize);
            TEST(memcmp(buffer, buffer2, msgsize) == 0);
        }
        
        /* Make sure that malloc counters work */
        TEST(get_alloc_count() > 0);
        
        /* Make sure that pb_release releases everything */
        pb_release(TestMessage_fields, &msg);
        TEST(get_alloc_count() == 0);
        
        /* Check that double-free is a no-op */
        pb_release(TestMessage_fields, &msg);
        TEST(get_alloc_count() == 0);
    }
    
    return true;
}

/* Oneofs */
static bool test_OneofMessage()
{
    uint8_t buffer[256];
    size_t msgsize;

    {
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

        /* Encode first with TestMessage */
        {
            OneofMessage msg = OneofMessage_init_zero;
            msg.which_msgs = OneofMessage_msg1_tag;

            fill_TestMessage(&msg.msgs.msg1);

            if (!pb_encode(&stream, OneofMessage_fields, &msg))
            {
                fprintf(stderr, "Encode failed: %s\n", PB_GET_ERROR(&stream));
                return false;
            }
        }

        /* Encode second with SubMessage, invoking 'merge' behaviour */
        {
            OneofMessage msg = OneofMessage_init_zero;
            msg.which_msgs = OneofMessage_msg2_tag;

            msg.first = 999;
            msg.msgs.msg2.dynamic_str = "ABCD";
            msg.last = 888;

            if (!pb_encode(&stream, OneofMessage_fields, &msg))
            {
                fprintf(stderr, "Encode failed: %s\n", PB_GET_ERROR(&stream));
                return false;
            }
        }
        msgsize = stream.bytes_written;
    }

    {
        OneofMessage msg = OneofMessage_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(buffer, msgsize);
        if (!pb_decode(&stream, OneofMessage_fields, &msg))
        {
            fprintf(stderr, "Decode failed: %s\n", PB_GET_ERROR(&stream));
            return false;
        }

        TEST(msg.first == 999);
        TEST(msg.which_msgs == OneofMessage_msg2_tag);
        TEST(msg.msgs.msg2.dynamic_str);
        TEST(strcmp(msg.msgs.msg2.dynamic_str, "ABCD") == 0);
        TEST(msg.msgs.msg2.dynamic_str_arr == NULL);
        TEST(msg.msgs.msg2.dynamic_submsg == NULL);
        TEST(msg.last == 888);

        pb_release(OneofMessage_fields, &msg);
        TEST(get_alloc_count() == 0);
        pb_release(OneofMessage_fields, &msg);
        TEST(get_alloc_count() == 0);
    }

    return true;
}

int main()
{
    if (test_TestMessage() && test_OneofMessage())
        return 0;
    else
        return 1;
}

