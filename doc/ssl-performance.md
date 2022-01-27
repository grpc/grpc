# SSL in gRPC and performance

The SSL requirement of gRPC isn't necessarily making it easy to integrate. The HTTP/2 protocol requires ALPN support, which is a fairly new handshake protocol only supported by recent implementations.

As a result, we've tried hard to provide a smooth experience to our users when compiling and distributing gRPC, but this may come at performance costs due to this. More specifically, we will sometime build the SSL library by disabling assembly code
(by setting the `OPENSSL_NO_ASM` option), which can impact performance by an order of magnitude when processing encrypted streams.

## gRPC C++: Building from Source

Build system | Condition | Platform | Uses assembly optimizations
---|---|---|--
Makefile | with OpenSSL 1.0.2 development files | all | :heavy_check_mark:
Makefile | all other cases | all | :x:
Bazel | | Linux | :heavy_check_mark:
Bazel | | MacOS | :heavy_check_mark:
Bazel | | Windows | :x:
CMake | boringssl from submodule (default) | Linux or MacOS | :heavy_check_mark:
CMake | boringssl from submodule (default), generator=Ninja | Windows | :heavy_check_mark:
CMake | boringssl from submodule (default), generator=Visual Studio | Windows | :x:
CMake | pre-installed OpenSSL 1.0.2+ (`gRPC_SSL_PROVIDER=package`) | all | :heavy_check_mark:

## Other Languages: Binary/Source Packages

In addition, we are shipping packages for language implementations. These packages are source packages, but also have pre-built binaries being distributed. Building packages from source may give a different result in some cases.

Language | From source | Platform | Uses assembly optimizations
---|---|---|---
C#      | n/a | Linux, 64bit | :heavy_check_mark:
C#      | n/a | Linux, 32bit | :x:
C#      | n/a | MacOS | :heavy_check_mark:
C#      | n/a | Windows | :heavy_check_mark:
Node.JS | n/a | Linux | :heavy_check_mark:
Node.JS | n/a | MacOS | :heavy_check_mark:
Node.JS | n/a | Windows | :x:
Electron | n/a | all | :heavy_check_mark:
ObjC | Yes | iOS | :x:
PHP | Yes | all | Same as the `Makefile` case from above
PHP | No | all | :x:
Python | n/a | Linux, 64bit | :heavy_check_mark:
Python | n/a | Linux, 32bit | :x:
Python | n/a | MacOS, 64bit | :heavy_check_mark:
Python | n/a | MacOS, 32bit | :x:
Python | n/a | Windows | :x:
Ruby | No | all | :x:
