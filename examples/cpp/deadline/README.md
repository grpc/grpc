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

If things go smoothly, you will see the client output:

```
[1] wanted = 0, got = 0
[2] wanted = 4, got = 4
[3] wanted = 0, got = 0
[4] wanted = 4, got = 4
```
