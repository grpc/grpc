gRPC PHP Extension
==================

# Requirements

 * PHP 5.5+
 * [gRPC core library](https://github.com/grpc/grpc) 0.10.0

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

Build and install the Protocol Buffers compiler (protoc)

```
$ # from grpc
$ git checkout --track origin/release-0_9
$ git pull --recurse-submodules && git submodule update --init --recursive
$ cd third_party/protobuf
$ ./autogen.sh
$ ./configure
$ make
$ make check
$ sudo make install
```

Build and install the gRPC C core library

```sh
$ # from grpc
$ make
$ sudo make install
```

## Install the gRPC PHP extension

Quick install

```sh
$ sudo pecl install grpc
```

Note: before a stable release, you may need to do

```sh
$ sudo pecl install grpc-0.5.1
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
