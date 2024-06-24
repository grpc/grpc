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

## GRPC_VERBOSITY (DEPRECATED)

[Environment Variables Overview](doc/environment_variables.md)

## GRPC_TRACE

`GRPC_TRACE` can be used to enable extra logging for some internal gRPC components. Enabling the right traces can be invaluable
for diagnosing for what is going wrong when things aren't working as intended. Possible values for `GRPC_TRACE` are [listed here](doc/trace_flags.md).
Multiple traces can be enabled at once (use comma as separator).

```
# Enable debug logs for an application
./helloworld_application_using_grpc --v=2 --minloglevel=0
```

```
# Print information about invocations of low-level C core API.
# Note that trace logs of log level DEBUG won't be displayed.
# Also note that many tracers user log level INFO,
# so without setting absl verbosity accordingly, no traces will be printed.
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
