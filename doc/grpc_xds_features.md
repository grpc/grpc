# xDS Features in gRPC

This document lists the [xDS](https://github.com/envoyproxy/data-plane-api/tree/master/envoy/api/v2)
features supported in various gRPC language implementations and versions.

Note that a gRPC client will simply ignore the configuration of a feature it
does not support. The gRPC client does not generate a log
to indicate that some configuration was ignored. It is impractical to generate
a log and keep it up-to-date because xDS has a large number of APIs that gRPC
does not support and the APIs keep evolving too. We recommend reading the
[first gRFC](https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md)
on xDS support in gRPC to understand the design philosophy.

The EDS policy will *not* support
[overprovisioning](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/load_balancing/overprovisioning),
which is different from Envoy.  Envoy takes the overprovisioning into
account in both [locality-weighted load balancing](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/load_balancing/locality_weight)
and [priority failover](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/load_balancing/priority),
but gRPC assumes that the xDS server will update it to redirect traffic
when this kind of graceful failover is needed.  gRPC will send the
[`envoy.lb.does_not_support_overprovisioning` client
feature](https://github.com/envoyproxy/envoy/pull/10136) to the xDS
server to tell the xDS server that it will not perform graceful failover;
xDS server implementations may use this to decide whether to perform
graceful failover themselves.

The EDS policy will not support per-endpoint stats; it will report only
per-locality stats.

An [`lb_endpoint`](https://github.com/envoyproxy/envoy/blob/12a4bc430eaf440ceb0d11286cfbd4c16b79cdd1/api/envoy/api/v2/endpoint/endpoint_components.proto#L72)
is ignored if the `health_status` is not HEALTHY or UNKNOWN.
The optional `load_balancing_weight` is always ignored.

Initially, only `google_default` channel creds will be supported
to authenticate with the xDS server.

The gRPC language implementations not listed in the table below do not support
xDS features.

Features | gRFCs  | [C++, Python,<br> Ruby, PHP](https://github.com/grpc/grpc/releases) | [Java](https://github.com/grpc/grpc-java/releases) | [Go](https://github.com/grpc/grpc-go/releases) | [Node](https://github.com/grpc/grpc-node/releases)
---------|--------|--------------|------|------|------
**xDS Infrastructure in gRPC client channel:**<ul><li>LDS->RDS->CDS->EDS flow</li><li>ADS stream</li></ul> | [A27](https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md) | v1.30.0  | v1.30.0 | v1.30.0 | v1.2.0 |
**Load Balancing:**<ul><li>[Virtual host](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/route/route_components.proto#route-virtualhost) domains matching</li><li>Only default path ("" or "/") matching</li><li>Priority-based weighted round-robin locality picking</li><li>Round-robin endpoint picking within locality</li><li>[Cluster](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/route/route_components.proto#envoy-api-msg-route-routeaction) route action</li><li>Client-side Load reporting via [LRS](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/service/load_stats/v2/lrs.proto)</li></ul> | [A27](https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md) | v1.30.0  | v1.30.0 | v1.30.0 | v1.2.0 |
Request matching based on:<ul><li>[Path](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/route/route_components.proto#route-routematch) (prefix, full path and safe regex)</li><ul><li>[case_sensitive](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/route/route_components.proto#route-routematch) must be true else config is NACKed</li></ul><li>[Headers](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/route/route_components.proto#route-headermatcher)</li></ul>Request routing to multiple clusters based on [weights](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/route/route_components.proto#route-weightedcluster) | [A28](https://github.com/grpc/proposal/blob/master/A28-xds-traffic-splitting-and-routing.md) | v1.31.0 | v1.31.0 | v1.31.0 | v1.3.0 |
Case insensitive prefix/full path matching:<ul><li>[case_sensitive](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/route/route_components.proto#route-routematch) can be true or false</li></ul> | | v1.34.0 | v1.34.0 | v1.34.0 | v1.3.0 |
Support for [xDS v3 APIs](https://www.envoyproxy.io/docs/envoy/latest/api-v3/api) | [A30](https://github.com/grpc/proposal/blob/master/A30-xds-v3.md) | v1.36.0 | v1.36.0 | v1.36.0 | |
[Maximum Stream Duration](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-routeaction-maxstreamduration):<ul><li>Only max_stream_duration is supported.</li></ul> | [A31](https://github.com/grpc/proposal/blob/master/A31-xds-timeout-support-and-config-selector.md) | v1.37.1  | v1.37.0 | v1.37.0 | |
[Circuit Breaking](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/cluster/v3/circuit_breaker.proto):<ul><li>Only max_requests is supported.</li></ul> | [A32](https://github.com/grpc/proposal/blob/master/A32-xds-circuit-breaking.md) | v1.37.1 (N/A for PHP) | v1.37.0 | v1.37.0 | |
[Fault Injection](https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/filters/http/fault/v3/fault.proto):<br> Only the following fields are supported:<ul><li>delay</li><li>abort</li><li>max_active_faults</li><li>headers</li></ul> | [A33](https://github.com/grpc/proposal/blob/master/A33-Fault-Injection.md) | v1.37.1  | v1.37.0 | v1.37.0 | |
[Client Status Discovery Service](https://github.com/envoyproxy/envoy/blob/main/api/envoy/service/status/v3/csds.proto) | [A40](https://github.com/grpc/proposal/blob/master/A40-csds-support.md) | v1.37.1 (Only C++)  | v1.37.0 | v1.37.0 | |
