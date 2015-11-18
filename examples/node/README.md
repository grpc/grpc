gRPC in 3 minutes (Node.js)
===========================

PREREQUISITES
-------------

- `node`: This requires Node 0.10.x or greater.
- [homebrew][] on Mac OS X.  This simplifies the installation of the gRPC C core.

INSTALL
-------
 - [Install gRPC Node][]

 - Install this package's dependencies

   ```sh
   $ cd examples/node
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
You can find a more detailed tutorial in [gRPC Basics: Node.js][]

[homebrew]:http://brew.sh
[Install gRPC Node]:../../src/node
[gRPC Basics: Node.js]:http://www.grpc.io/docs/tutorials/basic/node.html
