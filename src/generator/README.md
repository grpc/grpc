#gRPC Client Generation

## Overview

gRPC already has plugins that work with `protoc` to generate the library code 
for clients and servers. This directory contains plugins that take that code
generation one step further. Given a proto file, the cpp_client_generator will
produce a c++ client file that *just works*.

The generated client should compile and run "out of the box." The requests will
be programatically populated with random sentinel data. This could be used for
fuzz testing, but should also serve as a good entry point to creating more
focused tests. Use the generated code as a skeleton, then change the data to
make the test useful.

## How to use

At this point the generator code only makes clients. To see an example of the
generator working, we will use the cpp helloworld example that can be found
under examples/cpp/helloworld. Starting from the top of the gRPC repo:

```
# build the plugin
cd src/generator
make

# generate the other code needed by this helloworld example
cd ../../examples/cpp/helloworld
make

# generate the cpp client
protoc -I ../../protos                                                     \
    --grpc_out=.                                                           \
    --plugin=protoc-gen-grpc=../../../src/generator/grpc_cpp_client_plugin \
    ../../protos/helloworld.proto

# build the cpp client
g++ -I/usr/local/include              \
    -pthread -std=c++11               \
    -c -o helloworld.grpc.client.pb.o \
    helloworld.grpc.client.pb.cc

# link the cpp client
g++ helloworld.pb.o                  \
    helloworld.grpc.pb.o             \
    helloworld.grpc.client.pb.o      \
    -L/usr/local/lib                 \
    `pkg-config --libs grpc++ grpc`  \
    -lgrpc++_reflection -lprotobuf   \
    -lpthread -ldl -lgflags          \
    -o generated_helloworld_client

# run the handwritten server in the background
./greeter_server &

# run the generated client
./generated_helloworld_client --server_host localhost --server_port 50051
```

Examine the contents of `helloworld.grpc.client.pb.cc`; it should be easy to see
whats going on.

## Future plans

* More features
  - calls with metadata
  - async gRPC

* All supported client languages
* TLS support for connecting to already running services
