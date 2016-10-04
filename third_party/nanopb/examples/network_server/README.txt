Nanopb example "network_server"
===============================

This example demonstrates the use of nanopb to communicate over network
connections. It consists of a server that sends file listings, and of
a client that requests the file list from the server.

Example usage
-------------

user@host:~/nanopb/examples/network_server$ make        # Build the example
protoc -ofileproto.pb fileproto.proto
python ../../generator/nanopb_generator.py fileproto.pb
Writing to fileproto.pb.h and fileproto.pb.c
cc -ansi -Wall -Werror -I .. -g -O0 -I../.. -o server server.c
    ../../pb_decode.c ../../pb_encode.c fileproto.pb.c common.c
cc -ansi -Wall -Werror -I .. -g -O0 -I../.. -o client client.c
    ../../pb_decode.c ../../pb_encode.c fileproto.pb.c common.c

user@host:~/nanopb/examples/network_server$ ./server &  # Start the server on background
[1] 24462

petteri@oddish:~/nanopb/examples/network_server$ ./client /bin  # Request the server to list /bin
Got connection.
Listing directory: /bin
1327119    bzdiff
1327126    bzless
1327147    ps
1327178    ntfsmove
1327271    mv
1327187    mount
1327259    false
1327266    tempfile
1327285    zfgrep
1327165    gzexe
1327204    nc.openbsd
1327260    uname


Details of implementation
-------------------------
fileproto.proto contains the portable Google Protocol Buffers protocol definition.
It could be used as-is to implement a server or a client in any other language, for
example Python or Java.

fileproto.options contains the nanopb-specific options for the protocol file. This
sets the amount of space allocated for file names when decoding messages.

common.c/h contains functions that allow nanopb to read and write directly from
network socket. This way there is no need to allocate a separate buffer to store
the message.

server.c contains the code to open a listening socket, to respond to clients and
to list directory contents.

client.c contains the code to connect to a server, to send a request and to print
the response message.

The code is implemented using the POSIX socket api, but it should be easy enough
to port into any other socket api, such as lwip.
