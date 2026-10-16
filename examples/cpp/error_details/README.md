# Error Details Example

## Overview

This example shows you how to return error with details from the server
and how to handle it on the client.

### Try it!

Once you have working gRPC, you can build this example using either bazel or cmake.

Run the server, which will listen on port 50051:

```sh
$ ./greeter_server
```

Run the client (in a different terminal):

```sh
$ ./greeter_client
```

If things go smoothly, you will see the client output:

```
### Send: SayHello(name=World)
Ok. ReplyMessage=Hello World
### Send: SayHello(name=World)
Failed. Code=8 Message=Request limit exceeded
Details:
- Quota: subject=name: World description=Limit one greeting per person
```
