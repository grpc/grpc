GRPC Python Dockerfile
====================

Dockerfile for creating the Python development instances

As of 2015/02 this
- is based on the GRPC Python base
- adds a pull of the HEAD GRPC Python source from GitHub
- builds it
- runs its tests and aborts image creation if the tests don't pass
- specifies the Python GRPC interop test server as default command
