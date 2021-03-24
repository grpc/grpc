# gRPC EventEngine

An EventEngine handles all cross-platform I/O, task execution, and DNS
resolution for gRPC. A default, cross-platform implementation is provided with
gRPC, but part of the intent here is to provide an interface for external
integrators to bring their own functionality. This allows for integration with
external event loops, siloing I/O and task execution between channels or
servers, and various custom integrations that were previously unsupported.

*WARNING*: This is experimental code and is subject to change.

## Key differences with iomgr

An EventEngine is similar to the former `iomgr` system, in that it provides the
same low-level functionality described above.

However, gRPC no longer borrows threads from the application; EventEngines are
expected to bring all resources required to perform I/O and execute callbacks.
This also enables a means to support the public Callback API in C++.

Another key difference is that EventEngines can be provided to gRPC at runtime,
either per Channel or Server.

## TODO: documentation

* Example usage
* Link to gRFC
* Note on blocking behavior of callbacks and I/O
