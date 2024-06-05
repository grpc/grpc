# Troubleshooting gRPC

This guide is for troubleshooting gRPC implementations based on C core library (sources for most of them are living in the `grpc/grpc` repository).

## Enabling extra logging and tracing

Extra logging can be very useful for diagnosing problems. All gRPC implementations based on C core library support
the `GRPC_VERBOSITY` and `GRPC_TRACE` environment variables that can be used to increase the amount of information
that gets printed to stderr.

## GRPC_VERBOSITY

<!-- BEGIN_GOOGLE_INTERNAL_DOCUMENTATION" -->
This has been deprecated and will not work anymore.
If anyone wants to debug, we need to set verbose logs using absl.
<!-- END_GOOGLE_INTERNAL_DOCUMENTATION -->

<!-- BEGIN_OPEN_SOURCE_DOCUMENTATION -->
  gRPC logging verbosity - one of:
  - DEBUG - log INFO, WARNING, ERROR and FATAL messages. Also sets absl VLOG(2) logs enabled. This is not recommended for production systems. This will be expensive for staging environments too, so it can be used when you want to debug a specific issue. 
  - INFO - log INFO, WARNING, ERROR and FATAL messages. This is not recommended for production systems. This may be slightly expensive for staging environments too. We recommend that you use your discretion for staging environments.
  - ERROR - log ERROR and FATAL messages. This is recommended for production systems.
  - NONE - won't log any.
  GRPC_VERBOSITY will set verbosity of absl logging. 
  - If the external application sets some other verbosity, then whatever is set later will be honoured. 
  - If nothing is set as GRPC_VERBOSITY, then the setting of the exernal application will be honoured.
  - If nothing is set by the external application also, the default set by absl will be honoured.
<!-- END_OPEN_SOURCE_DOCUMENTATION -->

## GRPC_TRACE

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
