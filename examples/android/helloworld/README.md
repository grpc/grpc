gRPC on Android
==============

Note: Building the protobuf dependency for Android requires
https://github.com/protocolbuffers/protobuf/pull/3878. This fix will be in the next
protobuf release, but until then must be manually patched in to
`third_party/protobuf` to build gRPC for Android.

PREREQUISITES
-------------

- Android SDK
- Android NDK
- `protoc` and `grpc_cpp_plugin` binaries on the host system

INSTALL
-------

The example application can be built via Android Studio or on the command line
using `gradle`:

  ```sh
  $ ./gradlew installDebug
  ```
