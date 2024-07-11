# Deadline Example

## Overview

This example shows you how to use deadline when calling calls.

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

To simulate the test scenario, the test server implements following functionalities:
- Response Delay: The server intentionally delays its response for `delay` request messages to induce timeout conditions.
- Deadline Propagation: Upon receiving a request with the `[propagate me]` prefix, the server forwards it back to itselt.
  This simulates the propagation of deadlines within the system.

If things go smoothly, you will see the client output:

```
[Successful request] wanted = 0, got = 0
[Exceeds deadline] wanted = 4, got = 4
[Successful request with propagated deadline] wanted = 0, got = 0
[Exceeds propagated deadline] wanted = 4, got = 4
```
