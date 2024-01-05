# gRPC C++ Debugging Example

This example demonstrates a handful of ways you can debug your gRPC C++ applications.

## Enabling Trace Logs

gRPC allows you to configure more detailed log output for various aspects of gRPC behavior. The tracing log generation might have a large overhead and result in significantly larger log file sizes, especially when you try to trace transport or timer_check. But it is a powerful tool in your debugging toolkit.

### The Most Verbose Logging

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

## GDB and other debuggers

`gdb` (and the like) are tools that lets you inspect your application while it is running, view stack traces on exceptions, pause and step through code at specified points or under certain conditions, etc. See https://www.sourceware.org/gdb/

### Inspecting errors

```
bazel build --config=dbg examples/cpp/debugging:crashing_greeter_client
gdb -ex run \
    --args ./bazel-bin/examples/cpp/debugging/crashing_greeter_client \
            --crash_on_errors=true \
            --target=localhork:50051
```

Once the exception is thrown, you can use `bt` to see the stack trace and examine the crash, `info threads` to get the set of threads, etc. See the [GDB documentation](https://sourceware.org/gdb/current/onlinedocs/gdb.html/) for a more complete list of available features and commands.

### Breaking inside a function

After building the application above, this will break inside gRPC generated stub code:

```
gdb -ex 'b helloworld::Greeter::Stub::SayHello' \
    -ex run \
    --args ./bazel-bin/examples/cpp/debugging/crashing_greeter_client \
            --crash_on_errors=true \
            --target=localhork:50051
```

## gRPC Admin Interface: Live Channel Tracing

The [gRPC Admin Service](https://github.com/grpc/proposal/blob/master/A38-admin-interface-api.md)
provides a convenient API in each gRPC language to improve the usability of
creating a gRPC server with admin services to expose states in the gRPC library.
This includes channelz, which is a channel tracing feature; it tracks statistics
like how many messages have been sent, how many of them failed, what are the
connected sockets. See the [Channelz design doc](https://github.com/grpc/proposal/blob/master/A14-channelz.md).

### Integrating the gRPC Admin Service Into Your Server

As seen in the `greeter_callback_admin_server` target, you canenable admin services by using the `AddAdminServices` method.

```
grpc::ServerBuilder builder;
grpc::AddAdminServices(&builder);
builder.AddListeningPort(":50051", grpc::ServerCredentials(...));
std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
```

### Using grpcdebug

grpcdebug is a tool created to access the metrics from channelz and health services.

#### Installing the grpcdebug tool

The source code is located in a github project
[grpc-ecosystem/grpcdebug](https://github.com/grpc-ecosystem/grpcdebug). You
can either download [the latest built version]
(https://github.com/grpc-ecosystem/grpcdebug/releases/latest) (recommended) or
follow the README.md to build it yourself.

#### Running the grpcdebug tool
##### Usage
`grpcdebug <target address> [flags] channelz <command> [argument]`


| Command    |       Argument       | Description                                       |
| :--------- | :------------------: | :------------------------------------------------ |
| channel    | \<channel id or URL> | Display channel states in a human readable way.   |
| channels   |                      | Lists client channels for the target application. |
| server     |     \<server ID>     | Displays server state in a human readable way.    |
| servers    |                      | Lists servers in a human readable way.            |
| socket     |     \<socket ID>     | Displays socket states in a human readable way.   |
| subchannel |        \<id>         | Display subchannel states in human readable way.  |

Generally, you will start with either `servers` or `channels` and then work down
to the details

##### Getting overall server info

To begin with, build and run the server binary in the background

```
bazel build --config=dbg examples/cpp/debugging:all
./bazel-bin/examples/cpp/debugging/greeter_callback_server_admin&
```

You can then inspect the server
```bash
grpcdebug localhost:50051 channelz servers
```

This will show you the server ids with their activity
```text
Server ID   Listen Addresses   Calls(Started/Succeeded/Failed)   Last Call Started
1           [[::]:50051]       38/34/3                           now
```

For more information about `grpcdebug` features, please see [the grpcdebug documentation](https://github.com/grpc-ecosystem/grpcdebug)
