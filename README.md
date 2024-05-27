# gRPC – An RPC library and framework

gRPC is a modern, open source, high-performance remote procedure call (RPC)
framework that can run anywhere. gRPC enables client and server applications to
communicate transparently, and simplifies the building of connected systems.

<table>
  <tr>
    <td><b>Homepage:</b></td>
    <td><a href="https://grpc.io/">grpc.io</a></td>
  </tr>
  <tr>
    <td><b>Mailing List:</b></td>
    <td><a href="https://groups.google.com/forum/#!forum/grpc-io">grpc-io@googlegroups.com</a></td>
  </tr>
</table>

[![Join the chat at https://gitter.im/grpc/grpc](https://badges.gitter.im/grpc/grpc.svg)](https://gitter.im/grpc/grpc?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

## To start using gRPC

To maximize usability, gRPC supports the standard method for adding dependencies
to a user's chosen language (if there is one). In most languages, the gRPC
runtime comes as a package available in a user's language package manager.

For instructions on how to use the language-specific gRPC runtime for a project,
please refer to these documents

-   [C++](src/cpp): follow the instructions under the `src/cpp` directory
-   [C#/.NET](https://github.com/grpc/grpc-dotnet): NuGet packages
    `Grpc.Net.Client`, `Grpc.AspNetCore.Server`
-   [Dart](https://github.com/grpc/grpc-dart): pub package `grpc`
-   [Go](https://github.com/grpc/grpc-go): `go get google.golang.org/grpc`
-   [Java](https://github.com/grpc/grpc-java): Use JARs from Maven Central
    Repository
-   [Kotlin](https://github.com/grpc/grpc-kotlin): Use JARs from Maven Central
    Repository
-   [Node](https://github.com/grpc/grpc-node): `npm install @grpc/grpc-js`
-   [Objective-C](src/objective-c): Add `gRPC-ProtoRPC` dependency to podspec
-   [PHP](src/php): `pecl install grpc`
-   [Python](src/python/grpcio): `pip install grpcio`
-   [Ruby](src/ruby): `gem install grpc`
-   [WebJS](https://github.com/grpc/grpc-web): follow the grpc-web instructions

Per-language quickstart guides and tutorials can be found in the
[documentation section on the grpc.io website](https://grpc.io/docs/). Code
examples are available in the [examples](examples) directory.

Precompiled bleeding-edge package builds of gRPC `master` branch's `HEAD` are
uploaded daily to [packages.grpc.io](https://packages.grpc.io).

## To start developing gRPC

Contributions are welcome!

Please read [How to contribute](CONTRIBUTING.md) which will guide you through
the entire workflow of how to build the source code, how to run the tests, and
how to contribute changes to the gRPC codebase. The "How to contribute" document
also contains info on how the contribution process works and contains best
practices for creating contributions.

## Troubleshooting

Sometimes things go wrong. Please check out the
[Troubleshooting guide](TROUBLESHOOTING.md) if you are experiencing issues with
gRPC.

## Performance

See the [Performance dashboard](https://grafana-dot-grpc-testing.appspot.com/)
for performance numbers of master branch daily builds.

## Concepts

See [gRPC Concepts](CONCEPTS.md)

## About This Repository

This repository contains source code for gRPC libraries implemented in multiple
languages written on top of a shared C++ core library [src/core](src/core).

Libraries in different languages may be in various states of development. We are
seeking contributions for all of these libraries:

Language                  | Source
------------------------- | ----------------------------------
Shared C++ [core library] | [src/core](src/core)
C++                       | [src/cpp](src/cpp)
Ruby                      | [src/ruby](src/ruby)
Python                    | [src/python](src/python)
PHP                       | [src/php](src/php)
C# (core library based)   | [src/csharp](src/csharp)
Objective-C               | [src/objective-c](src/objective-c)

Language             | Source repo
-------------------- | --------------------------------------------------
Java                 | [grpc-java](https://github.com/grpc/grpc-java)
Kotlin               | [grpc-kotlin](https://github.com/grpc/grpc-kotlin)
Go                   | [grpc-go](https://github.com/grpc/grpc-go)
NodeJS               | [grpc-node](https://github.com/grpc/grpc-node)
WebJS                | [grpc-web](https://github.com/grpc/grpc-web)
Dart                 | [grpc-dart](https://github.com/grpc/grpc-dart)
.NET (pure C# impl.) | [grpc-dotnet](https://github.com/grpc/grpc-dotnet)
Swift                | [grpc-swift](https://github.com/grpc/grpc-swift)
