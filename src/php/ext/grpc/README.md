gRPC PHP Extension
==================

# Requirements

 * PHP 5.5+
 * [gRPC core library](https://github.com/grpc/grpc) 0.11.0

# Installation

## Install PHP 5

```
$ sudo apt-get install git php5 php5-dev php-pear unzip
```

## Compile gRPC Core Library

Clone the gRPC source code repository

```
$ git clone https://github.com/grpc/grpc.git
```

Build and install the gRPC C core libraries

```sh
$ cd grpc
$ git checkout --track origin/release-0_11
$ git pull --recurse-submodules && git submodule update --init --recursive
$ make
$ sudo make install
```

Note: you may encounter a warning about the Protobuf compiler `protoc` 3.0.0+ not being installed. The following might help, and will be useful later on when we need to compile the `protoc-gen-php` tool.

```sh
$ cd grpc/third_party/protobuf
$ sudo make install   # 'make' should have been run by core grpc
```

## Install the gRPC PHP extension

Quick install

```sh
$ sudo pecl install grpc
```

Note: before a stable release, you may need to do

```sh
$ sudo pecl install grpc-beta
```

OR

Compile from source

```sh
$ # from grpc
$ cd src/php/ext/grpc
$ phpize
$ ./configure
$ make
$ sudo make install
```
