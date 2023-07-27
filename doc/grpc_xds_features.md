# xDS Features in gRPC

This document lists the [xDS](https://github.com/envoyproxy/data-plane-api/tree/master/envoy/api/v2)
features supported in various gRPC language implementations and versions.

Note that a gRPC client will simply ignore the configuration of a feature it
does not support. The gRPC client does not generate a log
to indicate that some configuration was ignored. It is impractical to generate
a log and keep it up-to-date because xDS has a large number of APIs that gRPC
does not support and the APIs keep evolving too. In the case where an xDS
field corresponding to a feature is supported but the value configured for
that field is not supported, a gRPC client will NACK such a configuration.
We recommend reading the
[first gRFC](https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md)
on xDS support in gRPC to understand the design philosophy.

Not all cluster load balancing policies are supported. A gRPC client will
NACK the configuration that contains unsupported cluster load balancing
policy. This will cause all cluster configurations to be rejected by the
client because the xDS protocol currently requires rejecting all resources in
a given response, rather than being able to reject only an individual resource
from the response. Due to this limitation, you must ensure that all clients
support the required cluster load balancing policy before configuring that
policy for a service. For example, if you change the ROUND_ROBIN policy to
RING_HASH, you must ensure that all the clients are upgraded to a version that
supports RING_HASH.

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
**Load Balancing:**<ul><li>[Virtual host](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-virtualhost) domains matching</li><li>Only default path ("" or "/") matching</li><li>Priority-based weighted round-robin locality picking</li><li>Round-robin endpoint picking within locality</li><li>[Cluster](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-routeaction) route action</li><li>Client-side Load reporting via [LRS](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/service/load_stats/v3/lrs.proto)</li></ul> | [A27](https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md) | v1.30.0  | v1.30.0 | v1.30.0 | v1.2.0 |
Request matching based on:<ul><li>[Path](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-routematch) (prefix, full path and safe regex)</li><ul><li>[case_sensitive](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#envoy-v3-api-msg-config-route-v3-routematch) must be true else config is NACKed</li></ul><li>[Headers](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#envoy-v3-api-msg-config-route-v3-headermatcher)</li></ul>Request routing to multiple clusters based on [weights](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-weightedcluster) | [A28](https://github.com/grpc/proposal/blob/master/A28-xds-traffic-splitting-and-routing.md) | v1.31.0 | v1.31.0 | v1.31.0 | v1.3.0 |
Case insensitive prefix/full path matching:<ul><li>[case_sensitive](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#envoy-v3-api-msg-config-route-v3-routematch) can be true or false</li></ul> | | v1.34.0 | v1.34.0 | v1.34.0 | v1.3.0 |
Support for [xDS v3 APIs](https://www.envoyproxy.io/docs/envoy/latest/api-v3/api) | [A30](https://github.com/grpc/proposal/blob/master/A30-xds-v3.md) | v1.36.0 | v1.36.0 | v1.36.0 | v1.4.0 |
Support for [xDS v2 APIs](https://www.envoyproxy.io/docs/envoy/latest/api/api_supported_versions) | [A27](https://github.com/grpc/proposal/blob/master/A30-xds-v3.md#details-of-the-v2-to-v3-transition) | < v1.51.0  | < v1.53.0 | TBA | < v1.8.0 |
[Maximum Stream Duration](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#config-route-v3-routeaction-maxstreamduration):<ul><li>Only max_stream_duration is supported.</li></ul> | [A31](https://github.com/grpc/proposal/blob/master/A31-xds-timeout-support-and-config-selector.md) | v1.37.1  | v1.37.1 | v1.37.0 | v1.4.0 |
[Circuit Breaking](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/cluster/v3/circuit_breaker.proto):<ul><li>Only max_requests is supported.</li></ul> | [A32](https://github.com/grpc/proposal/blob/master/A32-xds-circuit-breaking.md) | v1.37.1 (N/A for PHP) | v1.37.1 | v1.37.0 | v1.4.0 |
[Fault Injection](https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/filters/http/fault/v3/fault.proto):<br> Only the following fields are supported:<ul><li>delay</li><li>abort</li><li>max_active_faults</li><li>headers</li></ul> | [A33](https://github.com/grpc/proposal/blob/master/A33-Fault-Injection.md) | v1.37.1  | v1.37.1 | v1.37.0 | v1.4.0 |
[Client Status Discovery Service](https://github.com/envoyproxy/envoy/blob/main/api/envoy/service/status/v3/csds.proto) | [A40](https://github.com/grpc/proposal/blob/master/A40-csds-support.md) | v1.37.1 (C++)<br>v1.38.0 (Python)  | v1.37.1 | v1.37.0 | v1.5.0 |
[Ring hash](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/load_balancing/load_balancers#ring-hash) load balancing policy:<br> Only the following [policy specifiers](https://github.com/envoyproxy/envoy/blob/2443032526cf6e50d63d35770df9473dd0460fc0/api/envoy/config/route/v3/route_components.proto#L706) are supported:<ul><li>header</li><li>filter_state with key `io.grpc.channel_id`</li></ul>Only [`XX_HASH`](https://github.com/envoyproxy/envoy/blob/2443032526cf6e50d63d35770df9473dd0460fc0/api/envoy/config/cluster/v3/cluster.proto#L383) function is supported. | [A42](https://github.com/grpc/proposal/blob/master/A42-xds-ring-hash-lb-policy.md) | v1.40.0<br>(C++ and Python) | v1.40.1 | 1.41.0 | |
[Retry](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto#envoy-v3-api-msg-config-route-v3-retrypolicy):<br>Only the following fields are supported:<ul><li>retry_on for the following conditions: cancelled, deadline-exceeded, internal, resource-exhausted, and unavailable.</li><li>num_retries</li><li>retry_back_off</li></ul> | [A44](https://github.com/grpc/proposal/blob/master/A44-xds-retry.md) | v1.40.0<br>(C++ and Python) | v1.40.1 | 1.41.0 | |
[Security](https://www.envoyproxy.io/docs/envoy/latest/configuration/security/security):<br>Uses [certificate providers](https://github.com/grpc/proposal/blob/master/A29-xds-tls-security.md#certificate-provider-plugin-framework) instead of SDS | [A29](https://github.com/grpc/proposal/blob/master/A29-xds-tls-security.md) | v1.41.0<br>(C++ and Python) | v1.41.0 | 1.41.0 | |
[Authorization (RBAC)](https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/filters/http/rbac/v3/rbac.proto):<br><ul><li>`LOG` action has no effect<li>CEL unsupported and rejected</ul> | [A41](https://github.com/grpc/proposal/blob/master/A41-xds-rbac.md) | v1.51.0<br>(C++ and Python) | v1.42.0 | 1.42.0 | |
[Outlier Detection](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/outlier):<br>Only the following detection types are supported:<ul><li>Success Rate</li><li>Failure Percentage</li></ul> | [A50](https://github.com/grpc/proposal/blob/master/A50-xds-outlier-detection.md) | v1.51.0 | v1.49.0 | v1.50.0 | v1.7.0 | |
[Custom Load Balancer Configuration](https://github.com/envoyproxy/envoy/blob/57be3189ffa3372b34e9480d1f02b2d165e49077/api/envoy/config/cluster/v3/cluster.proto#L1208) | [A52](https://github.com/grpc/proposal/blob/master/A52-xds-custom-lb-policies.md) | v1.55.0 | v1.47.0 | | |
[xDS Federation](https://github.com/cncf/xds/blob/main/proposals/TP1-xds-transport-next.md) | [A47](https://github.com/grpc/proposal/blob/master/A47-xds-federation.md) | v1.55.0 | | | |
[Client-Side Weighted Round Robin LB Policy](https://github.com/envoyproxy/envoy/blob/a6d46b6ac4750720eec9a49abe701f0df9bf8e0a/api/envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.proto#L36) | [A58](https://github.com/grpc/proposal/blob/master/A58-client-side-weighted-round-robin-lb-policy.md) | v1.55.0 | | | |
[StringMatcher for Header Matching](https://github.com/envoyproxy/envoy/blob/3fe4b8d335fa339ef6f17325c8d31f87ade7bb1a/api/envoy/config/route/v3/route_components.proto#L2280) | [A63](https://github.com/grpc/proposal/blob/master/A63-xds-string-matcher-in-header-matching.md) | v1.56.0 | | | |
mTLS Credentials in xDS Bootstrap File | [A65](https://github.com/grpc/proposal/blob/master/A65-xds-mtls-creds-in-bootstrap.md) | v1.57.0 | | | |
