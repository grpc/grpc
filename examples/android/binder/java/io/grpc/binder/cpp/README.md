# gRPC-core BinderTransport example apps

WIP.

## Build Instruction

1. Install Android SDK and NDK. Currently we only support SDK version 30.0.3 and
   NDK version 21.4.7075529 . Make sure you get these exact versions otherwise
   Bazel might complain.

2. Point environment variables to install locations of SDK and NDK
    ```
    export ANDROID_HOME=$HOME/Android/Sdk/
    export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/21.4.7075529
    ```
3. `bazel build //examples/android/binder/java/io/grpc/binder/cpp/exampleclient:app`
4. `bazel build //examples/android/binder/java/io/grpc/binder/cpp/exampleserver:app`
5. `adb install
   bazel-bin/examples/android/binder/java/io/grpc/binder/cpp/exampleclient/app.apk`
6. `adb install
   bazel-bin/examples/android/binder/java/io/grpc/binder/cpp/exampleserver/app.apk`
