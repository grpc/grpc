# Abseil in gRPC

This document explains how to use Abseil throughout gRPC. Note that this isn't
supposed to explain general usage of Abseil.

## The version of Abseil

gRPC intends to use the LTS versions of Abseil only because it simplifies
dependency management. Abseil is being distributed via package distribution
systems such as vcpkg and cocoapods. If gRPC depends on the certain version
that aren't registered, gRPC in that system cannot get the right version of
Abseil when being built, resulting in a build failure.
Therefore, gRPC will use the LTS version only, preferably the latest one.

## Libraries that are not ready to use

Most of Abseil libraries are okay to use but there are some exceptions
because they're not going well yet on some of our test machinaries or
platforms it supports. The following is a list of targets that are NOT
ready to use.

- `absl/synchronization:*`: Blocked by b/186685878.
- `absl/random`: [WIP](https://github.com/grpc/grpc/pull/23346).

## Implemetation only

You can use Abseil in gRPC Core and gRPC C++. But you cannot use it in
the public interface of gRPC C++ because i) it doesn't gurantee no breaking
API changes like gRPC C++ does and ii) it may make users change their build
system to address Abseil.  
 