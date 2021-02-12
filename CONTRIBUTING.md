# How to contribute

We definitely welcome feedback and contribution to Google APIs! Here
is some guideline and information about how to do so.

## Legal requirements

In order to protect both you and ourselves, you will need to sign the
[Contributor License Agreement](https://cla.developers.google.com/clas).

## Technical requirements

You will need several tools to work with this repository. At minimum,
you need both [Protocol Buffers](https://github.com/google/protobuf)
and [gRPC](https://github.com/grpc) in order to compile this
repository and generate client library source code in various
programming languages.

To compile the generated code into usable client libraries, you will
need to use appropriate development environments and setup proper
build configurations.

## Additional note

Currently, the root's Makefile only lets you generate source code for
the client library in the programming languages supported by
[gRPC](https://github.com/grpc). It does not generate the ready-to-use
client libraries yet.
