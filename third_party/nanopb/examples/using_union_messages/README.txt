Nanopb example "using_union_messages"
=====================================

Union messages is a common technique in Google Protocol Buffers used to
represent a group of messages, only one of which is passed at a time.
It is described in Google's documentation:
https://developers.google.com/protocol-buffers/docs/techniques#union

This directory contains an example on how to encode and decode union messages
with minimal memory usage. Usually, nanopb would allocate space to store
all of the possible messages at the same time, even though at most one of
them will be used at a time.

By using some of the lower level nanopb APIs, we can manually generate the
top level message, so that we only need to allocate the one submessage that
we actually want. Similarly when decoding, we can manually read the tag of
the top level message, and only then allocate the memory for the submessage
after we already know its type.


Example usage
-------------

Type `make` to run the example. It will build it and run commands like
following:

./encode 1 | ./decode
Got MsgType1: 42
./encode 2 | ./decode
Got MsgType2: true
./encode 3 | ./decode
Got MsgType3: 3 1415

This simply demonstrates that the "decode" program has correctly identified
the type of the received message, and managed to decode it.


Details of implementation
-------------------------

unionproto.proto contains the protocol used in the example. It consists of
three messages: MsgType1, MsgType2 and MsgType3, which are collected together
into UnionMessage.

encode.c takes one command line argument, which should be a number 1-3. It
then fills in and encodes the corresponding message, and writes it to stdout.

decode.c reads a UnionMessage from stdin. Then it calls the function
decode_unionmessage_type() to determine the type of the message. After that,
the corresponding message is decoded and the contents of it printed to the
screen.

