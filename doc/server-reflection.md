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

## Method reflection

We want to be able to answer the following queries:
 1. What methods does a server export?
 2. For a particular method, how do we call it?
Specifically, what are the names of the methods, are those methods unary or
streaming, and what are the types of the argument and result?

```
#TODO(dklempner): link to an actual .proto later.
package grpc.reflection.v1alpha;

message ListApisRequest {
}

message ListApisResponse {
  repeated google.protobuf.Api apis = 1;
}

message GetMethodRequest {
  string method = 1;
}
message GetMethodResponse {
  google.protobuf.Method method = 1;
}

service ServerReflection {
  rpc ListApis (ListApisRequest) returns (ListApisResponse);
  rpc GetMethod (GetMethodRequest) returns (GetMethodResponse);
}
```

Note that a server is under no obligation to return a complete list of all
methods it supports. For example, a reverse proxy may support server reflection
for methods implemented directly on the proxy but not enumerate all methods
supported by its backends.


### Open questions on method reflection
 * Consider how to extend this protocol to support non-protobuf methods.

## Argument reflection
The second half of the problem is converting between the human readable
input/output of a debugging tool and the binary format understood by the
method.

This is obviously dependent on protocol type. At one extreme, if both the
server and the debugging tool accept JSON, there may be no need for such a
conversion in the first place. At the opposite extreme, a server using a custom
binary format has no hope of being supported by a generic system. The
intermediate interesting common case is a server which speaks binary-proto and
a debugging client which speaks either ascii-proto or json-proto.

One approach would be to require servers directly support human readable input.
In the future method reflection may be extended to document such support,
should it become widespread or standardized.

## Protobuf descriptors

A second would be for the server to export its
google::protobuf::DescriptorDatabase over the wire. This is very easy to
implement in C++, and Google implementations of a similar protocol already
exist in C++, Go, and Java.

This protocol mostly returns FileDescriptorProtos, which are a proto encoding
of a parsed .proto file. It supports four queries:
 1. The FileDescriptorProto for a given file name
 2. The FileDescriptorProto for the file with a given symbol
 3. The FileDescriptorProto for the file with a given extension
 4. The list of known extension tag numbers of a given type

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

```
package grpc.reflection.v1alpha;
message DescriptorDatabaseRequest {
  string host = 1;
  oneof message_request {
    string files_for_file_name = 3;
    string files_for_symbol_name = 4;
    FileContainingExtensionRequest file_containing_extension = 5;
    string list_all_extensions_of_type = 6;
  }
}

message FileContainingExtensionRequest {
  string base_message = 1;
  int64 extension_id = 2;
}

message DescriptorDatabaseResponse {
  string valid_host = 1;
  DescriptorDatabaseRequest original_request = 2;
  oneof message_response {
    // These are proto2 type google.protobuf.FileDescriptorProto, but
    // we avoid taking a dependency on descriptor.proto, which uses
    // proto2 only features, by making them opaque
    // bytes instead
    repeated bytes fd_proto = 4;
    ListAllExtensionsResponse extensions_response = 5;
    // Notably includes error code 5, NOT FOUND
    int32 error_code = 6;
  }
}

message ListAllExtensionsResponse {
  string base_type_name;
  repeated int64 extension_number;
}

service ProtoDescriptorDatabase {
  rpc DescriptorDatabaseInfo(stream DescriptorDatabaseRequest) returns (stream DescriptorDatabaseResponse);
}
```

Any given request must either result in an error code or an answer, usually in
the form of a  series of FileDescriptorProtos with the requested file itself
and all previously unsent transitive imports of that file. Servers may track
which FileDescriptorProtos have been sent on a given stream, for a given value
of valid_host, and avoid sending them repeatedly for overlapping requests.

| message_request message     | Result                                          |
| files_for_file_name         | transitive closure of file name                 |
| files_for_symbol_name       | transitive closure file containing symbol       |
| file_containing_extension   | transitive closure of file containing a given extension number of a given symbol |
| list_all_extensions_of_type | ListAllExtensionsResponse containing all known extension numbers of a given type |

At some point it would make sense to additionally also support any.proto’s
format. Note that known any.proto messages can be queried by symbol using this
protocol even without any such support, by parsing the url and extracting the
symbol name from it.

## Language specific implementation thoughts
All of the information needed to implement Proto reflection is available to the
code generator, but I’m not certain we actually generate this in every
language. If the proto implementation in the  language doesn’t have something
like google::protobuf::DescriptorPool the grpc implementation for that language
will need to index those FileDescriptorProtos by file and symbol and imports.

One issue is that some grpc implementations are very loosely coupled with
protobufs; in such implementations it probably makes sense to split apart these
reflection APIs so as not to take an additional proto dependency.

## Known Implementations

Enabling server reflection differs language-to-language. Here are links to docs relevant to
each language:

- [Java](https://github.com/grpc/grpc-java/blob/master/documentation/server-reflection-tutorial.md#enable-server-reflection)
- [Go](https://github.com/grpc/grpc-go/blob/master/Documentation/server-reflection-tutorial.md#enable-server-reflection)
- [C++](https://grpc.io/grpc/cpp/md_doc_server_reflection_tutorial.html)
- [C#](https://github.com/grpc/grpc/blob/master/doc/csharp/server_reflection.md)
- [Python](https://github.com/grpc/grpc/blob/master/doc/python/server_reflection.md)
- Ruby: not yet implemented [#2567](https://github.com/grpc/grpc/issues/2567)
- Node: not yet implemented [#2568](https://github.com/grpc/grpc/issues/2568)
