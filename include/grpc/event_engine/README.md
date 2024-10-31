# gRPC EventEngine

An EventEngine handles all cross-platform I/O, task execution, and DNS
resolution for gRPC. A default, cross-platform implementation is provided with
gRPC, but part of the intent here is to provide an interface for external
integrators to bring their own functionality. This allows for integration with
external event loops, siloing I/O and task execution between channels or
servers, and other custom integrations that were previously unsupported.

*WARNING*: This is experimental code and is subject to change.

## High level expectations of an EventEngine implementation

### Provide their own I/O threads
EventEngines are expected to internally create whatever threads are required to
perform I/O and execute callbacks. For example, an EventEngine implementation
may want to spawn separate thread pools for polling and callback execution.

### Provisioning data buffers via Slice allocation
At a high level, gRPC provides a `ResourceQuota` system that allows gRPC to
reclaim memory and degrade gracefully when memory reaches application-defined
thresholds. To enable this feature, the memory allocation of read/write buffers
within an EventEngine must be acquired in the form of Slices from
SliceAllocators. This is covered more fully in the gRFC and code.

### Documenting expectations around callback execution
Some callbacks may be expensive to run. EventEngines should decide on and
document whether callback execution might block polling operations. This way,
application developers can plan accordingly (e.g., run their expensive callbacks
on a separate thread if necessary).

### Handling concurrent usage
Assume that gRPC may use an EventEngine concurrently across multiple threads.

## TODO: documentation

* Example usage
* Link to gRFC
