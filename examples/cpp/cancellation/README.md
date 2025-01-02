# Cancellation Example

## Overview

This example shows you how to cancel from the client and how to get informed on the server and the client.

### Try it!

Once you have working gRPC, you can build this example using either bazel or cmake.

Run the server, which will listen on port 50051:

```sh
$ ./server
```

Run the client (in a different terminal):

```sh
$ ./client
```

If things go smoothly, you will see the client output:

```
Begin : Begin Ack
Count 1 : Count 1 Ack
Count 2 : Count 2 Ack
Count 3 : Count 3 Ack
Count 4 : Count 4 Ack
Count 5 : Count 5 Ack
Count 6 : Count 6 Ack
Count 7 : Count 7 Ack
Count 8 : Count 8 Ack
Count 9 : Count 9 Ack
RPC Cancelled!
```
