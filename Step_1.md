# Step-1: Generate a service client.

In this step, we use protoc to generate the Java Stub classes.  A Stub is the
name gRPC uses for the code that initiates contact with a gRPC service running
remotely via the internet.

If you did not install protoc on your system, you can skip this step and move
onto the next one where we examine the generated code.

First, you'll need to build the protobuf plugin that generates the rpc
classes.  `protoc` uses other tools called plugins to add additional features
to generated code.

The gRPC Java Stub classes are created using a gRPC Java plugin, but first the
plugin must be built and installed.

To build the plugin:
```
$ pushd external/grpc_java
$ make java_plugin
$ popd
```

To use it to generate the code:
```
$ mkdir -p src/main/java
$ protoc -I . helloworld.proto --plugin=protoc-gen-grpc=external/grpc_java/bins/opt/java_plugin \
                               --grpc_out=src/main/java \
                               --java_out=src/main/java
```

Next, in [Step - 2](Step_2.md), we'll use the generated Stub implementation to
write a client that uses the generated code to make a call to a service.
