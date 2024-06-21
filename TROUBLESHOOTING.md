# Troubleshooting gRPC

This guide is for troubleshooting gRPC implementations based on C core library (sources for most of them are living in the `grpc/grpc` repository).

## Enabling extra logging and tracing

Extra logging can be very useful for diagnosing problems. It can be used to increase the amount of information
that gets printed to stderr.

## GRPC_VERBOSITY

<!-- BEGIN_GOOGLE_INTERNAL_DOCUMENTATION
GRPC_VERBOSITY has been disabled for internal usage and will not work anymore.
If anyone wants to debug, we need to [set log verbosity using absl](https://abseil.io/docs/cpp/guides/logging).

END_GOOGLE_INTERNAL_DOCUMENTATION -->

<!-- BEGIN_OPEN_SOURCE_DOCUMENTATION -->
Our recommendation is to avoid using this flag and [set log verbosity using absl](https://abseil.io/docs/cpp/guides/logging). We only support this flag for legacy reasons.

`GRPC_VERBOSITY` is used to set the minimum level of log messages printed. Supported values are `DEBUG`, `INFO` and `ERROR`. If this environment variable is unset, gRPC will not edit the absl settings. However if this environment variable is set, then gRPC will set absl MinLogValue and absl SetVLogLevel. This will alter the log settings of the entire application, not just gRPC code. For that reason, it is not recommended.
<!-- END_OPEN_SOURCE_DOCUMENTATION -->

## GRPC_TRACE

<!-- BEGIN_GOOGLE_INTERNAL_DOCUMENTATION
GRPC_VERBOSITY has been disabled for internal usage and will not work anymore.
If anyone wants to debug, we need to set verbose logs using absl.
END_GOOGLE_INTERNAL_DOCUMENTATION -->

`GRPC_TRACE` can be used to enable extra logging for some internal gRPC components. Enabling the right traces can be invaluable
for diagnosing for what is going wrong when things aren't working as intended. Possible values for `GRPC_TRACE` are listed in [Environment Variables Overview](doc/environment_variables.md).
Multiple traces can be enabled at once (use comma as separator).

```
# Enable debug logs for an application
GRPC_VERBOSITY=debug ./helloworld_application_using_grpc
```

```
# Print information about invocations of low-level C core API.
# Note that trace logs of log level DEBUG won't be displayed.
# Also note that most tracers user log level INFO, so without setting
# GPRC_VERBOSITY accordingly, no traces will be printed.
GRPC_VERBOSITY=info GRPC_TRACE=api ./helloworld_application_using_grpc
```

```
# Print info from 3 different tracers, including tracing logs with log level DEBUG
GRPC_VERBOSITY=debug GRPC_TRACE=tcp,http,api ./helloworld_application_using_grpc
```

Known limitations: `GPRC_TRACE=tcp` is currently not implemented for Windows (you won't see any tcp traces).

Please note that the `GRPC_TRACE` environment variable has nothing to do with gRPC's "tracing" feature (= tracing RPCs in
microservice environment to gain insight about how requests are processed by deployment), it is merely used to enable printing
of extra logs.
