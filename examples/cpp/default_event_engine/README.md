# gRPC C++ Hello World Example with a custom EventEngine

You can find a complete set of instructions for building gRPC and running the
Hello World app in the [C++ Quick Start][].

This example illustrates how to provide gRPC with a custom [EventEngine][].
By providing gRPC with an application-owned EventEngine, applications can customize most aspects how gRPC performs I/O, asynchronous callback execution, timer execution, and DNS resolution.

[C++ Quick Start]: https://grpc.io/docs/languages/cpp/quickstart
[EventEngine]: https://github.com/grpc/grpc/blob/master/include/grpc/event_engine/event_engine.h