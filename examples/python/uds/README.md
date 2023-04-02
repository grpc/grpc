# Unix Domain Socket Example in gRPC Python

## Check Our Guide First

For knowing the basics of gRPC Python and the context around the helloworld example, please checkout our gRPC Python [Quick Start guide](https://grpc.io/docs/languages/python/quickstart).

## Overview

This example demonstrate how gRPC Python can utilize the gRPC Name Resolution mechanism to specify UDS address for clients and servers. The gRPC Name Resolution mechanism is documented at https://github.com/grpc/grpc/blob/master/doc/naming.md.

Specifically, this example will bind the server to the following UDS addresses, and use clients to connect to them:

* `unix:path`: setting the unix domain socket path relatively or absolutely
* `unix://absolute_path`: setting the absolute path of the unix domain socket

## Prerequisite

The Python interpreter should have `grpcio` and `protobuf` installed.

## Running The Example

Starting the server:

```
$ python3 greeter_server.py
INFO:root:Server listening on: unix:helloworld.sock
INFO:root:Server listening on: unix:///tmp/helloworld.sock
...
```

```
$ python3 async_greeter_server.py
INFO:root:Server listening on: unix:helloworld.sock
INFO:root:Server listening on: unix:///tmp/helloworld.sock
...
```

Connecting with a client:

```
$ python3 greeter_client.py
INFO:root:Received: Hello to unix:helloworld.sock!
INFO:root:Received: Hello to unix:///tmp/helloworld.sock!
```

```
$ python3 async_greeter_client.py
INFO:root:Received: Hello to unix:helloworld.sock!
INFO:root:Received: Hello to unix:///tmp/helloworld.sock!
```
