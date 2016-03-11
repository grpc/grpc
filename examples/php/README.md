gRPC in 3 minutes (PHP)
===========================

PREREQUISITES
-------------

This requires `php` >=5.5, `phpize`, `pecl`, `phpunit`

INSTALL
-------
 - Install the gRPC PHP extension

   ```sh
   $ [sudo] pecl install grpc-beta
   ```

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
   $ npm install
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

[Node]:https://github.com/grpc/grpc/tree/master/examples/node
[gRPC Basics: PHP]:http://www.grpc.io/docs/tutorials/basic/php.html
