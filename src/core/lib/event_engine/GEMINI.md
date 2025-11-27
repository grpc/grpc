# EventEngine

This directory contains the implementation of the gRPC EventEngine.

## Overarching Purpose

The EventEngine is an abstraction layer that provides a consistent interface to the underlying operating system's I/O and threading primitives. It is used by gRPC to perform asynchronous I/O, schedule tasks, and resolve DNS names.

## Files

*   `event_engine.h`: The public interface to the EventEngine. This file is not located in this directory, but in `include/grpc/event_engine/`.
*   `default_event_engine_factory.h`, `default_event_engine_factory.cc`: These files provide the default, cross-platform implementation of the EventEngine.
*   `posix_engine`, `windows`, `cf_engine`: These directories contain the platform-specific implementations of the EventEngine.

## Major Classes

*   `grpc_event_engine::experimental::EventEngine`: The abstract base class for an EventEgine.
*   `grpc_event_engine::experimental::EventEngine::Endpoint`: Represents one end of a connection.
*   `grpc_event_engine::experimental::EventEngine::Listener`: Used to accept incoming connections.
*   `grpc_event_engine::experimental::EventEngine::DNSResolver`: Used to resolve DNS names.

## Notes

*   The EventEngine is a key component of gRPC's performance and portability. It allows gRPC to be easily ported to new platforms and to take advantage of the latest I/O and threading features.
*   Applications can provide their own implementation of the EventEngine by calling `SetEventEngineFactory`. This can be useful for integrating gRPC with an existing event loop or for using a custom network stack.
*   The EventEngine is a low-level component of the gRPC stack. Most application developers will not need to interact with it directly.

## See also

*   `src/core/lib/iomgr/GEMINI.md`
    *   The EventEngine is replacing the `iomgr` subsystem.
