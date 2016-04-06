gRPC in 3 minutes (Node.js)
===========================

PREREQUISITES
-------------

- `node`: This requires Node 0.12.x or greater.

INSTALL
-------

   ```sh
   $ # Get the gRPC repository
   $ export REPO_ROOT=grpc # REPO root can be any directory of your choice
   $ git clone https://github.com/grpc/grpc.git $REPO_ROOT
   $ cd $REPO_ROOT

   $ cd examples/node
   $ npm install
   ```

TRY IT!
-------

 - Run the server

   ```sh
   $ # from this directory
   $ node ./greeter_server.js &
   ```

 - Run the client

   ```sh
   $ # from this directory
   $ node ./greeter_client.js
   ```

TUTORIAL
--------
You can find a more detailed tutorial in [gRPC Basics: Node.js][]

[Install gRPC Node]:../../src/node
[gRPC Basics: Node.js]:http://www.grpc.io/docs/tutorials/basic/node.html
