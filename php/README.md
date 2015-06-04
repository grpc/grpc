gRPC in 3 minutes (PHP)
===========================

PREREQUISITES
-------------

This requires PHP 5.5 or greater.

INSTALL
-------
 - On Mac OS X, install [homebrew][]. On Linux, install [linuxbrew][]. Run the following command to install gRPC.

  ```sh
  $ curl -fsSL https://goo.gl/getgrpc | bash -
  ```
  This will download and run the [gRPC install script][].

 - Clone this repository

   ```sh
   $ git clone https://github.com/grpc/grpc-common.git
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
   $ curl -fsSL https://goo.gl/getgrpc | bash -s php
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

[homebrew]:http://brew.sh
[linuxbrew]:https://github.com/Homebrew/linuxbrew#installation
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[Node]:https://github.com/grpc/grpc-common/tree/master/node
