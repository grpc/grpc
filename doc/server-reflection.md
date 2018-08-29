gRPC Server Reflection Protocol
===============================

This document describes server reflection as an optional extension for servers
to assist clients in runtime construction of requests without having stub
information precompiled into the client.

The primary usecase for server reflection is to write (typically) command line
debugging tools for talking to a gRPC server. In particular, such a tool will
take in a method and a payload (in human readable text format) send it to the
server (typically in binary proto wire format), and then take the response and
decode it to text to present to the user.

This broadly involves two problems: determining what formats (which protobuf
messages) a server’s method uses, and determining how to convert messages
between human readable format and the (likely binary) wire format.


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


## Protobuf descriptors

The server reflection service exports its server's
google::protobuf::DescriptorDatabase over the wire. This is very easy to
implement in C++, and Google implementations of a similar protocol already
exist in C++, Go, and Java.

This protocol mostly returns FileDescriptorProtos, which are a proto encoding
of a parsed .proto file. It supports five queries:
 1. The FileDescriptorProto for a given file name
 2. The FileDescriptorProto for the file with a given symbol
 3. The FileDescriptorProto for the file with a given extension
 4. The list of known extension tag numbers of a given type
 5. The full names of all registered services

These directly correspond to the methods of
google::protobuf::DescriptorDatabase. Note that this protocol includes support
for extensions, which have been removed from proto3 but are still in widespread
use in Google’s codebase.

Because most usecases will require also requesting the transitive dependencies
of requested files, the queries will also return all transitive dependencies of
the returned file. Should interesting usecases for non-transitive queries turn
up later, we can easily extend the protocol to support them.

### Reverse proxy traversal

One potential issue with naive reverse proxies is that, while any individual
server will have a consistent and valid picture of the proto DB which is
sufficient to handle incoming requests, incompatibilities will arise if the
backend servers have a mix of builds. For example, if a given message is moved
from foo.proto to bar.proto, and the client requests foo.proto from an old
server and bar.proto from a new server, the resulting database will have a
double definition.

To solve this problem, the protocol is structured as a bidirectional stream,
ensuring all related requests go to a single server. This has the additional
benefit that overlapping recursive requests don’t require sending a lot of
redundant information, because there is a single stream to maintain context
between queries.


## Reflection Service Definition

Proto definition: [reflection.proto](https://github.com/grpc/grpc/blob/master/src/proto/grpc/reflection/v1alpha/reflection.proto)

Any given request must either result in an error code or an answer, usually in
the form of a series of FileDescriptorProtos with the requested file itself
and all previously unsent transitive imports of that file. Servers may track
which FileDescriptorProtos have been sent on a given stream, for a given value
of valid_host, and avoid sending them repeatedly for overlapping requests.


| message_request message     | Result                                          |
| --- | --- |
| file_by_filename         | transitive closure of file name                 |
| file_containing_symbol       | transitive closure file containing symbol       |
| file_containing_extension   | transitive closure of file containing a given extension number of a given symbol |
| all_extension_numbers_of_type | ExtensionNumberResponse containing all known extension numbers of a given type |
| list_services  | ListServiceResponse containing the full names of all registered services |

At some point it would make sense to additionally also support any.proto’s
format. Note that known any.proto messages can be queried by symbol using this
protocol even without any such support, by parsing the url and extracting the
symbol name from it.

## Language specific implementation thoughts
All of the information needed to implement Proto reflection is available to the
code generator, but it's not certain that we actually generate this in every
language. If the proto implementation in the  language doesn’t have something
like google::protobuf::DescriptorPool the gRPC implementation for that language
will need to index those FileDescriptorProtos by file and symbol and imports.

One issue is that some gRPC implementations are very loosely coupled with
protobufs; in such implementations it probably makes sense to split apart these
reflection APIs so as not to take an additional proto dependency.
