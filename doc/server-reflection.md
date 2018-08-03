GRPC Server Reflection Protocol
===============================

This document describes server reflection as an optional extension for servers
to assist clients in runtime construction of requests without having stub
information precompiled into the client.

The primary usecase for server reflection is to write (typically) command line
debugging tools for talking to a grpc server. In particular, such a tool will
take in a method and a payload (in human readable text format) send it to the
server (typically in binary proto wire format), and then take the response and
decode it to text to present to the user.

This broadly involves two problems: determining what formats (which protobuf
messages) a server’s method uses, and determining how to convert messages
between human readable format and the (likely binary) wire format.

## Reflection Service Definition

[reflection.proto](https://github.com/grpc/grpc/blob/master/src/proto/grpc/reflection/v1alpha/reflection.proto)

## Known Implementations

Enabling server reflection differs language-to-language. Here are links to docs relevant to
each language:

- [Java](https://github.com/grpc/grpc-java/blob/master/documentation/server-reflection-tutorial.md#enable-server-reflection)
- [Go](https://github.com/grpc/grpc-go/blob/master/Documentation/server-reflection-tutorial.md#enable-server-reflection)
- [C++](https://grpc.io/grpc/cpp/md_doc_server_reflection_tutorial.html)
- [C#](https://github.com/grpc/grpc/blob/master/doc/csharp/server_reflection.md)
- Python: (tutorial not yet written)
- Ruby: not yet implemented [#2567](https://github.com/grpc/grpc/issues/2567)
- Node: not yet implemented [#2568](https://github.com/grpc/grpc/issues/2568)
