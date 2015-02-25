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
 - Install 

   ```sh
   $ cd grpc-common/node
   $ npm install 
   # If node is not found, you'll need to clone the grpc repository (if you haven't already)
   $ git clone https://github.com/grpc/grpc.git
   $ npm install ~/grpc/src/node
   ```
 

Try it! 
-------

 - Run the server

   ```sh
   $ # from this directory
   $ nodejs ./greeter_server.js &
   ```

 - Run the client

   ```sh
   $ # from this directory
   $ nodejs ./greeter_client.js
   ```

Note
----

This directory has a copy of `helloworld.proto` because it currently depends on
some Protocol Buffer 2.0 syntax that is deprecated in Protocol Buffer 3.0.
