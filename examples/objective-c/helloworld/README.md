# gRPC in 3 minutes (Objective-C)

There are currently two ways to build projects with the gRPC Objective-C library:
* Cocoapods & Xcode
* Bazel (experimental)

## Cocoapods

## Installation

To run this example you should have [Cocoapods](https://cocoapods.org/#install) installed, as well
as the relevant tools to generate the client library code (and a server in another language, for
testing). You can obtain the latter by following [these setup instructions](https://github.com/grpc/homebrew-grpc).

### Hello Objective-C gRPC!

Here's how to build and run the Objective-C implementation of the [Hello World](../../protos/helloworld.proto)
example used in [Getting started](https://github.com/grpc/grpc/tree/master/examples).

The example code for this and our other examples lives in the `examples` directory. Clone
this repository at the [latest stable release tag](https://github.com/grpc/grpc/releases) to your local machine by running the following commands:


```sh
$ git clone -b RELEASE_TAG_HERE https://github.com/grpc/grpc
$ cd grpc
$ git submodule update --init
```

Change your current directory to `examples/objective-c/helloworld`

```sh
$ cd examples/objective-c/helloworld
```

#### Try it!
To try the sample app, we need a gRPC server running locally. Let's compile and run, for example,
the C++ server in this repository:

```shell
$ pushd ../../cpp/helloworld
$ make
$ ./greeter_server &
$ popd
```

Now have Cocoapods generate and install the client library for our .proto files:

```shell
$ pod install
```

(This might have to compile OpenSSL, which takes around 15 minutes if Cocoapods doesn't have it yet
on your computer's cache.)

Finally, open the XCode workspace created by Cocoapods, and run the app. You can check the calling
code in `main.m` and see the results in XCode's log console.

The code sends a `HLWHelloRequest` containing the string "Objective-C" to a local server. The server
responds with a `HLWHelloResponse`, which contains a string that is then output to the log.

## Bazel
### Installation
To run the examples in Bazel, you should have [Bazel](https://docs.bazel.build/versions/master/install-os-x.html) installed.

### Hello Objective-C gRPC!
Here's how to build and run the Objective-C implementation of the [Hello World](helloworld) example.

The code for the Hello World example and others live in the `examples` directory. Clone this repository to your local machine by running the following commands:
```shell
$ git clone --recursive https://github.com/grpc/grpc
```

Next, change your directory to `examples/objective-c`
```shell
$ cd grpc/examples/objective-c
```

Now build the Hello World project:
```shell
$ bazel build :HelloWorld
```

#### Try it!
To run the Hello World sample properly, we need a local server. Let's compile and run the corresponding C++ server:
```shell
$ bazel run //examples/cpp/helloworld:greeter_server
```

To run the sample, you need to know the available simulator runtimes in your machine. You could either list the available runtimes yourself by running:
```shell
$ xcrun simctl list
```
Or just try running the app and it will let you know what is available in the error messages:
```shell
$ bazel run :HelloWorld
```
Note that running this command will build the project even if it is not built beforehand.

Finally, launch the app with one of the available runtimes:
```shell
$ bazel run :HelloWorld --ios_simulator_version='<runtime>' --ios_sumlator_device='<device>'
```

## Tutorial

You can find a more detailed tutorial in [gRPC Basics: Objective-C](https://grpc.io/docs/languages/objective-c/basics).
