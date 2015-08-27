gRPC in 3 minutes (Node.js)
===========================

PREREQUISITES
-------------

- `node`: This requires Node 10.x or greater.
- [homebrew][] on Mac OS X, [linuxbrew][] on Linux.  These simplify the installation of the gRPC C core.

INSTALL
-------
 - On Mac OS X, install [homebrew][]. On Linux, install [linuxbrew][]. Run the following command to install gRPC Node.js.

  ```sh
  $ curl -fsSL https://goo.gl/getgrpc | bash -s nodejs
  ```
  This will download and run the [gRPC install script][], then install the latest version of gRPC Nodejs npm package.
 - Clone this repository

   ```sh
   $ git clone https://github.com/grpc/grpc.git
   ```

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
[linuxbrew]:https://github.com/Homebrew/linuxbrew#installation
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[gRPC Basics: Node.js]:https://github.com/grpc/grpc/blob/master/examples/node/route_guide/README.md
