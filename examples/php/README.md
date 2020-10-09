# gRPC PHP Quick Start Example


## Prerequisites

This requires `php` >= 7.0, `pecl`, `composer`

## Install

 - Install the `grpc` extension

   ```sh
   $ [sudo] pecl install grpc
   ```

 - Install the `protoc` compiler plugin `grpc_php_plugin`

   ```sh
   $ git clone -b RELEASE_TAG_HERE https://github.com/grpc/grpc
   $ cd grpc
   $ make grpc_php_plugin
   ```

 - Install the `grpc/grpc` composer package

   ```
   $ cd examples/php
   $ composer install
   ```

## Try it!

   ```
   $ cd examples/php
   ```

 - Generate proto files

   ```
   $ ./greeter_proto_gen.sh
   ```

 - Run the server

   ```
   $ php greeter_server.php
   ```

 - From another terminal, from the examples/php directory, run the client

   ```
   $ ./run_greeter_client.sh
   ```

## In-depth Tutorial

You can find a more detailed tutorial in [gRPC Basics: PHP][]

[Node]:https://github.com/grpc/grpc/tree/master/examples/node
[gRPC Basics: PHP]:https://grpc.io/docs/languages/php/basics
