# I/O Manager (iomgr)

This directory contains the implementation of the gRPC I/O manager, a platform-abstraction layer for I/O and threading.

**NOTE:** The I/O manager is a legacy component that is being replaced by the [Event Engine](../event_engine/GEMINI.md). New code should use the Event Engine instead of the I/O manager.

## Overarching Purpose

The I/O manager is responsible for all of the I/O in gRPC's C-core. It provides a platform-independent abstraction layer for network I/O, timers, and asynchronous execution. It is designed to be highly scalable and performant, and it uses a variety of techniques to achieve this, including non-blocking I/O, event-driven programming, and a thread pool.

## Core Concepts

The I/O manager is built around a few key abstractions:

*   **`grpc_endpoint`**: An endpoint is an abstraction for a communication channel, such as a TCP socket. It provides a platform-independent way to read and write data, and to register for I/O events.
*   **`grpc_closure`**: A closure is a callback function that is executed when an I/O event occurs. It is the basic unit of work in the I/O manager.
*   **`ExecCtx`**: An execution context is a thread-local object that is used to track a list of work that needs to be done. It provides a way to batch work together and to execute it in a specific order.
*   **`grpc_pollset`**: A pollset is a set of file descriptors that are being monitored for I/O events. The I/O manager uses pollsets to efficiently wait for I/O events on a large number of file descriptors.
*   **Threading Model**: The I/O manager uses a thread pool to execute closures. The number of threads in the pool is configurable, and the I/O manager can be configured to use a variety of different polling strategies, depending on the platform.

## Polling Strategies

The I/O manager supports several different polling strategies, which are used to wait for I/O events on different platforms:

*   **`epoll`**: The `epoll` polling strategy is used on Linux. It is a highly scalable and efficient polling mechanism that can handle a large number of file descriptors.
*   **`poll`**: The `poll` polling strategy is used on most other POSIX-like systems. It is a more traditional polling mechanism that is not as scalable as `epoll`, but is more portable.
*   **`iocp`**: The `iocp` (I/O Completion Ports) polling strategy is used on Windows. It is a highly scalable and efficient polling mechanism that is similar to `epoll`.
*   **`cfstream`**: The `cfstream` polling strategy is used on Apple platforms (macOS, iOS). It is based on the Core Foundation `CFStream` API.

## Files

*   **`iomgr.h`**: The public interface to the I/O manager. It provides functions for initializing and shutting down the I/O manager, and for scheduling work to be done.
*   **`endpoint.h`**: Defines the `grpc_endpoint` structure, which represents a communication channel.
*   **`closure.h`**: Defines the `grpc_closure` structure, which is used to represent a callback function.
*   **`exec_ctx.h`**: Defines the `ExecCtx` class, which represents an execution context.
*   **`timer.h`**: Defines the `grpc_timer` structure, which is used to represent a timer.
*   **`tcp_posix.h`, `tcp_windows.h`**: Platform-specific implementations of the TCP endpoint.
*   **`ev_epoll1_linux.cc`, `ev_poll_posix.cc`, `iocp_windows.cc`**: Platform-specific implementations of the polling strategies.
*   **`resolve_address.h`**: Provides a platform-independent way to resolve a hostname to a list of IP addresses.
*   **`combiner.h`**: A mechanism for serializing access to data that is shared between multiple threads.

## Notes

*   The I/O manager is a low-level component of the gRPC stack. Most application developers will not need to interact with it directly.
*   The I/O manager is a complex piece of machinery, and it can be difficult to understand how all of the different pieces fit together. The best way to understand it is to read the code and to trace the execution of a simple RPC.
*   The `ExecCtx` is a particularly important concept to understand. It is used to manage the execution of closures, and it plays a key role in ensuring that the I/O manager is thread-safe.
