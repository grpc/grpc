gRPC in 3 minutes (Node.js)
===========================

PREREQUISITES
-------------

This requires Node 10.x or greater.

INSTALL
-------

 - Clone this repository

   ```sh
   $ git clone https://github.com/grpc/grpc-common.git
   ```
 - Follow the instructions in [INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL) to install the gRPC C core.
 - Install this package's dependencies

   ```sh
   $ cd grpc-common/node
   $ npm install
   # If grpc is not found, you'll need to install it from the grpc repository
   $ git clone https://github.com/grpc/grpc.git
   $ npm install path/to/grpc/src/node
   ```


TRY IT!
-------

 - Run the server

   ```sh
   $ # from this directory (grpc_common/node).
   $ node ./greeter_server.js &
   ```

 - Run the client

   ```sh
   $ # from this directory
   $ node ./greeter_client.js
   ```

NOTE
----

This directory has a copy of `helloworld.proto` because it currently depends on
some Protocol Buffer 2.0 syntax that is deprecated in Protocol Buffer 3.0.

TUTORIAL
--------

You can find a more detailed tutorial in [gRPC Basics: Node.js](https://github.com/grpc/grpc-common/blob/master/node/route_guide/README.md).
