Nanopb example "simple"
=======================

This example demonstrates the very basic use of nanopb. It encodes and
decodes a simple message.

The code uses four different API functions:

  * pb_ostream_from_buffer() to declare the output buffer that is to be used
  * pb_encode() to encode a message
  * pb_istream_from_buffer() to declare the input buffer that is to be used
  * pb_decode() to decode a message

Example usage
-------------

On Linux, simply type "make" to build the example. After that, you can
run it with the command: ./simple

On other platforms, you first have to compile the protocol definition using
the following command::

  ../../generator-bin/protoc --nanopb_out=. simple.proto

After that, add the following four files to your project and compile:

  simple.c  simple.pb.c  pb_encode.c  pb_decode.c


