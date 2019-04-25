[![Cocoapods](https://img.shields.io/cocoapods/v/gRPC.svg)](https://cocoapods.org/pods/gRPC)
# gRPC Objective-C with CFStream

gRPC Objective-C library now provides the option to use Apple's CFStream API (rather than TCP
sockets) for networking. Using CFStream resolves a bunch of network connectivity transition issues
(see the [doc](https://github.com/grpc/grpc/blob/master/src/objective-c/NetworkTransitionBehavior.md)
for more information).

CFStream integration is now in experimental state. You will need explicit opt-in to use it to get
the benefits of resolving the issues above. We expect to make CFStream the default networking
interface that gRPC uses when it is ready for production.

## Usage
If you use gRPC following the instructions in
[README.md](https://github.com/grpc/grpc/blob/master/src/objective-c/README.md):
- Replace the
dependency on `gRPC-ProtoRPC` with `gRPC-ProtoRPC/CFStream`.
- Enable CFStream with environment variable `grpc_cfstream=1`. This can be done either in Xcode
  console or by your code with `setenv()` before gRPC is initialized.

If your project directly depends on podspecs other than `gRPC-ProtoRPC` (e.g. `gRPC` or
`gRPC-Core`):

- Make your projects depend on subspecs corresponding to CFStream in each gRPC podspec.
- Enable CFStream with environment variable `grpc_cfstream=1`. This can be done either in Xcode
  console or by your code with `setenv()` before gRPC is initialized.

## Notes

- Currently we do not support platforms other than iOS, although it is likely that this integration
  can run on MacOS targets with Apple's compiler.
- Let us know if you meet any issue by filing issue and ping @muxi.
