
#Overview

This directory contains source code for PHP implementation of gRPC layered on shared C library.

#Status

Pre-Alpha : This gRPC PHP implementation is work-in-progress and is not expected to work yet.

## ENVIRONMENT

Install `php5` and `php5-dev`.

To run the tests, additionally install `phpunit`.

Alternatively, build and install PHP 5.5 or later from source with standard
configuration options.

## Build from Homebrew

On Mac OS X, install [homebrew][]. On Linux, install [linuxbrew][]. Run the following command to
install gRPC.

```sh
$ curl -fsSL https://goo.gl/getgrpc | bash -s php
```

This will download and run the [gRPC install script][] and compile the gRPC PHP extension.

## Build from Source

Clone this repository

```
$ git clone https://github.com/grpc/grpc.git
```

Build and install the Protocol Buffers compiler (protoc)

```
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
$ cd third_party/protobuf
$ ./autogen.sh
$ ./configure
$ make
$ make check
$ sudo make install
```

Build and install the gRPC C core

```sh
$ cd grpc
$ make
$ sudo make install
```

Build the gRPC PHP extension

```sh
$ cd grpc/src/php/ext/grpc
$ phpize
$ ./configure
$ make
$ sudo make install
```

In your php.ini file, add the line `extension=grpc.so` to load the extension
at PHP startup.

Install Composer

```sh
$ cd grpc/src/php
$ curl -sS https://getcomposer.org/installer | php
$ php composer.phar install
```

## Unit Tests

Run unit tests

```sh
$ cd grpc/src/php
$ ./bin/run_tests.sh
```

## Generated Code Tests

Install `protoc-gen-php`

```sh
$ cd grpc/src/php/vendor/datto/protobuf-php
$ gem install rake ronn
$ rake pear:package version=1.0
$ sudo pear install Protobuf-1.0.tgz
```

Generate client stub code

```sh
$ cd grpc/src/php
$ ./bin/generate_proto_php.sh
```

Run a local server serving the math services

 - Please see [Node][] on how to run an example server

```sh
$ cd grpc/src/node
$ npm install
$ nodejs examples/math_server.js
```

Run the generated code tests

```sh
$ cd grpc/src/php
$ ./bin/run_gen_code_test.sh
```

[homebrew]:http://brew.sh
[linuxbrew]:https://github.com/Homebrew/linuxbrew#installation
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[Node]:https://github.com/grpc/grpc/tree/master/src/node/examples

