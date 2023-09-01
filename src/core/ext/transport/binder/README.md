# Binder transport for cross process IPC on Android

EXPERIMENTAL. API stability not guaranteed.

This transport implements
[BinderChannel for native cross-process communication on Android](https://github.com/grpc/proposal/blob/master/L73-java-binderchannel.md) and enables C++/Java cross-process communication on Android with gRPC.

Tests: https://github.com/grpc/grpc/tree/master/test/core/transport/binder/

Example apps: https://github.com/grpc/grpc/tree/master/examples/android/binder/java/io/grpc/binder/cpp
