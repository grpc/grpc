======================
Nanopb: Security model
======================

.. include :: menu.rst

.. contents ::



Importance of security in a Protocol Buffers library
====================================================
In the context of protocol buffers, security comes into play when decoding
untrusted data. Naturally, if the attacker can modify the contents of a
protocol buffers message, he can feed the application any values possible.
Therefore the application itself must be prepared to receive untrusted values.

Where nanopb plays a part is preventing the attacker from running arbitrary
code on the target system. Mostly this means that there must not be any
possibility to cause buffer overruns, memory corruption or invalid pointers
by the means of crafting a malicious message.

Division of trusted and untrusted data
======================================
The following data is regarded as **trusted**. It must be under the control of
the application writer. Malicious data in these structures could cause
security issues, such as execution of arbitrary code:

1. Callback, pointer and extension fields in message structures given to
   pb_encode() and pb_decode(). These fields are memory pointers, and are
   generated depending on the message definition in the .proto file.
2. The automatically generated field definitions, i.e. *pb_field_t* lists.
3. Contents of the *pb_istream_t* and *pb_ostream_t* structures (this does not
   mean the contents of the stream itself, just the stream definition).

The following data is regarded as **untrusted**. Invalid/malicious data in
these will cause "garbage in, garbage out" behaviour. It will not cause
buffer overflows, information disclosure or other security problems:

1. All data read from *pb_istream_t*.
2. All fields in message structures, except:
   
   - callbacks (*pb_callback_t* structures)
   - pointer fields (malloc support) and *_count* fields for pointers
   - extensions (*pb_extension_t* structures)

Invariants
==========
The following invariants are maintained during operation, even if the
untrusted data has been maliciously crafted:

1. Nanopb will never read more than *bytes_left* bytes from *pb_istream_t*.
2. Nanopb will never write more than *max_size* bytes to *pb_ostream_t*.
3. Nanopb will never access memory out of bounds of the message structure.
4. After pb_decode() returns successfully, the message structure will be
   internally consistent:

   - The *count* fields of arrays will not exceed the array size.
   - The *size* field of bytes will not exceed the allocated size.
   - All string fields will have null terminator.

5. After pb_encode() returns successfully, the resulting message is a valid
   protocol buffers message. (Except if user-defined callbacks write incorrect
   data.)

Further considerations
======================
Even if the nanopb library is free of any security issues, there are still
several possible attack vectors that the application author must consider.
The following list is not comprehensive:

1. Stack usage may depend on the contents of the message. The message
   definition places an upper bound on how much stack will be used. Tests
   should be run with all fields present, to record the maximum possible
   stack usage.
2. Callbacks can do anything. The code for the callbacks must be carefully
   checked if they are used with untrusted data.
3. If using stream input, a maximum size should be set in *pb_istream_t* to
   stop a denial of service attack from using an infinite message.
4. If using network sockets as streams, a timeout should be set to stop
   denial of service attacks.
5. If using *malloc()* support, some method of limiting memory use should be
   employed. This can be done by defining custom *pb_realloc()* function.
   Nanopb will properly detect and handle failed memory allocations.
