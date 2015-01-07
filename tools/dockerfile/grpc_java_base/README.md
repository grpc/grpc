GRPC Java Base Dockerfile
=========================

Dockerfile for creating the Java gRPC development Docker instance.

As of 2014/12 this
 - installs tools and dependencies needed to build gRPC Java
 - does not install gRPC Java itself; a separate Dockerfile that depends on
   this one will do that.