Base GRPC Dockerfile
====================

Dockerfile for creating the base gRPC development Docker instance.
For now, this assumes that the development will be done on GCE instances, with source code on Git-on-Borg.

As of 2014/09/29, it includes
- git
- some useful tools like curl, emacs, strace, telnet etc
- downloads the gerrit-compute-tools and installs the script that allows access to gerrit when on git-on-borg
- a patched version of protoc, to allow protos with stream tags to work
