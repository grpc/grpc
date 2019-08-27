# gRPC in 3 minutes (Objective-C with Bazel)

To save you from reading, if you use Cocoapods and Xcode, the right reference for you is in the [Hello World](helloworld) example directory.

## Background
There are currently two ways to build projects with the gRPC Objective-C library:
* Cocoapods & Xcode
* Bazel

The [helloworld](helloworld) sample is the entry point for the Cocoapods way of building projects. More is documented on grpc.io, including [installation](https://grpc.io/docs/quickstart/objective-c/) and some [tutorials](https://grpc.io/docs/tutorials/basic/objective-c/). They will not be reiterated here and they are not mandatory for the Bazel way.

A good place to get started with Bazel is their official [website](https://bazel.build). The documentation of using gRPC Objective-C library with Bazel is [here](https://github.com/grpc/proposal/blob/master/L56-objc-bazel-support.md).

## Installation
To run the examples in Bazel, you should have [Bazel](https://docs.bazel.build/versions/master/install-os-x.html) installed.

## Hello Objective-C gRPC!
Here's how to build and run the Objective-C implementation of the [Hello World](helloworld) example.

The code for the Hello World example and others live in the `examples` directory. Clone this repository to your local machine by running the following commands:
```shell
$ git clone --recursive https://github.com/grpc/grpc
```

Next, change your directory to `examples/objective-c`
```shell
$ cd examples/objective-c
```

### Try it!
To run the Hello World sample properly, we need a local server. Let's compile and run the corresponding C++ server:
```shell
$ pushd ../cpp/helloworld
$ make
$ ./greeter_server &
$ popd
```
Or do it the Bazel way (you might need to open another shell tab for this):
```shell
$ bazel run //examples:greeter_server
```

Now compile the sample:
```shell
$ bazel build :HelloWorld
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
