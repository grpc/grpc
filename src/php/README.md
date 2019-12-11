
# Overview

This directory contains source code for PHP implementation of gRPC layered on
shared C library. The same installation guides with more examples and
tutorials can be seen at [grpc.io](https://grpc.io/docs/quickstart/php.html).
gRPC PHP installation instructions for Google Cloud Platform is in
[cloud.google.com](https://cloud.google.com/php/grpc).

## Environment

### Prerequisites

* `php`: version 7.0 or above (PHP 5.x support will be deprecated some time
in 2020).
* `pecl`
* `composer`
* `phpunit` (optional)


## Install the _grpc_ extension

There are two ways to install the `grpc` extension.
* `pecl`
* Build from source

### Install from PECL

```sh
$ [sudo] pecl install grpc
```

or specific version

```sh
$ [sudo] pecl install grpc-1.25.0
```

Note: for users on CentOS/RHEL 6, unfortunately this step won’t work. 
Please follow the instructions below to compile the PECL extension from source.


### Install on Windows

You can download the pre-compiled `grpc.dll` extension from the PECL
[website](https://pecl.php.net/package/grpc).

### Build from source

Clone this repository

```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
```

#### Build and install the gRPC C core library

```sh
$ cd grpc
$ git submodule update --init
$ make
$ [sudo] make install
```

#### Build and install the `grpc` extension

Compile the `grpc` extension from source

```sh
$ cd grpc/src/php/ext/grpc
$ phpize
$ ./configure
$ make
$ [sudo] make install
```

This will compile and install the `grpc` extension into the 
standard PHP extension directory. You should be able to run 
the [unit tests](#unit-tests), with the `grpc` extension installed.


### Update php.ini

After installing the `grpc` extension, make sure you add this line to your
`php.ini` file, depending on where your PHP installation is, to enable the
`grpc` extension.

```sh
extension=grpc.so
```

## Composer package

In addition to the `grpc` extension, you will need to install the `grpc/grpc`
composer package as well. Add this to your project's `composer.json` file.

```
  "require": {
    "grpc/grpc": "v1.25.0"
  }
```

To run tests with generated stub code from `.proto` files, you will also 
need the `composer` and `protoc` binaries. You can find out how to get these
below.

## Protocol Buffers

gRPC PHP supports
[protocol buffers](https://developers.google.com/protocol-buffers)
out-of-the-box. You will need the following things to get started:

* `protoc`: the protobuf compiler binary to generate PHP classes for your
messages and service definition.
* `grpc_php_plugin`: a plugin for `protoc` to generate the service stub
classes.
* `protobuf.so`: the `protobuf` extension runtime library.

### `protoc` compiler

If you don't have it already, you need to install the protobuf compiler
`protoc`, version 3.5.0+ (the newer the better) for the current gRPC version.
If you installed already, make the protobuf version is compatible to the 
grpc version you installed. If you build grpc.so from the souce, you can check
the version of grpc inside package.xml file.

The compatibility between the grpc and protobuf version is listed as table
below:

grpc | protobuf
--- | --- 
v1.0.0 | 3.0.0(GA)
v1.0.1 | 3.0.2
v1.1.0 | 3.1.0 
v1.2.0 | 3.2.0 
v1.2.0 | 3.2.0 
v1.3.4 | 3.3.0 
v1.3.5 | 3.2.0
v1.4.0 | 3.3.0 
v1.6.0 | 3.4.0
v1.8.0 | 3.5.0
v1.12.0 | 3.5.2
v1.13.1 | 3.5.2
v1.14.2 | 3.5.2
v1.15.1 | 3.6.1
v1.16.1 | 3.6.1
v1.17.2 | 3.6.1
v1.18.0 | 3.6.1
v1.19.1 | 3.6.1
v1.20.1 | 3.7.0
v1.21.3 | 3.7.0
v1.22.0 | 3.8.0
v1.23.1 | 3.8.0
v1.24.0 | 3.8.0
v1.25.0 | 3.8.0

If `protoc` hasn't been installed, you can download the `protoc` binary from
the protocol buffers
[Github repository](https://github.com/google/protobuf/releases).
Then unzip this file and update the environment variable `PATH` to include the
path to the protoc binary file.

If you really must compile `protoc` from source, you can run the following
commands, but this is risky because there is no easy way to uninstall /
upgrade to a newer release.

```sh
$ cd grpc/third_party/protobuf
$ ./autogen.sh && ./configure && make
$ [sudo] make install
```

### `grpc_php_plugin` protoc plugin

You need the `grpc_php_plugin` to generate the PHP client stub classes. This
plugin works with the main `protoc` binary to generate classes that you can
import into your project.

It should already been compiled when you run `make` from the root directory
of this repo. The plugin can be found in the `bins/opt` directory. We are
planning to provide a better way to download and install the plugin
in the future.

You can also just build the `grpc_php_plugin` by running:

```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
$ cd grpc
$ git submodule update --init
$ make grpc_php_plugin
```

Plugin may use the new feature of the new protobuf version, thus please also
make sure that the protobuf version installed is compatible with the grpc
version you build this plugin.

### `protobuf` runtime library

There are two `protobuf` runtime libraries to choose from. They are identical
in terms of APIs offered. The C implementation provides better performance, 
while the native implementation is easier to install.

#### C implementation (for better performance)

Install the `protobuf` extension from PECL:

``` sh
$ [sudo] pecl install protobuf
```
or specific version

``` sh
$ [sudo] pecl install protobuf-3.8.0
```

And add this to your `php.ini` file:

```sh
extension=protobuf.so
```

#### PHP implementation (for easier installation)

Or require the `google/protobuf` composer package. Add this to your
`composer.json` file:

```
  "require": {
    "google/protobuf": "^v3.8.0"
  }
```

### Generate PHP classes from your service definition

With all the above done, now you can define your message and service defintion
in a `.proto` file and generate the corresponding PHP classes, which you can
import into your project, with a command similar to the following:

```
$ protoc -I=. echo.proto --php_out=. --grpc_out=. \
--plugin=protoc-gen-grpc=<path to grpc_php_plugin>
```

## Unit Tests

You will need the source code to run tests

```sh
$ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
$ cd grpc
$ git submodule update --init
```

Run unit tests

```sh
$ cd grpc/src/php
$ ./bin/run_tests.sh
```

## Generated Code Tests

This section specifies the prerequisites for running the generated code tests,
as well as how to run the tests themselves.

### Composer

Install the runtime dependencies via `composer install`.

```sh
$ cd grpc/src/php
$ composer install
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
$ cd grpc/src/php/tests/generated_code
$ npm install
$ node math_server.js
```

### Run test client

Run the generated code tests

```sh
$ cd grpc/src/php
$ ./bin/run_gen_code_test.sh
```

## Apache, PHP-FPM and Nginx

For more information on how you can run the `grpc` library with Apache,
PHP-FPM and Nginx, you can check out
[this guide](https://github.com/grpc/grpc/tree/master/examples/php/echo).
There you will find a series of Docker images where you can quickly run an
end-to-end example.

## Misc Config Options

### SSL credentials

Here's how you can specify SSL credentials when creating your PHP client:

```
$client = new Helloworld\GreeterClient('localhost:50051', [
    'credentials' => Grpc\ChannelCredentials::createSsl(
        file_get_contents('<path to certificate>'))
]);
```

### pcntl_fork() support

To make sure the `grpc` extension works with `pcntl_fork()` and related
functions, add the following lines to your `php.ini` file:

```
grpc.enable_fork_support = 1
grpc.poll_strategy = epoll1
```

### Tracing and Logging

To turn on gRPC tracing, add the following lines to your `php.ini` file. For
all possible values of the `grpc.grpc.trace` option, please check
[this doc](https://github.com/grpc/grpc/blob/master/doc/environment_variables.md).

```
grpc.grpc_verbosity=debug
grpc.grpc_trace=all,-timer_check
grpc.log_filename=/var/log/grpc.log
```

### User agent string

You can customize the user agent string for your gRPC PHP client by specifying
this `grpc.primary_user_agent` option when constructing your PHP client:

```
$client = new Helloworld\GreeterClient('localhost:50051', [
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
    'grpc.primary_user_agent' => 'my-user-agent-identifier',
]);
```

### Maximum message size

To change the default maximum message size, specify this
`grpc.max_receive_message_length` option when constructing your PHP client:

```
$client = new Helloworld\GreeterClient('localhost:50051', [
    'credentials' => Grpc\ChannelCredentials::createInsecure(),
    'grpc.max_receive_message_length' => 8*1024*1024,
]);
```

[Node]:https://github.com/grpc/grpc/tree/master/src/node/examples
