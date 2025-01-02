# Authentication Example

## Overview

SSL is a commonly used cryptographic protocol to provide end-to-end
communication security. In the example, we show how to set up a server
authenticated SSL connection to transmit RPC.

We provide `grpc::SslServerCredentials` and `grpc::SslCredentials` types
to use SSL connections.

In our example, we use the public/private keys created ahead: 
* "localhost.crt" contains the server certificate (public key). 
* "localhost.key" contains the server private key. 
* "root.crt" contains the certificate (certificate authority)
that can verify the server's certificate.

### Try it!

Once you have working gRPC, you can build this example using either bazel or cmake.
Make sure to run those at this directory so that they can read credential files properly.

Run the server, which will listen on port 50051:

```sh
$ ./ssl_server
```

Run the client (in a different terminal):

```sh
$ ./ssl_client
```

If things go smoothly, you will see the client output:

```
Greeter received: Hello world
```
