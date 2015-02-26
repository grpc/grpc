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

 - Download the grpc debian packages from the [latest grpc release](https://github.com/grpc/grpc/releases) and install them.
   - Later, it will possible to install them directly using `apt-get install`
   ```sh
   $ wget https://github.com/grpc/grpc/releases/download/release-0_5_0/libgrpc_0.5.0_amd64.deb
   $ wget https://github.com/grpc/grpc/releases/download/release-0_5_0/libgrpc-dev_0.5.0_amd64.deb
   $ sudo dpkg -i libgrpc_0.5.0_amd64.deb libgrpc-dev_0.5.0_amd64.deb
   ```

 - Install this package's dependencies

   ```sh
   $ cd grpc-common/node
   $ npm install
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
