[![Cocoapods](https://img.shields.io/cocoapods/v/gRPC.svg)](https://cocoapods.org/pods/gRPC)
# gRPC Objective-C with CFStream

gRPC now provides the option to use Apple's CFStream API (rather than TCP
sockets) for networking. Using CFStream resolves a bunch of network connectivity transition issues
(see the [doc](https://github.com/grpc/grpc/blob/master/src/objective-c/NetworkTransitionBehavior.md)
for more information).

<s>CFStream integration is now in experimental state. You will need explicit opt-in to use it to get
the benefits of resolving the issues above. We expect to make CFStream the default networking
interface that gRPC uses when it is ready for production.</s>

As of v1.21.0, CFStream integration is now the default networking stack being used by gRPC
Objective-C on iOS layer. You get to use it automatically without special configuration needed. See
below on how to disable CFStream in case of problem.

As of v1.23.0, CFStream is enabled by default on iOS for all wrapped languages. See below on how to
disable CFStream in case of a problem.

## Usage
If you use gRPC on iOS, CFStream is on automatically. If you use it on other
platforms, you can turn it on with macro `GRPC_CFSTREAM=1` for the pod 'gRPC-Core' and 'gRPC'. In
case of problem and you want to disable CFStream on iOS, you can set environment variable
"grpc\_cfstream=0".

## Notes

- Currently we do not support platforms other than iOS, although it is likely that this integration
  can run on MacOS targets with Apple's compiler.
- Let us know if you meet any issue by filing issue and ping @muxi.
