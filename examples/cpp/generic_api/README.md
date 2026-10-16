# Generic API Example

## Overview

While generated stub code is often the simpler and best choice for sending and handling API calls,
generic APIs offer unique advantages in specific scenarios, such as proxy implementation.
Their ability to manage multiple message types with a single function makes them particularly handy
in these cases. This example demonstrates how to use generic APIs to achieve this flexibility.

This example implements `greeter_callback_client` and `greeter_callback_server` using the generic APIs.
Therefore, looking at the difference would be helpful to understand how to use generic APIs.

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
### Send: SayHello(name=gRPC)
Ok. ReplyMessage=Hello gRPC
```
