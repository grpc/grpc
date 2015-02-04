GRPC Ruby Dockerfile
====================

Dockerfile for creating the Ruby development instances

As of 2014/10 this
- is based on the GRPC Ruby base
- adds a pull of the HEAD gRPC Ruby source from GitHub
- it builds it
- runs the tests, i.e, the image won't be created if the tests don't pass
