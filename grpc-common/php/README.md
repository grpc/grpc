gRPC in 3 minutes (PHP)
===========================

PREREQUISITES
-------------

This requires PHP 5.5 or greater.

INSTALL
-------
 - On Mac OS X, install [homebrew][]. On Linux, install [linuxbrew][]. Run the following command to install gRPC.

   ```sh
   $ curl -fsSL https://goo.gl/getgrpc | bash -s php
   ```
   This will download and run the [gRPC install script][] and compile the gRPC PHP extension.

 - Clone this repository

   ```sh
   $ git clone https://github.com/grpc/grpc-common.git
   ```

 - Install composer

   ```
   $ cd grpc-common/php
   $ curl -sS https://getcomposer.org/installer | php
   $ php composer.phar install
   ```

TRY IT!
-------

 - Run the server

   Please follow the instruction in [Node][] to run the server
   ```
   $ cd grpc-common/node
   $ nodejs greeter_server.js
   ```

 - Run the client

   ```
   $ cd grpc-common/php
   $ ./run_greeter_client.sh
   ```

NOTE
----

This directory has a copy of `helloworld.proto` because it currently depends on
some Protocol Buffer 2.0 syntax. There is no proto3 support for PHP yet.

TUTORIAL
--------

Coming soon

[homebrew]:http://brew.sh
[linuxbrew]:https://github.com/Homebrew/linuxbrew#installation
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[Node]:https://github.com/grpc/grpc-common/tree/master/node
