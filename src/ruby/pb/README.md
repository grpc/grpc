Protocol Buffers
================

This folder contains protocol buffers provided with gRPC ruby, and the generated
code to them.

PREREQUISITES
-------------

The code is is generated using the protoc (> 3.0.0.alpha.1) and the
grpc_ruby_plugin.  These must be installed to regenerate the IDL defined
classes, but that's not necessary just to use them.

health_check/v1alpha
--------------------

This package defines the surface of a simple health check service that gRPC
servers may choose to implement, and provides an implementation for it. To
re-generate the surface.

```bash
$ # (from this directory)
$ protoc -I . grpc/health/v1alpha/health.proto \
    --grpc_out=. \
    --ruby_out=. \
    --plugin=protoc-gen-grpc=`which grpc_ruby_plugin`
```
