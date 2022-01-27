# gRPC Python Debug Example

This example demonstrate the usage of Channelz. For a better looking website,
the [gdebug](https://github.com/grpc/grpc-experiments/tree/master/gdebug) uses
gRPC-Web protocol and will serve all useful information in web pages.

## Channelz: Live Channel Tracing

Channelz is a channel tracing feature. It will track statistics like how many
messages have been sent, how many of them failed, what are the connected
sockets. Since it is implemented in C-Core and has low-overhead, it is
recommended to turn on for production services. See [Channelz design
doc](https://github.com/grpc/proposal/blob/master/A14-channelz.md).

## How to enable tracing log
The tracing log generation might have larger overhead, especially when you try
to trace transport. It would result in replicating the traffic loads. However,
it is still the most powerful tool when you need to dive in.

### The Most Verbose Tracing Log

Specify environment variables, then run your application:

```
GRPC_VERBOSITY=debug
GRPC_TRACE=all
```

For more granularity, please see
[environment_variables](https://github.com/grpc/grpc/blob/master/doc/environment_variables.md).

### Debug Transport Protocol

```
GRPC_VERBOSITY=debug
GRPC_TRACE=tcp,http,secure_endpoint,transport_security
```

### Debug Connection Behavior

```
GRPC_VERBOSITY=debug
GRPC_TRACE=call_error,connectivity_state,pick_first,round_robin,glb
```

## How to debug your application?

`pdb` is a debugging tool that is available for Python interpreters natively.
You can set breakpoint, and execute commands while the application is stopped.

The simplest usage is add a single line in the place you want to inspect:
`import pdb; pdb.set_trace()`. When interpreter see this line, it would pop out
a interactive command line interface for you to inspect the application state.

For more detailed usage, see https://docs.python.org/3/library/pdb.html.

**Caveat**: gRPC Python uses C-Extension under-the-hood, so `pdb` may not be
able to trace through the whole stack.

## gRPC Command Line Tool

`grpc_cli` is a handy tool to interact with gRPC backend easily. Imageine you can
inspect what service does a server provide without writing any code, and make
gRPC calls just like `curl`.

The installation guide: https://github.com/grpc/grpc/blob/master/doc/command_line_tool.md#code-location
The usage guide: https://github.com/grpc/grpc/blob/master/doc/command_line_tool.md#usage
The source code: https://github.com/grpc/grpc/blob/master/test/cpp/util/grpc_cli.cc
