# gRPC xDS

This directory contains gRPC's xDS implementation, which allows gRPC clients and servers to discover and configure themselves dynamically via a central xDS control plane.

See also: [gRPC Core overview](../AGENTS.md)

## Two-Layer Architecture

The code in `src/core/xds/` is intentionally split into two distinct layers:

1. **`xds_client/` — Generic xDS client library**: Protocol-level xDS and LRS client infrastructure that avoids depending on gRPC internals. This layer is used by projects outside of gRPC (via the `//bazel:xds_client_core` visibility group on `//:xds_client`) and is intended to eventually be split out into a standalone OSS library independent of gRPC.
2. **`grpc/` — gRPC-specific extensions**: Everything tied closely to gRPC (gRPC bootstrap format, gRPC channel-based transport, LDS/RDS/CDS/EDS resource types and parsers, HTTP filters, LB policy/audit logger registries, certificate providers, routing, matchers, etc.). Not intended to be used outside of gRPC.

### Dependency Rules
*   **Never add dependencies from `src/core/xds/xds_client/` onto `src/core/xds/grpc/`** or onto heavy gRPC internals (such as `//:grpc`, `CoreConfiguration`, channel/LB/resolver/filter types).
*   **Keep generic test doubles in `test/core/xds/` free of gRPC-specific xDS dependencies** as well (e.g., `//test/core/xds:fake_xds_client`), so external consumers of `//:xds_client` can reuse them in their own tests.

## `xds_client/` Directory (Generic Layer)

Contains the generic xDS/LRS client (`//:xds_client` in the root `BUILD` file).

### Key Files & Classes
*   **`xds_client_interface.h`**: `XdsClientInterface` — pure-virtual interface for watching/cancelling resources, backoff reset, and bootstrap access, allowing consumers to inject fakes in unit tests.
*   **`xds_client.{h,cc}`**: `XdsClient` — concrete implementation of `XdsClientInterface` managing ADS streams, resource caching/versioning, subscriptions, and error handling.
*   **`lrs_client.{h,cc}`**: `LrsClient` — Load Reporting Service (LRS) client.
*   **`xds_api.{h,cc}`**: `XdsApi` — ADS request serialization and response parsing helpers.
*   **`xds_bootstrap.{h,cc}`**: `XdsBootstrap` — abstract bootstrap configuration interface (`Node`, `XdsServerTarget`, `XdsServer`, `Authority`).
*   **`xds_resource_type.h`, `xds_resource_type_impl.h`**: `XdsResourceType` and `XdsResourceTypeImpl` — generic plugin interface and typed CRTP base class for xDS resource types.
*   **`xds_transport.h`, `xds_transport_factory.h`**: `XdsTransport` and `XdsTransportFactory` — transport abstraction decoupling `XdsClient` from gRPC channels.
*   **`xds_locality.h`, `xds_metrics.h`, `xds_backend_metric_propagation.{h,cc}`, `xds_channel_args.h`**: Shared generic data types and metrics interfaces.

## `grpc/` Directory (gRPC-Specific Extensions)

Implements gRPC's integration on top of `xds_client/`.

### Key Components
*   **Client, Bootstrap & Transport**: `GrpcXdsClient` (`xds_client_grpc.{h,cc}`), `GrpcXdsBootstrap` (`xds_bootstrap_grpc.{h,cc}`, `xds_bootstrap_grpc_builder.{h,cc}`), `GrpcXdsServer` (`xds_server_grpc.{h,cc}`), and gRPC transport (`xds_transport_grpc.{h,cc}`, `xds_transport_factory_grpc.{h,cc}`).
*   **xDS Resource Types & Parsers**:
    *   **LDS (Listener Discovery Service)**: `xds_listener.{h,cc}`, `xds_listener_parser.{h,cc}`
    *   **RDS (Route Discovery Service)**: `xds_route_config.{h,cc}`, `xds_route_config_parser.{h,cc}`
    *   **CDS (Cluster Discovery Service)**: `xds_cluster.{h,cc}`, `xds_cluster_parser.{h,cc}`
    *   **EDS (Endpoint Discovery Service)**: `xds_endpoint.{h,cc}`, `xds_endpoint_parser.{h,cc}`
    *   Shared parsers/types: `xds_common_types*.{h,cc}`, `xds_tls_context*.{h,cc}`, `xds_metadata*.{h,cc}`, `xds_grpc_service_parser.{h,cc}`, `xds_health_status.{h,cc}`.
*   **Routing, Filters, Matchers & Registries**: `xds_routing.{h,cc}`, `xds_http_filter*`, `xds_matcher*`, `xds_cluster_specifier_plugin.{h,cc}`, `xds_lb_policy_registry.{h,cc}`, `xds_audit_logger_registry.{h,cc}`, `blackboard.{h,cc}`.
*   **Security & Certificate Providers**: `certificate_provider_store*.{h,cc}`, `xds_certificate_provider.{h,cc}`, `file_watcher_certificate_provider_factory.{h,cc}`.

## Testing Utilities (`test/core/xds/`)

*   **`fake_xds_client.{h,cc}` (`//test/core/xds:fake_xds_client`)**: Generic, `gtest`-free `FakeXdsClient` (implementing `XdsClientInterface`) and `FakeXdsBootstrap`, depending only on `//:xds_client`. Exposed via `//bazel:xds_client_core` for reuse by external projects as well as gRPC unit tests.
*   **`xds_transport_fake.{h,cc}` (`//test/core/xds:xds_transport_fake`)**: `FakeXdsTransportFactory` for testing the real `XdsClient` / `LrsClient` protocol behavior against scripted ADS/LRS streams.

## Notes

*   For details on the xDS protocol itself, see the official [Envoy documentation](https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol).
*   See also [service config](../service_config/AGENTS.md) for how xDS integrates with gRPC service configuration.
