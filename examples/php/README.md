# gRPC PHP Quick Start Example


## Prerequisites

This requires `php` >= 5.5, `pecl`, `composer`

## Install

 - Install the `grpc` extension

   ```sh
   $ [sudo] pecl install grpc
   ```

 - Install the `protoc` compiler plugin `grpc_php_plugin`

   ```sh
   $ git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc
   $ cd grpc
   $ make grpc_php_plugin
   ```

 - Install the `grpc/grpc` composer package

   ```
   $ cd examples/php
   $ composer install
   ```

## Try it!

 - Run the server

   Please follow the instruction in [Node][] to run the server
   ```
   $ cd examples/node
   $ npm install
   $ cd dynamic_codegen or cd static_codegen
   $ node greeter_server.js
   ```

 - Generate proto files and run the client

   ```
   $ cd examples/php
   $ ./greeter_proto_gen.sh
   $ ./run_greeter_client.sh
   ```

## In-depth Tutorial

You can find a more detailed tutorial in [gRPC Basics: PHP][]

[Node]:https://github.com/grpc/grpc/tree/master/examples/node
[gRPC Basics: PHP]:https://grpc.io/docs/tutorials/basic/php.html
