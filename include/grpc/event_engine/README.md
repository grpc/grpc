# gRPC EventEngine

An EventEngine handles all cross-platform I/O, task execution, and DNS
resolution for gRPC. A default, cross-platform implementation is provided with
gRPC, but part of the intent here is to provide an interface for external
integrators to bring their own functionality. This allows for integration with
external event loops, siloing I/O and task execution between channels or
servers, and other custom integrations that were previously unsupported.

*WARNING*: This is experimental code and is subject to change.

## Expectations of an EventEngine implementation

### Resource acquisition
EventEngines are expected to bring all resources required to perform I/O and
execute callbacks. This also enables a means to support the public Callback API
in C++.

### Blocking callback execution
Some callbacks may be expensive to run. EventEngines should decide on and
document whether callback execution could block polling operations. This way,
application developers can plan accordingly (e.g., run their expensive callbacks
on a separate thread).

### Concurrent usage
Assume that gRPC may use an EventEngine concurrently across multiple threads.

## TODO: documentation

* Example usage
* Link to gRFC
