# Troubleshooting gRPC

This guide is for troubleshooting gRPC implementations based on C core library (sources for most of them are living in the `grpc/grpc` repository).

## Enabling extra logging and tracing

Extra logging can be very useful for diagnosing problems. It can be used to increase the amount of information
that gets printed to stderr.

## Setting Logging Severity and Verbosity

[gRPC uses absl logging](https://abseil.io/docs/cpp/guides/logging).
Verbosity can be set using absl flags such as
`--minloglevel`, `--v` and `--vmodule`.

These can also be programmatically set using
[these absl APIs.](https://github.com/abseil/abseil-cpp/blob/master/absl/log/globals.h)

Example
```
# Disable all logs other than FATAL for the entire application
./helloworld_application_using_grpc --v=-1 --minloglevel=3
```

## GRPC_VERBOSITY

This is not recommended for gRPC C++. For gRPC C++ it is strongly recommended that you use absl to control gRPC verbosity. For Python, ObjC, PHP, and Ruby, GRPC_VERBOSITY will be supported. Users need to note that this will change settings for all libraries/binaries that use absl in the application. GRPC_VERBOSITY will alter the global absl settings, not just settings for gRPC.

To learn how to set GRPC_VERBOSITY refer [Environment Variables Overview](doc/environment_variables.md)

## GRPC_TRACE

`GRPC_TRACE` can be used to enable extra logging for specific internal gRPC components. Enabling the right traces can be invaluable
for diagnosing for what is going wrong when things aren't working as intended. Possible values for `GRPC_TRACE` are [listed here](doc/trace_flags.md).
Multiple traces can be enabled at once (use comma as separator).

```
# Enable debug logs for the entire application
./helloworld_application_using_grpc --v=2 --minloglevel=0
```

```
# Print information about invocations of low-level C core API.
# Note that trace logs that use `VLOG` won't be displayed.
# Many tracers user log level INFO.
# So unless absl settings are correct, no traces will be printed.
GRPC_TRACE=api ./helloworld_application_using_grpc --v=-1 --minloglevel=0
```

```
# Print info from 3 different tracers, including tracing logs
GRPC_TRACE=tcp,http,api ./helloworld_application_using_grpc  --v=2 --minloglevel=0
```

Known limitations: `GPRC_TRACE=tcp` is currently not implemented for Windows (you won't see any tcp traces).

Please note that the `GRPC_TRACE` environment variable has nothing to do with gRPC's "tracing" feature (= tracing RPCs in
microservice environment to gain insight about how requests are processed by deployment), it is merely used to enable printing
of extra logs.

## Preventing gRPC Log Noise

Log noise could consume a lot of resources. We recommend tuning settings for production systems very carefully.
*	Avoid using GRPC_VERBOSITY flag. This has been deprecated. If this value of this flag is anything other than "ERROR" or "NONE" it will cause log noise.
*	Always avoid setting --v and --vmodule to anything other than -1 for production systems.
*	Avoid setting --minloglevel=0 for production systems. Anything greater than 0 should be fine.
*   If setting this does not eliminate your log noise, look for instances of functions `--v`, `--vmodule`, `absl::SetVLogLevel` and `absl::SetMinLogLevel` in your entire codebase and any libraries/components/configs that you may be using.
