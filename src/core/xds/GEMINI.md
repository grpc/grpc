# gRPC xDS

This directory contains the implementation of gRPC's xDS support. xDS is a set of APIs that allow a gRPC client or server to discover and configure itself dynamically.

See also: [gRPC Core overview](../GEMINI.md)

## Overarching Purpose

The code in this directory provides a gRPC implementation of the xDS APIs. This allows gRPC clients and servers to be configured by a central control plane. xDS is one way that service configurations can be delivered to a gRPC application; see [service config](../service_config/GEMINI.md) for more information.

## Core Concepts

*   **`XdsClient`**: The `XdsClient` is the core of the xDS implementation. It is responsible for managing the connection to the xDS server, sending resource requests, and receiving and processing resource updates.
*   **Bootstrap File**: The `XdsClient` is configured via a bootstrap file. The bootstrap file is a JSON file that contains the information that the `XdsClient` needs to connect to the xDS server.
*   **xDS Resources**: xDS is a set of APIs for discovering and configuring different types of resources. The most important resource types for gRPC are:
    *   **LDS (Listener Discovery Service)**: Used to discover the listeners that are running on a gRPC server.
    *   **RDS (Route Discovery Service)**: Used to discover the routes that are available for a given listener.
    *   **CDS (Cluster Discovery Service)**: Used to discover the clusters that are available to the gRPC client.
    *   **EDS (Endpoint Discovery Service)**: Used to discover the endpoints (i.e., the backend servers) that are in a given cluster.

## `xds_client` Directory

The `xds_client` directory contains the core implementation of the xDS client.

### Files

*   **`xds_client.h`, `xds_client.cc`**: These files define the `XdsClient` class.
*   **`xds_bootstrap.h`, `xds_bootstrap.cc`**: These files define the `XdsBootstrap` class, which is responsible for reading the xDS bootstrap file and creating the initial `XdsClient` configuration.
*   **`xds_api.h`, `xds_api.cc`**: These files define the `XdsApi` class, which provides a high-level API for interacting with the xDS server.
*   **`lrs_client.h`, `lrs_client.cc`**: These files define the `LrsClient` class, which is used to send load reports to the xDS server.
*   **`xds_resource_type.h`**: Defines the interface for a specific xDS resource type. Each resource type (e.g., LDS, RDS, CDS, EDS) has its own implementation of this interface.

### Major Classes

*   **`grpc_core::XdsClient`**: The core of the xDS client implementation.
*   **`grpc_core::XdsBootstrap`**: Responsible for reading the xDS bootstrap file.
*   **`grpc_core::XdsApi`**: A high-level API for interacting with the xDS server.
*   **`grpc_core::LrsClient`**: A client for sending load reports to the xDS server.

## Notes

*   xDS is a complex and powerful feature. A full understanding of xDS requires reading the official [Envoy documentation](https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol).
*   The gRPC xDS implementation is still under active development.
*   The xDS implementation is a good example of how gRPC can be extended with new features. It is implemented as a set of plugins that are loaded at runtime, and it uses the `CoreConfiguration` class to register its factories.
