# SSL in gRPC and performance

The SSL requirement of gRPC isn't necessarily making it easy to integrate. The HTTP/2 protocol requires ALPN support, which is a fairly new handshake protocol only supported by recent implementations.

As a result, we've tried hard to provide a smooth experience to our users when compiling and distributing gRPC, but this may come at performance costs due to this. More specifically, we will sometime build the SSL library by disabling assembly code, which can impact performances by an order of magnitude when processing encrypted streams.

Build system | Condition | Platform | Uses assembly code
---|---|---|--
Makefile | with OpenSSL 1.0.2 development files | all | :heavy_check_mark:
Makefile | all other cases | all | :x:
Bazel | | Linux | :heavy_check_mark:
Bazel | | MacOS | :heavy_check_mark:
Bazel | | Windows | :x:
CMake | | Windows | :x:
CMake | | all others | :heavy_check_mark:


In addition, we are shipping packages for language implementations. These packages are source packages, but also have pre-built binaries being distributed. Building packages from source may give a different result in some cases.

Language | From source | Platform | Uses assembly code
---|---|---|---
Node.JS | n/a | Linux | :heavy_check_mark:
Node.JS | n/a | MacOS | :heavy_check_mark:
Node.JS | n/a | Windows | :x:
Electron | n/a | all | :heavy_check_mark:
Ruby | No | all | :x:
PHP | Yes | all | Same as the `Makefile` case from above
PHP | No | all | :x:
Python | n/a | all | :x:
