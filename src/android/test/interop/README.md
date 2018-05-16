gRPC on Android
==============

Note: Building the protobuf dependency for Android requires
https://github.com/google/protobuf/pull/3878. This fix will be in the next
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

INSTRUMENTATION TESTS
---------------------

The instrumentation tests can be run via the following `gradle` command. This
requires an emulator already running on your computer.

```
$ ./gradlew connectedAndroidTest \
  -Pandroid.testInstrumentationRunnerArguments.server_host=grpc-test.sandbox.googleapis.com \
  -Pandroid.testInstrumentationRunnerArguments.server_port=443 \
  -Pandroid.testInstrumentationRunnerArguments.use_tls=true
```
