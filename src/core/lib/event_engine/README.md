# gRPC EventEngine

An EventEngine handles all cross-platform I/O, task execution, and DNS
resolution for gRPC. A default, cross-platform implementation is provided with
gRPC, but part of the intent here is to provide an interface for external
integrators to bring their own functionality. This allows for integration with
external event loops, siloing I/O and task execution between channels or
servers, and various custom integrations that were previously unsupported.

## Key differences

An EventEngine is similar to the former `iomgr` system, in that is provides the
low-level functionality described above.

However, gRPC no longer borrows threads from the application, EventEngines are
expected to bring all resources required to perform I/O and execute callbacks.
This also enables a means of support for the public Callback API in C++.

Also, EventEngines can be provided to gRPC at runtime, per Channel or Server.

## TODO: documentation

* Example usage
* Link to gRFC
