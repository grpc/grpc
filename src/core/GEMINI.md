# gRPC Core Architecture

This document provides a high-level overview of the gRPC Core architecture. gRPC Core is a library written in C++ that provides a portable and performant implementation of the gRPC protocol.

## Core Concepts

The gRPC Core is built around a few key concepts:

*   **Channels and Calls**: A **channel** represents a connection to a gRPC service, while a **call** represents a single RPC. The [`call`](./call/GEMINI.md) directory contains the fundamental data structures for managing a call's lifecycle.
*   **Transports**: A **transport** is responsible for sending and receiving data over the network. gRPC Core supports several transports, including CHTTP2, in-process, and an experimental "Chaotic Good" transport. See the [`transport`](./transport/GEMINI.md) and [`ext/transport`](./ext/transport/GEMINI.md) directories for more information.
*   **Filters**: **Filters** are a mechanism for intercepting and modifying RPCs. They are used to implement a wide variety of features, including authentication, compression, and retry. See the [`filter`](./filter/GEMINI.md) and [`ext/filters`](./ext/filters/GEMINI.md) directories for more information.
*   **Promises**: **Promises** are a framework for asynchronous programming. They are used extensively throughout the gRPC Core to implement non-blocking I/O and other asynchronous operations. See the [`lib/promise`](./lib/promise/GEMINI.md) directory for more information.
*   **Event Engine**: The **Event Engine** is an abstraction layer that provides a consistent interface to the underlying operating system's I/O and threading primitives. See the [`lib/event_engine`](./lib/event_engine/GEMINI.md) directory for more information.

## Coding Style

*   **No Exceptions**: gRPC Core code does not use C++ exceptions. Functions should return an error code to indicate failure. Possible error types:
    - bool - if the error is success or failure for a simple function, this is simple and efficient
    - absl::Status, absl::StatusOr - a good fallback for cross layer code
    - StatusFlag (in src/core/lib/promise) - a boolean that is recognizable as an error condition by the promise library; the same library provides ValueOrError that fills the role of StatusOr for this type
    - It's ok and recommended to write a bespoke error type if your failures don't fit the above mold. It's strongly recommended to provide a mechanism to reduce custom errors to absl::Status for portability between layers.

## Directory Structure

The gRPC Core is organized into the following directories:

*   [`call`](./call/GEMINI.md): The heart of the gRPC C++ core, defining the fundamental data structures for an RPC.
*   [`channelz`](./channelz/GEMINI.md): A system for inspecting the state of gRPC channels.
*   [`client_channel`](./client_channel/GEMINI.md): The core implementation of the client-side channel, including name resolution, load balancing, and connectivity.
*   [`config`](./config/GEMINI.md): Manages static and dynamic configuration of the gRPC Core.
*   [`credentials`](./credentials/GEMINI.md): The core implementation of gRPC's credential system.
*   [`ext`](./ext/GEMINI.md): Contains extensions to the gRPC Core, including filters and transports.
*   [`filter`](./filter/GEMINI.md): The fundamental building blocks for the gRPC channel filter mechanism.
*   [`handshaker`](./handshaker/GEMINI.md): A framework for establishing a secure connection between a client and a server.
*   [`lib`](./lib/GEMINI.md): A collection of libraries that provide common functionality, such as data structures, memory management, and platform-specific code.
*   [`load_balancing`](./load_balancing/GEMINI.md): A flexible and extensible framework for load balancing.
*   [`plugin_registry`](./plugin_registry/GEMINI.md): The main entry point for configuring the gRPC Core library.
*   [`resolver`](./resolver/GEMINI.md): A pluggable mechanism for resolving a logical name into a list of network addresses.
*   [`server`](./server/GEMINI.md): The core implementation of the gRPC server.
*   [`service_config`](./service_config/GEMINI.md): A mechanism for per-service and per-method configuration of a gRPC channel.
*   [`telemetry`](./telemetry/GEMINI.md): A system for collecting and reporting metrics about the behavior of gRPC.
*   [`transport`](./transport/GEMINI.md): The core transport abstraction for gRPC.
*   [`tsi`](./tsi/GEMINI.md): An abstraction for different transport security mechanisms like TLS and ALTS.
*   [`util`](./util/GEMINI.md): A collection of utility classes and functions.
*   [`xds`](./xds/GEMINI.md): An implementation of the xDS APIs, which allow a gRPC client or server to discover and configure itself dynamically.
