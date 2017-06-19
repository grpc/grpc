
# Overview

This directory contains source code for HHVM implementation of gRPC layered on
shared C library.

## Environment

**Prerequisite:**
* `hhvm` 3.18.x
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
$ hphpize
$ ./configure
$ make
$ sudo make install
```
