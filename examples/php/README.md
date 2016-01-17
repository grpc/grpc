gRPC in 3 minutes (PHP)
===========================

PREREQUISITES
-------------

This requires PHP 5.5 or greater.

INSTALL
-------
 - On Mac OS X, install [homebrew][]. Run the following command to install gRPC.

   ```sh
   $ curl -fsSL https://goo.gl/getgrpc | bash -s php
   ```
   This will download and run the [gRPC install script][] and compile the gRPC PHP extension.

 - Clone this repository

   ```sh
   $ git clone https://github.com/grpc/grpc.git
   ```

 - Install composer

   ```
   $ cd examples/php
   $ curl -sS https://getcomposer.org/installer | php
   $ php composer.phar install
   ```

TRY IT!
-------

 - Run the server

   Please follow the instruction in [Node][] to run the server
   ```
   $ cd examples/node
   $ nodejs greeter_server.js
   ```

 - Run the client

   ```
   $ cd examples/php
   $ ./run_greeter_client.sh
   ```

NOTE
----

This directory has a copy of `helloworld.proto` because it currently depends on
some Protocol Buffer 2.0 syntax. There is no proto3 support for PHP yet.

TUTORIAL
--------

You can find a more detailed tutorial in [gRPC Basics: PHP][]

[homebrew]:http://brew.sh
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[Node]:https://github.com/grpc/grpc/tree/master/examples/node
[gRPC Basics: PHP]:http://www.grpc.io/docs/tutorials/basic/php.html
