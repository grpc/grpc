gRPC in 3 minutes (PHP)
===========================

PREREQUISITES
-------------

This requires PHP 5.5 or greater.

INSTALL
-------

 - Clone this repository

   ```sh
   $ git clone https://github.com/grpc/grpc-common.git
   ```

 - Install Protobuf-PHP

   ```
   $ git clone https://github.com/murgatroid99/Protobuf-PHP.git
   $ cd Protobuf-PHP
   $ rake pear:package version=1.0
   $ pear install Protobuf-1.0.tgz
   ```

 - Install composer

   ```
   $ cd grpc-common/php
   $ curl -sS https://getcomposer.org/installer | php
   ```

 - (Coming soon) Download the gRPC PECL extension

   ```
   Coming soon
   ```

 - (Temporary workaround) Compile gRPC extension from source

   ```
   $ git clone https://github.com/grpc/grpc.git
   $ cd grpc
   $ git checkout --track origin/release-0_9
   $ git pull --recurse-submodules && git submodule update --init --recursive
   $ cd third_party/protobuf
   $ ./autogen.sh && ./configure --prefix=/usr && make && make install
   $ cd ../..
   $ make && make install
   $ cd src/php/ext/grpc
   $ phpize && ./configure && make && make install
   ```


TRY IT!
-------

 - Run the server

   Please follow the instruction in [Node](https://github.com/grpc/grpc-common/tree/master/node) to run the server
   ```
   $ cd grpc-common/node
   $ nodejs greeter_server.js
   ```

 - Run the client

   ```
   $ cd grpc-common/php
   $ php composer.phar install
   $ php -d extension=grpc.so greeter_client.php
   ```

NOTE
----

This directory has a copy of `helloworld.proto` because it currently depends on
some Protocol Buffer 2.0 syntax. There is no proto3 support for PHP yet.

TUTORIAL
--------

Coming soon
