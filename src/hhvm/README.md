
# Overview

This directory contains source code for HHVM implementation of gRPC layered on
shared C library.

## Environment

**Prerequisite:**
* `hhvm` 3.18 or above
* `composer`
* `phpunit` (optional)


## Build from Source


### gRPC C core library

Clone this repository

```sh
$ git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc
```

Build and install the gRPC C core library

```sh
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
$ make
$ sudo make install
```

### gRPC PHP extension

Compile the gRPC HHVM extension

```sh
$ cd grpc/src/hhvm/
$ phpize
$ ./configure
$ make
$ sudo make install
```

## Unit Tests

You will need the source code to run tests

```sh
$ git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
```

Run unit tests

```sh
$ cd grpc/src/hhvm
$ ./bin/run_tests.sh
```

## Generated Code Tests

This section specifies the prerequisites for running the generated code tests,
as well as how to run the tests themselves.

### Composer

Install the runtime dependencies via `composer install`.

```sh
$ cd grpc/src/hhvm
$ composer install
```

### Protobuf compiler

Again if you don't have it already, you need to install the protobuf compiler
`protoc`, version 3.1.0+ (the newer the better).

If `protoc` hasn't been installed, you can download the `protoc` binaries from
[the protocol buffers Github repository](https://github.com/google/protobuf/releases).

If you really must compile `protoc` from source, you can run the following
commands, but this is risky because there is no easy way to uninstall /
upgrade to a newer release.

```sh
$ cd grpc/third_party/protobuf
$ ./autogen.sh && ./configure && make
$ sudo make install
```


### Protobuf Runtime library

There are two protobuf runtime libraries to choose from. They are idenfical in terms of APIs offered.

1. C implementation (for better performance)

``` sh
$ sudo pecl install protobuf
```

2. PHP implementation (for easier installation)


Add this to your `composer.json` file:

```
  "require": {
    "google/protobuf": "^v3.3.0"
  }
``` 


### PHP Protoc Plugin

You need the gRPC PHP protoc plugin to generate the client stub classes.

It should already been compiled when you run `make` from the root directory
of this repo. The plugin can be found in the `bins/opt` directory. We are
planning to provide a better way to download and install the plugin
in the future.

You can also just build the gRPC PHP protoc plugin by running:

```sh
$ cd grpc
$ make grpc_php_plugin
```


### Client Stub

Generate client stub classes from `.proto` files

```sh
$ cd grpc/src/php
$ ./bin/generate_proto_php.sh
```

### Run test server

Run a local server serving the math services. Please see [Node][] for how to
run an example server.

```sh
$ cd grpc
$ npm install
$ node src/node/test/math/math_server.js
```

### Run test client

Run the generated code tests

```sh
$ cd grpc/src/php
$ ./bin/run_gen_code_test.sh
```
