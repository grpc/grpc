GRPC RUBY Base Dockerfile
========================

Dockerfile for creating the Ruby gRPC development Docker instance.

As of 2014/10 this
- it installs tools and dependencies needed to build gRPC Ruby
- it does not install gRPC Ruby itself; a separate Dockerfile that depends on
  this one will do that
