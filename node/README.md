# gRPC Node.js Helloworld

## INSTALLATION REQUIREMENTS

This requires Node 10.x or greater.

## INSTALL

 - Clone this repository
 - Follow the instructions in [INSTALL](https://github.com/grpc/grpc/blob/master/INSTALL) to install the gRPC C core.
 - Run `npm install` to install dependencies
   - If `grpc` is not found, clone the [gRPC](https://github.com/grpc/grpc) repository and run `npm install path/to/grpc/src/node`.

## USAGE

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

## NOTE

This directory has a copy of `helloworld.proto` because it currently depends on
some Protocol Buffer 2.0 syntax that is deprecated in Protocol Buffer 3.0.
