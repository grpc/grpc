#include <stdio.h>
#include <string.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include "unittests.h"
#include "inline.pb.h"

int main()
{
    int status = 0;
    int i = 0;
    COMMENT("Test inline byte fields");

    {
      Message1 msg1 = Message1_init_zero;
      TEST(sizeof(msg1.data) == 32);
    }

    {
      Message1 msg1 = Message1_init_zero;
      pb_byte_t msg1_buffer[Message1_size];
      pb_ostream_t ostream = pb_ostream_from_buffer(msg1_buffer, Message1_size);
      Message1 msg1_deserialized = Message1_init_zero;
      pb_istream_t istream = pb_istream_from_buffer(msg1_buffer, Message1_size);

      for (i = 0; i < 32; i++) {
        msg1.data[i] = i;
      }

      TEST(pb_encode(&ostream, Message1_fields, &msg1));
      TEST(ostream.bytes_written == Message1_size);

      TEST(pb_decode(&istream, Message1_fields, &msg1_deserialized));

      TEST(istream.bytes_left == 0);
      TEST(memcmp(&msg1_deserialized, &msg1, sizeof(msg1)) == 0);
    }

    {
      Message2 msg2 = {true, {0}};
      Message2 msg2_no_data = {false, {1}};
      pb_byte_t msg2_buffer[Message2_size];
      pb_ostream_t ostream = pb_ostream_from_buffer(msg2_buffer, Message2_size);
      Message2 msg2_deserialized = Message2_init_zero;
      pb_istream_t istream = pb_istream_from_buffer(msg2_buffer, Message2_size);

      for (i = 0; i < 64; i++) {
        msg2.data[i] = i;
      }

      TEST(pb_encode(&ostream, Message2_fields, &msg2));
      TEST(ostream.bytes_written == Message2_size);

      TEST(pb_decode(&istream, Message2_fields, &msg2_deserialized));

      TEST(istream.bytes_left == 0);
      TEST(memcmp(&msg2_deserialized, &msg2, sizeof(msg2)) == 0);
      TEST(msg2_deserialized.has_data);

      memset(msg2_buffer, 0, sizeof(msg2_buffer));
      ostream = pb_ostream_from_buffer(msg2_buffer, Message2_size);
      TEST(pb_encode(&ostream, Message2_fields, &msg2_no_data));
      istream = pb_istream_from_buffer(msg2_buffer, Message2_size);
      TEST(pb_decode(&istream, Message2_fields, &msg2_deserialized));
      TEST(!msg2_deserialized.has_data);
      TEST(memcmp(&msg2_deserialized, &msg2, sizeof(msg2)) != 0);
    }

    if (status != 0)
        fprintf(stdout, "\n\nSome tests FAILED!\n");

    return status;
}
