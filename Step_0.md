# Step-0: define a service

This section presents an example of a simple service definition that receives
a message from a remote client. The message contains the user's name and
sends back a greeting to that person.

It's shown below in full; it's actually contained in separate file.
[helloworld.proto](src/main/proto/helloworld.proto).

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

The service stanza of the message is an example of protobuf service IDL
(Interface Defintion Language). Here, it defines a simple service that
receives a request containing a name and returns a response containing a
message.

Next, in [Step - 1](Step_1.md), we'll use protoc to generate client code from
this IDL.
