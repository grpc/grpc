# gRPC-core BinderTransport example apps

## Build Instruction

1. Install Android SDK and NDK. Only NDK version >= 25 is supported. We tested against SDK Platform `33` and NDK `26.2.11394342`.
2. Make sure Bazel is at least `7.0`. Use `export OVERRIDE_BAZEL_VERSION=7.1.0` to selected a supported version listed in `bazel/supported_versions.txt` if necessary.
3. Point environment variables to install locations of SDK and NDK
    ```
    export ANDROID_HOME=$HOME/android-sdk
    export ANDROID_NDK_HOME=$HOME/android-sdk/ndk/26.2.11394342
    ```
4. To build a fat APK that supports `x86_64`, `armv7`, and `arm64`:
    ```
    bazel build \
      --extra_toolchains=@androidndk//:all \
      --android_platforms=//:android_x86_64,//:android_armv7,//:android_arm64 \
      --copt=-Wno-unknown-warning-option \
      //examples/android/binder/java/io/grpc/binder/cpp/exampleserver:app \
      //examples/android/binder/java/io/grpc/binder/cpp/exampleclient:app
    ```
5. `adb install
   bazel-bin/examples/android/binder/java/io/grpc/binder/cpp/exampleclient/app.apk`
6. `adb install
   bazel-bin/examples/android/binder/java/io/grpc/binder/cpp/exampleserver/app.apk`
