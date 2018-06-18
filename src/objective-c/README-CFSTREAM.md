[![Cocoapods](https://img.shields.io/cocoapods/v/gRPC.svg)](https://cocoapods.org/pods/gRPC)
# gRPC Objective-C with CFStream

gRPC Objective-C library now provides the option to use Apple's CFStream API (rather than TCP
sockets) for networking. Using CFStream resolves a bunch of network connectivity transition issues
(see the [doc](https://github.com/grpc/grpc/blob/master/src/objective-c/NetworkBehavior.md) for more
information).

CFStream integration is now in experimental state. You will need explicit opt-in to use it to get
the benefits of resolving the issues above. We expect to make CFStream the default networking
interface that gRPC uses when it is ready for production.

## Usage
If you use gRPC following the instructions in
[README.md](https://github.com/grpc/grpc/blob/master/src/objective-c/README.md):
- Simply replace the
dependency on `gRPC-ProtoRPC` with `gRPC-ProtoRPC/CFStream`. The build system will take care of
everything else and switch networking to CFStream.

If your project directly depends on podspecs other than `gRPC-ProtoRPC` (e.g. `gRPC` or
`gRPC-Core`):

- Make your projects depend on subspecs corresponding to CFStream in each gRPC podspec. For
  `gRPC-Core`, you will need to make sure that the completion queue you create is of type
  `GRPC_CQ_NON_POLLING`. This is expected to be fixed soon so that you do not have to modify the
  completion queue type.

## Notes

- Currently we do not support platforms other than iOS, although it is likely that this integration
  can run on MacOS targets with Apple's compiler.
- Let us know if you meet any issue by filing issue and ping @muxi.
