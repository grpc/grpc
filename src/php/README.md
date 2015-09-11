
#Overview

This directory contains source code for PHP implementation of gRPC layered on shared C library.

#Status

Alpha : Ready for early adopters

## Environment

Prerequisite: PHP 5.5 or later, `phpunit`, `pecl`

**Linux:**

```sh
$ sudo apt-get install php5 php5-dev phpunit php-pear
```

**Mac OS X:**

```sh
$ curl https://phar.phpunit.de/phpunit.phar -o phpunit.phar
$ chmod +x phpunit.phar
$ sudo mv phpunit.phar /usr/local/bin/phpunit

$ curl -O http://pear.php.net/go-pear.phar
$ sudo php -d detect_unicode=0 go-pear.phar
```

## Quick Install

**Linux (Debian):**

Add [Debian jessie-backports][] to your `sources.list` file. Example:

```sh
echo "deb http://http.debian.net/debian jessie-backports main" | \
sudo tee -a /etc/apt/sources.list
```

Install the gRPC Debian package

```sh
sudo apt-get update
sudo apt-get install libgrpc-dev
```

Install the gRPC PHP extension

```sh
sudo pecl install grpc-alpha
```

**Mac OS X:**

Install [homebrew][]. Example:

```sh
ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Install the gRPC core library and the PHP extension in one step

```sh
$ curl -fsSL https://goo.gl/getgrpc | bash -s php
```

This will download and run the [gRPC install script][] and compile the gRPC PHP extension.


## Build from Source

Clone this repository

```sh
$ git clone https://github.com/grpc/grpc.git
```

Build and install the gRPC C core libraries

```sh
$ cd grpc
$ git pull --recurse-submodules && git submodule update --init --recursive
$ make
$ sudo make install
```

Note: you may encounter a warning about the Protobuf compiler `protoc` 3.0.0+ not being installed. The following might help, and will be useful later on when we need to compile the `protoc-gen-php` tool.

```sh
$ cd grpc/third_party/protobuf
$ sudo make install   # 'make' should have been run by core grpc
```

Install the gRPC PHP extension

```sh
$ sudo pecl install grpc-alpha
```

OR

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
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[Node]:https://github.com/grpc/grpc/tree/master/src/node/examples
[Debian jessie-backports]:http://backports.debian.org/Instructions/
