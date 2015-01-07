GRPC PHP Base Dockerfile
========================

Dockerfile for creating the PHP gRPC development Docker instance.

As of 2014/10 this
- it installs tools and dependencies needed to build gRPC PHP
- it does not install gRPC PHP itself; a separate Dockerfile that depends on
  this one will do that
