# Metadata Example

## Overview

This example shows you how to add custom headers on the client and server and 
how to access them.

Custom metadata must follow the "Custom-Metadata" format listed in 
https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md, with the 
exception of binary headers, which don't have to be base64 encoded.

### Get the tutorial source code
 The example code for this and our other examples lives in the `examples` directory. Clone this repository to your local machine by running the following command:
 ```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
```
 Change your current directory to examples/cpp/metadata
 ```sh
$ cd examples/cpp/metadata
```

### Generating gRPC code
 To generate the client and server side interfaces:
 ```sh
$ make helloworld.grpc.pb.cc helloworld.pb.cc
```
Which internally invokes the proto-compiler as:
 ```sh
$ protoc -I ../../protos/ --grpc_out=. --plugin=protoc-gen-grpc=grpc_cpp_plugin ../../protos/helloworld.proto
$ protoc -I ../../protos/ --cpp_out=. ../../protos/helloworld.proto
```
### Try it!
Build client and server:

```sh
$ make
```

Run the server, which will listen on port 50051:

```sh
$ ./greeter_server
```

Run the client (in a different terminal):

```sh
$ ./greeter_client
```

If things go smoothly, you will see in the client terminal:

"Client received initial metadata from server: initial metadata value"
"Client received trailing metadata from server: trailing metadata value"
"Client received message: Hello World"


And in the server terminal:

"Header key: custom-bin , value: 01234567"
"Header key: custom-header , value: Custom Value"
"Header key: user-agent , value: grpc-c++/1.16.0-dev grpc-c/6.0.0-dev (linux; chttp2; gao)"

We did not add the user-agent metadata as a custom header. This shows how 
the gRPC framework adds some headers under the hood that may show up in the 
metadata map.
