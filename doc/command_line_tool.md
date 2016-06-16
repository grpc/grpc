# gRPC command line tool

## Overview

This document describes the command line tool that comes with gRPC repository. It is desireable to have command line
tools written in other languages to roughly follow the same syntax and flags.

At this point, the tool needs to be built from source, and it should be moved out to grpc-tools repository as a stand
alone application once it is mature enough.

## Core functionality

The command line tool can do the following things:

- Send unary rpc.
- Attach metadata and display received metadata.
- Handle common authentication to server.
- Find the request/response types from a given proto file.
- Read proto request in text form.
- Read request in wire form (for protobuf messages, this means serialized binary form).
- Display proto response in text form.
- Write response in wire form to a file.

The command line tool should support the following things:

- List server services and methods through server reflection.
- Infer request/response types from server reflection result.
- Fine-grained auth control (such as, use this oauth token to talk to the server).
- Send streaming rpc.

## Code location

To use the tool, you need to get the grpc repository and in the grpc directory execute

```
$ make grpc_cli
```

The main file can be found at
https://github.com/grpc/grpc/blob/master/test/cpp/util/grpc_cli.cc

## Usage

### Basic usage

Send a rpc to a helloworld server at `localhost:50051`:

```
$ bins/opt/grpc_cli call localhost:50051 SayHello examples/protos/helloworld.proto \
    "name: 'world'"  --enable_ssl=false
```

On success, the tool will print out

```
Rpc succeeded with OK status
Response: 
 message: "Hello world"
```

The `localhost:50051` part indicates the server you are connecting to. `SayHello` is (part of) the
gRPC method string. Then there is the path to the proto file containing the service definition,
if it is not under current directory, you can use `--proto_path` to specify a new search root.
`"name: 'world'"` is the text format of the request proto message. 
We are not using ssl here by `--enable_ssl=false`. For information on more
flags, look at the comments of `grpc_cli.cc`.

### Send non-proto rpc

For using gRPC with protocols other than probobuf, you will need the exact method name string
and a file containing the raw bytes to be sent on the wire

```
$ bins/opt/grpc_cli call localhost:50051 /helloworld.Greeter/SayHello --input_binary_file=input.bin \
    --output_binary_file=output.bin
```
On success, you will need to read or decode the response from the `output.bin` file.
