Base GRPC Dockerfile
====================

Dockerfile for creating the base gRPC development Docker instance.
For now, this assumes that the development will be done on GCE instances,
with source code on GitHub.

As of 2015/02/02, it includes
- git
- some useful tools like curl, emacs, strace, telnet etc
- a patched version of protoc, to allow protos with stream tags to work
