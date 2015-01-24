# Step-0: define a service

This section presents an example of a very simple service definition that
receives a message from a remote client. The messages contains the users's
name and sends's back a greeting for that person.

Here it is in full; to be used to generate gRPC code it's defined in it's own
file [helloworld.proto](helloworld.proto).

```
syntax = "proto3";

package helloworld;

// The request message containing the user's name.
message HelloRequest {
  optional string name = 1;
}

// The response message containing the greetings
message HelloReply {
  optional string message = 1;
}

// The greeting service definition.
service Greeting {

  // Sends a greeting
  rpc hello (HelloRequest) returns (HelloReply) {
  }
}

```

The service stanza of the messages is an example of protobuf service IDL
(Interface Defintion Language).  Here, it defines a very simple service that
receives a request and returns a response.

Next in [Step-1](Step-1.md), we'll use protoc to generate code this simple
definition.
